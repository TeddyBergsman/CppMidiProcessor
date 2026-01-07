#include "playback/VirtuosoBalladMvpPlaybackEngine.h"

#include "midiprocessor.h"
#include "playback/BalladReferenceTuning.h"
#include "playback/LookaheadPlanner.h"
#include "playback/SemanticMidiAnalyzer.h"
#include "playback/VibeStateMachine.h"
#include "virtuoso/theory/FunctionalHarmony.h"
#include "virtuoso/theory/ScaleSuggester.h"
#include "virtuoso/util/StableHash.h"

#include <QHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtGlobal>
#include <algorithm>
#include <limits>

namespace playback {
namespace {

static QVector<const chart::Bar*> flattenBarsFrom(const chart::ChartModel& model) {
    QVector<const chart::Bar*> bars;
    for (const auto& line : model.lines) {
        for (const auto& bar : line.bars) {
            bars.push_back(&bar);
        }
    }
    return bars;
}

static int normalizePc(int pc) {
    int v = pc % 12;
    if (v < 0) v += 12;
    return v;
}

static QString pcName(int pc) {
    static const char* names[] = {"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};
    return names[normalizePc(pc)];
}

static QString ontologyChordKeyFor(const music::ChordSymbol& c) {
    using music::ChordQuality;
    using music::SeventhQuality;
    if (c.noChord || c.placeholder) return {};
    // Dominant family
    if (c.quality == ChordQuality::Dominant) {
        if (c.alt) return "7alt";
        bool hasB9 = false, hasSharp9 = false, hasB13 = false, hasSharp11 = false;
        for (const auto& a : c.alterations) {
            if (a.degree == 9 && a.delta < 0) hasB9 = true;
            if (a.degree == 9 && a.delta > 0) hasSharp9 = true;
            if (a.degree == 13 && a.delta < 0) hasB13 = true;
            if (a.degree == 11 && a.delta > 0) hasSharp11 = true;
        }
        if (hasB9 && hasSharp9) return "7b9#9";
        if (hasB9 && hasB13) return "7b9b13";
        if (hasSharp9 && hasB13) return "7#9b13";
        if (hasB9) return "7b9";
        if (hasSharp9) return "7#9";
        if (hasB13) return "7b13";
        if (c.extension >= 13 && hasSharp11) return "13#11";
        if (c.extension >= 13) return "13";
        if (c.extension >= 11) return "11";
        if (c.extension >= 9) return "9";
        if (c.seventh != SeventhQuality::None || c.extension >= 7) return "7";
        return "7";
    }
    // Half diminished
    if (c.quality == ChordQuality::HalfDiminished) return "m7b5";
    // Diminished
    if (c.quality == ChordQuality::Diminished) {
        if (c.seventh == SeventhQuality::Dim7) return "dim7";
        return (c.extension >= 7) ? "dim7" : "dim";
    }
    // Minor
    if (c.quality == ChordQuality::Minor) {
        if (c.seventh == SeventhQuality::Major7) {
            if (c.extension >= 13) return "minmaj13";
            if (c.extension >= 11) return "minmaj11";
            if (c.extension >= 9) return "minmaj9";
            return "min_maj7";
        }
        if (c.extension >= 13) return "min13";
        if (c.extension >= 11) return "min11";
        if (c.extension >= 9) return "min9";
        if (c.seventh != SeventhQuality::None || c.extension >= 7) return "min7";
        return "min";
    }
    // Major
    if (c.quality == ChordQuality::Major) {
        bool hasSharp11 = false;
        for (const auto& a : c.alterations) {
            if (a.degree == 11 && a.delta > 0) hasSharp11 = true;
        }
        if (c.extension >= 13 && hasSharp11) return "maj13#11";
        if (c.extension >= 13) return "maj13";
        if (c.extension >= 11) return "maj11";
        if (c.extension >= 9 && hasSharp11) return "maj9#11";
        if (c.extension >= 9) return "maj9";
        if (c.seventh == SeventhQuality::Major7 || c.extension >= 7) return "maj7";
        if (c.extension >= 6) return "6";
        return "maj";
    }
    // Sus
    if (c.quality == ChordQuality::Sus2) return "sus2";
    if (c.quality == ChordQuality::Sus4) {
        if (c.extension >= 13) return "13sus4";
        if (c.extension >= 9) return "9sus4";
        if (c.seventh == SeventhQuality::Minor7 || c.extension >= 7) return "7sus4";
        return "sus4";
    }
    // Aug
    if (c.quality == ChordQuality::Augmented) {
        if (c.seventh == SeventhQuality::Minor7 || c.extension >= 7) return "aug7";
        return "aug";
    }
    // Power
    if (c.quality == ChordQuality::Power5) return "5";
    return {};
}

static const virtuoso::ontology::ChordDef* chordDefForSymbol(const virtuoso::ontology::OntologyRegistry& reg,
                                                             const music::ChordSymbol& c) {
    const QString key = ontologyChordKeyFor(c);
    if (key.isEmpty()) return nullptr;
    return reg.chord(key);
}

static QSet<int> pitchClassesForChordDef(int rootPc, const virtuoso::ontology::ChordDef& chord) {
    QSet<int> pcs;
    const int r = normalizePc(rootPc);
    pcs.insert(r);
    for (int iv : chord.intervals) pcs.insert(normalizePc(r + iv));
    return pcs;
}

static int estimateMajorKeyPcGuess(const virtuoso::ontology::OntologyRegistry& reg,
                                   const QVector<music::ChordSymbol>& chords,
                                   int fallbackPc) {
    double bestScore = -1.0;
    int bestTonic = normalizePc(fallbackPc);
    for (int tonic = 0; tonic < 12; ++tonic) {
        double score = 0.0;
        int used = 0;
        for (const auto& c : chords) {
            if (c.noChord || c.placeholder || c.rootPc < 0) continue;
            const auto* def = chordDefForSymbol(reg, c);
            if (!def) continue;
            const auto h = virtuoso::theory::analyzeChordInMajorKey(tonic, c.rootPc, *def);
            score += h.confidence;
            used++;
        }
        score += 0.02 * double(used);
        if (score > bestScore) { bestScore = score; bestTonic = tonic; }
    }
    return bestTonic;
}

static virtuoso::theory::KeyMode keyModeForScaleKey(const QString& k) {
    // MVP: treat Ionian/HarmonicMajor as Major; Aeolian/HarmonicMinor/MelodicMinor as Minor.
    // Modes like Dorian/Phrygian/Mixolydian are treated as Major for functional tagging (Stage 2.5 can improve).
    const QString s = k.toLower();
    if (s == "aeolian" || s == "harmonic_minor" || s == "melodic_minor") return virtuoso::theory::KeyMode::Minor;
    return virtuoso::theory::KeyMode::Major;
}

static void estimateGlobalKeyByScale(const virtuoso::ontology::OntologyRegistry& reg,
                                     const QVector<music::ChordSymbol>& chords,
                                     int fallbackPc,
                                     int* outTonicPc,
                                     QString* outScaleKey,
                                     QString* outScaleName,
                                     virtuoso::theory::KeyMode* outMode) {
    if (!outTonicPc || !outScaleKey || !outScaleName || !outMode) return;
    *outTonicPc = normalizePc(fallbackPc);
    outScaleKey->clear();
    outScaleName->clear();
    *outMode = virtuoso::theory::KeyMode::Major;
    if (chords.isEmpty()) return;

    QSet<int> pcs;
    pcs.reserve(24);
    for (const auto& c : chords) {
        if (c.noChord || c.placeholder || c.rootPc < 0) continue;
        const auto* def = chordDefForSymbol(reg, c);
        if (!def) continue;
        const auto chordPcs = pitchClassesForChordDef(c.rootPc, *def);
        for (int pc : chordPcs) pcs.insert(pc);
    }
    if (pcs.isEmpty()) return;

    const auto sug = virtuoso::theory::suggestScalesForPitchClasses(reg, pcs, 10);
    if (sug.isEmpty()) return;
    const auto& best = sug.first();
    *outTonicPc = normalizePc(best.bestTranspose);
    *outScaleKey = best.key;
    *outScaleName = best.name;
    *outMode = keyModeForScaleKey(best.key);
}

static QVector<playback::LocalKeyEstimate> estimateLocalKeysByBar(
    const virtuoso::ontology::OntologyRegistry& reg,
    const QVector<const chart::Bar*>& bars,
    int windowBars,
    int fallbackTonicPc,
    const QString& fallbackScaleKey,
    const QString& fallbackScaleName,
    virtuoso::theory::KeyMode fallbackMode) {
    QVector<playback::LocalKeyEstimate> out;
    out.resize(bars.size());
    if (bars.isEmpty()) return out;
    windowBars = qMax(1, windowBars);

    for (int i = 0; i < bars.size(); ++i) {
        QSet<int> pcs;
        pcs.reserve(24);
        QVector<music::ChordSymbol> chords;
        chords.reserve(windowBars * 2);

        const int end = qMin(bars.size(), i + windowBars);
        for (int b = i; b < end; ++b) {
            const auto* bar = bars[b];
            if (!bar) continue;
            for (const auto& cell : bar->cells) {
                const QString t = cell.chord.trimmed();
                if (t.isEmpty()) continue;
                music::ChordSymbol parsed;
                if (!music::parseChordSymbol(t, parsed)) continue;
                if (parsed.placeholder || parsed.noChord || parsed.rootPc < 0) continue;
                chords.push_back(parsed);
                const auto* def = chordDefForSymbol(reg, parsed);
                if (!def) continue;
                const auto chordPcs = pitchClassesForChordDef(parsed.rootPc, *def);
                for (int pc : chordPcs) pcs.insert(pc);
            }
        }

        playback::LocalKeyEstimate lk;
        lk.tonicPc = fallbackTonicPc;
        lk.scaleKey = fallbackScaleKey;
        lk.scaleName = fallbackScaleName;
        lk.mode = fallbackMode;
        lk.score = 0.0;
        lk.coverage = 0.0;

        if (!pcs.isEmpty()) {
            const auto sug = virtuoso::theory::suggestScalesForPitchClasses(reg, pcs, 6);
            if (!sug.isEmpty()) {
                const auto& best = sug.first();
                lk.tonicPc = normalizePc(best.bestTranspose);
                lk.scaleKey = best.key;
                lk.scaleName = best.name;
                lk.mode = keyModeForScaleKey(best.key);
                lk.score = best.score;
                lk.coverage = best.coverage;
            }
        }
        out[i] = lk;
    }
    return out;
}
static QString chooseScaleUsedForChord(const virtuoso::ontology::OntologyRegistry& reg,
                                       int keyPc,
                                       virtuoso::theory::KeyMode keyMode,
                                       const music::ChordSymbol& chordSym,
                                       const virtuoso::ontology::ChordDef& chordDef,
                                       QString* outRoman = nullptr,
                                       QString* outFunction = nullptr) {
    const QSet<int> pcs = pitchClassesForChordDef(chordSym.rootPc, chordDef);
    const auto sugg = virtuoso::theory::suggestScalesForPitchClasses(reg, pcs, 12);
    if (sugg.isEmpty()) return {};
    const auto h = virtuoso::theory::analyzeChordInKey(keyPc, keyMode, chordSym.rootPc, chordDef);
    if (outRoman) *outRoman = h.roman;
    if (outFunction) *outFunction = h.function;

    struct Sc { virtuoso::theory::ScaleSuggestion s; double score = 0.0; };
    QVector<Sc> ranked;
    ranked.reserve(sugg.size());
    const QString chordKey = ontologyChordKeyFor(chordSym);
    const QVector<QString> hints = virtuoso::theory::explicitHintScalesForContext(/*voicingKey*/QString(), chordKey);
    for (const auto& s : sugg) {
        double bonus = 0.0;
        if (normalizePc(s.bestTranspose) == normalizePc(chordSym.rootPc)) bonus += 0.6;
        const QString name = s.name.toLower();
        if (h.function == "Dominant") {
            if (name.contains("altered") || name.contains("lydian dominant") || name.contains("mixolydian") || name.contains("half-whole")) bonus += 0.35;
        } else if (h.function == "Subdominant") {
            if (name.contains("dorian") || name.contains("lydian") || name.contains("phrygian")) bonus += 0.25;
        } else if (h.function == "Tonic") {
            if (name.contains("ionian") || name.contains("major") || name.contains("lydian")) bonus += 0.25;
        }
        // Explicit hint nudges (UST/dominant language). Earlier hints get more bonus.
        for (int i = 0; i < hints.size(); ++i) {
            if (s.key == hints[i]) bonus += (0.45 - 0.08 * double(i));
        }
        ranked.push_back({s, s.score + bonus});
    }
    std::sort(ranked.begin(), ranked.end(), [](const Sc& a, const Sc& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.s.name < b.s.name;
    });
    const auto& best = ranked.first().s;
    return QString("%1 (%2)").arg(best.name).arg(pcName(best.bestTranspose));
}

// Copied (intentionally) from BandPlaybackEngine/SilentPlaybackEngine to keep repeat/ending behavior stable.
static QVector<int> buildPlaybackSequenceFrom(const chart::ChartModel& model) {
    const QVector<const chart::Bar*> bars = flattenBarsFrom(model);
    const int nBars = bars.size();
    QVector<int> seq;
    if (nBars <= 0) return seq;
    seq.reserve(nBars * 4);

    int fineBar = -1;
    int segnoBar = -1;
    for (int i = 0; i < nBars; ++i) {
        const QString ann = bars[i]->annotation.trimmed();
        if (fineBar < 0 && ann.compare("Fine", Qt::CaseInsensitive) == 0) fineBar = i;
        if (segnoBar < 0 && ann.contains("Segno", Qt::CaseInsensitive)) segnoBar = i;
    }

    const QString footer = model.footerText.trimmed();
    const bool wantsJump = footer.startsWith("D.C.", Qt::CaseInsensitive) || footer.startsWith("D.S.", Qt::CaseInsensitive);
    const bool jumpIsDS = footer.startsWith("D.S.", Qt::CaseInsensitive);
    const bool alFine = footer.contains("al Fine", Qt::CaseInsensitive);
    const int jumpTarget = jumpIsDS ? (segnoBar >= 0 ? segnoBar : 0) : 0;

    QVector<int> repeatStartStack;
    QHash<int, int> startToEnd;
    QHash<int, int> endToStart;
    repeatStartStack.reserve(8);
    for (int i = 0; i < nBars; ++i) {
        const QString l = bars[i]->barlineLeft;
        const QString r = bars[i]->barlineRight;
        if (l.contains('{')) {
            repeatStartStack.push_back(i);
        }
        if (r.contains('}')) {
            int start = 0;
            if (!repeatStartStack.isEmpty()) {
                start = repeatStartStack.takeLast();
            }
            startToEnd.insert(start, i);
            endToStart.insert(i, start);
        }
    }

    QHash<int, int> endingStartToEnd;
    for (int i = 0; i < nBars; ++i) {
        const int n = bars[i]->endingStart;
        if (n <= 0) continue;
        int end = i;
        for (int j = i; j < nBars; ++j) {
            if (bars[j]->endingEnd == n) { end = j; break; }
        }
        endingStartToEnd.insert(i, end);
    }

    QHash<int, int> repeatEndToPasses;
    for (auto it = startToEnd.constBegin(); it != startToEnd.constEnd(); ++it) {
        const int start = it.key();
        const int end = it.value();
        int maxEnding = 0;
        for (int i = start; i <= end && i < nBars; ++i) {
            maxEnding = std::max(maxEnding, bars[i]->endingStart);
            maxEnding = std::max(maxEnding, bars[i]->endingEnd);
        }
        repeatEndToPasses.insert(end, std::max(2, maxEnding));
    }

    struct RepeatCtx { int start = 0; int end = -1; int pass = 1; int passes = 2; };
    QVector<RepeatCtx> stack;
    stack.reserve(4);

    bool jumped = false;
    int pc = 0;
    int guardSteps = 0;
    const int guardMax = 20000;

    auto currentPass = [&]() -> int { return stack.isEmpty() ? 1 : stack.last().pass; };

    while (pc < nBars && guardSteps++ < guardMax) {
        if (startToEnd.contains(pc)) {
            const int end = startToEnd.value(pc);
            bool already = false;
            if (!stack.isEmpty() && stack.last().start == pc && stack.last().end == end) already = true;
            if (!already) {
                RepeatCtx ctx;
                ctx.start = pc;
                ctx.end = end;
                ctx.pass = 1;
                ctx.passes = repeatEndToPasses.value(end, 2);
                stack.push_back(ctx);
            }
        }

        if (!stack.isEmpty()) {
            const int n = bars[pc]->endingStart;
            if (n > 0 && n != currentPass()) {
                const int end = endingStartToEnd.value(pc, pc);
                pc = end + 1;
                continue;
            }
        }

        for (int c = 0; c < 4; ++c) {
            seq.push_back(pc * 4 + c);
        }

        if (jumped && alFine && fineBar >= 0 && pc == fineBar) {
            break;
        }

        if (!stack.isEmpty() && pc == stack.last().end) {
            if (stack.last().pass < stack.last().passes) {
                stack.last().pass += 1;
                pc = stack.last().start;
                continue;
            }
            stack.removeLast();
            pc += 1;
            continue;
        }

        pc += 1;
        if (pc >= nBars) {
            if (wantsJump && !jumped) {
                jumped = true;
                pc = jumpTarget;
                continue;
            }
            break;
        }
    }

    return seq;
}

static bool sameChordKey(const music::ChordSymbol& a, const music::ChordSymbol& b) {
    return (a.rootPc == b.rootPc && a.bassPc == b.bassPc && a.quality == b.quality && a.seventh == b.seventh && a.extension == b.extension && a.alt == b.alt);
}

static virtuoso::groove::Rational durationWholeFromHoldMs(int holdMs, int bpm) {
    // GrooveGrid::wholeNotesToMs: wholeMs = 240000 / bpm
    // => whole = holdMs / wholeMs = holdMs * bpm / 240000
    if (holdMs <= 0) return virtuoso::groove::Rational(1, 16);
    if (bpm <= 0) bpm = 120;
    return virtuoso::groove::Rational(qint64(holdMs) * qint64(bpm), qint64(240000));
}

static QString intentsToString(const SemanticMidiAnalyzer::IntentState& i) {
    QStringList out;
    if (i.densityHigh) out << "DENSITY_HIGH";
    if (i.registerHigh) out << "REGISTER_HIGH";
    if (i.intensityPeak) out << "INTENSITY_PEAK";
    if (i.playingOutside) out << "PLAYING_OUTSIDE";
    if (i.silence) out << "SILENCE";
    return out.join(",");
}

} // namespace

void VirtuosoBalladMvpPlaybackEngine::onGuitarNoteOn(int note, int vel) {
    m_listener.ingestGuitarNoteOn(note, vel, QDateTime::currentMSecsSinceEpoch());
}

void VirtuosoBalladMvpPlaybackEngine::onGuitarNoteOff(int note) {
    m_listener.ingestGuitarNoteOff(note, QDateTime::currentMSecsSinceEpoch());
}

void VirtuosoBalladMvpPlaybackEngine::onVoiceCc2Stream(int cc2) {
    m_listener.ingestCc2(cc2, QDateTime::currentMSecsSinceEpoch());
}

void VirtuosoBalladMvpPlaybackEngine::onVoiceNoteOn(int note, int vel) {
    m_listener.ingestVoiceNoteOn(note, vel, QDateTime::currentMSecsSinceEpoch());
}

void VirtuosoBalladMvpPlaybackEngine::onVoiceNoteOff(int note) {
    m_listener.ingestVoiceNoteOff(note, QDateTime::currentMSecsSinceEpoch());
}

VirtuosoBalladMvpPlaybackEngine::VirtuosoBalladMvpPlaybackEngine(QObject* parent)
    : QObject(parent)
    , m_registry(virtuoso::groove::GrooveRegistry::builtins()) {
    m_tickTimer.setInterval(10);
    m_tickTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_tickTimer, &QTimer::timeout, this, &VirtuosoBalladMvpPlaybackEngine::onTick);

    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::theoryEventJson,
            this, &VirtuosoBalladMvpPlaybackEngine::theoryEventJson);
    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::plannedTheoryEventJson,
            this, &VirtuosoBalladMvpPlaybackEngine::plannedTheoryEventJson);

    // Load data-driven vocabulary (rhythmic/phrase patterns) from resources.
    {
        QString err;
        m_vocabLoaded = m_vocab.loadFromResourcePath(":/virtuoso/vocab/cool_jazz_vocabulary.json", &err);
        m_vocabError = err;
        // Bass planner consumes VocabularyRegistry directly.
        m_bassPlanner.setVocabulary(m_vocabLoaded ? &m_vocab : nullptr);
        // Piano planner is currently procedural; we keep its vocab interface as deprecated/no-op.
        m_pianoPlanner.setVocabulary(nullptr);
    }

    // Ontology is the canonical musical truth for voicing choices.
    m_pianoPlanner.setOntology(&m_ontology);
}

void VirtuosoBalladMvpPlaybackEngine::emitLookaheadPlanOnce() {
    if (m_sequence.isEmpty()) return;

    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;

    // If we have a live playhead, preview from the current bar; otherwise preview from song start.
    const int stepNow = (m_lastPlayheadStep >= 0) ? m_lastPlayheadStep : 0;

    LookaheadPlanner::Inputs li;
    li.bpm = m_bpm;
    li.ts = ts;
    li.repeats = m_repeats;
    li.model = &m_model;
    li.sequence = &m_sequence;
    li.hasLastChord = m_hasLastChord;
    li.lastChord = m_lastChord;
    li.ontology = &m_ontology;
    li.hasKeyPcGuess = m_hasKeyPcGuess;
    li.keyPcGuess = m_keyPcGuess;
    li.keyScaleKey = m_keyScaleKey;
    li.keyScaleName = m_keyScaleName;
    li.keyMode = m_keyMode;
    li.localKeysByBar = &m_localKeysByBar;
    li.listener = &m_listener;
    li.vibe = &m_vibe;
    li.bassPlanner = &m_bassPlanner;
    li.pianoPlanner = &m_pianoPlanner;
    li.drummer = &m_drummer;
    li.chDrums = m_chDrums;
    li.chBass = m_chBass;
    li.chPiano = m_chPiano;
    li.stylePresetKey = m_stylePresetKey;
    li.agentEnergyMult = m_agentEnergyMult;
    li.debugEnergyAuto = m_debugEnergyAuto;
    li.debugEnergy = m_debugEnergy;
    li.virtAuto = m_virtAuto;
    li.virtHarmonicRisk = m_virtHarmonicRisk;
    li.virtRhythmicComplexity = m_virtRhythmicComplexity;
    li.virtInteraction = m_virtInteraction;
    li.virtToneDark = m_virtToneDark;
    li.engineNowMs = m_engine.elapsedMs();

    const QString json = LookaheadPlanner::buildLookaheadPlanJson(li, stepNow, /*horizonBars=*/4);
    if (!json.trimmed().isEmpty()) emit lookaheadPlanJson(json);
}

void VirtuosoBalladMvpPlaybackEngine::setMidiProcessor(MidiProcessor* midi) {
    m_midi = midi;
    if (!m_midi) return;

    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::noteOn,
            m_midi, &MidiProcessor::sendVirtualNoteOn, Qt::UniqueConnection);
    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::noteOff,
            m_midi, &MidiProcessor::sendVirtualNoteOff, Qt::UniqueConnection);
    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::allNotesOff,
            m_midi, &MidiProcessor::sendVirtualAllNotesOff, Qt::UniqueConnection);
    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::cc,
            m_midi, &MidiProcessor::sendVirtualCC, Qt::UniqueConnection);

    // Listening MVP: tap *transposed* live performance notes.
    // Use QueuedConnection because MidiProcessor may emit from its worker thread.
    connect(m_midi, &MidiProcessor::guitarNoteOn,
            this, &VirtuosoBalladMvpPlaybackEngine::onGuitarNoteOn,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
    connect(m_midi, &MidiProcessor::guitarNoteOff,
            this, &VirtuosoBalladMvpPlaybackEngine::onGuitarNoteOff,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
    connect(m_midi, &MidiProcessor::voiceCc2Stream,
            this, &VirtuosoBalladMvpPlaybackEngine::onVoiceCc2Stream,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));

    // Vocal melody tracking (NOT used for density): allows later call/response.
    connect(m_midi, &MidiProcessor::voiceNoteOn,
            this, &VirtuosoBalladMvpPlaybackEngine::onVoiceNoteOn,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
    connect(m_midi, &MidiProcessor::voiceNoteOff,
            this, &VirtuosoBalladMvpPlaybackEngine::onVoiceNoteOff,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
}

void VirtuosoBalladMvpPlaybackEngine::setTempoBpm(int bpm) {
    m_bpm = qBound(30, bpm, 300);
    m_engine.setTempoBpm(m_bpm);
}

void VirtuosoBalladMvpPlaybackEngine::setRepeats(int repeats) {
    m_repeats = qMax(1, repeats);
}

void VirtuosoBalladMvpPlaybackEngine::setChartModel(const chart::ChartModel& model) {
    m_model = model;
    rebuildSequence();

    virtuoso::groove::TimeSignature ts;
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;
    m_engine.setTimeSignature(ts);

    // Stage 2: Estimate a global key center + scale (major/minor/modal) from the chart,
    // and compute a per-bar local key (sliding window) for modulation detection.
    QVector<music::ChordSymbol> chords;
    chords.reserve(128);
    int fallbackPc = 0;
    bool haveFallback = false;
    const QVector<const chart::Bar*> bars = flattenBarsFrom(m_model);
    for (const auto* bar : bars) {
        if (!bar) continue;
        for (const auto& cell : bar->cells) {
            const QString t = cell.chord.trimmed();
            if (t.isEmpty()) continue;
            music::ChordSymbol parsed;
            if (!music::parseChordSymbol(t, parsed)) continue;
            if (parsed.placeholder || parsed.noChord || parsed.rootPc < 0) continue;
            chords.push_back(parsed);
            if (!haveFallback) { fallbackPc = parsed.rootPc; haveFallback = true; }
        }
    }
    if (!chords.isEmpty()) {
        estimateGlobalKeyByScale(m_ontology, chords, fallbackPc, &m_keyPcGuess, &m_keyScaleKey, &m_keyScaleName, &m_keyMode);
        // Keep the old major-key heuristic available as fallback for now.
        if (m_keyScaleKey.trimmed().isEmpty()) {
            m_keyPcGuess = estimateMajorKeyPcGuess(m_ontology, chords, fallbackPc);
            m_keyScaleKey = "ionian";
            m_keyScaleName = "Ionian (Major)";
            m_keyMode = virtuoso::theory::KeyMode::Major;
        }
        m_hasKeyPcGuess = true;
    } else {
        m_keyPcGuess = 0;
        m_keyScaleKey.clear();
        m_keyScaleName.clear();
        m_keyMode = virtuoso::theory::KeyMode::Major;
        m_hasKeyPcGuess = false;
    }

    m_localKeysByBar = estimateLocalKeysByBar(m_ontology,
                                              bars,
                                              /*windowBars=*/8,
                                              m_keyPcGuess,
                                              m_keyScaleKey,
                                              m_keyScaleName,
                                              m_keyMode);
}

void VirtuosoBalladMvpPlaybackEngine::setStylePresetKey(const QString& key) {
    const QString k = key.trimmed();
    if (k.isEmpty()) return;
    m_stylePresetKey = k;
    // Apply immediately so lookahead/auditions and the next scheduled events reflect the preset.
    applyPresetToEngine();
}

void VirtuosoBalladMvpPlaybackEngine::play() {
    if (m_playing) return;
    if (m_sequence.isEmpty()) return;

    applyPresetToEngine();
    m_engine.start();

    m_playing = true;
    m_lastPlayheadStep = -1;
    m_lastEmittedCell = -1;
    m_nextScheduledStep = 0;
    m_hasLastChord = false;
    m_lastChord = music::ChordSymbol{};
    m_bassPlanner.reset();
    m_pianoPlanner.reset();
    m_listener.reset();
    m_vibe.reset();
    // Keep drummer profile wired to channel/mapping choices.
    {
        auto p = m_drummer.profile();
        p.channel = m_chDrums;
        p.noteKick = m_noteKick;
        p.noteSnareSwish = m_noteSnareHit;
        p.noteBrushLoopA = m_noteBrushLoop;
        m_drummer.setProfile(p);
    }

    m_tickTimer.start();
}

void VirtuosoBalladMvpPlaybackEngine::stop() {
    if (!m_playing) return;
    m_playing = false;

    m_tickTimer.stop();
    m_engine.stop();

    // Hard silence (safety against stuck notes)
    if (m_midi) {
        m_midi->sendVirtualAllNotesOff(m_chDrums);
        m_midi->sendVirtualAllNotesOff(m_chBass);
        m_midi->sendVirtualAllNotesOff(m_chPiano);
        m_midi->sendVirtualCC(m_chPiano, 64, 0);
    }
}

void VirtuosoBalladMvpPlaybackEngine::rebuildSequence() {
    m_sequence = buildPlaybackSequenceFromModel();
}

void VirtuosoBalladMvpPlaybackEngine::applyPresetToEngine() {
    const auto* preset = m_registry.stylePreset(m_stylePresetKey);
    if (!preset) return;

    // Tempo/TS remain owned by UI; preset provides defaults elsewhere. Here we only apply groove params.
    const auto* gt = m_registry.grooveTemplate(preset->grooveTemplateKey);
    if (gt) {
        virtuoso::groove::GrooveTemplate scaled = *gt;
        scaled.amount = qBound(0.0, preset->templateAmount, 1.0);
        m_engine.setGrooveTemplate(scaled);
    }

    if (preset->instrumentProfiles.contains("Drums")) m_engine.setInstrumentGrooveProfile("Drums", preset->instrumentProfiles.value("Drums"));
    if (preset->instrumentProfiles.contains("Bass"))  m_engine.setInstrumentGrooveProfile("Bass",  preset->instrumentProfiles.value("Bass"));
    if (preset->instrumentProfiles.contains("Piano")) m_engine.setInstrumentGrooveProfile("Piano", preset->instrumentProfiles.value("Piano"));

    // Stage 3 Virtuosity Matrix defaults are preset-driven (not just groove).
    // In Auto mode, these are treated as baseline weights; in Manual mode, they are the defaults.
    m_virtHarmonicRisk = qBound(0.0, preset->virtuosityDefaults.harmonicRisk, 1.0);
    m_virtRhythmicComplexity = qBound(0.0, preset->virtuosityDefaults.rhythmicComplexity, 1.0);
    m_virtInteraction = qBound(0.0, preset->virtuosityDefaults.interaction, 1.0);
    m_virtToneDark = qBound(0.0, preset->virtuosityDefaults.toneDark, 1.0);
}

QVector<const chart::Bar*> VirtuosoBalladMvpPlaybackEngine::flattenBars() const {
    return flattenBarsFrom(m_model);
}

QVector<int> VirtuosoBalladMvpPlaybackEngine::buildPlaybackSequenceFromModel() const {
    return buildPlaybackSequenceFrom(m_model);
}

const chart::Cell* VirtuosoBalladMvpPlaybackEngine::cellForFlattenedIndex(int cellIndex) const {
    if (cellIndex < 0) return nullptr;
    const QVector<const chart::Bar*> bars = flattenBars();
    const int barIndex = cellIndex / 4;
    const int cellInBar = cellIndex % 4;
    if (barIndex < 0 || barIndex >= bars.size()) return nullptr;
    const auto* bar = bars[barIndex];
    if (!bar) return nullptr;
    if (cellInBar < 0 || cellInBar >= bar->cells.size()) return nullptr;
    return &bar->cells[cellInBar];
}

bool VirtuosoBalladMvpPlaybackEngine::chordForCellIndex(int cellIndex, music::ChordSymbol& outChord, bool& isNewChord) {
    isNewChord = false;
    const chart::Cell* c = cellForFlattenedIndex(cellIndex);
    if (!c) return false;

    const QString t = c->chord.trimmed();
    if (t.isEmpty()) {
        if (m_hasLastChord) { outChord = m_lastChord; return true; }
        return false;
    }

    music::ChordSymbol parsed;
    if (!music::parseChordSymbol(t, parsed)) {
        if (m_hasLastChord) { outChord = m_lastChord; return true; }
        return false;
    }
    if (parsed.placeholder) {
        if (m_hasLastChord) { outChord = m_lastChord; return true; }
        return false;
    }

    outChord = parsed;
    if (!m_hasLastChord) {
        isNewChord = true;
    } else {
        isNewChord = !sameChordKey(outChord, m_lastChord);
    }
    m_lastChord = outChord;
    m_hasLastChord = true;
    return true;
}

void VirtuosoBalladMvpPlaybackEngine::onTick() {
    const int seqLen = m_sequence.size();
    if (!m_playing || seqLen <= 0) return;

    // Beat duration (quarter-note BPM); apply time signature denominator.
    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;

    const double quarterMs = 60000.0 / double(qMax(1, m_bpm));
    const double beatMs = quarterMs * (4.0 / double(qMax(1, ts.den)));

    const qint64 elapsedMs = m_engine.elapsedMs();
    const int stepNow = int(double(elapsedMs) / beatMs);

    const int total = seqLen * qMax(1, m_repeats);
    if (stepNow >= total) {
        stop();
        return;
    }

    // Update playhead highlight once per beat-step.
    if (stepNow != m_lastPlayheadStep) {
        m_lastPlayheadStep = stepNow;
        const int cellIndex = m_sequence[stepNow % seqLen];
        if (cellIndex != m_lastEmittedCell) {
            m_lastEmittedCell = cellIndex;
            emit currentCellChanged(cellIndex);
        }
    }

    // --- Lookahead plan (4 bars) for UI: emit a full fixed window as JSON array. ---
    // This is *not* scheduling: it has no side-effects on engine timing or planner state.
    {
        LookaheadPlanner::Inputs li;
        li.bpm = m_bpm;
        li.ts = ts;
        li.repeats = m_repeats;
        li.model = &m_model;
        li.sequence = &m_sequence;
        li.hasLastChord = m_hasLastChord;
        li.lastChord = m_lastChord;
        li.ontology = &m_ontology;
        li.hasKeyPcGuess = m_hasKeyPcGuess;
        li.keyPcGuess = m_keyPcGuess;
        li.keyScaleKey = m_keyScaleKey;
        li.keyScaleName = m_keyScaleName;
        li.keyMode = m_keyMode;
        li.localKeysByBar = &m_localKeysByBar;
        li.listener = &m_listener;
        li.vibe = &m_vibe;
        li.bassPlanner = &m_bassPlanner;
        li.pianoPlanner = &m_pianoPlanner;
        li.drummer = &m_drummer;
        li.chDrums = m_chDrums;
        li.chBass = m_chBass;
        li.chPiano = m_chPiano;
        li.stylePresetKey = m_stylePresetKey;
        li.agentEnergyMult = m_agentEnergyMult;
        li.debugEnergyAuto = m_debugEnergyAuto;
        li.debugEnergy = m_debugEnergy;
        li.virtAuto = m_virtAuto;
        li.virtHarmonicRisk = m_virtHarmonicRisk;
        li.virtRhythmicComplexity = m_virtRhythmicComplexity;
        li.virtInteraction = m_virtInteraction;
        li.virtToneDark = m_virtToneDark;
        li.engineNowMs = m_engine.elapsedMs();

        const QString json = LookaheadPlanner::buildLookaheadPlanJson(li, stepNow, /*horizonBars=*/4);
        if (!json.trimmed().isEmpty()) emit lookaheadPlanJson(json);
    }

    // Lookahead scheduling window (tight timing).
    constexpr int kLookaheadMs = 220;
    const int scheduleUntil = int(double(elapsedMs + kLookaheadMs) / beatMs);
    const int maxStepToSchedule = std::min(total - 1, scheduleUntil);

    while (m_nextScheduledStep <= maxStepToSchedule) {
        scheduleStep(m_nextScheduledStep, seqLen);
        m_nextScheduledStep++;
    }
}

void VirtuosoBalladMvpPlaybackEngine::scheduleStep(int stepIndex, int seqLen) {
    // "Playback position" defines absolute time (monotonic), independent of chart jumps (D.C./D.S.).
    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;

    const int playbackBarIndex = (ts.num > 0) ? (stepIndex / ts.num) : (stepIndex / 4);
    const int beatInBar = (ts.num > 0) ? (stepIndex % ts.num) : (stepIndex % 4);

    music::ChordSymbol chord;
    bool chordIsNew = false;
    const int cellIndex = m_sequence[stepIndex % seqLen];
    const bool haveChord = chordForCellIndex(cellIndex, chord, chordIsNew);

    // Update listener harmonic context for "playing outside" classification.
    if (haveChord && !chord.noChord) m_listener.setChordContext(chord);

    // Lookahead: next harmony boundary (prefer within-bar chord changes; fallback to barline).
    // IMPORTANT: do NOT call chordForCellIndex() here, because it mutates last-chord state.
    auto parseCellChordNoState = [&](int anyCellIndex, const music::ChordSymbol& fallback, bool* outIsExplicit = nullptr) -> music::ChordSymbol {
        if (outIsExplicit) *outIsExplicit = false;
        const chart::Cell* c = cellForFlattenedIndex(anyCellIndex);
        if (!c) return fallback;
        const QString t = c->chord.trimmed();
        if (t.isEmpty()) return fallback;
        music::ChordSymbol parsed;
        if (!music::parseChordSymbol(t, parsed)) return fallback;
        if (parsed.placeholder) return fallback;
        if (outIsExplicit) *outIsExplicit = true;
        return parsed;
    };

    music::ChordSymbol nextChord;
    bool haveNext = false;
    int beatsUntilChange = 0;
    if (seqLen > 0 && haveChord) {
        const int beatsPerBar = (ts.num > 0) ? ts.num : 4;
        const int total = seqLen * qMax(1, m_repeats);
        // 1) Within-bar scan for explicit chord change (prevents "old chord rings until beat 4" on mid-bar changes).
        const int maxLook = qMax(1, beatsPerBar - beatInBar);
        for (int k = 1; k <= maxLook; ++k) {
            const int stepFwd = stepIndex + k;
            if (stepFwd >= total) break;
            const int cellNext = m_sequence[stepFwd % seqLen];
            bool explicitNext = false;
            const music::ChordSymbol cand = parseCellChordNoState(cellNext, /*fallback*/chord, &explicitNext);
            if (!explicitNext || cand.noChord) continue;
            if (!sameChordKey(cand, chord)) {
                nextChord = cand;
                haveNext = true;
                beatsUntilChange = k;
                break;
            }
        }
        // 2) Fallback: barline chord (for bass approaches / phrase cadence intent).
        if (!haveNext) {
            const int stepNextBar = stepIndex + (beatsPerBar - beatInBar);
            if (stepNextBar < total) {
                const int cellNext = m_sequence[stepNextBar % seqLen];
                bool explicitNext = false;
                nextChord = parseCellChordNoState(cellNext, /*fallback*/chord, &explicitNext);
                haveNext = explicitNext || (nextChord.rootPc >= 0);
                if (nextChord.noChord) haveNext = false;
            }
        }
    }

    const bool nextChanges = haveNext && !nextChord.noChord && (nextChord.rootPc >= 0) &&
                             ((nextChord.rootPc != chord.rootPc) || (nextChord.bassPc != chord.bassPc));

    // Phrase model (v1): 4-bar phrases with cadence intent on bar 4 and setup on bar 3.
    const int phraseBars = 4;
    const int barInPhrase = (phraseBars > 0) ? (qMax(0, playbackBarIndex) % phraseBars) : 0;
    const bool phraseEndBar = (phraseBars > 0) ? (barInPhrase == (phraseBars - 1)) : false;
    const bool phraseSetupBar = (phraseBars > 1) ? (barInPhrase == (phraseBars - 2)) : false;
    double cadence01 = 0.0;
    if (phraseEndBar) cadence01 = (nextChanges || chordIsNew) ? 1.0 : 0.65;
    else if (phraseSetupBar) cadence01 = (nextChanges ? 0.60 : 0.35);

    const bool strongBeat = (beatInBar == 0 || beatInBar == 2);
    const bool structural = strongBeat || chordIsNew;

    // Snapshot live intent state once per scheduled step (ms domain).
    const qint64 nowMsWall = QDateTime::currentMSecsSinceEpoch();
    const auto intent = m_listener.compute(nowMsWall);
    auto vibeEff = m_vibe.update(intent, nowMsWall);
    const double baseEnergy = qBound(0.0, m_debugEnergyAuto ? vibeEff.energy : m_debugEnergy, 1.0);
    const QString vibeStr = m_debugEnergyAuto ? VibeStateMachine::vibeName(vibeEff.vibe)
                                              : (VibeStateMachine::vibeName(vibeEff.vibe) + " (manual)");

    const QString intentStr = intentsToString(intent);
    const bool userBusy = (intent.densityHigh || intent.intensityPeak || intent.registerHigh);

    // Debug UI status (emitted once per beat step).
    const QString virtStr = m_virtAuto
        ? "Virt=Auto"
        : QString("Virt=Manual r=%1 rc=%2 i=%3 t=%4")
              .arg(m_virtHarmonicRisk, 0, 'f', 2)
              .arg(m_virtRhythmicComplexity, 0, 'f', 2)
              .arg(m_virtInteraction, 0, 'f', 2)
              .arg(m_virtToneDark, 0, 'f', 2);
    emit debugStatus(QString("Preset=%1  Vibe=%2  energy=%3  %4  intents=%5  nps=%6  reg=%7  gVel=%8  cc2=%9  vNote=%10  silenceMs=%11  outside=%12")
                         .arg(m_stylePresetKey)
                         .arg(vibeStr)
                         .arg(baseEnergy, 0, 'f', 2)
                         .arg(virtStr)
                         .arg(intentStr.isEmpty() ? "-" : intentStr)
                         .arg(intent.notesPerSec, 0, 'f', 2)
                         .arg(intent.registerCenterMidi)
                         .arg(intent.lastGuitarVelocity)
                         .arg(intent.lastCc2)
                         .arg(intent.lastVoiceMidi)
                         .arg(intent.msSinceLastActivity == std::numeric_limits<qint64>::max() ? -1 : intent.msSinceLastActivity)
                         .arg(intent.outsideRatio, 0, 'f', 2));
    emit debugEnergy(baseEnergy, m_debugEnergyAuto);

    // Energy-driven instrument layering:
    // - energy ~0.0: piano only
    // - then bass enters
    // - then drums enter
    const double eBand = qBound(0.0, baseEnergy, 1.0);
    const bool allowBass = (eBand >= 0.10);
    const bool allowDrums = (eBand >= 0.22);

    // Drums: enter after bass (energy layering). We schedule Drums first when enabled,
    // because Bass groove-lock references Drums kick timing.
    const quint32 detSeed = virtuoso::util::StableHash::fnv1a32((QString("ballad|") + m_stylePresetKey).toUtf8());
    virtuoso::engine::AgentIntentNote kickIntent;
    virtuoso::groove::HumanizedEvent kickHe;
    bool haveKickHe = false;

    if (allowDrums) {
        BrushesBalladDrummer::Context dc;
        dc.bpm = m_bpm;
        dc.ts = ts;
        dc.playbackBarIndex = playbackBarIndex;
        dc.beatInBar = beatInBar;
        dc.structural = structural;
        dc.determinismSeed = detSeed ^ 0xD00D'BEEFu;
        dc.phraseBars = phraseBars;
        dc.barInPhrase = barInPhrase;
        dc.phraseEndBar = phraseEndBar;
        dc.cadence01 = cadence01;
        {
            const double mult = m_agentEnergyMult.value("Drums", 1.0);
            dc.energy = qBound(0.0, baseEnergy * mult, 1.0);
        }
        // Interaction ducking: when user is busy, keep drummer in timekeeper mode (avoid extra ride patterns).
        if (userBusy) dc.energy = qMin(dc.energy, 0.55);
        dc.intensityPeak = intent.intensityPeak;

        auto drumIntents = m_drummer.planBeat(dc);

        // If there is a kick intent, humanize/schedule it first so bass can lock to its onMs.
        int kickIndex = -1;
        for (int i = 0; i < drumIntents.size(); ++i) {
            if (drumIntents[i].note == m_noteKick) { kickIndex = i; break; }
        }
        if (kickIndex >= 0) {
            kickIntent = drumIntents[kickIndex];
            kickIntent.vibe_state = vibeStr;
            kickIntent.user_intents = intentStr;
            kickIntent.user_outside_ratio = intent.outsideRatio;
            kickHe = m_engine.humanizeIntent(kickIntent);
            haveKickHe = (kickHe.offMs > kickHe.onMs);
            if (haveKickHe) {
                m_engine.scheduleHumanizedIntentNote(kickIntent, kickHe);
            }
            drumIntents.removeAt(kickIndex);
        }

        for (auto n : drumIntents) {
            // Macro dynamics: scale drums velocity by energy (much softer at low energies).
            // e=0.22 -> ~0.67x, e=1.0 -> ~1.10x
            const double e = qBound(0.0, baseEnergy, 1.0);
            const double mult = 0.55 + 0.55 * e;
            n.baseVelocity = qBound(1, int(llround(double(n.baseVelocity) * mult)), 127);
            n.vibe_state = vibeStr;
            n.user_intents = intentStr;
            n.user_outside_ratio = intent.outsideRatio;
            m_engine.scheduleNote(n);
        }

        // IMPORTANT: single source of truth for drums is `BrushesBalladDrummer`.
        // We intentionally do not add any extra JSON-driven drum vocabulary here.
    }

    if (!haveChord || chord.noChord) return; // leave space on N.C.

    // Bass + piano planners (Ballad Brain v1).
    const QString chordText = chord.originalText.trimmed().isEmpty() ? QString("pc=%1").arg(chord.rootPc) : chord.originalText.trimmed();
    const int barIdx = cellIndex / 4;
    const playback::LocalKeyEstimate lk = (barIdx >= 0 && barIdx < m_localKeysByBar.size())
        ? m_localKeysByBar[barIdx]
        : playback::LocalKeyEstimate{m_keyPcGuess, m_keyScaleKey, m_keyScaleName, m_keyMode, 0.0, 0.0};
    const int keyPc = m_hasKeyPcGuess ? lk.tonicPc : normalizePc(chord.rootPc);
    const auto keyCenterStr = QString("%1 %2").arg(pcName(keyPc)).arg(lk.scaleName.isEmpty() ? QString("Ionian (Major)") : lk.scaleName);
    const auto* chordDef = chordDefForSymbol(m_ontology, chord);
    QString roman;
    QString func;
    const QString scaleUsed = (chordDef && chord.rootPc >= 0)
        ? chooseScaleUsedForChord(m_ontology, keyPc, lk.mode, chord, *chordDef, &roman, &func)
        : QString();
    const BalladRefTuning tune = tuningForReferenceTrack(m_stylePresetKey);

    JazzBalladBassPlanner::Context bc;
    bc.bpm = m_bpm;
    bc.playbackBarIndex = playbackBarIndex;
    bc.beatInBar = beatInBar;
    bc.chordIsNew = chordIsNew;
    bc.chord = chord;
    bc.hasNextChord = haveNext && !nextChord.noChord;
    bc.nextChord = nextChord;
    bc.chordText = chordText;
    bc.phraseBars = phraseBars;
    bc.barInPhrase = barInPhrase;
    bc.phraseEndBar = phraseEndBar;
    bc.cadence01 = cadence01;
    bc.determinismSeed = detSeed;
    bc.approachProbBeat3 = tune.bassApproachProbBeat3;
    bc.skipBeat3ProbStable = tune.bassSkipBeat3ProbStable;
    bc.allowApproachFromAbove = tune.bassAllowApproachFromAbove;
    bc.userDensityHigh = intent.densityHigh;
    bc.userIntensityPeak = intent.intensityPeak;
    bc.userSilence = intent.silence;
    bc.forceClimax = (baseEnergy >= 0.85);
    {
        const double mult = m_agentEnergyMult.value("Bass", 1.0);
        bc.energy = qBound(0.0, baseEnergy * mult, 1.0);
    }
    // Very low energy (before drums enter): keep bass extremely sparse/supportive.
    if (!allowDrums) {
        bc.energy *= 0.70;
        bc.rhythmicComplexity *= 0.55;
        bc.approachProbBeat3 *= 0.35;
        bc.skipBeat3ProbStable = qMin(0.98, bc.skipBeat3ProbStable + 0.12);
    }
    // Stage 2 context for Stage 3 solver.
    bc.chordFunction = func;
    bc.roman = roman;
    // Stage 3 solver weights.
    const double progress01 = qBound(0.0, double(qMax(0, playbackBarIndex)) / 24.0, 1.0);
    if (m_virtAuto) {
        // Preset-driven baseline + dynamic adjustments.
        bc.harmonicRisk = qBound(0.0, m_virtHarmonicRisk + 0.35 * bc.energy + 0.15 * progress01, 1.0);
        bc.rhythmicComplexity = qBound(0.0, m_virtRhythmicComplexity + 0.45 * bc.energy + 0.20 * progress01, 1.0);
        bc.interaction = qBound(0.0, m_virtInteraction + 0.30 * (intent.silence ? 1.0 : 0.0) + 0.10 * bc.energy, 1.0);
        bc.toneDark = qBound(0.0, m_virtToneDark + 0.15 * (1.0 - bc.energy), 1.0);
    } else {
        bc.harmonicRisk = m_virtHarmonicRisk;
        bc.rhythmicComplexity = m_virtRhythmicComplexity;
        bc.interaction = m_virtInteraction;
        bc.toneDark = m_virtToneDark;
    }
    // Interaction heuristics: when user is dense/high/intense, bass simplifies and avoids chromaticism.
    if (intent.densityHigh || intent.intensityPeak) {
        bc.approachProbBeat3 *= 0.35;
        bc.skipBeat3ProbStable = qMin(0.65, bc.skipBeat3ProbStable + 0.20);
    }
    // Phrase cadence: more motion into turnarounds (session bassist sets up the next bar).
    if (bc.cadence01 >= 0.55) {
        bc.approachProbBeat3 = qMin(1.0, bc.approachProbBeat3 + 0.25 * bc.cadence01);
        bc.skipBeat3ProbStable = qMax(0.0, bc.skipBeat3ProbStable - 0.15 * bc.cadence01);
    }
    // Interaction ducking: if user is busy, bass simplifies (less chromaticism/walk) and ducks motion.
    if (userBusy) {
        bc.approachProbBeat3 *= 0.35;
        bc.skipBeat3ProbStable = qMin(0.90, bc.skipBeat3ProbStable + 0.20);
        bc.rhythmicComplexity *= 0.35;
        bc.harmonicRisk *= 0.45;
        bc.cadence01 *= 0.55;
    }
    if (baseEnergy >= 0.85) {
        bc.approachProbBeat3 *= 0.60; // slightly more motion, but still ballad-safe
        bc.skipBeat3ProbStable = qMax(0.10, bc.skipBeat3ProbStable - 0.08);
    }
    if (baseEnergy >= 0.55 && baseEnergy < 0.85) {
        // Make Build audibly different: fewer omissions, slightly more motion.
        bc.approachProbBeat3 = qMin(1.0, bc.approachProbBeat3 + 0.12);
        bc.skipBeat3ProbStable = qMax(0.0, bc.skipBeat3ProbStable - 0.12);
    }
    if (allowBass) {
        auto bassIntents = m_bassPlanner.planBeat(bc, m_chBass, ts);
        for (auto& n : bassIntents) {
            if (!scaleUsed.isEmpty()) n.scale_used = scaleUsed;
            n.key_center = keyCenterStr;
            if (!roman.isEmpty()) n.roman = roman;
            if (!func.isEmpty()) n.chord_function = func;
            n.vibe_state = vibeStr;
            n.user_intents = intentStr;
            n.user_outside_ratio = intent.outsideRatio;
            n.has_virtuosity = true;
            n.virtuosity.harmonicRisk = bc.harmonicRisk;
            n.virtuosity.rhythmicComplexity = bc.rhythmicComplexity;
            n.virtuosity.interaction = bc.interaction;
            n.virtuosity.toneDark = bc.toneDark;
            // Macro dynamics: small velocity lift under higher energy.
            const double e = qBound(0.0, baseEnergy, 1.0);
            n.baseVelocity = qBound(1, int(llround(double(n.baseVelocity) * (0.90 + 0.25 * e))), 127);
            // Groove lock (prototype): on beat 1, if a feather kick exists, align bass attack to kick.
            if (m_kickLocksBass && beatInBar == 0 && haveKickHe) {
                auto bhe = m_engine.humanizeIntent(n);
                if (bhe.offMs > bhe.onMs) {
                    const qint64 delta = kickHe.onMs - bhe.onMs;
                    if (qAbs(delta) <= qMax<qint64>(0, m_kickLockMaxMs)) {
                        bhe.onMs += delta;
                        bhe.offMs += delta;
                        bhe.timing_offset_ms += int(delta);
                        const QString tag = n.logic_tag.isEmpty() ? "GrooveLock:Kick" : (n.logic_tag + "|GrooveLock:Kick");
                        m_engine.scheduleHumanizedIntentNote(n, bhe, tag);
                        continue;
                    }
                }
            }
            m_engine.scheduleNote(n);
        }
    }

    JazzBalladPianoPlanner::Context pc;
    pc.bpm = m_bpm;
    pc.playbackBarIndex = playbackBarIndex;
    pc.beatInBar = beatInBar;
    pc.chordIsNew = chordIsNew;
    pc.chord = chord;
    pc.chordText = chordText;
    pc.phraseBars = phraseBars;
    pc.barInPhrase = barInPhrase;
    pc.phraseEndBar = phraseEndBar;
    pc.cadence01 = cadence01;
    pc.hasKey = true;
    pc.keyTonicPc = lk.tonicPc;
    pc.keyMode = lk.mode;
    pc.hasNextChord = haveNext && !nextChord.noChord;
    pc.nextChord = nextChord;
    pc.nextChanges = nextChanges;
    pc.beatsUntilChordChange = beatsUntilChange;
    pc.determinismSeed = detSeed ^ 0xBADC0FFEu;
    pc.lhLo = tune.pianoLhLo; pc.lhHi = tune.pianoLhHi;
    pc.rhLo = tune.pianoRhLo; pc.rhHi = tune.pianoRhHi;
    pc.sparkleLo = tune.pianoSparkleLo; pc.sparkleHi = tune.pianoSparkleHi;
    pc.skipBeat2ProbStable = tune.pianoSkipBeat2ProbStable;
    pc.addSecondColorProb = tune.pianoAddSecondColorProb;
    pc.sparkleProbBeat4 = tune.pianoSparkleProbBeat4;
    pc.preferShells = tune.pianoPreferShells;
    pc.userDensityHigh = intent.densityHigh;
    pc.userIntensityPeak = intent.intensityPeak;
    pc.userRegisterHigh = intent.registerHigh;
    pc.userSilence = intent.silence;
    pc.forceClimax = (baseEnergy >= 0.85);
    {
        const double mult = m_agentEnergyMult.value("Piano", 1.0);
        pc.energy = qBound(0.0, baseEnergy * mult, 1.0);
    }
    // Very low energy: keep piano extremely sparse/soft and avoid bright features.
    if (eBand < 0.12) {
        pc.preferShells = true;
        pc.skipBeat2ProbStable = qMin(0.995, pc.skipBeat2ProbStable + 0.25);
        pc.sparkleProbBeat4 = 0.0;
        pc.rhythmicComplexity *= 0.30;
        pc.harmonicRisk *= 0.25;
        pc.cadence01 *= 0.65;
    }
    // Stage 3 solver weights (Virtuosity Matrix).
    const double progress01p = qBound(0.0, double(qMax(0, playbackBarIndex)) / 24.0, 1.0);
    if (m_virtAuto) {
        // Preset-driven baseline + dynamic adjustments.
        pc.harmonicRisk = qBound(0.0, m_virtHarmonicRisk + 0.40 * pc.energy + 0.20 * progress01p, 1.0);
        pc.rhythmicComplexity = qBound(0.0, m_virtRhythmicComplexity + 0.55 * pc.energy + 0.15 * progress01p, 1.0);
        pc.interaction = qBound(0.0, m_virtInteraction + 0.30 * (intent.silence ? 1.0 : 0.0) + 0.15 * pc.energy, 1.0);
        pc.toneDark = qBound(0.0, m_virtToneDark + 0.20 * (1.0 - pc.energy) + 0.10 * (intent.registerHigh ? 1.0 : 0.0), 1.0);
    } else {
        pc.harmonicRisk = m_virtHarmonicRisk;
        pc.rhythmicComplexity = m_virtRhythmicComplexity;
        pc.interaction = m_virtInteraction;
        pc.toneDark = m_virtToneDark;
    }
    // Interaction heuristics:
    // - User register high => piano stays lower (and reduces sparkle)
    // - User density high/intensity peak => piano comp sparser
    // - User silence => piano is allowed to fill slightly more
    if (intent.registerHigh) {
        pc.rhHi = qMax(pc.rhLo + 4, pc.rhHi - 6);
        pc.sparkleProbBeat4 *= 0.25;
    }
    if (intent.densityHigh || intent.intensityPeak) {
        pc.skipBeat2ProbStable = qMin(0.95, pc.skipBeat2ProbStable + 0.25);
        pc.preferShells = true;
        pc.sparkleProbBeat4 *= 0.20;
    } else if (intent.silence) {
        pc.skipBeat2ProbStable = qMax(0.0, pc.skipBeat2ProbStable - 0.12);
        pc.sparkleProbBeat4 = qMin(0.40, pc.sparkleProbBeat4 + 0.08);
    }
    if (vibeEff.vibe == VibeStateMachine::Vibe::Climax) {
        pc.skipBeat2ProbStable = qMax(0.0, pc.skipBeat2ProbStable - 0.10);
        pc.addSecondColorProb = qMin(0.65, pc.addSecondColorProb + 0.10);
        pc.sparkleProbBeat4 = qMin(0.55, pc.sparkleProbBeat4 + 0.08);
    }
    if (vibeEff.vibe == VibeStateMachine::Vibe::Build) {
        // Make Build audibly different without needing huge boost:
        // more comp density + color, but not full Climax.
        pc.skipBeat2ProbStable = qMax(0.0, pc.skipBeat2ProbStable - 0.18);
        pc.addSecondColorProb = qMin(0.60, pc.addSecondColorProb + 0.15);
        pc.sparkleProbBeat4 = qMin(0.45, pc.sparkleProbBeat4 + 0.10);
    }
    if (vibeEff.vibe == VibeStateMachine::Vibe::CoolDown) {
        pc.skipBeat2ProbStable = qMin(0.98, pc.skipBeat2ProbStable + 0.10);
        pc.sparkleProbBeat4 *= 0.20;
    }
    // Interaction ducking: if user is busy, pianist stays supportive (lower register + fewer RH ornaments).
    if (userBusy) {
        pc.preferShells = true;
        pc.skipBeat2ProbStable = qMin(0.98, pc.skipBeat2ProbStable + 0.18);
        pc.sparkleProbBeat4 *= 0.05;
        pc.rhHi = qMax(pc.rhLo + 4, pc.rhHi - 8);
        pc.rhythmicComplexity *= 0.35;
        pc.harmonicRisk *= 0.45;
        pc.cadence01 *= 0.55;
    }
    auto pianoIntents = m_pianoPlanner.planBeat(pc, m_chPiano, ts);
    for (auto& n : pianoIntents) {
        if (!scaleUsed.isEmpty()) n.scale_used = scaleUsed;
        n.key_center = keyCenterStr;
        if (!roman.isEmpty()) n.roman = roman;
        if (!func.isEmpty()) n.chord_function = func;
        n.vibe_state = vibeStr;
        n.user_intents = intentStr;
        n.user_outside_ratio = intent.outsideRatio;
        n.has_virtuosity = true;
        n.virtuosity.harmonicRisk = pc.harmonicRisk;
        n.virtuosity.rhythmicComplexity = pc.rhythmicComplexity;
        n.virtuosity.interaction = pc.interaction;
        n.virtuosity.toneDark = pc.toneDark;
        // Macro dynamics: piano a bit more responsive to vibe energy.
        const double e = qBound(0.0, vibeEff.energy, 1.0);
        n.baseVelocity = qBound(1, int(llround(double(n.baseVelocity) * (0.82 + 0.40 * e))), 127);
        m_engine.scheduleNote(n);
    }
}

int VirtuosoBalladMvpPlaybackEngine::thirdIntervalForQuality(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::Minor:
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished: return 3;
        case music::ChordQuality::Sus2: return 2;
        case music::ChordQuality::Sus4: return 5;
        case music::ChordQuality::Power5: return 0;
        default: return 4;
    }
}

int VirtuosoBalladMvpPlaybackEngine::seventhIntervalFor(const music::ChordSymbol& c) {
    if (c.seventh == music::SeventhQuality::Major7) return 11;
    if (c.seventh == music::SeventhQuality::Dim7) return 9;
    if (c.seventh == music::SeventhQuality::Minor7) return 10;
    return -1;
}

int VirtuosoBalladMvpPlaybackEngine::chooseBassMidi(int pc) {
    // Keep in a warm ballad range, roughly E1..E2.
    if (pc < 0) pc = 0;
    int midi = 36 + (pc % 12); // C2 base
    while (midi < 36) midi += 12;
    while (midi > 52) midi -= 12;
    return midi;
}

int VirtuosoBalladMvpPlaybackEngine::choosePianoMidi(int pc, int targetLow, int targetHigh) {
    if (pc < 0) pc = 0;
    int midi = targetLow + (pc - (targetLow % 12));
    while (midi < targetLow) midi += 12;
    while (midi > targetHigh) midi -= 12;
    return midi;
}

// NOTE: legacy MVP bass/piano scheduling functions removed in favor of planners.

} // namespace playback

