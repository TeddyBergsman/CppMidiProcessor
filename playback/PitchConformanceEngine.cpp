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

    // Find nearest T1 pitch (chord tone) - STRICT: only snap to chord tones
    int bestTarget = -1;
    int bestDistance = 7;  // Max is 6 semitones

    // Search T1 only (chord tones are the only valid snap targets)
    for (int target : chord.tier1Absolute) {
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
    result.snapDelayMs = 0.0f;
    result.snapTargetPitch = inputPitch;

    // ========================================================================
    // SIMPLIFIED CONFORMANCE (v3.3)
    //
    // T1 (chord tones): Always allowed
    // T2 (tensions - 9th, 11th, 13th): Allowed - these are color tones
    // T3 (scale tones): Allowed - these are passing tones
    // T4 (chromatic/avoid notes): Snap to nearest chord tone - truly dissonant
    //
    // Only T4 notes get corrected. This gives musical freedom for color/passing
    // tones while still preventing truly wrong notes.
    // ========================================================================

    int targetPitch = ChordOntology::findNearestInOctave(inputPitch, gravity.nearestTarget);
    bool goingUp = targetPitch > inputPitch;

    // T1 (chord tones): Always allowed - these are home
    if (gravity.tier == 1) {
        result.behavior = ConformanceBehavior::ALLOW;
        qDebug() << "ALLOW T1 (chord tone): note" << inputPitch << "pc" << (inputPitch % 12);
        return result;
    }

    // T2 (tensions): Allowed - 9th, 11th, 13th are color tones
    if (gravity.tier == 2) {
        result.behavior = ConformanceBehavior::ALLOW;
        qDebug() << "ALLOW T2 (tension): note" << inputPitch << "pc" << (inputPitch % 12);
        return result;
    }

    // T3 (scale tones): Allowed - these are passing tones
    if (gravity.tier == 3) {
        result.behavior = ConformanceBehavior::ALLOW;
        qDebug() << "ALLOW T3 (scale tone): note" << inputPitch << "pc" << (inputPitch % 12);
        return result;
    }

    // T4 (chromatic/avoid): Snap to nearest chord tone
    // These are truly dissonant and should be corrected
    if (goingUp) {
        // Snap UP with a short grace note delay
        result.behavior = ConformanceBehavior::TIMED_SNAP;
        result.outputPitch = inputPitch;
        result.snapTargetPitch = targetPitch;
        result.snapDelayMs = 30.0f;  // Quick grace note
        qDebug() << "TIMED_SNAP T4 (up): note" << inputPitch << "->" << targetPitch
                 << "after 30ms";
        return result;
    }

    // Snap DOWN: immediate snap (no grace note delay)
    result.behavior = ConformanceBehavior::SNAP;
    result.outputPitch = targetPitch;
    result.snapTargetPitch = targetPitch;
    qDebug() << "SNAP T4 (down): note" << inputPitch << "->" << targetPitch;
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
    normalized = std::max(-1.0f, std::min(1.0f, normalized));
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
