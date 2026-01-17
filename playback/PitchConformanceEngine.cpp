#include "PitchConformanceEngine.h"
#include <algorithm>
#include <cmath>
#include <QDebug>

namespace playback {

// ============================================================================
// Constructor
// ============================================================================

PitchConformanceEngine::PitchConformanceEngine() = default;

// ============================================================================
// Gravity Calculation - Per spec Section 3.3
// ============================================================================

GravityResult PitchConformanceEngine::calculateGravity(int pitchClass, const ActiveChord& chord) const {
    GravityResult result;
    pitchClass = ChordOntology::normalizePc(pitchClass);

    result.tier = ChordOntology::instance().getTier(pitchClass, chord);
    result.isAvoidNote = chord.isAvoidNote(pitchClass);

    qDebug() << "PitchConformance: calculateGravity pc=" << pitchClass
             << "tier=" << result.tier << "isAvoid=" << result.isAvoidNote
             << "chord.tier1 size=" << chord.tier1Absolute.size();

    // T1 pitches: no gravity (already home)
    if (result.tier == 1) {
        result.nearestTarget = pitchClass;
        result.distance = 0;
        result.gravityStrength = 0.0f;
        return result;
    }

    // Find nearest T1 or T2 pitch
    int bestTarget = -1;
    int bestDistance = 7;  // Max is 6 semitones

    // Search T1 first (stronger targets)
    for (int target : chord.tier1Absolute) {
        int dist = ChordOntology::minDistance(pitchClass, target);
        if (dist < bestDistance) {
            bestDistance = dist;
            bestTarget = target;
        }
    }

    // Also consider T2 if closer
    for (int target : chord.tier2Absolute) {
        int dist = ChordOntology::minDistance(pitchClass, target);
        if (dist < bestDistance) {
            bestDistance = dist;
            bestTarget = target;
        }
    }

    // Fallback if no target found (shouldn't happen with valid chord)
    if (bestTarget < 0) {
        bestTarget = chord.rootPc;
        bestDistance = ChordOntology::minDistance(pitchClass, bestTarget);
    }

    result.nearestTarget = bestTarget;
    result.distance = ChordOntology::signedDistance(pitchClass, bestTarget);

    // Gravity strength based on tier
    switch (result.tier) {
        case 2:
            result.gravityStrength = kTier2Gravity;
            break;
        case 3:
            result.gravityStrength = kTier3Gravity;
            break;
        case 4:
        default:
            result.gravityStrength = kTier4Gravity;
            break;
    }

    // Avoid notes get boosted gravity
    if (result.isAvoidNote) {
        result.gravityStrength = std::min(1.0f, result.gravityStrength + kAvoidNoteBoost);
    }

    // Apply global multiplier
    result.gravityStrength *= m_gravityMultiplier;
    result.gravityStrength = std::min(1.0f, result.gravityStrength);

    return result;
}

// ============================================================================
// Behavior Selection - Per spec Section 3.6
// ============================================================================

ConformanceResult PitchConformanceEngine::selectBehavior(
    int inputPitch,
    const GravityResult& gravity,
    const ConformanceContext& ctx
) const {
    ConformanceResult result;
    result.outputPitch = inputPitch;
    result.pitchBendCents = 0.0f;
    result.delayMs = 0.0f;

    // T1 pitches: always allow
    if (gravity.tier == 1) {
        result.behavior = ConformanceBehavior::ALLOW;
        return result;
    }

    // T2 pitches: allow, mild bend on long notes
    if (gravity.tier == 2) {
        if (ctx.estimatedDurationMs > kLongDurationMs && ctx.isStrongBeat) {
            result.behavior = ConformanceBehavior::BEND;
            // Gentle bend toward target (20 cents per semitone of distance)
            result.pitchBendCents = gravity.distance * 20.0f;
        } else {
            result.behavior = ConformanceBehavior::ALLOW;
        }
        return result;
    }

    // Check anticipation first - if note is valid for next chord
    if (ctx.inAnticipationWindow()) {
        int pc = ChordOntology::normalizePc(inputPitch);
        if (ctx.nextChord.tier1Absolute.count(pc) ||
            ctx.nextChord.tier2Absolute.count(pc)) {
            result.behavior = ConformanceBehavior::ANTICIPATE;
            return result;
        }
    }

    // T3 scale tones
    if (gravity.tier == 3) {
        // Short duration + weak beat + stepwise = allow as passing tone
        if (ctx.estimatedDurationMs < kShortDurationMs &&
            !ctx.isStrongBeat &&
            ctx.previousPitch >= 0 &&
            isStepwiseMotion(ctx.previousPitch, inputPitch)) {
            result.behavior = ConformanceBehavior::ALLOW;
            return result;
        }

        // Avoid notes on strong beats: snap
        if (gravity.isAvoidNote && ctx.isStrongBeat) {
            result.behavior = ConformanceBehavior::SNAP;
            result.outputPitch = ChordOntology::findNearestInOctave(inputPitch, gravity.nearestTarget);
            return result;
        }

        // Otherwise: bend toward target
        result.behavior = ConformanceBehavior::BEND;
        result.pitchBendCents = gravity.distance * gravity.gravityStrength * 100.0f;
        return result;
    }

    // T4 chromatic
    // Stepwise approach: delay to recontextualize
    if (ctx.previousPitch >= 0 &&
        isStepwiseMotion(ctx.previousPitch, inputPitch) &&
        std::abs(gravity.distance) == 1) {
        result.behavior = ConformanceBehavior::DELAY;
        result.delayMs = kChromaticDelayMs;
        return result;
    }

    // Otherwise: snap (chromatic is too wrong to sustain)
    result.behavior = ConformanceBehavior::SNAP;
    result.outputPitch = ChordOntology::findNearestInOctave(inputPitch, gravity.nearestTarget);
    return result;
}

// ============================================================================
// Convenience: Combined gravity + behavior
// ============================================================================

ConformanceResult PitchConformanceEngine::conformPitch(
    int inputPitch,
    const ConformanceContext& ctx
) const {
    int pc = ChordOntology::normalizePc(inputPitch);
    GravityResult gravity = calculateGravity(pc, ctx.currentChord);
    return selectBehavior(inputPitch, gravity, ctx);
}

// ============================================================================
// Harmony Conformance (Simpler: always snap)
// ============================================================================

int PitchConformanceEngine::conformHarmonyPitch(int rawPitch, const ActiveChord& chord) const {
    int pc = ChordOntology::normalizePc(rawPitch);
    GravityResult gravity = calculateGravity(pc, chord);

    // Harmony always snaps to valid pitches (no bend option)
    if (gravity.tier <= 2) {
        return rawPitch;  // Already valid (T1 or T2)
    }

    // Snap to nearest chord tone or tension
    return ChordOntology::findNearestInOctave(rawPitch, gravity.nearestTarget);
}

// ============================================================================
// Bend State Management
// ============================================================================

void PitchConformanceEngine::updateBend(BendState& state, float deltaMs) {
    float diff = state.targetBendCents - state.currentBendCents;
    float maxChange = state.bendRatePerMs * deltaMs;

    if (std::abs(diff) <= maxChange) {
        state.currentBendCents = state.targetBendCents;
    } else {
        state.currentBendCents += (diff > 0 ? maxChange : -maxChange);
    }
}

int PitchConformanceEngine::centsToMidiBend(float cents, int bendRangeSemitones) {
    // MIDI bend: 0 = -range, 8192 = center, 16383 = +range
    float normalized = cents / (bendRangeSemitones * 100.0f);
    normalized = std::clamp(normalized, -1.0f, 1.0f);
    return static_cast<int>((normalized + 1.0f) * 8191.5f);
}

float PitchConformanceEngine::midiBendToCents(int bendValue, int bendRangeSemitones) {
    // Convert MIDI bend (0-16383) to cents
    float normalized = (bendValue / 8191.5f) - 1.0f;
    return normalized * bendRangeSemitones * 100.0f;
}

// ============================================================================
// Utilities
// ============================================================================

bool PitchConformanceEngine::isStepwiseMotion(int prevPitch, int currentPitch) {
    int interval = std::abs(currentPitch - prevPitch);
    return interval <= 2;  // Whole step or less
}

} // namespace playback
