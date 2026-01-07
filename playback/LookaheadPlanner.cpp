#include "playback/LookaheadPlanner.h"

#include "playback/BrushesBalladDrummer.h"
#include "playback/HarmonyTypes.h"
#include "playback/JazzBalladBassPlanner.h"
#include "playback/JazzBalladPianoPlanner.h"
#include "playback/SemanticMidiAnalyzer.h"
#include "playback/VibeStateMachine.h"

#include "virtuoso/groove/GrooveGrid.h"
#include "virtuoso/theory/ScaleSuggester.h"
#include "virtuoso/theory/TheoryEvent.h"
#include "virtuoso/util/StableHash.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QtGlobal>
#include <algorithm>

namespace playback {
namespace {

static QVector<const chart::Bar*> flattenBarsFrom(const chart::ChartModel& model) {
    QVector<const chart::Bar*> bars;
    for (const auto& line : model.lines) {
        for (const auto& bar : line.bars) bars.push_back(&bar);
    }
    return bars;
}

static const chart::Cell* cellForFlattenedIndex(const chart::ChartModel& model, int cellIndex) {
    if (cellIndex < 0) return nullptr;
    const QVector<const chart::Bar*> bars = flattenBarsFrom(model);
    const int barIndex = cellIndex / 4;
    const int cellInBar = cellIndex % 4;
    if (barIndex < 0 || barIndex >= bars.size()) return nullptr;
    const auto* bar = bars[barIndex];
    if (!bar) return nullptr;
    if (cellInBar < 0 || cellInBar >= bar->cells.size()) return nullptr;
    return &bar->cells[cellInBar];
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

static bool sameChordKey(const music::ChordSymbol& a, const music::ChordSymbol& b) {
    return (a.rootPc == b.rootPc && a.bassPc == b.bassPc && a.quality == b.quality && a.seventh == b.seventh && a.extension == b.extension && a.alt == b.alt);
}

static QString ontologyChordKeyFor(const music::ChordSymbol& c) {
    using music::ChordQuality;
    using music::SeventhQuality;
    if (c.noChord || c.placeholder) return {};
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
    if (c.quality == ChordQuality::HalfDiminished) return "m7b5";
    if (c.quality == ChordQuality::Diminished) {
        if (c.seventh == SeventhQuality::Dim7) return "dim7";
        return (c.extension >= 7) ? "dim7" : "dim";
    }
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
    if (c.quality == ChordQuality::Major) {
        bool hasSharp11 = false;
        for (const auto& a : c.alterations) if (a.degree == 11 && a.delta > 0) hasSharp11 = true;
        if (c.extension >= 13 && hasSharp11) return "maj13#11";
        if (c.extension >= 13) return "maj13";
        if (c.extension >= 11) return "maj11";
        if (c.extension >= 9 && hasSharp11) return "maj9#11";
        if (c.extension >= 9) return "maj9";
        if (c.seventh == SeventhQuality::Major7 || c.extension >= 7) return "maj7";
        if (c.extension >= 6) return "6";
        return "maj";
    }
    if (c.quality == ChordQuality::Sus2) return "sus2";
    if (c.quality == ChordQuality::Sus4) {
        if (c.extension >= 13) return "13sus4";
        if (c.extension >= 9) return "9sus4";
        if (c.seventh == SeventhQuality::Minor7 || c.extension >= 7) return "7sus4";
        return "sus4";
    }
    if (c.quality == ChordQuality::Augmented) {
        if (c.seventh == SeventhQuality::Minor7 || c.extension >= 7) return "aug7";
        return "aug";
    }
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

static QString chooseScaleUsedForChord(const virtuoso::ontology::OntologyRegistry& reg,
                                       int keyPc,
                                       virtuoso::theory::KeyMode keyMode,
                                       const music::ChordSymbol& chordSym,
                                       const virtuoso::ontology::ChordDef& chordDef,
                                       QString* outRoman,
                                       QString* outFunction) {
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
    const QVector<QString> hints = virtuoso::theory::explicitHintScalesForContext(QString(), chordKey);
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

QString LookaheadPlanner::buildLookaheadPlanJson(const Inputs& in, int stepNow, int horizonBars) {
    if (!in.model || !in.sequence || in.sequence->isEmpty()) return {};
    if (!in.listener || !in.vibe || !in.bassPlanner || !in.pianoPlanner || !in.drummer) return {};
    if (!in.ontology) return {};

    const QVector<int>& seq = *in.sequence;
    const int seqLen = seq.size();
    const int beatsPerBar = qMax(1, in.ts.num);
    const int total = seqLen * qMax(1, in.repeats);

    // Anchor to bar start so events persist for the UI.
    if (stepNow < 0) stepNow = 0;
    const int startStep = qMax(0, stepNow - (stepNow % beatsPerBar));
    const int horizonBeats = beatsPerBar * qMax(1, horizonBars);
    const int endStep = qMin(total, startStep + horizonBeats);

    // Snapshot interaction state once for this lookahead block.
    const qint64 nowMsWall = QDateTime::currentMSecsSinceEpoch();
    const auto intent = in.listener->compute(nowMsWall);
    const auto vibeEff = in.vibe->update(intent, nowMsWall);
    const double baseEnergy = qBound(0.0, in.debugEnergyAuto ? vibeEff.energy : in.debugEnergy, 1.0);
    const QString vibeStr = in.debugEnergyAuto ? VibeStateMachine::vibeName(vibeEff.vibe)
                                               : (VibeStateMachine::vibeName(vibeEff.vibe) + " (manual)");
    const QString intentStr = intentsToString(intent);
    const bool userBusy = (intent.densityHigh || intent.intensityPeak || intent.registerHigh);

    // Clone planners so lookahead does not mutate live state.
    JazzBalladBassPlanner bassSim = *in.bassPlanner;
    JazzBalladPianoPlanner pianoSim = *in.pianoPlanner;

    // Local chord simulation baseline (do NOT mutate in.lastChord).
    music::ChordSymbol simLast = in.hasLastChord ? in.lastChord : music::ChordSymbol{};
    bool simHasLast = in.hasLastChord;

    auto parseCellChordNoStateLocal = [&](int anyCellIndex, const music::ChordSymbol& fallback, bool* outIsExplicit = nullptr) -> music::ChordSymbol {
        if (outIsExplicit) *outIsExplicit = false;
        const chart::Cell* c = cellForFlattenedIndex(*in.model, anyCellIndex);
        if (!c) return fallback;
        const QString t = c->chord.trimmed();
        if (t.isEmpty()) return fallback;
        music::ChordSymbol parsed;
        if (!music::parseChordSymbol(t, parsed)) return fallback;
        if (parsed.placeholder) return fallback;
        if (outIsExplicit) *outIsExplicit = true;
        return parsed;
    };

    QJsonArray arr;

    auto emitIntentAsJson = [&](const virtuoso::engine::AgentIntentNote& n) {
        virtuoso::theory::TheoryEvent te;
        te.agent = n.agent;
        te.timestamp = ""; // UI uses on_ms/grid_pos
        te.chord_context = n.chord_context;
        te.scale_used = n.scale_used;
        te.key_center = n.key_center;
        te.roman = n.roman;
        te.chord_function = n.chord_function;
        te.voicing_type = n.voicing_type;
        te.logic_tag = n.logic_tag;
        te.target_note = n.target_note;
        te.dynamic_marking = QString::number(n.baseVelocity);
        te.grid_pos = virtuoso::groove::GrooveGrid::toString(n.startPos, in.ts);
        te.channel = n.channel;
        te.note = n.note;
        te.tempo_bpm = in.bpm;
        te.ts_num = in.ts.num;
        te.ts_den = in.ts.den;
        te.engine_now_ms = in.engineNowMs;
        // Plan timing is grid-accurate (no micro jitter).
        const qint64 on = virtuoso::groove::GrooveGrid::posToMs(n.startPos, in.ts, in.bpm);
        const qint64 off = on + qMax<qint64>(1, virtuoso::groove::GrooveGrid::wholeNotesToMs(n.durationWhole, in.bpm));
        te.on_ms = on;
        te.off_ms = off;
        te.vibe_state = vibeStr;
        te.user_intents = intentStr;
        te.user_outside_ratio = intent.outsideRatio;
        te.has_virtuosity = n.has_virtuosity;
        te.virtuosity = n.virtuosity;
        arr.push_back(te.toJsonObject());
    };

    for (int step = startStep; step < endStep; ++step) {
        const int playbackBarIndex = step / beatsPerBar;
        const int beatInBar = step % beatsPerBar;
        const int cellIndex = seq[step % seqLen];

        // Determine chord and chordIsNew in this simulated stream.
        music::ChordSymbol chord = simHasLast ? simLast : music::ChordSymbol{};
        bool chordIsNew = false;
        {
            bool explicitChord = false;
            const music::ChordSymbol parsed = parseCellChordNoStateLocal(cellIndex, chord, &explicitChord);
            if (explicitChord) chord = parsed;
            if (!simHasLast) chordIsNew = explicitChord;
            else chordIsNew = explicitChord && !sameChordKey(chord, simLast);
            if (explicitChord) { simLast = chord; simHasLast = true; }
        }
        if (!simHasLast) continue;

        // Next chord boundary (prefer within-bar explicit change; fallback to barline).
        music::ChordSymbol nextChord = chord;
        bool haveNext = false;
        int beatsUntilChange = 0;
        {
            const int maxLook = qMax(1, beatsPerBar - beatInBar);
            for (int k = 1; k <= maxLook; ++k) {
                const int stepFwd = step + k;
                if (stepFwd >= total) break;
                const int cellNext = seq[stepFwd % seqLen];
                bool explicitNext = false;
                const music::ChordSymbol cand = parseCellChordNoStateLocal(cellNext, chord, &explicitNext);
                if (!explicitNext || cand.noChord) continue;
                if (!sameChordKey(cand, chord)) {
                    nextChord = cand;
                    haveNext = true;
                    beatsUntilChange = k;
                    break;
                }
            }
            if (!haveNext) {
                const int stepNextBar = step + (beatsPerBar - beatInBar);
                if (stepNextBar < total) {
                    const int cellNext = seq[stepNextBar % seqLen];
                    bool explicitNext = false;
                    nextChord = parseCellChordNoStateLocal(cellNext, chord, &explicitNext);
                    haveNext = explicitNext || (nextChord.rootPc >= 0);
                    if (nextChord.noChord) haveNext = false;
                }
            }
        }

        const bool nextChanges = haveNext && !nextChord.noChord && (nextChord.rootPc >= 0) &&
                                 ((nextChord.rootPc != chord.rootPc) || (nextChord.bassPc != chord.bassPc));

        // Phrase model (v1): 4-bar phrases.
        const int phraseBars = 4;
        const int barInPhrase = (phraseBars > 0) ? (qMax(0, playbackBarIndex) % phraseBars) : 0;
        const bool phraseEndBar = (phraseBars > 0) ? (barInPhrase == (phraseBars - 1)) : false;
        const bool phraseSetupBar = (phraseBars > 1) ? (barInPhrase == (phraseBars - 2)) : false;
        double cadence01 = 0.0;
        if (phraseEndBar) cadence01 = (nextChanges || chordIsNew) ? 1.0 : 0.65;
        else if (phraseSetupBar) cadence01 = (nextChanges ? 0.60 : 0.35);

        const QString chordText = chord.originalText.trimmed().isEmpty() ? QString("pc=%1").arg(chord.rootPc) : chord.originalText.trimmed();
        const bool strongBeat = (beatInBar == 0 || beatInBar == 2);
        const bool structural = strongBeat || chordIsNew;

        // Key context (sliding window preferred).
        const int barIdx = cellIndex / 4;
        const LocalKeyEstimate lk = (in.harmonyCtx)
            ? in.harmonyCtx->estimateLocalKeyWindow(*in.model, barIdx, qMax(1, in.keyWindowBars))
            : ((in.localKeysByBar && barIdx >= 0 && barIdx < in.localKeysByBar->size())
                   ? (*in.localKeysByBar)[barIdx]
                   : LocalKeyEstimate{in.keyPcGuess, in.keyScaleKey, in.keyScaleName, in.keyMode, 0.0, 0.0});
        const int keyPc = in.hasKeyPcGuess ? lk.tonicPc : normalizePc(chord.rootPc);
        const QString keyCenterStr = QString("%1 %2").arg(pcName(keyPc)).arg(lk.scaleName.isEmpty() ? QString("Ionian (Major)") : lk.scaleName);

        const auto* chordDef = chordDefForSymbol(*in.ontology, chord);
        QString roman;
        QString func;
        const QString scaleUsed = (chordDef && chord.rootPc >= 0)
            ? chooseScaleUsedForChord(*in.ontology, keyPc, lk.mode, chord, *chordDef, &roman, &func)
            : QString();

        // Drums
        {
            BrushesBalladDrummer::Context dc;
            dc.bpm = in.bpm;
            dc.ts = in.ts;
            dc.playbackBarIndex = playbackBarIndex;
            dc.beatInBar = beatInBar;
            dc.structural = structural;
            const quint32 detSeed = virtuoso::util::StableHash::fnv1a32((QString("ballad|") + in.stylePresetKey).toUtf8());
            dc.determinismSeed = detSeed ^ 0xD00D'BEEFu;
            dc.phraseBars = phraseBars;
            dc.barInPhrase = barInPhrase;
            dc.phraseEndBar = phraseEndBar;
            dc.cadence01 = cadence01;
            const double mult = in.agentEnergyMult.value("Drums", 1.0);
            dc.energy = qBound(0.0, baseEnergy * mult, 1.0);
            if (userBusy) dc.energy = qMin(dc.energy, 0.55);
            dc.intensityPeak = intent.intensityPeak;
            const auto dnotes = in.drummer->planBeat(dc);
            for (auto n : dnotes) emitIntentAsJson(n);
        }

        // Bass + piano
        if (!chord.noChord) {
            const quint32 detSeed = virtuoso::util::StableHash::fnv1a32((QString("ballad|") + in.stylePresetKey).toUtf8());

            JazzBalladBassPlanner::Context bc;
            bc.bpm = in.bpm;
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
            bc.userDensityHigh = intent.densityHigh;
            bc.userIntensityPeak = intent.intensityPeak;
            bc.userSilence = intent.silence;
            bc.forceClimax = (baseEnergy >= 0.85);
            bc.chordFunction = func;
            bc.roman = roman;
            const double bassMult = in.agentEnergyMult.value("Bass", 1.0);
            bc.energy = qBound(0.0, baseEnergy * bassMult, 1.0);

            const double progress01 = qBound(0.0, double(qMax(0, playbackBarIndex)) / 24.0, 1.0);
            if (in.virtAuto) {
                bc.harmonicRisk = qBound(0.0, in.virtHarmonicRisk + 0.35 * bc.energy + 0.15 * progress01, 1.0);
                bc.rhythmicComplexity = qBound(0.0, in.virtRhythmicComplexity + 0.45 * bc.energy + 0.20 * progress01, 1.0);
                bc.interaction = qBound(0.0, in.virtInteraction + 0.30 * (intent.silence ? 1.0 : 0.0) + 0.10 * bc.energy, 1.0);
                bc.toneDark = qBound(0.0, in.virtToneDark + 0.15 * (1.0 - bc.energy), 1.0);
            } else {
                bc.harmonicRisk = in.virtHarmonicRisk;
                bc.rhythmicComplexity = in.virtRhythmicComplexity;
                bc.interaction = in.virtInteraction;
                bc.toneDark = in.virtToneDark;
            }

            auto bnotes = bassSim.planBeat(bc, in.chBass, in.ts);
            for (auto& n : bnotes) {
                n.key_center = keyCenterStr;
                if (!roman.isEmpty()) n.roman = roman;
                if (!func.isEmpty()) n.chord_function = func;
                if (!scaleUsed.isEmpty()) n.scale_used = scaleUsed;
                n.has_virtuosity = true;
                n.virtuosity.harmonicRisk = bc.harmonicRisk;
                n.virtuosity.rhythmicComplexity = bc.rhythmicComplexity;
                n.virtuosity.interaction = bc.interaction;
                n.virtuosity.toneDark = bc.toneDark;
                emitIntentAsJson(n);
            }

            JazzBalladPianoPlanner::Context pc;
            pc.bpm = in.bpm;
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
            pc.userDensityHigh = intent.densityHigh;
            pc.userIntensityPeak = intent.intensityPeak;
            pc.userRegisterHigh = intent.registerHigh;
            pc.userSilence = intent.silence;
            pc.forceClimax = (baseEnergy >= 0.85);
            const double pianoMult = in.agentEnergyMult.value("Piano", 1.0);
            pc.energy = qBound(0.0, baseEnergy * pianoMult, 1.0);

            const double progress01p = qBound(0.0, double(qMax(0, playbackBarIndex)) / 24.0, 1.0);
            if (in.virtAuto) {
                pc.harmonicRisk = qBound(0.0, in.virtHarmonicRisk + 0.40 * pc.energy + 0.20 * progress01p, 1.0);
                pc.rhythmicComplexity = qBound(0.0, in.virtRhythmicComplexity + 0.55 * pc.energy + 0.15 * progress01p, 1.0);
                pc.interaction = qBound(0.0, in.virtInteraction + 0.30 * (intent.silence ? 1.0 : 0.0) + 0.15 * pc.energy, 1.0);
                pc.toneDark = qBound(0.0, in.virtToneDark + 0.20 * (1.0 - pc.energy) + 0.10 * (intent.registerHigh ? 1.0 : 0.0), 1.0);
            } else {
                pc.harmonicRisk = in.virtHarmonicRisk;
                pc.rhythmicComplexity = in.virtRhythmicComplexity;
                pc.interaction = in.virtInteraction;
                pc.toneDark = in.virtToneDark;
            }

            auto pnotes = pianoSim.planBeat(pc, in.chPiano, in.ts);
            for (auto& n : pnotes) {
                n.key_center = keyCenterStr;
                if (!roman.isEmpty()) n.roman = roman;
                if (!func.isEmpty()) n.chord_function = func;
                if (!scaleUsed.isEmpty()) n.scale_used = scaleUsed;
                n.has_virtuosity = true;
                n.virtuosity.harmonicRisk = pc.harmonicRisk;
                n.virtuosity.rhythmicComplexity = pc.rhythmicComplexity;
                n.virtuosity.interaction = pc.interaction;
                n.virtuosity.toneDark = pc.toneDark;
                emitIntentAsJson(n);
            }
        }
    }

    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

} // namespace playback

