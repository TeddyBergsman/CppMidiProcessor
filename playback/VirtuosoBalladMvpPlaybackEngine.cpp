#include "playback/VirtuosoBalladMvpPlaybackEngine.h"

#include "midiprocessor.h"
#include "playback/AgentCoordinator.h"
#include "playback/HarmonyContext.h"
#include "playback/LookaheadPlanner.h"
#include "playback/SemanticMidiAnalyzer.h"
#include "playback/TransportTimeline.h"
#include "virtuoso/theory/FunctionalHarmony.h"
#include "virtuoso/theory/ScaleSuggester.h"

#include <QHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtGlobal>
#include <algorithm>

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

} // namespace

void VirtuosoBalladMvpPlaybackEngine::onGuitarNoteOn(int note, int vel) {
    m_interaction.ingestGuitarNoteOn(note, vel, QDateTime::currentMSecsSinceEpoch());
}

void VirtuosoBalladMvpPlaybackEngine::onGuitarNoteOff(int note) {
    m_interaction.ingestGuitarNoteOff(note, QDateTime::currentMSecsSinceEpoch());
}

void VirtuosoBalladMvpPlaybackEngine::onVoiceCc2Stream(int cc2) {
    m_interaction.ingestCc2(cc2, QDateTime::currentMSecsSinceEpoch());
}

void VirtuosoBalladMvpPlaybackEngine::onVoiceNoteOn(int note, int vel) {
    m_interaction.ingestVoiceNoteOn(note, vel, QDateTime::currentMSecsSinceEpoch());
}

void VirtuosoBalladMvpPlaybackEngine::onVoiceNoteOff(int note) {
    m_interaction.ingestVoiceNoteOff(note, QDateTime::currentMSecsSinceEpoch());
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
    m_pianoPlanner.setMotivicMemory(&m_motivicMemory);

    // Harmony context uses ontology as its substrate.
    m_harmony.setOntology(&m_ontology);
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
    li.hasLastChord = m_harmony.hasLastChord();
    li.lastChord = m_harmony.lastChord();
    li.ontology = &m_ontology;
    li.harmonyCtx = &m_harmony;
    li.keyWindowBars = 8;
    li.hasKeyPcGuess = m_harmony.hasKeyPcGuess();
    li.keyPcGuess = m_harmony.keyPcGuess();
    li.keyScaleKey = m_harmony.keyScaleKey();
    li.keyScaleName = m_harmony.keyScaleName();
    li.keyMode = m_harmony.keyMode();
    {
        const auto& lks = m_harmony.localKeysByBar();
        li.localKeysByBar = &lks;
    }
    li.listener = &m_interaction.listener();
    li.vibe = &m_interaction.vibe();
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
    m_transport.setModel(&m_model);
    rebuildSequence();

    virtuoso::groove::TimeSignature ts;
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;
    m_engine.setTimeSignature(ts);

    // Harmony analysis (global key + local keys).
    m_harmony.rebuildFromModel(m_model);
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
    m_harmony.resetRuntimeState();
    m_bassPlanner.reset();
    m_pianoPlanner.reset();
    m_interaction.reset();
    m_motivicMemory.clear();
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
    m_transport.rebuild();
    m_sequence = m_transport.sequence();
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

const chart::Cell* VirtuosoBalladMvpPlaybackEngine::cellForFlattenedIndex(int cellIndex) const {
    return m_transport.cellForFlattenedIndex(cellIndex);
}

bool VirtuosoBalladMvpPlaybackEngine::chordForCellIndex(int cellIndex, music::ChordSymbol& outChord, bool& isNewChord) {
    return m_harmony.chordForCellIndex(m_model, cellIndex, outChord, isNewChord);
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
        li.hasLastChord = m_harmony.hasLastChord();
        li.lastChord = m_harmony.lastChord();
        li.ontology = &m_ontology;
        li.harmonyCtx = &m_harmony;
        li.keyWindowBars = 8;
        li.hasKeyPcGuess = m_harmony.hasKeyPcGuess();
        li.keyPcGuess = m_harmony.keyPcGuess();
        li.keyScaleKey = m_harmony.keyScaleKey();
        li.keyScaleName = m_harmony.keyScaleName();
        li.keyMode = m_harmony.keyMode();
        {
            const auto& lks = m_harmony.localKeysByBar();
            li.localKeysByBar = &lks;
        }
        li.listener = &m_interaction.listener();
        li.vibe = &m_interaction.vibe();
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
    (void)seqLen;
    AgentCoordinator::Inputs ai;
    ai.owner = this;
    ai.model = &m_model;
    ai.sequence = &m_sequence;
    ai.repeats = m_repeats;
    ai.bpm = m_bpm;
    ai.stylePresetKey = m_stylePresetKey;
    ai.agentEnergyMult = m_agentEnergyMult;

    ai.virtAuto = m_virtAuto;
    ai.virtHarmonicRisk = m_virtHarmonicRisk;
    ai.virtRhythmicComplexity = m_virtRhythmicComplexity;
    ai.virtInteraction = m_virtInteraction;
    ai.virtToneDark = m_virtToneDark;

    ai.debugEnergyAuto = m_debugEnergyAuto;
    ai.debugEnergy = m_debugEnergy;

    ai.chDrums = m_chDrums;
    ai.chBass = m_chBass;
    ai.chPiano = m_chPiano;
    ai.noteKick = m_noteKick;
    ai.kickLocksBass = m_kickLocksBass;
    ai.kickLockMaxMs = m_kickLockMaxMs;

    ai.harmony = &m_harmony;
    ai.interaction = &m_interaction;
    ai.engine = &m_engine;
    ai.ontology = &m_ontology;
    ai.bassPlanner = &m_bassPlanner;
    ai.pianoPlanner = &m_pianoPlanner;
    ai.drummer = &m_drummer;
    ai.motivicMemory = &m_motivicMemory;

    AgentCoordinator::scheduleStep(ai, stepIndex);
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

