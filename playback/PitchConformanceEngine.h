#pragma once

#include "HarmonyTypes.h"
#include "ChordOntology.h"

namespace playback {

// ============================================================================
// Conformance Context - Information about the musical context
// ============================================================================

struct ConformanceContext {
    int previousPitch = -1;         // For melodic direction analysis
    float estimatedDurationMs = 100.0f; // Expected note length
    float beatPosition = 0.0f;      // 0.0-3.999 for 4/4
    bool isStrongBeat = false;      // Beat 1 or 3
    int velocity = 64;              // 1-127

    // Chord context
    ActiveChord currentChord;
    ActiveChord nextChord;
    float msToNextChord = 10000.0f; // Time until next chord change

    // Helper to check if we're within anticipation window
    bool inAnticipationWindow() const {
        return msToNextChord < 250.0f;
    }
};

// ============================================================================
// Pitch Conformance Engine - Core algorithm per spec Section 3
// ============================================================================

class PitchConformanceEngine {
public:
    PitchConformanceEngine();

    // ========================================================================
    // Main API
    // ========================================================================

    // Calculate gravity result for a pitch class
    GravityResult calculateGravity(int pitchClass, const ActiveChord& chord) const;

    // Select conformance behavior based on gravity and context
    ConformanceResult selectBehavior(
        int inputPitch,
        const GravityResult& gravity,
        const ConformanceContext& ctx
    ) const;

    // Convenience: combined gravity + behavior selection
    ConformanceResult conformPitch(
        int inputPitch,
        const ConformanceContext& ctx
    ) const;

    // Conform a harmony pitch (simpler: always snap T3/T4 to T1/T2)
    int conformHarmonyPitch(int rawPitch, const ActiveChord& chord) const;

    // ========================================================================
    // Bend State Management
    // ========================================================================

    // Update bend state over time
    static void updateBend(BendState& state, float deltaMs);

    // Convert cents to MIDI pitch bend value (0-16383, 8192 = center)
    static int centsToMidiBend(float cents, int bendRangeSemitones = 2);

    // Convert MIDI pitch bend value to cents
    static float midiBendToCents(int bendValue, int bendRangeSemitones = 2);

    // ========================================================================
    // Configuration
    // ========================================================================

    void setGravityMultiplier(float multiplier) { m_gravityMultiplier = multiplier; }
    float gravityMultiplier() const { return m_gravityMultiplier; }

private:
    // Check if motion from prev to current is stepwise (whole step or less)
    static bool isStepwiseMotion(int prevPitch, int currentPitch);

    // Gravity strength constants per tier (per spec)
    static constexpr float kTier2Gravity = 0.2f;
    static constexpr float kTier3Gravity = 0.5f;
    static constexpr float kTier4Gravity = 0.9f;
    static constexpr float kAvoidNoteBoost = 0.3f;

    // Duration thresholds
    static constexpr float kShortDurationMs = 150.0f;
    static constexpr float kLongDurationMs = 500.0f;

    // Anticipation window
    static constexpr float kAnticipationWindowMs = 250.0f;

    // Delay amount for chromatic approach
    static constexpr float kChromaticDelayMs = 30.0f;

    float m_gravityMultiplier = 1.0f;
};

} // namespace playback
