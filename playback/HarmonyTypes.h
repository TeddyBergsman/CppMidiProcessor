#pragma once

#include <QString>
#include <set>
#include <array>

#include "virtuoso/theory/FunctionalHarmony.h"

namespace playback {

// ============================================================================
// Existing types (preserved for compatibility)
// ============================================================================

struct LocalKeyEstimate {
    int tonicPc = 0;
    QString scaleKey;
    QString scaleName;
    virtuoso::theory::KeyMode mode = virtuoso::theory::KeyMode::Major;
    double score = 0.0;
    double coverage = 0.0;
};

// ============================================================================
// Phase 1: Chord Ontology Types
// ============================================================================

// Chord quality definitions for tier system
enum class ChordQuality {
    MAJ7,       // Major 7th
    DOM7,       // Dominant 7th
    MIN7,       // Minor 7th
    MIN7B5,     // Half-diminished (m7b5)
    DIM7,       // Fully diminished 7th
    AUG,        // Augmented triad
    SUS4,       // Suspended 4th
    SUS2,       // Suspended 2nd
    MAJ6,       // Major 6th
    MIN6,       // Minor 6th
    ADD9,       // Add 9 (no 7th)
    MAJ,        // Major triad
    MIN         // Minor triad
};

// Scale types for tier 3 pitch class derivation
enum class ScaleType {
    IONIAN,          // Major scale
    DORIAN,          // Minor with raised 6th
    PHRYGIAN,        // Minor with lowered 2nd
    LYDIAN,          // Major with raised 4th
    MIXOLYDIAN,      // Major with lowered 7th
    AEOLIAN,         // Natural minor
    LOCRIAN,         // Diminished scale mode
    MELODIC_MINOR,   // Ascending melodic minor
    HARMONIC_MINOR,  // Natural minor with raised 7th
    DIMINISHED_WH,   // Whole-half diminished
    DIMINISHED_HW,   // Half-whole diminished
    WHOLE_TONE,      // Whole tone scale
    ALTERED          // Altered dominant (superlocrian)
};

// ============================================================================
// Phase 2: Pitch Conformance Types
// ============================================================================

// Behavior selected by conformance algorithm
enum class ConformanceBehavior {
    ALLOW,      // No correction - output pitch as-is
    SNAP,       // Immediately correct to nearest T1/T2 target
    TIMED_SNAP, // Play original, then snap after delay (for passing tones)
    TIMED_BEND, // Play original, then bend to target over time
    BEND,       // Output original pitch, apply pitch bend toward target (immediate)
    ANTICIPATE, // Allow note as anticipation of upcoming chord
    DELAY       // Micro-delay note onset to recontextualize as approach
};

// Result of gravity calculation for a pitch
struct GravityResult {
    int nearestTarget = 0;      // Pitch class of nearest T1/T2
    int distance = 0;           // Semitones to target (-6 to +6)
    float gravityStrength = 0.0f; // 0.0 to 1.0
    int tier = 4;               // Current pitch's tier (1-4)
    bool isAvoidNote = false;   // Special flag for avoid notes
};

// Result of conformance behavior selection
struct ConformanceResult {
    ConformanceBehavior behavior = ConformanceBehavior::ALLOW;
    int outputPitch = 0;        // May differ from input if SNAP
    float pitchBendCents = 0.0f;// Non-zero if BEND
    float delayMs = 0.0f;       // Non-zero if DELAY
    int snapTargetPitch = 0;    // For TIMED_SNAP: pitch to snap to
    float snapDelayMs = 0.0f;   // For TIMED_SNAP: time before snap
};

// State for tracking pitch bend over time
struct BendState {
    float currentBendCents = 0.0f;
    float targetBendCents = 0.0f;
    float bendRatePerMs = 0.5f; // How fast to approach target
};

// ============================================================================
// Phase 3: Lead Configuration Types
// ============================================================================

// Lead processing configuration
struct LeadConfig {
    bool conformanceEnabled = false;  // User toggle
    float gravityMultiplier = 1.0f;   // 0.0-2.0, scales gravity strength
    float bendRatePerMs = 0.5f;       // How fast bend approaches target
    int maxBendCents = 200;           // Limit bend range
};

// ============================================================================
// Phase 4: Harmony Framework Types
// ============================================================================

// Harmony mode selection
enum class HarmonyMode {
    OFF,          // No harmony output
    SINGLE,       // User-selected harmony type
    PRE_PLANNED,  // Automatic phrase-based selection
    VOICE         // Vocal MIDI as harmony source
};

// Harmony generation types (for Single and Pre-Planned modes)
enum class HarmonyType {
    PARALLEL,       // Same direction, same interval (3rds/6ths only)
    SIMILAR,        // Same direction, different intervals (cannot approach perfect consonances)
    CONTRARY,       // Opposite direction movement
    OBLIQUE,        // Pedal tone held while lead moves
    CONVERGENT,     // Voices move toward unison
    DIVERGENT,      // Voices spread from unison
    ISORHYTHMIC,    // Same rhythm, independent chord-tone pitch
    HETEROPHONIC,   // Near-unison with micro-variation
    CALL_RESPONSE,  // Delayed echo/imitation
    DESCANT,        // High obligato above lead
    SHADOW          // Delayed + harmonized (pitched reverb)
};

// Harmony processing configuration
struct HarmonyConfig {
    HarmonyMode mode = HarmonyMode::OFF;
    HarmonyType singleType = HarmonyType::PARALLEL;
    int voiceCount = 1;           // 1-4 harmony voices
    float velocityRatio = 0.85f;  // Harmony velocity vs lead (0.0-1.0)
    // Note: conformance is always enabled for harmony (not user-configurable)
};

// Single harmony voice state
struct HarmonyVoice {
    int channel = 12;             // 12, 13, 14, or 15
    int currentPitch = -1;        // Currently sounding pitch (-1 if none)
    int velocity = 0;
    BendState bendState;
};

// ============================================================================
// Phase 6: Pre-Planned Mode Types
// ============================================================================

// Phrase function classification
enum class PhraseFunction {
    OPENING,      // First phrase of section
    DEVELOPMENT,  // Building/exploring
    CLIMAX,       // Peak intensity
    CADENTIAL,    // Approaching resolution
    RESOLUTION    // Final phrase, resolving
};

// Phrase boundary for pre-planned mode
struct PhraseBoundary {
    int startBar = 0;
    int endBar = 0;
    PhraseFunction function = PhraseFunction::DEVELOPMENT;
    HarmonyType suggestedType = HarmonyType::PARALLEL;
};

// ============================================================================
// Channel Constants
// ============================================================================

namespace channels {
    constexpr int LEAD = 1;        // Lead melody (guitar with optional conformance)
    constexpr int HARMONY_1 = 12;  // Primary harmony voice
    constexpr int HARMONY_2 = 13;  // Second harmony voice
    constexpr int HARMONY_3 = 14;  // Third harmony voice
    constexpr int HARMONY_4 = 15;  // Fourth harmony voice
}

// ============================================================================
// Utility Functions (declarations - defined in ChordOntology.cpp)
// ============================================================================

// Get the tier (1-4) of a pitch class relative to a chord
// Defined in ChordOntology.cpp after ActiveChord is defined

} // namespace playback
