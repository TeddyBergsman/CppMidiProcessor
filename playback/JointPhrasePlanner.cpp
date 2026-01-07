#include "playback/JointPhrasePlanner.h"

#include "playback/BalladReferenceTuning.h"
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

struct DrumCand {
    QString id;
    QVector<virtuoso::engine::AgentIntentNote> plan;
};
struct BassCand {
    QString id;
    JazzBalladBassPlanner::Context ctx;
    JazzBalladBassPlanner::BeatPlan plan;
    JazzBalladBassPlanner::PlannerState next;
};
struct PianoCand {
    QString id;
    JazzBalladPianoPlanner::Context ctx;
    JazzBalladPianoPlanner::BeatPlan plan;
    JazzBalladPianoPlanner::PlannerState next;
};

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

static double spacingPenalty(const QVector<virtuoso::engine::AgentIntentNote>& bassNotes,
                             const QVector<virtuoso::engine::AgentIntentNote>& pianoNotes) {
    if (bassNotes.isEmpty() || pianoNotes.isEmpty()) return 0.0;
    int bHi = 0;
    for (const auto& n : bassNotes) bHi = qMax(bHi, qBound(0, n.note, 127));
    int pLo = 127;
    for (const auto& n : pianoNotes) pLo = qMin(pLo, qBound(0, n.note, 127));
    const int spacingMin = 7;
    if (pLo >= bHi + spacingMin) return 0.0;
    const int overlap = (bHi + spacingMin) - pLo;
    return 6.0 + 0.85 * double(overlap);
}

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

        // Build base contexts (same as AgentCoordinator, but without scheduling).
        const QString chordText = look.currentChord.originalText.trimmed().isEmpty()
            ? QString("pc=%1").arg(look.currentChord.rootPc)
            : look.currentChord.originalText.trimmed();

        const auto* chordDef = in.harmony->chordDefForSymbol(look.currentChord);
        QString roman;
        QString func;
        const int keyPc = in.harmony->hasKeyPcGuess() ? look.key.tonicPc : HarmonyContext::normalizePc(look.currentChord.rootPc);
        const QString scaleUsed = (chordDef && look.currentChord.rootPc >= 0)
            ? in.harmony->chooseScaleUsedForChord(keyPc, look.key.mode, look.currentChord, *chordDef, &roman, &func)
            : QString();

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

        // Virtuosity (same policy as runtime, simplified for planning).
        const double progress01 = qBound(0.0, double(qMax(0, playbackBarIndex)) / 24.0, 1.0);
        const auto virtAuto = in.virtAuto;
        if (virtAuto) {
            bc.harmonicRisk = qBound(0.0, in.virtHarmonicRisk + 0.35 * bc.energy + 0.15 * progress01, 1.0);
            bc.rhythmicComplexity = qBound(0.0, in.virtRhythmicComplexity + 0.45 * bc.energy + 0.20 * progress01, 1.0);
            bc.interaction = qBound(0.0, in.virtInteraction + 0.30 * (intent.silence ? 1.0 : 0.0) + 0.10 * bc.energy, 1.0);
            bc.toneDark = qBound(0.0, in.virtToneDark + 0.15 * (1.0 - bc.energy), 1.0);
            pc.harmonicRisk = qBound(0.0, in.virtHarmonicRisk + 0.40 * pc.energy + 0.20 * progress01, 1.0);
            pc.rhythmicComplexity = qBound(0.0, in.virtRhythmicComplexity + 0.55 * pc.energy + 0.15 * progress01, 1.0);
            pc.interaction = qBound(0.0, in.virtInteraction + 0.30 * (intent.silence ? 1.0 : 0.0) + 0.15 * pc.energy, 1.0);
            pc.toneDark = qBound(0.0, in.virtToneDark + 0.20 * (1.0 - pc.energy) + 0.10 * (intent.registerHigh ? 1.0 : 0.0), 1.0);
        } else {
            bc.harmonicRisk = in.virtHarmonicRisk;
            bc.rhythmicComplexity = in.virtRhythmicComplexity;
            bc.interaction = in.virtInteraction;
            bc.toneDark = in.virtToneDark;
            pc.harmonicRisk = in.virtHarmonicRisk;
            pc.rhythmicComplexity = in.virtRhythmicComplexity;
            pc.interaction = in.virtInteraction;
            pc.toneDark = in.virtToneDark;
        }

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

        QVector<DrumCand> drumCands;
        drumCands.push_back({"dry", in.drummer->planBeat(dcDry)});
        drumCands.push_back({"wet", in.drummer->planBeat(dcWet)});
        if (!allowDrums) drumCands = {{"none", {}}};

        // Candidate contexts.
        // Hive-mind space negotiation: when the user is busy, prefer sparse across the band.
        // When we are in a response window, prefer richer/more conversational.
        const bool inResponse = (responseUntilBar >= 0 && playbackBarIndex <= responseUntilBar);

        JazzBalladBassPlanner::Context bcSparse = bc;
        JazzBalladBassPlanner::Context bcBase = bc;
        JazzBalladBassPlanner::Context bcRich = bc;
        bcSparse.rhythmicComplexity *= 0.55;
        bcSparse.approachProbBeat3 *= 0.55;
        bcSparse.skipBeat3ProbStable = qMin(0.98, bcSparse.skipBeat3ProbStable + 0.18);
        bcSparse.harmonicRisk *= 0.70;
        bcRich.rhythmicComplexity = qMin(1.0, bcRich.rhythmicComplexity + 0.18);
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
        if (pcRich.harmonicRisk >= 0.55 && !userBusy) pcRich.preferShells = false;

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

        const virtuoso::control::VirtuosityMatrix virtAvg{
            0.5 * (bc.harmonicRisk + pc.harmonicRisk),
            0.5 * (bc.rhythmicComplexity + pc.rhythmicComplexity),
            0.5 * (bc.interaction + pc.interaction),
            0.5 * (bc.toneDark + pc.toneDark),
        };
        const auto weights = virtuoso::solver::weightsFromVirtuosity(virtAvg);

        QVector<BeamNode> nextBeam;
        nextBeam.reserve(beamWidth * 6);

        for (const auto& node : beam) {
            // Generate bass candidates from this node's state.
            QVector<BassCand> bassCands;
            for (const auto& bid : {"sparse", "base", "rich"}) {
                const JazzBalladBassPlanner::Context ctx = (QString(bid) == "sparse") ? bcSparse : (QString(bid) == "rich") ? bcRich : bcBase;
                in.bassPlanner->restoreState(node.bassState);
                auto plan = in.bassPlanner->planBeatWithActions(ctx, in.chBass, ts);
                const auto nextS = in.bassPlanner->snapshotState();
                bassCands.push_back({bid, ctx, plan, nextS});
            }

            // Piano candidates from node state.
            QVector<PianoCand> pianoCands;
            for (const auto& pid : {"sparse", "base", "rich"}) {
                const JazzBalladPianoPlanner::Context ctx = (QString(pid) == "sparse") ? pcSparse : (QString(pid) == "rich") ? pcRich : pcBase;
                in.pianoPlanner->restoreState(node.pianoState);
                auto plan = in.pianoPlanner->planBeatWithActions(ctx, in.chPiano, ts);
                const auto nextS = in.pianoPlanner->snapshotState();
                pianoCands.push_back({pid, ctx, plan, nextS});
            }

            // Evaluate cartesian products.
            for (const auto& dc : drumCands) {
                for (const auto& bcand : bassCands) {
                    for (const auto& pcand : pianoCands) {
                        virtuoso::solver::CostBreakdown bd;
                        bd.harmonicStability =
                            0.65 * virtuoso::solver::harmonicOutsidePenalty01(bcand.plan.notes, look.currentChord) +
                            0.95 * virtuoso::solver::harmonicOutsidePenalty01(pcand.plan.notes, look.currentChord);
                        bd.voiceLeadingDistance =
                            0.55 * virtuoso::solver::voiceLeadingPenalty(bcand.plan.notes, node.lastBassCenter) +
                            0.55 * virtuoso::solver::voiceLeadingPenalty(pcand.plan.notes, node.lastPianoCenter);
                        bd.rhythmicInterest =
                            0.55 * virtuoso::solver::rhythmicInterestPenalty01(bcand.plan.notes, ts) +
                            0.65 * virtuoso::solver::rhythmicInterestPenalty01(pcand.plan.notes, ts) +
                            0.20 * virtuoso::solver::rhythmicInterestPenalty01(dc.plan, ts);
                        // Interaction: density targeting
                        const double totalNotes = double(bcand.plan.notes.size() + pcand.plan.notes.size()) + 0.35 * double(dc.plan.size());
                        double target = 2.0 + 4.5 * qBound(0.0, virtAvg.rhythmicComplexity, 1.0);
                        if (intent.silence) target += qBound(0.0, virtAvg.interaction, 1.0) * 2.0;
                        if (userBusy) target -= 2.5;
                        target = qBound(0.0, target, 10.0);
                        bd.interactionFactor = 0.55 * qAbs(totalNotes - target);
                        if (userBusy) bd.interactionFactor += 0.45 * qMax(0.0, totalNotes - 3.0);
                        // Cadence "something happens"
                        if (look.cadence01 >= 0.80 && beatInBar == 0) {
                            if (totalNotes <= 0.01) bd.interactionFactor += 6.0;
                            else bd.interactionFactor = qMax(0.0, bd.interactionFactor - 0.30 * qMin(4.0, totalNotes));
                        }

                        double c = bd.total(weights);
                        c += spacingPenalty(bcand.plan.notes, pcand.plan.notes);

                        // Transition penalty: avoid flipping decisions too often within phrase.
                        if (!node.lastBassId.isEmpty() && node.lastBassId != bcand.id) c += 0.20;
                        if (!node.lastPianoId.isEmpty() && node.lastPianoId != pcand.id) c += 0.15;
                        if (!node.lastDrumsId.isEmpty() && node.lastDrumsId != dc.id) c += 0.10;

                        // Hive-mind: during response, reward "wet" drums and richer piano slightly.
                        if (inResponse) {
                            if (dc.id == "wet") c -= 0.25;
                            if (pcand.id == "rich") c -= 0.18;
                            if (bcand.id == "rich") c -= 0.08;
                        }

                        BeamNode nn = node;
                        nn.cost += c;
                        nn.bassState = bcand.next;
                        nn.pianoState = pcand.next;
                        // Update centers (mean midi)
                        nn.lastBassCenter = node.lastBassCenter;
                        if (!bcand.plan.notes.isEmpty()) {
                            qint64 sum = 0;
                            for (const auto& n : bcand.plan.notes) sum += qBound(0, n.note, 127);
                            nn.lastBassCenter = qBound(28, int(llround(double(sum) / double(bcand.plan.notes.size()))), 67);
                        }
                        nn.lastPianoCenter = node.lastPianoCenter;
                        if (!pcand.plan.notes.isEmpty()) {
                            qint64 sum = 0;
                            for (const auto& n : pcand.plan.notes) sum += qBound(0, n.note, 127);
                            nn.lastPianoCenter = qBound(48, int(llround(double(sum) / double(pcand.plan.notes.size()))), 96);
                        }

                        nn.lastBassId = bcand.id;
                        nn.lastPianoId = pcand.id;
                        nn.lastDrumsId = dc.id;

                        StoryState::JointStepChoice choice;
                        choice.stepIndex = stepIndex;
                        choice.bassId = bcand.id;
                        choice.pianoId = pcand.id;
                        choice.drumsId = dc.id;
                        choice.costTag = bd.shortTag(weights);
                        choice.drumsNotes = dc.plan;
                        choice.bassPlan = bcand.plan;
                        choice.pianoPlan = pcand.plan;
                        choice.bassStateAfter = bcand.next;
                        choice.pianoStateAfter = pcand.next;
                        nn.choices.push_back(choice);
                        nextBeam.push_back(std::move(nn));
                    }
                }
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

