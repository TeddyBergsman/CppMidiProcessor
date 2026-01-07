#include "playback/LookaheadPlanner.h"

#include "playback/BrushesBalladDrummer.h"
#include "playback/HarmonyTypes.h"
#include "playback/JazzBalladBassPlanner.h"
#include "playback/JazzBalladPianoPlanner.h"
#include "playback/SemanticMidiAnalyzer.h"
#include "playback/VibeStateMachine.h"
#include "playback/InteractionContext.h"

#include "virtuoso/groove/GrooveGrid.h"
#include "virtuoso/theory/TheoryEvent.h"
#include "virtuoso/util/StableHash.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtGlobal>

namespace playback {
namespace {

static int adaptivePhraseBarsLocal(int bpm) {
    return (bpm <= 84) ? 8 : 4;
}

} // namespace

QString LookaheadPlanner::buildLookaheadPlanJson(const Inputs& in, int stepNow, int horizonBars) {
    if (!in.model || !in.sequence || in.sequence->isEmpty()) return {};
    if (!in.harmonyCtx) return {};
    if ((!in.listener && !in.hasIntentSnapshot) || (!in.vibe && !in.hasVibeSnapshot)) return {};
    if (!in.bassPlanner || !in.pianoPlanner || !in.drummer) return {};

    const QVector<int>& seq = *in.sequence;
    const int seqLen = seq.size();
    const int beatsPerBar = qMax(1, in.ts.num);
    const int total = seqLen * qMax(1, in.repeats);

    // Anchor to bar start so events persist for the UI.
    if (stepNow < 0) stepNow = 0;
    const int startStep = qMax(0, stepNow - (stepNow % beatsPerBar));
    const int horizonBeats = beatsPerBar * qMax(1, horizonBars);
    const int endStep = qMin(total, startStep + horizonBeats);

    // Snapshot interaction state once for this lookahead block (caller-controlled time).
    const qint64 nowMs = (in.nowMs > 0) ? in.nowMs : QDateTime::currentMSecsSinceEpoch();
    const auto intent = in.hasIntentSnapshot ? in.intentSnapshot : in.listener->compute(nowMs);
    const auto vibeEff = in.hasVibeSnapshot ? in.vibeSnapshot : ([&]() {
        // Lookahead must not mutate live vibe state.
        VibeStateMachine vibeSim = *in.vibe;
        return vibeSim.update(intent, nowMs);
    })();
    const double baseEnergy = qBound(0.0, in.debugEnergyAuto ? vibeEff.energy : in.debugEnergy, 1.0);
    const QString vibeStr = in.debugEnergyAuto ? VibeStateMachine::vibeName(vibeEff.vibe)
                                               : (VibeStateMachine::vibeName(vibeEff.vibe) + " (manual)");
    const QString intentStr = InteractionContext::intentsToString(intent);
    const bool userBusy = (intent.densityHigh || intent.intensityPeak || intent.registerHigh);

    // Clone planners so lookahead does not mutate live state.
    JazzBalladBassPlanner bassSim = *in.bassPlanner;
    JazzBalladPianoPlanner pianoSim = *in.pianoPlanner;

    // Local chord simulation baseline (do NOT mutate in.lastChord).
    music::ChordSymbol simLast = in.hasLastChord ? in.lastChord : music::ChordSymbol{};
    bool simHasLast = in.hasLastChord;

    auto parseCellChordNoStateLocal = [&](int anyCellIndex, const music::ChordSymbol& fallback, bool* outIsExplicit = nullptr) -> music::ChordSymbol {
        return in.harmonyCtx->parseCellChordNoState(*in.model, anyCellIndex, fallback, outIsExplicit);
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

    auto emitCcAsJson = [&](const QString& agent,
                            int channel,
                            int cc,
                            int value,
                            const virtuoso::groove::GridPos& pos,
                            const QString& logicTag) {
        virtuoso::theory::TheoryEvent te;
        te.event_kind = "cc";
        te.agent = agent;
        te.timestamp = "";
        te.logic_tag = logicTag;
        te.dynamic_marking = QString::number(value);
        te.grid_pos = virtuoso::groove::GrooveGrid::toString(pos, in.ts);
        te.channel = channel;
        te.note = -1;
        te.cc = cc;
        te.cc_value = value;
        te.tempo_bpm = in.bpm;
        te.ts_num = in.ts.num;
        te.ts_den = in.ts.den;
        te.engine_now_ms = in.engineNowMs;
        const qint64 on = virtuoso::groove::GrooveGrid::posToMs(pos, in.ts, in.bpm);
        te.on_ms = on;
        te.off_ms = on; // actions are instantaneous in the plan view
        te.vibe_state = vibeStr;
        te.user_intents = intentStr;
        te.user_outside_ratio = intent.outsideRatio;
        arr.push_back(te.toJsonObject());
    };

    auto emitKeyswitchAsJson = [&](const QString& agent,
                                   int channel,
                                   int note,
                                   const virtuoso::groove::GridPos& pos,
                                   const QString& logicTag) {
        virtuoso::theory::TheoryEvent te;
        te.event_kind = "keyswitch";
        te.agent = agent;
        te.timestamp = "";
        te.logic_tag = logicTag;
        te.dynamic_marking = "1";
        te.grid_pos = virtuoso::groove::GrooveGrid::toString(pos, in.ts);
        te.channel = channel;
        te.note = note;
        te.tempo_bpm = in.bpm;
        te.ts_num = in.ts.num;
        te.ts_den = in.ts.den;
        te.engine_now_ms = in.engineNowMs;
        const double quarterMs = 60000.0 / double(qMax(1, in.bpm));
        const double beatMs = quarterMs * (4.0 / double(qMax(1, in.ts.den)));
        const qint64 eighthMs = qMax<qint64>(30, qint64(llround(beatMs / 2.0)));
        const qint64 sixteenthMs = qMax<qint64>(20, qint64(llround(beatMs / 4.0)));

        const qint64 baseOn = virtuoso::groove::GrooveGrid::posToMs(pos, in.ts, in.bpm);
        qint64 on = baseOn;
        qint64 off = baseOn + 24;
        // Visualize keyswitch lead times in musical subdivisions (not ms):
        // LS/HP need a bigger pre-trigger window, Sus/PM a smaller one.
        if (logicTag.endsWith(":LS") || logicTag.endsWith(":HP")) {
            // For two-feel, the relevant "previous note" is typically 2 beats earlier (beat1->beat3),
            // so visualize these keyswitches with a larger lead.
            on = qMax<qint64>(0, baseOn - qint64(llround(beatMs * 2.0)));
            off = baseOn + sixteenthMs;
        } else if (logicTag.endsWith(":Sus") || logicTag.endsWith(":PM") || logicTag.contains("PM_Ghost")) {
            on = qMax<qint64>(0, baseOn - sixteenthMs);
            off = baseOn + 24;
        }
        te.on_ms = on;
        te.off_ms = off;
        te.vibe_state = vibeStr;
        te.user_intents = intentStr;
        te.user_outside_ratio = intent.outsideRatio;
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
            else chordIsNew = explicitChord && !HarmonyContext::sameChordKey(chord, simLast);
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
                if (!HarmonyContext::sameChordKey(cand, chord)) {
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

        // Phrase model: adaptive 4–8 bars (tempo-based).
        const int phraseBars = adaptivePhraseBarsLocal(in.bpm);
        const int barInPhrase = (phraseBars > 0) ? (qMax(0, playbackBarIndex) % phraseBars) : 0;
        const bool phraseEndBar = (phraseBars > 0) ? (barInPhrase == (phraseBars - 1)) : false;
        const bool phraseSetupBar = (phraseBars > 1) ? (barInPhrase == (phraseBars - 2)) : false;
        double cadence01 = 0.0;
        if (phraseEndBar) cadence01 = (nextChanges || chordIsNew) ? 1.0 : 0.65;
        else if (phraseSetupBar) cadence01 = (nextChanges ? 0.60 : 0.35);

        const QString chordText = chord.originalText.trimmed().isEmpty() ? QString("pc=%1").arg(chord.rootPc) : chord.originalText.trimmed();
        const bool strongBeat = (beatInBar == 0 || beatInBar == 2);
        const bool structural = strongBeat || chordIsNew;

        // Key context (sliding window).
        const int barIdx = cellIndex / 4;
        const LocalKeyEstimate lk = in.harmonyCtx->estimateLocalKeyWindow(*in.model, barIdx, qMax(1, in.keyWindowBars));
        const int keyPc = in.harmonyCtx->hasKeyPcGuess() ? lk.tonicPc : HarmonyContext::normalizePc(chord.rootPc);
        const QString keyCenterStr = QString("%1 %2")
                                         .arg(HarmonyContext::pcName(keyPc))
                                         .arg(lk.scaleName.isEmpty() ? QString("Ionian (Major)") : lk.scaleName);

        const auto* chordDef = in.harmonyCtx->chordDefForSymbol(chord);
        QString roman;
        QString func;
        const QString scaleUsed = (chordDef && chord.rootPc >= 0)
            ? in.harmonyCtx->chooseScaleUsedForChord(keyPc, lk.mode, chord, *chordDef, &roman, &func)
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

            const auto bplan = bassSim.planBeatWithActions(bc, in.chBass, in.ts);
            for (const auto& ks : bplan.keyswitches) {
                // keyswitches may include visualization-only markers (midi < 0)
                emitKeyswitchAsJson("Bass", in.chBass, ks.midi, ks.startPos, ks.logic_tag);
            }
            auto bnotes = bplan.notes;
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
            for (auto n : bplan.fxNotes) {
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

            const auto pplan = pianoSim.planBeatWithActions(pc, in.chPiano, in.ts);
            for (const auto& ci : pplan.ccs) {
                emitCcAsJson("Piano", in.chPiano, ci.cc, ci.value, ci.startPos, ci.logic_tag);
            }
            auto pnotes = pplan.notes;
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

