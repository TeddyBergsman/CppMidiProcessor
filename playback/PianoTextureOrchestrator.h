#pragma once

#include <QString>
#include <QVector>

#include "music/ChordSymbol.h"
#include "virtuoso/groove/GrooveGrid.h"

namespace playback {

/**
 * PianoTextureOrchestrator - Bill Evans Style Texture Coordination
 *
 * This is the CENTRAL decision-maker for piano playing. It answers the question:
 * "What is the piano doing right now?" BEFORE any hand-specific logic runs.
 *
 * Key insight: Evans' piano isn't two independent hands - it's unified textures
 * where both hands serve a musical purpose together.
 *
 * CRITICAL: This is for ACCOMPANIMENT, not solo piano. The user (singer/trumpet)
 * needs to clearly hear chord changes and feel the form. Conservative defaults,
 * expressive options only when safe.
 *
 * Accompaniment Hierarchy of Needs:
 * 1. CLARITY - User must always know where they are harmonically
 * 2. SUPPORT - Stable foundation, predictable structure
 * 3. COLOR - Extensions, inner motion (only after 1 & 2 satisfied)
 * 4. EXPRESSION - Extreme techniques (rare, special moments only)
 */
class PianoTextureOrchestrator {
public:
    // ========== Input State (what orchestrator receives) ==========

    struct RhythmSectionState {
        // Bass player state
        bool bassIsPlaying = false;         // Bass is currently sounding
        int lastBassNote = 36;              // Most recent bass pitch (MIDI)
        int bassRegisterHigh = 55;          // Upper limit of bass register
        int beatsSinceBassRoot = 0;         // How long since bass played root

        // Drummer state
        bool strongBeatComing = false;      // Drum accent on next beat
        bool drumFillInProgress = false;    // Drummer is filling
        bool cymbalCrash = false;           // Recent cymbal crash (back off)
        double drumActivity = 0.5;          // 0-1 how active drums are
    };

    struct SoloistState {
        bool userBusy = false;              // Soloist is playing/singing
        bool userSilence = true;            // Soloist is pausing
        double userSilenceDuration = 0.0;   // How long they've been silent (beats)
        int userMeanMidi = 72;              // Center of user's recent range
        int userHighMidi = 84;              // High note of user's range
        int userLowMidi = 60;               // Low note of user's range
        bool userApproachingPhraseEnd = false;  // User seems to be ending phrase
    };

    struct MusicalContext {
        // Timing
        int beatInBar = 0;                  // 0-3 for 4/4
        int barInPhrase = 0;                // Position in current phrase
        int phraseBars = 4;                 // Length of phrase
        bool isChordChange = false;         // New chord on this beat
        bool isPhraseEnd = false;           // At phrase boundary
        bool isClimaxPoint = false;         // Song climax

        // Harmony
        music::ChordSymbol chord;           // Current chord
        music::ChordSymbol nextChord;       // Upcoming chord (if known)
        bool hasNextChord = false;          // Whether nextChord is valid
        int beatsUntilChordChange = 4;      // Beats until next chord
        QString chordFunction;              // "Tonic", "Dominant", etc.

        // Energy and dynamics
        double energy = 0.2;                // 0.0-1.0 (conservative default)
        double cadence01 = 0.0;             // Cadence strength 0-1

        // Key context
        int keyTonicPc = 0;                 // Key tonic pitch class
        bool isMinorKey = false;            // Major or minor key
    };

    struct OrchestratorInput {
        SoloistState soloist;
        RhythmSectionState rhythmSection;
        MusicalContext context;
    };

    // ========== Output: Texture Decisions ==========

    // Primary texture modes
    enum class TextureMode {
        // When soloist is playing (userBusy)
        Support,            // LH rootless only, RH rests (DEFAULT for accompaniment)
        SupportWithColor,   // LH rootless + RH color dyad on chord changes only
        Space,              // Neither hand plays (let soloist breathe)

        // When soloist pauses (userSilence)
        Fill,               // LH sustains, RH plays brief melodic fill
        BalancedComp,       // LH rootless, RH supportive triads
        Dialogue,           // LH statement, RH responds (extended silence only)

        // Rhythm section coordination
        ShellAnticipation,  // Shell on "&4" anticipating bass root on "1"
        DelayedEntry,       // Wait for bass to establish root, then add color

        // Structural moments
        BlockChord,         // Both hands locked together (rare, climax only)
        Resolution          // Full voicing with gentle roll
    };

    // Voicing type for each hand
    enum class VoicingRole {
        None,               // Don't play
        Shell,              // 3-7 only
        Dyad,               // 2 notes (color)
        Triad,              // 3 notes
        Rootless,           // 4-note rootless (Type A or B)
        Block,              // Full block chord portion
        MelodicDyad         // Walking 3rds/6ths (chromatic motion)
    };

    // Timing role for each hand
    enum class TimingRole {
        Rest,               // Don't play this beat
        OnBeat,             // Play on the beat
        Anticipate,         // Play before beat ("&4" before "1")
        Delay,              // Play after beat ("&1" after "1")
        WithOther,          // Play exactly with other hand (unison)
        Respond             // Play after other hand (call-response)
    };

    // Complete decision for one hand
    struct HandRole {
        VoicingRole voicing = VoicingRole::None;
        TimingRole timing = TimingRole::Rest;
        int registerLow = 48;               // MIDI low bound
        int registerHigh = 72;              // MIDI high bound
        int targetTopMidi = -1;             // Melodic target (-1 = don't care)
        double velocityMult = 1.0;          // Velocity modifier
        double durationMult = 1.0;          // Duration modifier
        bool accentTop = false;             // Accent top voice
    };

    // Full orchestrator output
    struct TextureDecision {
        TextureMode mode = TextureMode::Support;
        HandRole leftHand;
        HandRole rightHand;

        // Timing offsets (milliseconds)
        int lhTimingOffsetMs = 0;           // LH timing adjustment
        int rhTimingOffsetMs = 0;           // RH timing adjustment (slightly after LH for Evans)

        // Additional directives
        bool omitRoot = true;               // Suppress root (bass has it)
        bool innerVoiceMotion = false;      // Apply inner voice movement
        bool useHemiola = false;            // Apply hemiola rhythm (RARE)
        bool dramaticPause = false;         // Intentional silence

        // Debug/logging
        QString rationale;                  // Why this decision was made
    };

    // ========== Public Interface ==========

    PianoTextureOrchestrator();

    /**
     * Main entry point: decide texture for this beat.
     * This should be called BEFORE any hand-specific voicing generation.
     */
    TextureDecision decide(const OrchestratorInput& input) const;

    /**
     * Check if root should be omitted (bass is handling it).
     */
    bool shouldOmitRoot(const OrchestratorInput& input) const;

    /**
     * Get timing role based on bass coordination.
     */
    TimingRole getTimingForBass(const OrchestratorInput& input, double energy) const;

    /**
     * Get anticipation amount in beats (conservative by default).
     * Returns 0.0-1.5 depending on context.
     */
    double getAnticipationBeats(const OrchestratorInput& input) const;

private:
    // ========== Mode Selection Logic ==========

    TextureMode selectModeWhenUserBusy(const OrchestratorInput& input) const;
    TextureMode selectModeWhenUserSilent(const OrchestratorInput& input) const;
    TextureMode selectModeForStructuralMoment(const OrchestratorInput& input) const;

    // ========== Hand Role Assignment ==========

    HandRole assignLeftHandRole(TextureMode mode, const OrchestratorInput& input) const;
    HandRole assignRightHandRole(TextureMode mode, const OrchestratorInput& input) const;

    // ========== Timing Calculation ==========

    int calculateLhTimingOffset(TextureMode mode, const OrchestratorInput& input) const;
    int calculateRhTimingOffset(TextureMode mode, const OrchestratorInput& input) const;

    // ========== Safety Checks (Accompaniment Hierarchy) ==========

    /**
     * Ensure we're not doing anything that would confuse the soloist.
     * Priority 1: Clarity > Priority 2: Support > Priority 3: Color > Priority 4: Expression
     */
    TextureDecision applySafetyConstraints(TextureDecision decision,
                                            const OrchestratorInput& input) const;

    // ========== Expression Gates ==========

    /**
     * Check if expressive technique is safe to use.
     * Expressive techniques (hemiola, extreme anticipation, etc.) are only
     * allowed when ALL safety conditions are met.
     */
    bool isExpressionSafe(const OrchestratorInput& input) const;

    // Returns true if we're at a moment where extreme anticipation is appropriate
    bool isClimaxMoment(const OrchestratorInput& input) const;

    // Returns true if user silence is long enough for extended fills
    bool isExtendedSilence(const OrchestratorInput& input) const;

    // ========== Stage 6: Rhythmic Phrase System ==========

public:
    /**
     * Rhythmic phrase types for Evans-style comping.
     * These are multi-beat patterns, not beat-by-beat decisions.
     */
    enum class RhythmicPhraseType {
        Sustained,        // Long held voicing with inner voice motion
        Punctuation,      // Single chord at phrase boundary, then rest
        Hemiola,          // 3-note groupings over 4/4 (floating feel) - RARE
        DisplacedShell,   // Shell anticipates bass root
        Conversational,   // LH/RH alternate, filling each other's gaps
        Unison,           // LH/RH strike together (reinforced texture)
        DramaticPause     // Intentional silence for breathing room
    };

    /**
     * A rhythmic phrase pattern spanning multiple beats.
     * This replaces beat-by-beat probability with musical coherence.
     */
    struct RhythmicPhrase {
        RhythmicPhraseType type = RhythmicPhraseType::Sustained;

        // Per-beat play decisions (indexed by beatInBar 0-3)
        bool lhPlays[4] = {true, false, false, false};   // Conservative default: beat 1 only
        bool rhPlays[4] = {false, false, false, false};  // RH rests by default

        // Per-beat timing offsets (ms, negative = early)
        int lhTimingMs[4] = {0, 0, 0, 0};
        int rhTimingMs[4] = {0, 0, 0, 0};

        // Overall phrase characteristics
        double density = 0.25;          // How "filled" the bar is (0-1)
        bool hasAnticipation = false;   // Does phrase anticipate next chord?
        int anticipationBeat = -1;      // Which beat has the anticipation (-1 = none)

        QString description;            // Debug: what kind of phrase
    };

    /**
     * Generate a rhythmic phrase pattern for the current bar.
     * Called once per bar, not per beat.
     */
    RhythmicPhrase generateRhythmicPhrase(const OrchestratorInput& input, quint32 hash) const;

    /**
     * Check if this beat should play based on the current phrase pattern.
     * This replaces the old shouldPlayBeat() probability approach.
     */
    bool shouldPlayBeatInPhrase(const RhythmicPhrase& phrase, int beatInBar, bool isLH) const;

    /**
     * Get timing offset for a specific beat in the phrase.
     */
    int getTimingOffsetForBeat(const RhythmicPhrase& phrase, int beatInBar, bool isLH) const;

    /**
     * Extreme chord anticipation check.
     * Returns true ONLY when ALL conditions for extreme anticipation are met.
     * This is intentionally VERY restrictive - maybe 1-2 times per song.
     */
    bool isExtremeAnticipationAppropriate(const OrchestratorInput& input) const;

private:
    // ========== Phrase Generation Helpers ==========

    RhythmicPhrase generateSustainedPhrase(const OrchestratorInput& input) const;
    RhythmicPhrase generatePunctuationPhrase(const OrchestratorInput& input) const;
    RhythmicPhrase generateHemiolaPhrase(const OrchestratorInput& input, quint32 hash) const;
    RhythmicPhrase generateDisplacedShellPhrase(const OrchestratorInput& input) const;
    RhythmicPhrase generateConversationalPhrase(const OrchestratorInput& input, quint32 hash) const;
    RhythmicPhrase generateUnisonPhrase(const OrchestratorInput& input) const;

    /**
     * Select phrase type based on context.
     * Returns the most appropriate phrase type for current musical situation.
     */
    RhythmicPhraseType selectPhraseType(const OrchestratorInput& input, quint32 hash) const;

    /**
     * Scale phrase density based on energy.
     * Low energy = sparse, high energy = filled (but never overwhelming).
     */
    double calculatePhraseDensity(double energy, bool userBusy) const;
};

} // namespace playback
