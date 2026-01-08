#include "playback/AgentCoordinator.h"

#include "playback/BalladReferenceTuning.h"
#include "playback/JointPhrasePlanner.h"
#include "playback/JointCandidateModel.h"
#include "playback/LookaheadWindow.h"
#include "playback/VirtuosoBalladMvpPlaybackEngine.h"
#include "virtuoso/solver/BeatCostModel.h"
#include "virtuoso/util/StableHash.h"
#include "virtuoso/theory/ScaleSuggester.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>
#include <limits>

namespace playback {
namespace {

static virtuoso::groove::TimeSignature timeSigFromModel(const chart::ChartModel& model) {
    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (model.timeSigNum > 0) ? model.timeSigNum : 4;
    ts.den = (model.timeSigDen > 0) ? model.timeSigDen : 4;
    return ts;
}

static int adaptivePhraseBars(int bpm) {
    // Adaptive 4–8 bar horizon: slower tempos get 8-bar phrasing, faster tempos get 4-bar phrasing.
    // Keep this intentionally simple + deterministic (no hidden state).
    return (bpm <= 84) ? 8 : 4;
}

static int clampBassCenterMidi(int m) { return qBound(28, m, 67); }  // ~E1..G4 (upright-ish practical)
static int clampPianoCenterMidi(int m) { return qBound(48, m, 96); } // ~C3..C7

static QString representativeVoicingType(const QVector<virtuoso::engine::AgentIntentNote>& notes) {
    // Most notes in the plan share the same voicing_type; pick the longest string among non-empty as a decent proxy.
    QString best;
    for (const auto& n : notes) {
        const QString v = n.voicing_type.trimmed();
        if (v.isEmpty()) continue;
        if (v.size() > best.size()) best = v;
    }
    return best;
}

struct RegisterTargets {
    int bassCenterMidi = 45;
    int pianoCenterMidi = 72;
};

static RegisterTargets chooseJointRegisterTargets(int desiredBassCenter,
                                                  int desiredPianoCenter,
                                                  double energy01,
                                                  bool userRegisterHigh) {
    // Candidate octave shifts. Bass is allowed to climb intentionally (tenor), but still must avoid collisions.
    const int bassShifts[] = {-12, 0, 12};
    const int pianoShifts[] = {-12, 0, 12};

    RegisterTargets best;
    double bestCost = 1e18;

    const int spacingMin = 9; // semitones between Bass high region and Piano low region
    for (int bs : bassShifts) {
        const int b = clampBassCenterMidi(desiredBassCenter + bs);
        // Predicted "high point" of bass activity near the center.
        const int bassHi = b + 7;

        for (int ps : pianoShifts) {
            int p = clampPianoCenterMidi(desiredPianoCenter + ps);
            if (userRegisterHigh) p = clampPianoCenterMidi(p + 5);

            // Predicted "low point" of the pianist's LH activity near the center.
            const int pianoLo = p - 12;

            // Collision avoidance cost (hard-ish).
            double cost = 0.0;
            if (pianoLo < bassHi + spacingMin) {
                const int overlap = (bassHi + spacingMin) - pianoLo;
                cost += 8.0 + 0.65 * double(overlap);
            }

            // Stay near the desired arcs, but allow intentional motion (energy makes bigger arcs cheaper).
            const double arcW = 0.55 - 0.25 * qBound(0.0, energy01, 1.0);
            cost += arcW * (double(qAbs(b - desiredBassCenter)) / 12.0);
            cost += arcW * (double(qAbs(p - desiredPianoCenter)) / 12.0);

            if (cost < bestCost) {
                bestCost = cost;
                best.bassCenterMidi = b;
                best.pianoCenterMidi = p;
            }
        }
    }

    // Final guard: enforce spacing by nudging piano upward if needed.
    {
        const int bassHi = best.bassCenterMidi + 7;
        const int pianoLo = best.pianoCenterMidi - 12;
        if (pianoLo < bassHi + spacingMin) {
            best.pianoCenterMidi = clampPianoCenterMidi(best.pianoCenterMidi + ((bassHi + spacingMin) - pianoLo));
        }
    }
    return best;
}

struct NoteStats {
    int count = 0;
    int minMidi = 127;
    int maxMidi = 0;
    double meanMidi = 0.0;
};

static NoteStats statsForNotes(const QVector<virtuoso::engine::AgentIntentNote>& notes) {
    NoteStats s;
    if (notes.isEmpty()) {
        s.minMidi = 127;
        s.maxMidi = 0;
        s.meanMidi = 0.0;
        s.count = 0;
        return s;
    }
    qint64 sum = 0;
    s.count = notes.size();
    for (const auto& n : notes) {
        const int m = qBound(0, n.note, 127);
        s.minMidi = qMin(s.minMidi, m);
        s.maxMidi = qMax(s.maxMidi, m);
        sum += m;
    }
    s.meanMidi = double(sum) / double(qMax(1, s.count));
    return s;
}

static QJsonObject noteStatsJson(const NoteStats& st) {
    QJsonObject o;
    o.insert("count", st.count);
    o.insert("min_midi", st.minMidi);
    o.insert("max_midi", st.maxMidi);
    o.insert("mean_midi", st.meanMidi);
    return o;
}

static QJsonObject noteStatsJson(const JointCandidateModel::NoteStats& st) {
    QJsonObject o;
    o.insert("count", st.count);
    o.insert("min_midi", st.minMidi);
    o.insert("max_midi", st.maxMidi);
    o.insert("mean_midi", st.meanMidi);
    return o;
}

} // namespace

void AgentCoordinator::scheduleStep(const Inputs& in, int stepIndex) {
    if (!in.model || !in.sequence || in.sequence->isEmpty()) return;
    if (!in.harmony || !in.interaction || !in.engine || !in.ontology || !in.bassPlanner || !in.pianoPlanner || !in.drummer) return;

    const QVector<int>& seq = *in.sequence;
    const int seqLen = seq.size();
    const virtuoso::groove::TimeSignature ts = timeSigFromModel(*in.model);

    // Canonical lookahead window (replaces ad-hoc next-chord + per-bar key windows).
    // Phrase bars are adaptive (4–8) to support longer-horizon musical storytelling.
    const int phraseBars = adaptivePhraseBars(in.bpm);
    auto look = buildLookaheadWindow(*in.model, seq, in.repeats, stepIndex, /*horizonBars=*/8, phraseBars, /*keyWindowBars=*/8, *in.harmony);

    const int beatsPerBar = qMax(1, ts.num);
    const int playbackBarIndex = stepIndex / beatsPerBar;
    const int beatInBar = stepIndex % beatsPerBar;

    const bool haveChord = look.haveCurrentChord;
    const music::ChordSymbol chord = look.currentChord;
    const bool chordIsNew = look.chordIsNew;
    const bool haveNext = look.haveNextChord;
    const music::ChordSymbol nextChord = look.nextChord;
    const int beatsUntilChange = look.beatsUntilChange;
    const bool nextChanges = look.nextChanges;
    const double cadence01 = look.cadence01;

    const bool structural = (beatInBar == 0 || beatInBar == 2) || chordIsNew;

    // Update listener harmonic context for "playing outside" classification.
    if (haveChord && !chord.noChord) in.interaction->setChordContext(chord);

    // Snapshot interaction.
    const auto snap = in.interaction->snapshot(QDateTime::currentMSecsSinceEpoch(), in.debugEnergyAuto, in.debugEnergy);
    const auto intent = snap.intent;
    const auto vibeEff = snap.vibe;
    const double baseEnergy = snap.energy01;
    const QString vibeStr = snap.vibeStr;
    const QString intentStr = snap.intentStr;
    const bool userBusy = snap.userBusy;

    // Hive-mind macro: detect user phrase end and set a short response window (1–2 bars).
    if (in.story && intent.questionEnded && beatInBar == 0) {
        // Respond for the next bar (and optionally the following, at strong cadences).
        in.story->responseUntilBar = qMax(in.story->responseUntilBar, playbackBarIndex + (cadence01 >= 0.75 ? 2 : 1));
    }

    // Debug UI status (emitted once per beat step).
    if (in.owner) {
        const QString virtStr = in.virtAuto
            ? "Virt=Auto"
            : QString("Virt=Manual r=%1 rc=%2 i=%3 t=%4")
                  .arg(in.virtHarmonicRisk, 0, 'f', 2)
                  .arg(in.virtRhythmicComplexity, 0, 'f', 2)
                  .arg(in.virtInteraction, 0, 'f', 2)
                  .arg(in.virtToneDark, 0, 'f', 2);
        in.owner->debugStatus(QString("Preset=%1  Vibe=%2  energy=%3  %4  intents=%5  nps=%6  reg=%7  gVel=%8  cc2=%9  vNote=%10  silenceMs=%11  outside=%12")
                                  .arg(in.stylePresetKey)
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
        in.owner->debugEnergy(baseEnergy, in.debugEnergyAuto);
    }

    // Energy-driven instrument layering.
    const double eBand = qBound(0.0, baseEnergy, 1.0);
    const bool allowBass = (eBand >= 0.10);
    const bool allowDrums = (eBand >= 0.22);

    // Instant energy response despite phrase planning:
    // If energy changed significantly, replan starting immediately (from the current beat).
    bool forceReplanNow = false;
    if (in.story) {
        const double prev = in.story->lastPlannedEnergy01;
        if (prev >= 0.0 && qAbs(prev - baseEnergy) >= 0.08) {
            forceReplanNow = true;
        }
        in.story->lastPlannedEnergy01 = baseEnergy;
    }

    // Determinism seed.
    const quint32 detSeed = virtuoso::util::StableHash::fnv1a32((QString("ballad|") + in.stylePresetKey).toUtf8());

    // --- Persistent 4–8 bar story state (motif + register arcs) ---
    // This drives intentional register motion over the phrase horizon, while the joint selector
    // avoids collisions and preserves spacing.
    int desiredBassCenterMidi = 45;
    int desiredPianoCenterMidi = 72;
    if (in.story) {
        const int phraseStartBar = playbackBarIndex - look.barInPhrase;
        const bool newPhrase = (in.story->phraseStartBar != phraseStartBar) || (in.story->phraseBars != look.phraseBars);
        if (beatInBar == 0 && (in.story->phraseStartBar < 0 || newPhrase)) {
            in.story->phraseStartBar = phraseStartBar;
            in.story->phraseBars = look.phraseBars;

            const quint32 sh = virtuoso::util::StableHash::fnv1a32(QString("story|%1|%2|%3")
                                                                       .arg(in.stylePresetKey)
                                                                       .arg(in.story->phraseStartBar)
                                                                       .arg(detSeed)
                                                                       .toUtf8());
            int dir = ((sh & 1u) != 0u) ? 1 : -1;
            if (vibeEff.vibe == VibeStateMachine::Vibe::Build) dir = 1;
            if (vibeEff.vibe == VibeStateMachine::Vibe::Climax) dir = 1;
            if (vibeEff.vibe == VibeStateMachine::Vibe::CoolDown) dir = -1;

            const int bassDelta = qBound(5, int(llround(6.0 + 6.0 * eBand)), 12);
            const int pianoDelta = qBound(4, int(llround(5.0 + 7.0 * eBand)), 12);

            in.story->bassArc.startCenterMidi = clampBassCenterMidi(in.story->lastBassCenterMidi);
            in.story->bassArc.endCenterMidi = clampBassCenterMidi(in.story->bassArc.startCenterMidi + dir * bassDelta);

            in.story->pianoArc.startCenterMidi = clampPianoCenterMidi(in.story->lastPianoCenterMidi);
            in.story->pianoArc.endCenterMidi = clampPianoCenterMidi(in.story->pianoArc.startCenterMidi + dir * pianoDelta);
            if (intent.registerHigh) in.story->pianoArc.endCenterMidi = clampPianoCenterMidi(in.story->pianoArc.endCenterMidi + 5);

            // Ensure an end-of-phrase vertical spacing target (avoid "lane locking", but prevent mud).
            if (in.story->pianoArc.endCenterMidi < in.story->bassArc.endCenterMidi + 18) {
                in.story->pianoArc.endCenterMidi = clampPianoCenterMidi(in.story->bassArc.endCenterMidi + 18);
            }
        }

        desiredBassCenterMidi = clampBassCenterMidi(in.story->bassArc.centerAtBar(look.barInPhrase, in.story->phraseBars));
        desiredPianoCenterMidi = clampPianoCenterMidi(in.story->pianoArc.centerAtBar(look.barInPhrase, in.story->phraseBars));
    }

    const RegisterTargets regs = chooseJointRegisterTargets(desiredBassCenterMidi,
                                                           desiredPianoCenterMidi,
                                                           eBand,
                                                           intent.registerHigh);

    // --- Phrase-level joint planning (beam search) ---
    // At phrase start, compute a full phrase plan (one decision per beat-step) and store it in StoryState.
    QString plannedBassId;
    QString plannedPianoId;
    QString plannedDrumsId;
    QString plannedCostTag;
    const StoryState::JointStepChoice* plannedStep = nullptr;
    if (in.story) {
        const int phraseStartBar = playbackBarIndex - look.barInPhrase;
        const int phraseStartStep = phraseStartBar * beatsPerBar;
        const int phraseSteps = look.phraseBars * beatsPerBar;
        const bool atPhraseStart = (beatInBar == 0) && (look.barInPhrase == 0);
        const bool needPlan = (in.story->plan.isEmpty() || in.story->planStartStep < 0 || in.story->planSteps <= 0);
        const bool wrongWindow = (in.story->planStartStep != phraseStartStep || in.story->planSteps != phraseSteps);
        if ((atPhraseStart && (needPlan || wrongWindow)) || forceReplanNow) {
            JointPhrasePlanner::Inputs pi;
            pi.in = in;
            // When energy changes, replan from *this beat* so behavior changes immediately.
            pi.startStep = forceReplanNow ? stepIndex : phraseStartStep;
            // Plan at least a phrase horizon ahead.
            pi.steps = forceReplanNow ? qMax(1, phraseSteps - (stepIndex - phraseStartStep)) : phraseSteps;
            pi.beamWidth = 6;
            in.story->plan = JointPhrasePlanner::plan(pi);
            in.story->planStartStep = pi.startStep;
            in.story->planSteps = pi.steps;
        }
        const int idx = stepIndex - in.story->planStartStep;
        if (idx >= 0 && idx < in.story->plan.size()) {
            const auto& ch = in.story->plan[idx];
            if (ch.stepIndex == stepIndex) {
                plannedBassId = ch.bassId;
                plannedPianoId = ch.pianoId;
                plannedDrumsId = ch.drumsId;
                plannedCostTag = ch.costTag;
                plannedStep = &ch;
            }
        }
    }

    // We will schedule drums as part of the joint optimizer (so they participate in the decision),
    // but still need a fallback: if there is no chord context, run drums-only.
    virtuoso::groove::HumanizedEvent kickHe;
    bool haveKickHe = false;
    auto scheduleDrums = [&](QVector<virtuoso::engine::AgentIntentNote> drumIntents, const QString& jointTag) {
        // Separate kick for groove-lock timing anchor.
        int kickIndex = -1;
        for (int i = 0; i < drumIntents.size(); ++i) {
            if (drumIntents[i].note == in.noteKick) { kickIndex = i; break; }
        }
        if (kickIndex >= 0) {
            auto kickIntent = drumIntents[kickIndex];
            kickIntent.vibe_state = vibeStr;
            kickIntent.user_intents = intentStr;
            kickIntent.user_outside_ratio = intent.outsideRatio;
            kickIntent.logic_tag = kickIntent.logic_tag.isEmpty() ? jointTag : (kickIntent.logic_tag + "|" + jointTag);
            kickHe = in.engine->humanizeIntent(kickIntent);
            haveKickHe = (kickHe.offMs > kickHe.onMs);
            if (haveKickHe) in.engine->scheduleHumanizedIntentNote(kickIntent, kickHe);
            drumIntents.removeAt(kickIndex);
        }

        for (auto n : drumIntents) {
            const double e = qBound(0.0, baseEnergy, 1.0);
            const double mult = 0.55 + 0.55 * e;
            n.baseVelocity = qBound(1, int(llround(double(n.baseVelocity) * mult)), 127);
            n.vibe_state = vibeStr;
            n.user_intents = intentStr;
            n.user_outside_ratio = intent.outsideRatio;
            n.logic_tag = n.logic_tag.isEmpty() ? jointTag : (n.logic_tag + "|" + jointTag);
            in.engine->scheduleNote(n);
            if (in.motivicMemory) in.motivicMemory->push(n);
        }
    };

    if (!haveChord || chord.noChord) {
        if (allowDrums) {
            BrushesBalladDrummer::Context dc;
            dc.bpm = in.bpm;
            dc.ts = ts;
            dc.playbackBarIndex = playbackBarIndex;
            dc.beatInBar = beatInBar;
            dc.structural = structural;
            dc.determinismSeed = detSeed ^ 0xD00D'BEEFu;
            dc.phraseBars = look.phraseBars;
            dc.barInPhrase = look.barInPhrase;
            dc.phraseEndBar = look.phraseEndBar;
            dc.cadence01 = cadence01;
            {
                const double mult = in.agentEnergyMult.value("Drums", 1.0);
                dc.energy = qBound(0.0, baseEnergy * mult, 1.0);
            }
            if (userBusy) dc.energy = qMin(dc.energy, 0.55);
            dc.intensityPeak = intent.intensityPeak;
            scheduleDrums(in.drummer->planBeat(dc), "joint=drums_only");
        }
        return;
    }

    const QString chordText = chord.originalText.trimmed().isEmpty() ? QString("pc=%1").arg(chord.rootPc) : chord.originalText.trimmed();
    const playback::LocalKeyEstimate lk = look.key;
    const auto keyCenterStr = look.keyCenterStr;
    const int keyPc = in.harmony->hasKeyPcGuess() ? lk.tonicPc : HarmonyContext::normalizePc(chord.rootPc);

    const auto* chordDef = in.harmony->chordDefForSymbol(chord);
    QString roman;
    QString func;
    const auto scaleChoice = (chordDef && chord.rootPc >= 0)
        ? in.harmony->chooseScaleForChord(keyPc, lk.mode, chord, *chordDef, &roman, &func)
        : HarmonyContext::ScaleChoice{};
    const QString scaleUsed = scaleChoice.display;
    const QString scaleKey = scaleChoice.key;
    const QString scaleName = scaleChoice.name;

    const BalladRefTuning tune = tuningForReferenceTrack(in.stylePresetKey);

    // Bass
    JazzBalladBassPlanner::Context bc;
    bc.bpm = in.bpm;
    bc.playbackBarIndex = playbackBarIndex;
    bc.beatInBar = beatInBar;
    bc.chordIsNew = chordIsNew;
    bc.chord = chord;
    bc.hasNextChord = haveNext && !nextChord.noChord;
    bc.nextChord = nextChord;
    bc.chordText = chordText;
    bc.phraseBars = look.phraseBars;
    bc.barInPhrase = look.barInPhrase;
    bc.phraseEndBar = look.phraseEndBar;
    bc.cadence01 = cadence01;
    bc.registerCenterMidi = regs.bassCenterMidi;
    bc.determinismSeed = detSeed;
    bc.approachProbBeat3 = tune.bassApproachProbBeat3;
    bc.skipBeat3ProbStable = tune.bassSkipBeat3ProbStable;
    bc.allowApproachFromAbove = tune.bassAllowApproachFromAbove;
    bc.userDensityHigh = intent.densityHigh;
    bc.userIntensityPeak = intent.intensityPeak;
    bc.userSilence = intent.silence;
    bc.forceClimax = (baseEnergy >= 0.85);
    {
        const double mult = in.agentEnergyMult.value("Bass", 1.0);
        bc.energy = qBound(0.0, baseEnergy * mult, 1.0);
    }
    if (!allowDrums) {
        bc.energy *= 0.70;
        bc.rhythmicComplexity *= 0.55;
        bc.approachProbBeat3 *= 0.35;
        bc.skipBeat3ProbStable = qMin(0.98, bc.skipBeat3ProbStable + 0.12);
    }
    bc.chordFunction = func;
    bc.roman = roman;
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
    if (intent.densityHigh || intent.intensityPeak) {
        bc.approachProbBeat3 *= 0.35;
        bc.skipBeat3ProbStable = qMin(0.65, bc.skipBeat3ProbStable + 0.20);
    }
    if (bc.cadence01 >= 0.55) {
        bc.approachProbBeat3 = qMin(1.0, bc.approachProbBeat3 + 0.25 * bc.cadence01);
        bc.skipBeat3ProbStable = qMax(0.0, bc.skipBeat3ProbStable - 0.15 * bc.cadence01);
    }
    if (userBusy) {
        bc.approachProbBeat3 *= 0.35;
        bc.skipBeat3ProbStable = qMin(0.90, bc.skipBeat3ProbStable + 0.20);
        bc.rhythmicComplexity *= 0.35;
        bc.harmonicRisk *= 0.45;
        bc.cadence01 *= 0.55;
    }
    if (baseEnergy >= 0.85) {
        bc.approachProbBeat3 *= 0.60;
        bc.skipBeat3ProbStable = qMax(0.10, bc.skipBeat3ProbStable - 0.08);
    }
    if (baseEnergy >= 0.55 && baseEnergy < 0.85) {
        bc.approachProbBeat3 = qMin(1.0, bc.approachProbBeat3 + 0.12);
        bc.skipBeat3ProbStable = qMax(0.0, bc.skipBeat3ProbStable - 0.12);
    }

    // NOTE: Bass is scheduled after we build both Bass+Piano contexts (joint optimizer).

    // Piano
    JazzBalladPianoPlanner::Context pc;
    pc.bpm = in.bpm;
    pc.playbackBarIndex = playbackBarIndex;
    pc.beatInBar = beatInBar;
    pc.chordIsNew = chordIsNew;
    pc.chord = chord;
    pc.chordText = chordText;
    pc.phraseBars = look.phraseBars;
    pc.barInPhrase = look.barInPhrase;
    pc.phraseEndBar = look.phraseEndBar;
    pc.cadence01 = cadence01;
    pc.hasKey = true;
    pc.keyTonicPc = lk.tonicPc;
    pc.keyMode = lk.mode;
    pc.hasNextChord = haveNext && !nextChord.noChord;
    pc.nextChord = nextChord;
    pc.nextChanges = nextChanges;
    pc.beatsUntilChordChange = beatsUntilChange;
    pc.determinismSeed = detSeed ^ 0xBADC0FFEu;
    pc.rhLo = tune.pianoRhLo;
    pc.rhHi = tune.pianoRhHi;
    pc.lhLo = tune.pianoLhLo;
    pc.lhHi = tune.pianoLhHi;
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
        const double mult = in.agentEnergyMult.value("Piano", 1.0);
        pc.energy = qBound(0.0, baseEnergy * mult, 1.0);
    }
    if (eBand < 0.12) {
        pc.preferShells = true;
        pc.skipBeat2ProbStable = qMin(0.995, pc.skipBeat2ProbStable + 0.25);
        pc.sparkleProbBeat4 = 0.0;
        pc.rhythmicComplexity *= 0.30;
        pc.harmonicRisk *= 0.25;
        pc.cadence01 *= 0.65;
    }
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
    if (intent.registerHigh) {
        pc.rhHi = qMax(pc.rhLo + 4, pc.rhHi - 6);
        pc.sparkleProbBeat4 *= 0.25;
    }

    // Joint register targets: shift the piano band toward the story arc,
    // allowing intentional down/up movement while maintaining spacing with bass.
    {
        const int baseLhCenter = (pc.lhLo + pc.lhHi) / 2;
        const int baseRhCenter = (pc.rhLo + pc.rhHi) / 2;
        const int baseCenter = (baseLhCenter + baseRhCenter) / 2;
        const int shift = regs.pianoCenterMidi - baseCenter;
        auto clampPair = [](int& lo, int& hi, int minSpan) {
            lo = qBound(0, lo, 127);
            hi = qBound(0, hi, 127);
            if (hi < lo + minSpan) hi = qMin(127, lo + minSpan);
        };
        pc.lhLo += shift; pc.lhHi += shift;
        pc.rhLo += shift; pc.rhHi += shift;
        pc.sparkleLo += shift; pc.sparkleHi += shift;
        clampPair(pc.lhLo, pc.lhHi, 4);
        clampPair(pc.rhLo, pc.rhHi, 8);
        clampPair(pc.sparkleLo, pc.sparkleHi, 8);
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
        pc.skipBeat2ProbStable = qMax(0.0, pc.skipBeat2ProbStable - 0.18);
        pc.addSecondColorProb = qMin(0.60, pc.addSecondColorProb + 0.15);
        pc.sparkleProbBeat4 = qMin(0.45, pc.sparkleProbBeat4 + 0.10);
    }
    if (vibeEff.vibe == VibeStateMachine::Vibe::CoolDown) {
        pc.skipBeat2ProbStable = qMin(0.98, pc.skipBeat2ProbStable + 0.10);
        pc.sparkleProbBeat4 *= 0.20;
    }
    if (userBusy) {
        pc.preferShells = true;
        pc.skipBeat2ProbStable = qMin(0.98, pc.skipBeat2ProbStable + 0.18);
        pc.sparkleProbBeat4 *= 0.05;
        pc.rhHi = qMax(pc.rhLo + 4, pc.rhHi - 8);
        pc.rhythmicComplexity *= 0.35;
        pc.harmonicRisk *= 0.45;
        pc.cadence01 *= 0.55;
    }

    // --- Joint beat optimizer (Drums + Bass + Piano) ---
    JazzBalladBassPlanner::Context bcChosen = bc;
    JazzBalladPianoPlanner::Context pcChosen = pc;
    QString bassChoiceId = "base";
    QString pianoChoiceId = "base";
    QString drumChoiceId = "base";
    virtuoso::solver::CostWeights jointWeights{};
    virtuoso::solver::CostBreakdown jointBd{};
    bool haveJointBd = false;
    bool emittedCandidatePool = false;
    const bool usePlannedBeat = (plannedStep != nullptr && plannedStep->stepIndex == stepIndex);

    // Drums candidates (stateless planner): build contexts once, reuse in optimizer.
    BrushesBalladDrummer::Context dcBase;
    BrushesBalladDrummer::Context dcDry;
    BrushesBalladDrummer::Context dcWet;
    QVector<virtuoso::engine::AgentIntentNote> drumPlanDry;
    QVector<virtuoso::engine::AgentIntentNote> drumPlanWet;
    const bool phraseSetupBar = (look.phraseBars > 1) ? (look.barInPhrase == (look.phraseBars - 2)) : false;
    if (!usePlannedBeat && allowDrums) {
        dcBase.bpm = in.bpm;
        dcBase.ts = ts;
        dcBase.playbackBarIndex = playbackBarIndex;
        dcBase.beatInBar = beatInBar;
        dcBase.structural = structural;
        dcBase.determinismSeed = detSeed ^ 0xD00D'BEEFu;
        dcBase.phraseBars = look.phraseBars;
        dcBase.barInPhrase = look.barInPhrase;
        dcBase.phraseEndBar = look.phraseEndBar;
        dcBase.cadence01 = cadence01;
        {
            const double mult = in.agentEnergyMult.value("Drums", 1.0);
            dcBase.energy = qBound(0.0, baseEnergy * mult, 1.0);
        }
        if (userBusy) dcBase.energy = qMin(dcBase.energy, 0.55);
        dcBase.intensityPeak = intent.intensityPeak;

        dcDry = dcBase;
        dcWet = dcBase;
        dcDry.energy = qMin(dcDry.energy, 0.42);
        dcDry.gestureBias = -0.75;
        dcDry.allowRide = false;
        dcDry.allowPhraseGestures = false;
        dcDry.intensityPeak = false;
        const double vibeBoost = (vibeEff.vibe == VibeStateMachine::Vibe::Build || vibeEff.vibe == VibeStateMachine::Vibe::Climax) ? 0.10 : 0.0;
        dcWet.energy = qBound(0.0, dcWet.energy + vibeBoost + 0.15 * cadence01, 1.0);
        dcWet.gestureBias = 0.85;
        dcWet.allowRide = true;
        dcWet.allowPhraseGestures = true;
        dcWet.intensityPeak = intent.intensityPeak || (cadence01 >= 0.70);

        // Shared motivic memory (drums): if the recent drum rhythm is already dense,
        // avoid repeatedly stacking phrase gestures; if it's very sparse, allow gestures.
        if (in.motivicMemory) {
            const quint64 mask = in.motivicMemory->recentRhythmMotifMask16("Drums", /*bars=*/2, ts, /*slotsPerBeat=*/4);
            const int beatsPerBar = qMax(1, ts.num);
            const int slotsPerBar = qBound(1, beatsPerBar * 4, 64);
            const int on = int(__builtin_popcountll((unsigned long long)mask));
            const double dens01 = (slotsPerBar > 0) ? (double(on) / double(slotsPerBar)) : 0.0;
            if (dens01 >= 0.45) dcWet.allowPhraseGestures = false;
            else if (dens01 <= 0.15) dcWet.allowPhraseGestures = true;
        }

        drumPlanDry = in.drummer->planBeat(dcDry);
        drumPlanWet = in.drummer->planBeat(dcWet);
    }

    if (usePlannedBeat) {
        bassChoiceId = plannedBassId.isEmpty() ? plannedStep->bassId : plannedBassId;
        pianoChoiceId = plannedPianoId.isEmpty() ? plannedStep->pianoId : plannedPianoId;
        drumChoiceId = plannedDrumsId.isEmpty() ? plannedStep->drumsId : plannedDrumsId;
        if (allowDrums) {
            const QString jt = QString("joint=%1+%2+%3|%4")
                                   .arg(bassChoiceId, pianoChoiceId, drumChoiceId, plannedCostTag.isEmpty() ? QString("planned") : plannedCostTag);
            scheduleDrums(plannedStep->drumsNotes, jt);
        }
    } else if (allowBass) {
        JazzBalladBassPlanner::Context bcSparse = bc;
        JazzBalladBassPlanner::Context bcBase = bc;
        JazzBalladBassPlanner::Context bcRich = bc;
        // Sparse = more air / fewer approaches; Rich = more motion.
        bcSparse.rhythmicComplexity *= 0.55;
        bcSparse.approachProbBeat3 *= 0.55;
        bcSparse.skipBeat3ProbStable = qMin(0.98, bcSparse.skipBeat3ProbStable + 0.18);
        bcSparse.harmonicRisk *= 0.70;
        bcRich.rhythmicComplexity = qMin(1.0, bcRich.rhythmicComplexity + 0.18);
        bcRich.approachProbBeat3 = qMin(1.0, bcRich.approachProbBeat3 + 0.20);
        bcRich.skipBeat3ProbStable = qMax(0.0, bcRich.skipBeat3ProbStable - 0.12);

        JazzBalladPianoPlanner::Context pcSparse = pc;
        JazzBalladPianoPlanner::Context pcRich = pc;
        pcSparse.preferShells = true;
        pcSparse.skipBeat2ProbStable = qMin(0.995, pcSparse.skipBeat2ProbStable + 0.18);
        pcSparse.addSecondColorProb *= 0.45;
        pcSparse.sparkleProbBeat4 *= 0.45;
        pcRich.skipBeat2ProbStable = qMax(0.0, pcRich.skipBeat2ProbStable - 0.18);
        pcRich.addSecondColorProb = qMin(0.85, pcRich.addSecondColorProb + 0.18);
        pcRich.sparkleProbBeat4 = qMin(0.85, pcRich.sparkleProbBeat4 + 0.18);
        if (pcRich.harmonicRisk >= 0.55 && !userBusy) pcRich.preferShells = false;

        const auto bassSnap = in.bassPlanner->snapshotState();
        const auto pianoSnap = in.pianoPlanner->snapshotState();

        // Generate bass/piano candidates via the shared model.
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
        gi.pcBase = pc;
        gi.pcRich = pcRich;
        gi.bassStart = bassSnap;
        gi.pianoStart = pianoSnap;

        QVector<JointCandidateModel::BassCand> bCands;
        QVector<JointCandidateModel::PianoCand> pCands;
        JointCandidateModel::generateBassPianoCandidates(gi, bCands, pCands);
        for (auto& c : bCands) c.plan.chosenScaleKey = scaleKey;
        for (auto& c : pCands) { c.plan.chosenScaleKey = scaleKey; c.plan.chosenScaleName = scaleName; }

        // Drum candidates are computed once above (dry/wet) and wrapped here.
        QVector<JointCandidateModel::DrumCand> dCands;
        if (allowDrums) {
            JointCandidateModel::DrumCand dry;
            dry.id = "dry";
            dry.ctx = dcDry;
            dry.plan = drumPlanDry;
            dry.st = JointCandidateModel::statsForNotes(dry.plan);
            dry.hasKick = false;
            for (const auto& n : dry.plan) if (n.note == in.noteKick) { dry.hasKick = true; break; }
            dCands.push_back(dry);

            JointCandidateModel::DrumCand wet;
            wet.id = "wet";
            wet.ctx = dcWet;
            wet.plan = drumPlanWet;
            wet.st = JointCandidateModel::statsForNotes(wet.plan);
            wet.hasKick = false;
            for (const auto& n : wet.plan) if (n.note == in.noteKick) { wet.hasKick = true; break; }
            dCands.push_back(wet);
        } else {
            JointCandidateModel::DrumCand none;
            none.id = "none";
            none.plan.clear();
            none.st = JointCandidateModel::statsForNotes(none.plan);
            none.hasKick = false;
            dCands.push_back(none);
        }

        const int prevBassCenter = in.story ? clampBassCenterMidi(in.story->lastBassCenterMidi) : regs.bassCenterMidi;
        const int prevPianoCenter = in.story ? clampPianoCenterMidi(in.story->lastPianoCenterMidi) : regs.pianoCenterMidi;

        const virtuoso::control::VirtuosityMatrix virtAvg{
            0.5 * (bc.harmonicRisk + pc.harmonicRisk),
            0.5 * (bc.rhythmicComplexity + pc.rhythmicComplexity),
            0.5 * (bc.interaction + pc.interaction),
            0.5 * (bc.toneDark + pc.toneDark),
        };
        const auto w = virtuoso::solver::weightsFromVirtuosity(virtAvg);

        JointCandidateModel::ScoringInputs si;
        si.ts = ts;
        si.chord = chord;
        si.beatInBar = beatInBar;
        si.cadence01 = cadence01;
        si.phraseSetupBar = phraseSetupBar;
        si.phraseEndBar = look.phraseEndBar;
        si.userBusy = userBusy;
        si.userSilence = intent.silence;
        si.prevBassCenterMidi = prevBassCenter;
        si.prevPianoCenterMidi = prevPianoCenter;
        si.virtAvg = virtAvg;
        si.weights = w;

        const bool havePlanned = (!plannedBassId.isEmpty() || !plannedPianoId.isEmpty() || !plannedDrumsId.isEmpty());
        const auto best = JointCandidateModel::chooseBestCombo(si, bCands, pCands, dCands,
                                                               havePlanned ? plannedBassId : QString(),
                                                               havePlanned ? plannedPianoId : QString(),
                                                               havePlanned ? plannedDrumsId : QString());

        const int bestBi = best.bestBi;
        const int bestPi = best.bestPi;
        const int bestDi = best.bestDi;
        virtuoso::solver::CostBreakdown bestBd = best.bestBd;
        const double bestCost = best.bestCost;

        // Emit exact candidate pool + evaluated combinations for visualization.
        {
            QJsonObject root;
            root.insert("event_kind", "candidate_pool");
            root.insert("schema", 2);
            root.insert("tempo_bpm", in.bpm);
            root.insert("ts_num", ts.num);
            root.insert("ts_den", ts.den);
            root.insert("style_preset_key", in.stylePresetKey);
            root.insert("chord_is_new", chordIsNew);
            const auto poolPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(playbackBarIndex, beatInBar, 0, 1, ts);
            root.insert("grid_pos", virtuoso::groove::GrooveGrid::toString(poolPos, ts));
            // Anchor to engine-clock time so UIs can sync to transport (not to UI click time).
            const qint64 baseMs = (in.engine ? in.engine->gridBaseMsEnsure() : 0);
            root.insert("on_ms", qint64(virtuoso::groove::GrooveGrid::posToMs(poolPos, ts, in.bpm) + baseMs));
            root.insert("chord_context", chordText);
            root.insert("scale_used", scaleUsed);
            root.insert("scale_key", scaleKey);
            root.insert("roman", roman);
            root.insert("chord_function", func);
            root.insert("chord_root_pc", chord.rootPc);
            root.insert("key_tonic_pc", lk.tonicPc);
            root.insert("key_mode", int(lk.mode));
            if (chordDef) root.insert("chord_def_key", chordDef->key);
            if (in.engine) root.insert("groove_template", in.engine->currentGrooveTemplateKey());

            // Candidate sets (exact IDs considered by the joint optimizer).
            QJsonArray bassCandsJson;
            for (const auto& c : bCands) {
                QJsonObject o;
                o.insert("id", c.id);
                o.insert("stats", noteStatsJson(c.st));
                o.insert("energy", c.ctx.energy);
                o.insert("harmonicRisk", c.ctx.harmonicRisk);
                o.insert("rhythmicComplexity", c.ctx.rhythmicComplexity);
                o.insert("interaction", c.ctx.interaction);
                o.insert("toneDark", c.ctx.toneDark);
                bassCandsJson.push_back(o);
            }
            QJsonArray pianoCandsJson;
            for (const auto& c : pCands) {
                QJsonObject o;
                o.insert("id", c.id);
                o.insert("stats", noteStatsJson(c.st));
                o.insert("energy", c.ctx.energy);
                o.insert("harmonicRisk", c.ctx.harmonicRisk);
                o.insert("rhythmicComplexity", c.ctx.rhythmicComplexity);
                o.insert("interaction", c.ctx.interaction);
                o.insert("toneDark", c.ctx.toneDark);
                o.insert("pianist_cost", c.pianistFeasibilityCost);
                o.insert("pedal_cost", c.pedalClarityCost);
                o.insert("topline_cost", c.topLineContinuityCost);
                const QString vt = representativeVoicingType(c.plan.notes);
                if (!vt.isEmpty()) o.insert("voicing_type", vt);
                if (!c.plan.chosenVoicingKey.trimmed().isEmpty()) o.insert("voicing_key", c.plan.chosenVoicingKey.trimmed());
                if (!c.plan.motifSourceAgent.trimmed().isEmpty()) o.insert("motif_source", c.plan.motifSourceAgent.trimmed());
                if (!c.plan.motifTransform.trimmed().isEmpty()) o.insert("motif_transform", c.plan.motifTransform.trimmed());
                if (!c.plan.performance.pedalProfile.trimmed().isEmpty()) o.insert("pedal_profile", c.plan.performance.pedalProfile.trimmed());
                if (!c.plan.performance.gestureProfile.trimmed().isEmpty()) o.insert("gesture_profile", c.plan.performance.gestureProfile.trimmed());
                if (!c.plan.performance.toplineSummary.trimmed().isEmpty()) o.insert("topline", c.plan.performance.toplineSummary.trimmed());
                if (!c.plan.performance.compPhraseId.trimmed().isEmpty()) o.insert("comp_phrase_id", c.plan.performance.compPhraseId.trimmed());
                if (!c.plan.performance.compBeatId.trimmed().isEmpty()) o.insert("comp_beat_id", c.plan.performance.compBeatId.trimmed());
                if (!c.plan.performance.toplinePhraseId.trimmed().isEmpty()) o.insert("topline_phrase_id", c.plan.performance.toplinePhraseId.trimmed());
                if (!c.plan.performance.gestureId.trimmed().isEmpty()) o.insert("gesture_id", c.plan.performance.gestureId.trimmed());
                if (!c.plan.performance.pedalId.trimmed().isEmpty()) o.insert("pedal_id", c.plan.performance.pedalId.trimmed());
                pianoCandsJson.push_back(o);
            }
            QJsonArray drumsCandsJson;
            for (const auto& c : dCands) {
                QJsonObject o;
                o.insert("id", c.id);
                o.insert("stats", noteStatsJson(c.st));
                o.insert("energy", c.ctx.energy);
                o.insert("gestureBias", c.ctx.gestureBias);
                o.insert("allowRide", c.ctx.allowRide);
                o.insert("allowPhraseGestures", c.ctx.allowPhraseGestures);
                o.insert("hasKick", c.hasKick);
                drumsCandsJson.push_back(o);
            }

            // Scale candidate pool (exact scale keys available from ontology for this chord).
            QJsonArray scaleCandsJson;
            if (in.ontology && chordDef && chord.rootPc >= 0) {
                QSet<int> pcs;
                pcs.reserve(16);
                const int r = HarmonyContext::normalizePc(chord.rootPc);
                pcs.insert(r);
                for (int iv : chordDef->intervals) pcs.insert(HarmonyContext::normalizePc(r + iv));
                const auto sug = virtuoso::theory::suggestScalesForPitchClasses(*in.ontology, pcs, 12);
                for (const auto& s : sug) {
                    QJsonObject so;
                    so.insert("key", s.key);
                    so.insert("name", s.name);
                    so.insert("score", s.score);
                    so.insert("coverage", s.coverage);
                    so.insert("best_transpose", s.bestTranspose);
                    scaleCandsJson.push_back(so);
                }
            }

            QJsonObject cands;
            cands.insert("bass", bassCandsJson);
            cands.insert("piano", pianoCandsJson);
            cands.insert("drums", drumsCandsJson);
            cands.insert("scales", scaleCandsJson);
            root.insert("candidates", cands);

            // Evaluated cartesian product (exactly what the optimizer compared).
            QJsonArray combos;
            for (const auto& ce : best.combos) {
                QJsonObject cj;
                cj.insert("bass", ce.bassId);
                cj.insert("piano", ce.pianoId);
                cj.insert("drums", ce.drumsId);
                cj.insert("total_cost", ce.cost);
                cj.insert("piano_extra_cost", ce.pianoExtraCost);
                cj.insert("cost_tag", ce.bd.shortTag(w));
                QJsonObject bdj;
                bdj.insert("harmonicStability", ce.bd.harmonicStability);
                bdj.insert("voiceLeadingDistance", ce.bd.voiceLeadingDistance);
                bdj.insert("rhythmicInterest", ce.bd.rhythmicInterest);
                bdj.insert("interactionFactor", ce.bd.interactionFactor);
                cj.insert("breakdown", bdj);
                const bool isChosen = (ce.bassId == bCands[bestBi].id && ce.pianoId == pCands[bestPi].id && ce.drumsId == dCands[bestDi].id);
                if (isChosen) cj.insert("chosen", true);
                if (havePlanned) cj.insert("planned_choice", isChosen);
                combos.push_back(cj);
            }
            root.insert("combinations", combos);

            QJsonObject chosen;
            chosen.insert("bass", bCands[bestBi].id);
            chosen.insert("piano", pCands[bestPi].id);
            chosen.insert("drums", dCands[bestDi].id);
            chosen.insert("scale_used", scaleUsed);
            chosen.insert("scale_key", scaleKey);
            if (!pCands[bestPi].plan.motifSourceAgent.trimmed().isEmpty()) chosen.insert("motif_source", pCands[bestPi].plan.motifSourceAgent.trimmed());
            if (!pCands[bestPi].plan.motifTransform.trimmed().isEmpty()) chosen.insert("motif_transform", pCands[bestPi].plan.motifTransform.trimmed());
            if (!pCands[bestPi].plan.performance.pedalProfile.trimmed().isEmpty()) chosen.insert("pedal_profile", pCands[bestPi].plan.performance.pedalProfile.trimmed());
            if (!pCands[bestPi].plan.performance.gestureProfile.trimmed().isEmpty()) chosen.insert("gesture_profile", pCands[bestPi].plan.performance.gestureProfile.trimmed());
            if (!pCands[bestPi].plan.performance.toplineSummary.trimmed().isEmpty()) chosen.insert("topline", pCands[bestPi].plan.performance.toplineSummary.trimmed());
            if (!pCands[bestPi].plan.performance.compPhraseId.trimmed().isEmpty()) chosen.insert("comp_phrase_id", pCands[bestPi].plan.performance.compPhraseId.trimmed());
            if (!pCands[bestPi].plan.performance.compBeatId.trimmed().isEmpty()) chosen.insert("comp_beat_id", pCands[bestPi].plan.performance.compBeatId.trimmed());
            if (!pCands[bestPi].plan.performance.toplinePhraseId.trimmed().isEmpty()) chosen.insert("topline_phrase_id", pCands[bestPi].plan.performance.toplinePhraseId.trimmed());
            if (!pCands[bestPi].plan.performance.gestureId.trimmed().isEmpty()) chosen.insert("gesture_id", pCands[bestPi].plan.performance.gestureId.trimmed());
            if (!pCands[bestPi].plan.performance.pedalId.trimmed().isEmpty()) chosen.insert("pedal_id", pCands[bestPi].plan.performance.pedalId.trimmed());
            // Chosen voicing key/type (for exact Library selection).
            {
                const QString vk = pCands[bestPi].plan.chosenVoicingKey.trimmed();
                if (!vk.isEmpty()) chosen.insert("voicing_key", vk);
                const QString vt = pCands[bestPi].plan.chosenVoicingKey.trimmed().isEmpty()
                    ? representativeVoicingType(pCands[bestPi].plan.notes)
                    : representativeVoicingType(pCands[bestPi].plan.notes);
                if (!vt.isEmpty()) chosen.insert("voicing_type", vt);
                chosen.insert("has_polychord", (!vk.isEmpty() && vk.startsWith("piano_ust_", Qt::CaseInsensitive)));
            }
            root.insert("chosen", chosen);

            QJsonObject weights;
            weights.insert("harmony", w.harmony);
            weights.insert("voiceLeading", w.voiceLeading);
            weights.insert("rhythm", w.rhythm);
            weights.insert("interaction", w.interaction);
            root.insert("weights", weights);

            if (in.engine) {
                const auto pos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(playbackBarIndex, beatInBar, 0, 1, ts);
                in.engine->scheduleTheoryJsonAtGridPos(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)), pos);
            }
            emittedCandidatePool = true;
        }

        bcChosen = bCands[bestBi].ctx;
        pcChosen = pCands[bestPi].ctx;
        bassChoiceId = bCands[bestBi].id;
        pianoChoiceId = pCands[bestPi].id;
        drumChoiceId = dCands[bestDi].id;
        if (!plannedCostTag.isEmpty()) {
            // Prefer phrase-planner cost tag when available (it reflects horizon reasoning).
            bestBd = virtuoso::solver::CostBreakdown{};
            // We still append plannedCostTag in jointTag below.
        }

        // Schedule drums first so bass can groove-lock to the kick.
        if (allowDrums) {
            const QString costTag = plannedCostTag.isEmpty() ? bestBd.shortTag(w) : plannedCostTag;
            const QString jt = QString("joint=%1+%2+%3|%4").arg(bassChoiceId, pianoChoiceId, drumChoiceId, costTag);
            scheduleDrums(dCands[bestDi].plan, jt);
        }

        jointWeights = w;
        jointBd = bestBd;
        haveJointBd = true;
    }

    QString jointTag = QString("joint=%1+%2+%3").arg(bassChoiceId, pianoChoiceId, drumChoiceId);
    if (!plannedCostTag.isEmpty()) jointTag += "|" + plannedCostTag;
    else if (haveJointBd) jointTag += "|" + jointBd.shortTag(jointWeights);

    // If bass is not participating, still schedule drums (chosen heuristically) so the band breathes.
    if (allowDrums && !haveKickHe && !allowBass) {
        drumChoiceId = userBusy
            ? "dry"
            : (((phraseSetupBar || look.phraseEndBar) && beatInBar == (qMax(1, ts.num) - 1) && cadence01 >= 0.35) ||
                       (intent.intensityPeak || baseEnergy >= 0.55))
                  ? "wet"
                  : "dry";
        jointTag = QString("joint=%1+%2+%3").arg(bassChoiceId, pianoChoiceId, drumChoiceId);
        scheduleDrums((drumChoiceId == "wet") ? drumPlanWet : drumPlanDry, jointTag);
    }

    // --- Schedule Bass (chosen) ---
    if (allowBass) {
        const double quarterMsB = 60000.0 / double(qMax(1, in.bpm));
        const double beatMsB = quarterMsB * (4.0 / double(qMax(1, ts.den)));
        const qint64 sixteenthMsB = qMax<qint64>(20, qint64(llround(beatMsB / 4.0)));
        const qint64 eighthMsB = qMax<qint64>(30, qint64(llround(beatMsB / 2.0)));
        auto leadForLegato = [&](const QString& bassLogicTag, int beatInBarLocal) -> qint64 {
            const bool isWalk = bassLogicTag.contains("walk", Qt::CaseInsensitive);
            if (isWalk) return eighthMsB;
            const int leadBeats = (beatInBarLocal == 0 || beatInBarLocal == 2) ? 2 : 1;
            return qMax<qint64>(eighthMsB, qint64(llround(double(leadBeats) * beatMsB)));
        };
        const qint64 legatoHoldMsB = qMax<qint64>(60, eighthMsB);
        const qint64 restoreDelayMsB = qMax<qint64>(80, qint64(llround(beatMsB * 2.0)));

        if (bcChosen.registerCenterMidi >= 55) {
            bcChosen.skipBeat3ProbStable = qMax(0.0, bcChosen.skipBeat3ProbStable - 0.08);
        }

        JazzBalladBassPlanner::BeatPlan bassPlan;
        if (usePlannedBeat) bassPlan = plannedStep->bassPlan;
        else bassPlan = in.bassPlanner->planBeatWithActions(bcChosen, in.chBass, ts);
        const int desiredArtMidi = bassPlan.desiredArtKeyswitchMidi;

        bool haveLegatoKs = false;
        int legatoMidi = -1;
        QString legatoTag;
        bool haveNhKs = false;
        int nhMidi = -1;
        QString nhTag;
        bool haveSioOutKs = false;
        int sioMidi = -1;
        QString sioTag;

        for (const auto& ks : bassPlan.keyswitches) {
            const bool isArt = ks.logic_tag.endsWith(":Sus") || ks.logic_tag.endsWith(":PM");
            const bool isLegato = ks.logic_tag.endsWith(":LS") || ks.logic_tag.endsWith(":HP");
            const bool isNh = ks.logic_tag.endsWith(":NH");
            const bool isSioOut = ks.logic_tag.endsWith(":SIO_OUT");
            if (isLegato && ks.midi >= 0) {
                haveLegatoKs = true;
                legatoMidi = ks.midi;
                legatoTag = ks.logic_tag;
                continue;
            }
            if (isNh && ks.midi >= 0) {
                haveNhKs = true;
                nhMidi = ks.midi;
                nhTag = ks.logic_tag;
                continue;
            }
            if (isSioOut && ks.midi >= 0) {
                haveSioOutKs = true;
                sioMidi = ks.midi;
                sioTag = ks.logic_tag;
                continue;
            }
            const int lead = qBound(0, ks.leadMs, 30);
            const int hold = isArt ? 0 : qBound(24, ks.holdMs, 400); // latch Sus/PM
            if (ks.midi >= 0) {
                const QString tag = ks.logic_tag.isEmpty() ? jointTag : (ks.logic_tag + "|" + jointTag);
                in.engine->scheduleKeySwitch("Bass", in.chBass, ks.midi, ks.startPos,
                                             /*structural=*/true,
                                             /*leadMs=*/lead,
                                             /*holdMs=*/hold,
                                             tag);
            }
        }

        auto bassIntents = bassPlan.notes;
        int bassSum = 0;
        int bassN = 0;
        for (auto& n : bassIntents) {
            if (!scaleUsed.isEmpty()) n.scale_used = scaleUsed;
            n.key_center = keyCenterStr;
            if (!roman.isEmpty()) n.roman = roman;
            if (!func.isEmpty()) n.chord_function = func;
            n.vibe_state = vibeStr;
            n.user_intents = intentStr;
            n.user_outside_ratio = intent.outsideRatio;
            n.has_virtuosity = true;
            n.virtuosity.harmonicRisk = bcChosen.harmonicRisk;
            n.virtuosity.rhythmicComplexity = bcChosen.rhythmicComplexity;
            n.virtuosity.interaction = bcChosen.interaction;
            n.virtuosity.toneDark = bcChosen.toneDark;
            n.logic_tag = n.logic_tag.isEmpty() ? jointTag : (n.logic_tag + "|" + jointTag);
            const double e = qBound(0.0, baseEnergy, 1.0);
            n.baseVelocity = qBound(1, int(llround(double(n.baseVelocity) * (0.90 + 0.25 * e))), 127);

            if (in.kickLocksBass && beatInBar == 0 && haveKickHe) {
                auto bhe = in.engine->humanizeIntent(n);
                if (bhe.offMs > bhe.onMs) {
                    const qint64 delta = kickHe.onMs - bhe.onMs;
                    if (qAbs(delta) <= qMax<qint64>(0, in.kickLockMaxMs)) {
                        bhe.onMs += delta;
                        bhe.offMs += delta;
                        bhe.timing_offset_ms += int(delta);
                        if (haveLegatoKs && legatoMidi >= 0) {
                            const qint64 legLead = leadForLegato(n.logic_tag, beatInBar);
                            in.engine->scheduleKeySwitchAtMs("Bass", in.chBass, legatoMidi,
                                                             qMax<qint64>(0, bhe.onMs - legLead),
                                                             /*holdMs=*/int(qBound<qint64>(qint64(60), legatoHoldMsB, qint64(900))),
                                                             legatoTag + "|" + jointTag);
                            if (desiredArtMidi >= 0) {
                                in.engine->scheduleKeySwitchAtMs("Bass", in.chBass, desiredArtMidi,
                                                                 bhe.onMs + restoreDelayMsB,
                                                                 /*holdMs=*/60,
                                                                 QString("Bass:keyswitch:restore|%1").arg(jointTag));
                            }
                        }
                        const QString tag = n.logic_tag.isEmpty() ? "GrooveLock:Kick" : (n.logic_tag + "|GrooveLock:Kick");
                        in.engine->scheduleHumanizedIntentNote(n, bhe, tag);
                        continue;
                    }
                }
            }

            auto he = in.engine->humanizeIntent(n);
            if (he.offMs > he.onMs) {
                if (haveLegatoKs && legatoMidi >= 0) {
                    const qint64 legLead = leadForLegato(n.logic_tag, beatInBar);
                    in.engine->scheduleKeySwitchAtMs("Bass", in.chBass, legatoMidi,
                                                     qMax<qint64>(0, he.onMs - legLead),
                                                     /*holdMs=*/int(qBound<qint64>(qint64(60), legatoHoldMsB, qint64(900))),
                                                     legatoTag + "|" + jointTag);
                    if (desiredArtMidi >= 0) {
                        in.engine->scheduleKeySwitchAtMs("Bass", in.chBass, desiredArtMidi,
                                                         he.onMs + restoreDelayMsB,
                                                         /*holdMs=*/60,
                                                         QString("Bass:keyswitch:restore|%1").arg(jointTag));
                    }
                }
                if (haveNhKs && nhMidi >= 0) {
                    in.engine->scheduleKeySwitchAtMs("Bass", in.chBass, nhMidi,
                                                     qMax<qint64>(0, he.onMs - sixteenthMsB),
                                                     /*holdMs=*/int(qBound<qint64>(qint64(40), sixteenthMsB, qint64(240))),
                                                     nhTag + "|" + jointTag);
                    if (desiredArtMidi >= 0) {
                        in.engine->scheduleKeySwitchAtMs("Bass", in.chBass, desiredArtMidi,
                                                         he.onMs + qMax<qint64>(60, sixteenthMsB),
                                                         /*holdMs=*/60,
                                                         QString("Bass:keyswitch:restore|%1").arg(jointTag));
                    }
                }
                if (haveSioOutKs && sioMidi >= 0) {
                    const qint64 dur = qMax<qint64>(1, he.offMs - he.onMs);
                    const qint64 t = he.onMs + qint64(llround(double(dur) * 0.72));
                    in.engine->scheduleKeySwitchAtMs("Bass", in.chBass, sioMidi,
                                                     t,
                                                     /*holdMs=*/int(qBound<qint64>(qint64(60), sixteenthMsB, qint64(260))),
                                                     sioTag + "|" + jointTag);
                    if (desiredArtMidi >= 0) {
                        in.engine->scheduleKeySwitchAtMs("Bass", in.chBass, desiredArtMidi,
                                                         t + qMax<qint64>(80, sixteenthMsB),
                                                         /*holdMs=*/60,
                                                         QString("Bass:keyswitch:restore|%1").arg(jointTag));
                    }
                }
                in.engine->scheduleHumanizedIntentNote(n, he);
            }

            if (in.motivicMemory) in.motivicMemory->push(n);
            bassSum += n.note;
            bassN++;
        }
        for (auto fx : bassPlan.fxNotes) {
            fx.vibe_state = vibeStr;
            fx.user_intents = intentStr;
            fx.user_outside_ratio = intent.outsideRatio;
            fx.logic_tag = fx.logic_tag.isEmpty() ? jointTag : (fx.logic_tag + "|" + jointTag);
            in.engine->scheduleNote(fx);
        }
        if (in.story && bassN > 0) {
            in.story->lastBassCenterMidi = clampBassCenterMidi(int(llround(double(bassSum) / double(bassN))));
        }
        if (usePlannedBeat) {
            in.bassPlanner->restoreState(plannedStep->bassStateAfter);
        }
    }

    // --- Schedule Piano (chosen) ---
    JazzBalladPianoPlanner::BeatPlan pianoPlan;
    if (usePlannedBeat) pianoPlan = plannedStep->pianoPlan;
    else pianoPlan = in.pianoPlanner->planBeatWithActions(pcChosen, in.chPiano, ts);
    for (const auto& ci : pianoPlan.ccs) {
        const QString tag = ci.logic_tag.isEmpty() ? jointTag : (ci.logic_tag + "|" + jointTag);
        in.engine->scheduleCC("Piano", in.chPiano, ci.cc, ci.value, ci.startPos, ci.structural, tag);
    }
    auto pianoIntents = pianoPlan.notes;
    int pianoSum = 0;
    int pianoN = 0;
    for (auto& n : pianoIntents) {
        if (!scaleUsed.isEmpty()) n.scale_used = scaleUsed;
        n.key_center = keyCenterStr;
        if (!roman.isEmpty()) n.roman = roman;
        if (!func.isEmpty()) n.chord_function = func;
        n.vibe_state = vibeStr;
        n.user_intents = intentStr;
        n.user_outside_ratio = intent.outsideRatio;
        n.has_virtuosity = true;
        n.virtuosity.harmonicRisk = pcChosen.harmonicRisk;
        n.virtuosity.rhythmicComplexity = pcChosen.rhythmicComplexity;
        n.virtuosity.interaction = pcChosen.interaction;
        n.virtuosity.toneDark = pcChosen.toneDark;
        n.logic_tag = n.logic_tag.isEmpty() ? jointTag : (n.logic_tag + "|" + jointTag);
        const double e = qBound(0.0, vibeEff.energy, 1.0);
        n.baseVelocity = qBound(1, int(llround(double(n.baseVelocity) * (0.82 + 0.40 * e))), 127);
        in.engine->scheduleNote(n);
        if (in.motivicMemory) in.motivicMemory->push(n);
        pianoSum += n.note;
        pianoN++;
    }
    if (in.story && pianoN > 0) {
        in.story->lastPianoCenterMidi = clampPianoCenterMidi(int(llround(double(pianoSum) / double(pianoN))));
    }
    if (usePlannedBeat) {
        in.pianoPlanner->restoreState(plannedStep->pianoStateAfter);
    }

    // If we didn't emit a full candidate pool (e.g. planned beat, bass resting, etc.),
    // emit a minimal "exactly considered" pool (single choice per lane).
    if (!emittedCandidatePool && in.engine) {
        QJsonObject root;
        root.insert("event_kind", "candidate_pool");
        root.insert("schema", 2);
        root.insert("tempo_bpm", in.bpm);
        root.insert("ts_num", ts.num);
        root.insert("ts_den", ts.den);
        root.insert("style_preset_key", in.stylePresetKey);
        root.insert("chord_is_new", chordIsNew);
        const auto poolPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(playbackBarIndex, beatInBar, 0, 1, ts);
        root.insert("grid_pos", virtuoso::groove::GrooveGrid::toString(poolPos, ts));
        const qint64 baseMs = (in.engine ? in.engine->gridBaseMsEnsure() : 0);
        root.insert("on_ms", qint64(virtuoso::groove::GrooveGrid::posToMs(poolPos, ts, in.bpm) + baseMs));
        root.insert("chord_context", chordText);
        root.insert("scale_used", scaleUsed);
        root.insert("scale_key", scaleKey);
        root.insert("roman", roman);
        root.insert("chord_function", func);
        root.insert("chord_root_pc", chord.rootPc);
        root.insert("key_tonic_pc", lk.tonicPc);
        root.insert("key_mode", int(lk.mode));
        if (chordDef) root.insert("chord_def_key", chordDef->key);
        root.insert("groove_template", in.engine->currentGrooveTemplateKey());

        // Minimal candidates: what we actually considered in this branch is a single plan per lane.
        QJsonObject cands;
        QJsonArray bassArr;
        {
            QJsonObject o;
            o.insert("id", allowBass ? bassChoiceId : QString("none"));
            bassArr.push_back(o);
        }
        QJsonArray pianoArr;
        {
            QJsonObject o;
            o.insert("id", pianoChoiceId);
            // voicing key/type from the *actual* chosen piano plan.
            const QString vk = pianoPlan.chosenVoicingKey.trimmed();
            if (!vk.isEmpty()) o.insert("voicing_key", vk);
            const QString vt = representativeVoicingType(pianoPlan.notes);
            if (!vt.isEmpty()) o.insert("voicing_type", vt);
            if (!pianoPlan.performance.pedalProfile.trimmed().isEmpty()) o.insert("pedal_profile", pianoPlan.performance.pedalProfile.trimmed());
            if (!pianoPlan.performance.gestureProfile.trimmed().isEmpty()) o.insert("gesture_profile", pianoPlan.performance.gestureProfile.trimmed());
            if (!pianoPlan.performance.toplineSummary.trimmed().isEmpty()) o.insert("topline", pianoPlan.performance.toplineSummary.trimmed());
            if (!pianoPlan.performance.compPhraseId.trimmed().isEmpty()) o.insert("comp_phrase_id", pianoPlan.performance.compPhraseId.trimmed());
            if (!pianoPlan.performance.compBeatId.trimmed().isEmpty()) o.insert("comp_beat_id", pianoPlan.performance.compBeatId.trimmed());
            if (!pianoPlan.performance.toplinePhraseId.trimmed().isEmpty()) o.insert("topline_phrase_id", pianoPlan.performance.toplinePhraseId.trimmed());
            if (!pianoPlan.performance.gestureId.trimmed().isEmpty()) o.insert("gesture_id", pianoPlan.performance.gestureId.trimmed());
            if (!pianoPlan.performance.pedalId.trimmed().isEmpty()) o.insert("pedal_id", pianoPlan.performance.pedalId.trimmed());
            pianoArr.push_back(o);
        }
        QJsonArray drumsArr;
        {
            QJsonObject o;
            o.insert("id", drumChoiceId);
            drumsArr.push_back(o);
        }

        // Scale candidates still come from ontology for this chord (these are the true available options).
        QJsonArray scaleArr;
        if (in.ontology && chordDef && chord.rootPc >= 0) {
            QSet<int> pcs;
            pcs.reserve(16);
            const int r = HarmonyContext::normalizePc(chord.rootPc);
            pcs.insert(r);
            for (int iv : chordDef->intervals) pcs.insert(HarmonyContext::normalizePc(r + iv));
            const auto sug = virtuoso::theory::suggestScalesForPitchClasses(*in.ontology, pcs, 12);
            for (const auto& s : sug) {
                QJsonObject so;
                so.insert("key", s.key);
                so.insert("name", s.name);
                scaleArr.push_back(so);
            }
        }

        cands.insert("bass", bassArr);
        cands.insert("piano", pianoArr);
        cands.insert("drums", drumsArr);
        cands.insert("scales", scaleArr);
        root.insert("candidates", cands);

        QJsonObject chosen;
        chosen.insert("bass", bassChoiceId);
        chosen.insert("piano", pianoChoiceId);
        chosen.insert("drums", drumChoiceId);
        chosen.insert("scale_used", scaleUsed);
        chosen.insert("scale_key", scaleKey);
        if (!pianoPlan.motifSourceAgent.trimmed().isEmpty()) chosen.insert("motif_source", pianoPlan.motifSourceAgent.trimmed());
        if (!pianoPlan.motifTransform.trimmed().isEmpty()) chosen.insert("motif_transform", pianoPlan.motifTransform.trimmed());
        if (!pianoPlan.performance.pedalProfile.trimmed().isEmpty()) chosen.insert("pedal_profile", pianoPlan.performance.pedalProfile.trimmed());
        if (!pianoPlan.performance.gestureProfile.trimmed().isEmpty()) chosen.insert("gesture_profile", pianoPlan.performance.gestureProfile.trimmed());
        if (!pianoPlan.performance.toplineSummary.trimmed().isEmpty()) chosen.insert("topline", pianoPlan.performance.toplineSummary.trimmed());
        if (!pianoPlan.performance.compPhraseId.trimmed().isEmpty()) chosen.insert("comp_phrase_id", pianoPlan.performance.compPhraseId.trimmed());
        if (!pianoPlan.performance.compBeatId.trimmed().isEmpty()) chosen.insert("comp_beat_id", pianoPlan.performance.compBeatId.trimmed());
        if (!pianoPlan.performance.toplinePhraseId.trimmed().isEmpty()) chosen.insert("topline_phrase_id", pianoPlan.performance.toplinePhraseId.trimmed());
        if (!pianoPlan.performance.gestureId.trimmed().isEmpty()) chosen.insert("gesture_id", pianoPlan.performance.gestureId.trimmed());
        if (!pianoPlan.performance.pedalId.trimmed().isEmpty()) chosen.insert("pedal_id", pianoPlan.performance.pedalId.trimmed());
        {
            const QString vk = pianoPlan.chosenVoicingKey.trimmed();
            if (!vk.isEmpty()) chosen.insert("voicing_key", vk);
            const QString vt = representativeVoicingType(pianoPlan.notes);
            if (!vt.isEmpty()) chosen.insert("voicing_type", vt);
            chosen.insert("has_polychord", (!vk.isEmpty() && vk.startsWith("piano_ust_", Qt::CaseInsensitive)));
        }
        root.insert("chosen", chosen);

        const auto pos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(playbackBarIndex, beatInBar, 0, 1, ts);
        in.engine->scheduleTheoryJsonAtGridPos(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)), pos);
    }
}

} // namespace playback

