#include "playback/JointPhrasePlanner.h"

#include "playback/BalladReferenceTuning.h"
#include "playback/JointCandidateModel.h"
#include "playback/LookaheadWindow.h"
#include "virtuoso/solver/BeatCostModel.h"
#include "virtuoso/util/StableHash.h"

#include <QDateTime>
#include <QtGlobal>
#include <algorithm>

namespace playback {
namespace {

static virtuoso::groove::TimeSignature timeSigFromModel(const chart::ChartModel& model) {
    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (model.timeSigNum > 0) ? model.timeSigNum : 4;
    ts.den = (model.timeSigDen > 0) ? model.timeSigDen : 4;
    return ts;
}

struct BeamNode {
    double cost = 0.0;
    JazzBalladBassPlanner::PlannerState bassState;
    JazzBalladPianoPlanner::PlannerState pianoState;
    int lastBassCenter = 45;
    int lastPianoCenter = 72;
    QVector<StoryState::JointStepChoice> choices;

    QString lastBassId;
    QString lastPianoId;
    QString lastDrumsId;
};

} // namespace

QVector<StoryState::JointStepChoice> JointPhrasePlanner::plan(const Inputs& p) {
    if (!p.in.model || !p.in.sequence || p.in.sequence->isEmpty()) return {};
    if (!p.in.harmony || !p.in.interaction || !p.in.engine || !p.in.ontology) return {};
    if (!p.in.bassPlanner || !p.in.pianoPlanner || !p.in.drummer) return {};

    const auto& in = p.in;
    const QVector<int>& seq = *in.sequence;
    const virtuoso::groove::TimeSignature ts = timeSigFromModel(*in.model);
    const int beatsPerBar = qMax(1, ts.num);

    // Snapshot interaction once for the phrase plan (glass-box: keep it stable).
    const auto snap = in.interaction->snapshot(QDateTime::currentMSecsSinceEpoch(), in.debugEnergyAuto, in.debugEnergy);
    const auto intent = snap.intent;
    const auto vibeEff = snap.vibe;
    const double baseEnergy = snap.energy01;
    const bool userBusy = snap.userBusy;
    const int responseUntilBar = (in.story ? in.story->responseUntilBar : -1);
    const bool allowDrums = (qBound(0.0, baseEnergy, 1.0) >= 0.22);

    // Determine phrase length (adaptive 4–8 bars already decided by caller).
    const int steps = qMax(1, p.steps);
    const int beamWidth = qBound(2, p.beamWidth, 12);

    // Determinism seed.
    const quint32 detSeed = virtuoso::util::StableHash::fnv1a32((QString("ballad|") + in.stylePresetKey).toUtf8());

    // Starting planner states (live continuity).
    const auto bassStart = in.bassPlanner->snapshotState();
    const auto pianoStart = in.pianoPlanner->snapshotState();

    BeamNode root;
    root.cost = 0.0;
    root.bassState = bassStart;
    root.pianoState = pianoStart;
    root.lastBassCenter = (in.story ? qBound(28, in.story->lastBassCenterMidi, 67) : 45);
    root.lastPianoCenter = (in.story ? qBound(48, in.story->lastPianoCenterMidi, 96) : 72);
    QVector<BeamNode> beam;
    beam.push_back(root);

    for (int si = 0; si < steps; ++si) {
        const int stepIndex = p.startStep + si;
        auto look = buildLookaheadWindow(*in.model, seq, in.repeats, stepIndex, /*horizonBars=*/8,
                                         /*phraseBars=*/qBound(4, in.story ? in.story->phraseBars : 4, 8),
                                         /*keyWindowBars=*/8, *in.harmony);
        if (!look.haveCurrentChord || look.currentChord.noChord) {
            // If harmony is missing, keep previous choices and skip.
            QVector<BeamNode> nextBeam = beam;
            for (auto& n : nextBeam) {
                StoryState::JointStepChoice c;
                c.stepIndex = stepIndex;
                c.bassId = n.lastBassId;
                c.pianoId = n.lastPianoId;
                c.drumsId = n.lastDrumsId;
                c.costTag = "no_chord";
                n.choices.push_back(c);
            }
            beam = nextBeam;
            continue;
        }

        const int playbackBarIndex = stepIndex / beatsPerBar;
        const int beatInBar = stepIndex % beatsPerBar;
        const bool phraseSetupBar = (look.phraseBars > 1) ? (look.barInPhrase == (look.phraseBars - 2)) : false;

        // Build base contexts (same as AgentCoordinator, but without scheduling).
        const QString chordText = look.currentChord.originalText.trimmed().isEmpty()
            ? QString("pc=%1").arg(look.currentChord.rootPc)
            : look.currentChord.originalText.trimmed();

        const auto* chordDef = in.harmony->chordDefForSymbol(look.currentChord);
        QString roman;
        QString func;
        const int keyPc = in.harmony->hasKeyPcGuess() ? look.key.tonicPc : HarmonyContext::normalizePc(look.currentChord.rootPc);
        const auto scaleChoice = (chordDef && look.currentChord.rootPc >= 0)
            ? in.harmony->chooseScaleForChord(keyPc, look.key.mode, look.currentChord, *chordDef, &roman, &func)
            : HarmonyContext::ScaleChoice{};
        const QString scaleUsed = scaleChoice.display;

        const BalladRefTuning tune = tuningForReferenceTrack(in.stylePresetKey);

        JazzBalladBassPlanner::Context bc;
        bc.bpm = in.bpm;
        bc.playbackBarIndex = playbackBarIndex;
        bc.beatInBar = beatInBar;
        bc.chordIsNew = look.chordIsNew;
        bc.chord = look.currentChord;
        bc.hasNextChord = look.haveNextChord && !look.nextChord.noChord;
        bc.nextChord = look.nextChord;
        bc.chordText = chordText;
        bc.phraseBars = look.phraseBars;
        bc.barInPhrase = look.barInPhrase;
        bc.phraseEndBar = look.phraseEndBar;
        bc.cadence01 = look.cadence01;
        bc.registerCenterMidi = (in.story ? qBound(28, in.story->lastBassCenterMidi, 67) : 45);
        bc.determinismSeed = detSeed;
        bc.approachProbBeat3 = tune.bassApproachProbBeat3;
        bc.skipBeat3ProbStable = tune.bassSkipBeat3ProbStable;
        bc.allowApproachFromAbove = tune.bassAllowApproachFromAbove;
        bc.userDensityHigh = intent.densityHigh;
        bc.userIntensityPeak = intent.intensityPeak;
        bc.userSilence = intent.silence;
        bc.forceClimax = (baseEnergy >= 0.85);
        bc.energy = baseEnergy * in.agentEnergyMult.value("Bass", 1.0);
        bc.chordFunction = func;
        bc.roman = roman;

        JazzBalladPianoPlanner::Context pc;
        pc.bpm = in.bpm;
        pc.playbackBarIndex = playbackBarIndex;
        pc.beatInBar = beatInBar;
        pc.chordIsNew = look.chordIsNew;
        pc.chord = look.currentChord;
        pc.chordText = chordText;
        pc.phraseBars = look.phraseBars;
        pc.barInPhrase = look.barInPhrase;
        pc.phraseEndBar = look.phraseEndBar;
        pc.cadence01 = look.cadence01;
        pc.hasKey = true;
        pc.keyTonicPc = look.key.tonicPc;
        pc.keyMode = look.key.mode;
        pc.hasNextChord = look.haveNextChord && !look.nextChord.noChord;
        pc.nextChord = look.nextChord;
        pc.nextChanges = look.nextChanges;
        pc.beatsUntilChordChange = look.beatsUntilChange;
        pc.determinismSeed = detSeed ^ 0xBADC0FFEu;
        pc.rhLo = tune.pianoRhLo; pc.rhHi = tune.pianoRhHi;
        pc.lhLo = tune.pianoLhLo; pc.lhHi = tune.pianoLhHi;
        pc.skipBeat2ProbStable = tune.pianoSkipBeat2ProbStable;
        pc.addSecondColorProb = tune.pianoAddSecondColorProb;
        pc.sparkleProbBeat4 = tune.pianoSparkleProbBeat4;
        pc.preferShells = tune.pianoPreferShells;
        pc.userDensityHigh = intent.densityHigh;
        pc.userIntensityPeak = intent.intensityPeak;
        pc.userRegisterHigh = intent.registerHigh;
        pc.userSilence = intent.silence;
        pc.forceClimax = (baseEnergy >= 0.85);
        pc.energy = baseEnergy * in.agentEnergyMult.value("Piano", 1.0);

        // Weights v2 negotiation (single source of truth; no legacy virt knobs).
        static WeightNegotiator::State negState; // deterministic within this process; phrase plan is rebuilt often
        WeightNegotiator::Inputs wi;
        wi.global = in.weightsV2;
        wi.userBusy = userBusy;
        wi.userSilence = intent.silence;
        wi.cadence = (look.cadence01 >= 0.55);
        wi.phraseEnd = look.phraseEndBar;
        wi.sectionLabel = ""; // section labels handled by the playback engine
        const auto negotiated = WeightNegotiator::negotiate(wi, negState, /*smoothingAlpha=*/0.25);

        bc.weights = negotiated.bass.w;
        pc.weights = negotiated.piano.w;

        // Local shaping (still v2 axes, consistent with beat scheduler).
        const double progress01 = qBound(0.0, double(qMax(0, playbackBarIndex)) / 24.0, 1.0);
        bc.weights.density = qBound(0.0, bc.weights.density + 0.35 * bc.energy + 0.15 * progress01, 1.0);
        bc.weights.rhythm = qBound(0.0, bc.weights.rhythm + 0.45 * bc.energy + 0.20 * progress01, 1.0);
        bc.weights.interactivity = qBound(0.0, bc.weights.interactivity + 0.30 * (intent.silence ? 1.0 : 0.0) + 0.10 * bc.energy, 1.0);
        bc.weights.warmth = qBound(0.0, bc.weights.warmth + 0.15 * (1.0 - bc.energy), 1.0);
        bc.weights.creativity = qBound(0.0, bc.weights.creativity + 0.20 * bc.energy + 0.10 * progress01, 1.0);

        pc.weights.density = qBound(0.0, pc.weights.density + 0.40 * pc.energy + 0.20 * progress01, 1.0);
        pc.weights.rhythm = qBound(0.0, pc.weights.rhythm + 0.55 * pc.energy + 0.15 * progress01, 1.0);
        pc.weights.interactivity = qBound(0.0, pc.weights.interactivity + 0.30 * (intent.silence ? 1.0 : 0.0) + 0.15 * pc.energy, 1.0);
        pc.weights.warmth = qBound(0.0, pc.weights.warmth + 0.20 * (1.0 - pc.energy) + 0.10 * (intent.registerHigh ? 1.0 : 0.0), 1.0);
        pc.weights.creativity = qBound(0.0, pc.weights.creativity + 0.30 * pc.energy + 0.15 * progress01, 1.0);

        // Drums contexts (dry/wet).
        BrushesBalladDrummer::Context dcBase;
        dcBase.bpm = in.bpm;
        dcBase.ts = ts;
        dcBase.playbackBarIndex = playbackBarIndex;
        dcBase.beatInBar = beatInBar;
        dcBase.structural = (beatInBar == 0 || beatInBar == 2) || look.chordIsNew;
        dcBase.determinismSeed = detSeed ^ 0xD00D'BEEFu;
        dcBase.phraseBars = look.phraseBars;
        dcBase.barInPhrase = look.barInPhrase;
        dcBase.phraseEndBar = look.phraseEndBar;
        dcBase.cadence01 = look.cadence01;
        dcBase.energy = baseEnergy * in.agentEnergyMult.value("Drums", 1.0);
        dcBase.intensityPeak = intent.intensityPeak;

        BrushesBalladDrummer::Context dcDry = dcBase;
        dcDry.energy = qMin(dcDry.energy, 0.42);
        dcDry.gestureBias = -0.75;
        dcDry.allowRide = false;
        dcDry.allowPhraseGestures = false;
        dcDry.intensityPeak = false;
        BrushesBalladDrummer::Context dcWet = dcBase;
        dcWet.energy = qBound(0.0, dcWet.energy + 0.10 + 0.15 * look.cadence01, 1.0);
        dcWet.gestureBias = 0.85;
        dcWet.allowRide = true;
        dcWet.allowPhraseGestures = true;
        dcWet.intensityPeak = intent.intensityPeak || (look.cadence01 >= 0.70);

        QVector<JointCandidateModel::DrumCand> drumCands;
        {
            JointCandidateModel::DrumCand dry;
            dry.id = "dry";
            dry.ctx = dcDry;
            dry.plan = in.drummer->planBeat(dcDry);
            dry.st = JointCandidateModel::statsForNotes(dry.plan);
            dry.hasKick = false;
            drumCands.push_back(dry);

            JointCandidateModel::DrumCand wet;
            wet.id = "wet";
            wet.ctx = dcWet;
            wet.plan = in.drummer->planBeat(dcWet);
            wet.st = JointCandidateModel::statsForNotes(wet.plan);
            wet.hasKick = false;
            drumCands.push_back(wet);
        }
        if (!allowDrums) {
            drumCands.clear();
            JointCandidateModel::DrumCand none;
            none.id = "none";
            none.plan.clear();
            none.st = JointCandidateModel::statsForNotes(none.plan);
            none.hasKick = false;
            drumCands.push_back(none);
        }

        // Candidate contexts.
        // Hive-mind space negotiation: when the user is busy, prefer sparse across the band.
        // When we are in a response window, prefer richer/more conversational.
        const bool inResponse = (responseUntilBar >= 0 && playbackBarIndex <= responseUntilBar);

        JazzBalladBassPlanner::Context bcSparse = bc;
        JazzBalladBassPlanner::Context bcBase = bc;
        JazzBalladBassPlanner::Context bcRich = bc;
        bcSparse.weights.rhythm *= 0.55;
        bcSparse.approachProbBeat3 *= 0.55;
        bcSparse.skipBeat3ProbStable = qMin(0.98, bcSparse.skipBeat3ProbStable + 0.18);
        bcSparse.weights.creativity *= 0.70;
        bcRich.weights.rhythm = qMin(1.0, bcRich.weights.rhythm + 0.18);
        bcRich.approachProbBeat3 = qMin(1.0, bcRich.approachProbBeat3 + 0.20);
        bcRich.skipBeat3ProbStable = qMax(0.0, bcRich.skipBeat3ProbStable - 0.12);

        JazzBalladPianoPlanner::Context pcSparse = pc;
        JazzBalladPianoPlanner::Context pcBase = pc;
        JazzBalladPianoPlanner::Context pcRich = pc;
        pcSparse.preferShells = true;
        pcSparse.skipBeat2ProbStable = qMin(0.995, pcSparse.skipBeat2ProbStable + 0.18);
        pcSparse.addSecondColorProb *= 0.45;
        pcSparse.sparkleProbBeat4 *= 0.45;
        pcRich.skipBeat2ProbStable = qMax(0.0, pcRich.skipBeat2ProbStable - 0.18);
        pcRich.addSecondColorProb = qMin(0.85, pcRich.addSecondColorProb + 0.18);
        pcRich.sparkleProbBeat4 = qMin(0.85, pcRich.sparkleProbBeat4 + 0.18);
        if (pcRich.weights.creativity >= 0.55 && !userBusy) pcRich.preferShells = false;

        if (userBusy) {
            // Strong space negotiation: both agents avoid richness.
            bcRich = bcBase;
            pcRich = pcBase;
        } else if (inResponse) {
            // Conversational response: bias toward richer candidates.
            bcSparse = bcBase;
            pcSparse = pcBase;
            bcRich.approachProbBeat3 = qMin(1.0, bcRich.approachProbBeat3 + 0.10);
            pcRich.addSecondColorProb = qMin(0.95, pcRich.addSecondColorProb + 0.10);
        }

        auto avgW = [&](double a, double b) { return qBound(0.0, 0.5 * (a + b), 1.0); };
        virtuoso::control::PerformanceWeightsV2 weightsAvg;
        weightsAvg.density = avgW(bc.weights.density, pc.weights.density);
        weightsAvg.rhythm = avgW(bc.weights.rhythm, pc.weights.rhythm);
        weightsAvg.emotion = avgW(bc.weights.emotion, pc.weights.emotion);
        weightsAvg.intensity = avgW(bc.weights.intensity, pc.weights.intensity);
        weightsAvg.dynamism = avgW(bc.weights.dynamism, pc.weights.dynamism);
        weightsAvg.creativity = avgW(bc.weights.creativity, pc.weights.creativity);
        weightsAvg.tension = avgW(bc.weights.tension, pc.weights.tension);
        weightsAvg.interactivity = avgW(bc.weights.interactivity, pc.weights.interactivity);
        weightsAvg.variability = avgW(bc.weights.variability, pc.weights.variability);
        weightsAvg.warmth = avgW(bc.weights.warmth, pc.weights.warmth);
        weightsAvg.clamp01();

        const auto weights = virtuoso::solver::weightsFromWeightsV2(weightsAvg);

        QVector<BeamNode> nextBeam;
        nextBeam.reserve(beamWidth * 6);

        for (const auto& node : beam) {
            // Generate bass/piano candidates from this node's planner states.
            JointCandidateModel::GenerationInputs gi;
            gi.bassPlanner = in.bassPlanner;
            gi.pianoPlanner = in.pianoPlanner;
            gi.chBass = in.chBass;
            gi.chPiano = in.chPiano;
            gi.ts = ts;
            gi.bcSparse = bcSparse;
            gi.bcBase = bcBase;
            gi.bcRich = bcRich;
            gi.pcSparse = pcSparse;
            gi.pcBase = pcBase;
            gi.pcRich = pcRich;
            gi.bassStart = node.bassState;
            gi.pianoStart = node.pianoState;

            QVector<JointCandidateModel::BassCand> bassCands;
            QVector<JointCandidateModel::PianoCand> pianoCands;
            JointCandidateModel::generateBassPianoCandidates(gi, bassCands, pianoCands);

            // Score all combinations with shared model.
            JointCandidateModel::ScoringInputs si;
            si.ts = ts;
            si.chord = look.currentChord;
            si.beatInBar = beatInBar;
            si.cadence01 = look.cadence01;
            si.phraseSetupBar = phraseSetupBar;
            si.phraseEndBar = look.phraseEndBar;
            si.userBusy = userBusy;
            si.userSilence = intent.silence;
            si.prevBassCenterMidi = node.lastBassCenter;
            si.prevPianoCenterMidi = node.lastPianoCenter;
            si.weightsAvg = weightsAvg;
            si.weights = weights;
            si.lastBassId = node.lastBassId;
            si.lastPianoId = node.lastPianoId;
            si.lastDrumsId = node.lastDrumsId;
            si.inResponse = inResponse;

            const auto scored = JointCandidateModel::chooseBestCombo(si, bassCands, pianoCands, drumCands);

            for (const auto& ce : scored.combos) {
                const int bi = ce.bi;
                const int pi = ce.pi;
                const int di = ce.di;

                BeamNode nn = node;
                nn.cost += ce.cost;
                nn.bassState = bassCands[bi].nextState;
                nn.pianoState = pianoCands[pi].nextState;
                nn.lastBassCenter = node.lastBassCenter;
                if (bassCands[bi].st.count > 0) nn.lastBassCenter = qBound(28, int(llround(bassCands[bi].st.meanMidi)), 67);
                nn.lastPianoCenter = node.lastPianoCenter;
                if (pianoCands[pi].st.count > 0) nn.lastPianoCenter = qBound(48, int(llround(pianoCands[pi].st.meanMidi)), 96);

                nn.lastBassId = bassCands[bi].id;
                nn.lastPianoId = pianoCands[pi].id;
                nn.lastDrumsId = drumCands[di].id;

                StoryState::JointStepChoice choice;
                choice.stepIndex = stepIndex;
                choice.bassId = bassCands[bi].id;
                choice.pianoId = pianoCands[pi].id;
                choice.drumsId = drumCands[di].id;
                choice.costTag = ce.bd.shortTag(weights);
                choice.drumsNotes = drumCands[di].plan;
                choice.bassPlan = bassCands[bi].plan;
                choice.pianoPlan = pianoCands[pi].plan;
                choice.bassStateAfter = bassCands[bi].nextState;
                choice.pianoStateAfter = pianoCands[pi].nextState;
                nn.choices.push_back(choice);
                nextBeam.push_back(std::move(nn));
            }
        }

        // Keep top beamWidth nodes.
        std::sort(nextBeam.begin(), nextBeam.end(), [](const BeamNode& a, const BeamNode& b) { return a.cost < b.cost; });
        if (nextBeam.size() > beamWidth) nextBeam.resize(beamWidth);
        beam = nextBeam;
    }

    if (beam.isEmpty()) return {};
    // Best node is beam[0] after sort above at last iteration.
    std::sort(beam.begin(), beam.end(), [](const BeamNode& a, const BeamNode& b) { return a.cost < b.cost; });
    const auto out = beam.front().choices;
    // IMPORTANT: planning must not mutate live planner state.
    in.bassPlanner->restoreState(bassStart);
    in.pianoPlanner->restoreState(pianoStart);
    return out;
}

} // namespace playback

