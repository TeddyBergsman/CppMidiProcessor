#pragma once

#include "HarmonyTypes.h"
#include "ChordOntology.h"
#include <memory>

namespace playback {

// ============================================================================
// Harmony Generator Interface
//
// Base interface for all harmony generation algorithms. Each generator
// implements a specific harmony type (Parallel, Contrary, Oblique, etc.)
// and produces harmony pitches based on the lead melody.
// ============================================================================

class IHarmonyGenerator {
public:
    virtual ~IHarmonyGenerator() = default;

    // Get the harmony type this generator produces
    virtual HarmonyType type() const = 0;

    // Generate harmony pitch(es) for a given lead note
    // Returns up to 4 harmony pitches (one per voice), or -1 for inactive voices
    // Parameters:
    //   leadPitch: The input lead MIDI note number (0-127)
    //   velocity: The input lead velocity (1-127)
    //   chord: The current active chord with tier information
    //   voiceCount: How many harmony voices to generate (1-4)
    // Returns:
    //   Array of 4 MIDI note numbers, -1 for inactive voices
    virtual std::array<int, 4> generate(
        int leadPitch,
        int velocity,
        const ActiveChord& chord,
        int voiceCount
    ) = 0;

    // Called when the lead note off occurs
    // Some generators (like Call-Response) may use this to trigger delayed notes
    virtual void onLeadNoteOff(int leadPitch) { (void)leadPitch; }

    // Called periodically to update time-based state
    // deltaMs: milliseconds since last update
    virtual void update(float deltaMs) { (void)deltaMs; }

    // Reset internal state (called on mode changes, etc.)
    virtual void reset() {}
};

// ============================================================================
// Generator Factory
//
// Creates harmony generators based on HarmonyType
// ============================================================================

std::unique_ptr<IHarmonyGenerator> createHarmonyGenerator(HarmonyType type);

// ============================================================================
// Harmony Voice Manager
//
// Manages 4 harmony voices on channels 12-15.
// Coordinates generator selection and voice allocation.
// ============================================================================

class HarmonyVoiceManager {
public:
    HarmonyVoiceManager();
    ~HarmonyVoiceManager();

    // Configuration
    void setHarmonyType(HarmonyType type);
    HarmonyType harmonyType() const { return m_harmonyType; }

    void setVoiceCount(int count);
    int voiceCount() const { return m_voiceCount; }

    void setVelocityRatio(float ratio);
    float velocityRatio() const { return m_velocityRatio; }

    // Process a new lead note
    // Returns array of 4 HarmonyVoice structs with the generated harmony
    std::array<HarmonyVoice, 4> processLeadNote(
        int leadPitch,
        int leadVelocity,
        const ActiveChord& chord
    );

    // Called when lead note off occurs
    void onLeadNoteOff(int leadPitch);

    // Get current voice states
    const std::array<HarmonyVoice, 4>& voices() const { return m_voices; }

    // Periodic update for time-based effects
    void update(float deltaMs);

    // Reset all voices and state
    void reset();

    // Output channel for a voice index (0-3)
    static int channelForVoice(int voiceIndex);

private:
    HarmonyType m_harmonyType = HarmonyType::PARALLEL;
    int m_voiceCount = 1;
    float m_velocityRatio = 0.85f;

    std::unique_ptr<IHarmonyGenerator> m_generator;
    std::array<HarmonyVoice, 4> m_voices;
};

} // namespace playback
