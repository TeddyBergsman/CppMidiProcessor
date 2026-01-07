#include "playback/AgentCoordinator.h"

#include "playback/BalladReferenceTuning.h"
#include "playback/LookaheadWindow.h"
#include "playback/VirtuosoBalladMvpPlaybackEngine.h"
#include "virtuoso/util/StableHash.h"

#include <QDateTime>
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

    // Drums first (for kick-lock).
    virtuoso::engine::AgentIntentNote kickIntent;
    virtuoso::groove::HumanizedEvent kickHe;
    bool haveKickHe = false;

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

        auto drumIntents = in.drummer->planBeat(dc);

        int kickIndex = -1;
        for (int i = 0; i < drumIntents.size(); ++i) {
            if (drumIntents[i].note == in.noteKick) { kickIndex = i; break; }
        }
        if (kickIndex >= 0) {
            kickIntent = drumIntents[kickIndex];
            kickIntent.vibe_state = vibeStr;
            kickIntent.user_intents = intentStr;
            kickIntent.user_outside_ratio = intent.outsideRatio;
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
            in.engine->scheduleNote(n);
        }
    }

    if (!haveChord || chord.noChord) return;

    const QString chordText = chord.originalText.trimmed().isEmpty() ? QString("pc=%1").arg(chord.rootPc) : chord.originalText.trimmed();
    const playback::LocalKeyEstimate lk = look.key;
    const auto keyCenterStr = look.keyCenterStr;
    const int keyPc = in.harmony->hasKeyPcGuess() ? lk.tonicPc : HarmonyContext::normalizePc(chord.rootPc);

    const auto* chordDef = in.harmony->chordDefForSymbol(chord);
    QString roman;
    QString func;
    const QString scaleUsed = (chordDef && chord.rootPc >= 0)
        ? in.harmony->chooseScaleUsedForChord(keyPc, lk.mode, chord, *chordDef, &roman, &func)
        : QString();

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

    if (allowBass) {
        // If bass is intentionally climbing this phrase, avoid thinning the groove too much.
        if (bc.registerCenterMidi >= 55) {
            bc.skipBeat3ProbStable = qMax(0.0, bc.skipBeat3ProbStable - 0.08);
        }
        const auto bassPlan = in.bassPlanner->planBeatWithActions(bc, in.chBass, ts);
        for (const auto& ks : bassPlan.keyswitches) {
            in.engine->scheduleKeySwitch("Bass", in.chBass, ks.midi, ks.startPos, /*structural=*/true, /*leadMs=*/18, /*holdMs=*/28, ks.logic_tag);
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
            n.virtuosity.harmonicRisk = bc.harmonicRisk;
            n.virtuosity.rhythmicComplexity = bc.rhythmicComplexity;
            n.virtuosity.interaction = bc.interaction;
            n.virtuosity.toneDark = bc.toneDark;
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
                        const QString tag = n.logic_tag.isEmpty() ? "GrooveLock:Kick" : (n.logic_tag + "|GrooveLock:Kick");
                        in.engine->scheduleHumanizedIntentNote(n, bhe, tag);
                        continue;
                    }
                }
            }
            in.engine->scheduleNote(n);
            if (in.motivicMemory) in.motivicMemory->push(n);
            bassSum += n.note;
            bassN++;
        }
        if (in.story && bassN > 0) {
            in.story->lastBassCenterMidi = clampBassCenterMidi(int(llround(double(bassSum) / double(bassN))));
        }
    }

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
    const auto pianoPlan = in.pianoPlanner->planBeatWithActions(pc, in.chPiano, ts);
    for (const auto& ci : pianoPlan.ccs) {
        in.engine->scheduleCC("Piano", in.chPiano, ci.cc, ci.value, ci.startPos, ci.structural, ci.logic_tag);
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
        n.virtuosity.harmonicRisk = pc.harmonicRisk;
        n.virtuosity.rhythmicComplexity = pc.rhythmicComplexity;
        n.virtuosity.interaction = pc.interaction;
        n.virtuosity.toneDark = pc.toneDark;
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
}

} // namespace playback

