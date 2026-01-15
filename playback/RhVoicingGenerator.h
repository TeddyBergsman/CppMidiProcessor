#pragma once

#include <QVector>
#include <QString>
#include "music/ChordSymbol.h"
#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/control/PerformanceWeightsV2.h"
#include "virtuoso/theory/FunctionalHarmony.h"

namespace playback {

/**
 * RhVoicingGenerator - Right Hand Voicing Generator (Bill Evans style)
 *
 * Generates right-hand voicings for jazz ballad piano:
 * - Drop-2 voicings (chord tone drops octave)
 * - Triads (close position, color tones)
 * - Dyads (guide tones + extensions)
 * - Single notes (melodic line)
 * - Upper Structure Triads (sophisticated extensions on dominants)
 * - Ornaments (grace notes, turns, mordents)
 *
 * Operates in register MIDI 65-88.
 */
class RhVoicingGenerator {
public:
    // ========== Input Context ==========
    struct Context {
        music::ChordSymbol chord;
        int rhLo = 65;
        int rhHi = 84;
        int sparkleLo = 84;
        int sparkleHi = 96;
        int beatInBar = 0;
        double energy = 0.12;  // Start very low (12%)
        bool chordIsNew = false;
        virtuoso::control::PerformanceWeightsV2 weights;
        
        // Key context
        int keyTonicPc = 0;
        virtuoso::theory::KeyMode keyMode = virtuoso::theory::KeyMode::Major;
        
        // Phrase context
        int barInPhrase = 0;
        int phraseBars = 8;           // Total bars in phrase
        bool phraseEndBar = false;
        double cadence01 = 0.0;

        // Melodic direction hint (from planner, based on phrase position)
        // -1 = descending toward resolution, +1 = ascending toward climax, 0 = neutral
        int melodicDirectionHint = 0;

        // User interaction
        bool userSilence = false;
        bool userBusy = false;
        int userMeanMidi = 72;
    };
    
    // ========== Voicing Types ==========
    enum class VoicingType {
        Drop2,      // 4-note voiced with 2nd from top dropped
        Triad,      // 3-note close position
        Dyad,       // 2-note (guide tones or colorful interval)
        Single,     // Single melodic note
        UST         // Upper Structure Triad
    };
    
    // ========== Output ==========
    struct RhVoicing {
        QVector<int> midiNotes;          // Realized MIDI notes
        QVector<int> velocities;         // Per-note velocity (Phase 4: voice shading)
        QVector<double> timingOffsets;   // Per-note timing offset in beats (Phase 5)
        double voicingOffset = 0.0;      // Overall voicing offset in beats
        int topNoteMidi = -1;            // The melodic line note
        int melodicDirection = 0;        // -1=down, 0=hold, +1=up
        QString ontologyKey;             // e.g., "piano_rh_drop2", "piano_ust_bIII"
        VoicingType type = VoicingType::Dyad;
        bool isColorTone = false;        // Uses extensions (9/11/13)?
        double cost = 0.0;               // Voice-leading cost
        int baseVelocity = 70;           // Base velocity before shading
    };
    
    // ========== Upper Structure Triads ==========
    struct UpperStructureTriad {
        int rootPc;           // Root of the triad (0-11)
        bool isMajor;         // true = major triad, false = minor
        double tensionLevel;  // 0.0 = consonant, 1.0 = very tense
        QString colorDescription; // e.g., "9-#11-13", "b9-#11-b13"
        QString ontologyKey;  // e.g., "piano_ust_bIII"
    };
    
    // ========== Ornaments ==========
    enum class OrnamentType {
        None,
        GraceNote,    // Single short note before main
        Turn,         // Upper-main-lower-main
        Mordent,      // Quick main-upper-main
        Appoggiatura  // Leaning note that resolves
    };
    
    struct Ornament {
        OrnamentType type = OrnamentType::None;
        QVector<int> notes;           // Grace/ornament notes (MIDI)
        QVector<int> durationsMs;     // Duration of each note in ms
        QVector<int> velocities;      // Velocity of each note
        int mainNoteDelayMs = 0;      // How much to delay main note
    };
    
    // ========== State for continuity ==========
    struct State {
        QVector<int> lastRhMidi;          // Previous RH voicing
        int lastRhTopMidi = 74;           // RH melodic line tracking
        int lastRhSecondMidi = 69;        // Second voice for dyads
        int rhMelodicDirection = 0;       // -1 descending, 0 neutral, +1 ascending
        int rhMotionsThisChord = 0;       // Count of RH movements on current chord
        music::ChordSymbol lastChordForRh; // Track when chord changes
        VoicingType lastVoicingType = VoicingType::Dyad;

        // Melodic contour tracking (for avoiding repetition)
        int consecutiveSameTop = 0;       // How many beats on same top note
        int targetMelodicDirection = 0;   // Phrase-driven direction (-1, 0, +1)

        // Inner voice shimmer state (Evans signature)
        int shimmerPhase = 0;             // 0-3 cycle through shimmer patterns
        int beatsOnSameChord = 0;         // How long we've been on current chord
        int innerVoice1Target = -1;       // Chromatic target for inner voice 1
        int innerVoice2Target = -1;       // Chromatic target for inner voice 2
    };

    // ========== Inner Voice Shimmer (Phase 2) ==========

    /**
     * Apply inner voice shimmer to a voicing.
     * Called when sustaining on the same chord - creates Evans' signature
     * "breathing" quality where inner voices move chromatically while
     * top and bottom voices remain anchored.
     *
     * @param base The base voicing to shimmer
     * @param c Context for chord/energy info
     * @return Modified voicing with inner voice movement
     */
    RhVoicing applyInnerVoiceShimmer(const RhVoicing& base, const Context& c) const;

    /**
     * Check if shimmer should be applied.
     * Shimmer only when: same chord for 2+ beats, energy < 0.5, user not busy
     */
    bool shouldApplyShimmer(const Context& c) const;

    // ========== Velocity Shading (Phase 4) ==========

    /**
     * Apply voice shading to a voicing.
     * Evans signature: top voice prominent (+8), inner voices recede (-5)
     * Also applies phrase-dynamic shaping based on position in phrase.
     *
     * @param voicing The voicing to apply shading to
     * @param c Context for phrase position
     * @return Modified voicing with populated velocities
     */
    RhVoicing applyVelocityShading(const RhVoicing& voicing, const Context& c) const;

    /**
     * Calculate phrase-position velocity offset.
     * Returns -5 to +5 based on where we are in the phrase arc.
     */
    int phraseVelocityOffset(const Context& c) const;

    // ========== Micro-Timing (Phase 5) ==========

    /**
     * Apply phrase-aware and voice-specific micro-timing.
     * All offsets are in BEATS (BPM-constrained).
     *
     * Phrase timing:
     * - Phrase start: slightly late (+0.02 beats) - settling in
     * - Mid-phrase: on beat (0) - stable groove
     * - Phrase climax: slightly early (-0.015 beats) - forward momentum
     * - Resolution: slightly late (+0.025 beats) - relaxed arrival
     *
     * Voice timing (Evans roll):
     * - Bottom note: on time
     * - Each subsequent voice: +0.008 beats later
     * - Creates gentle "bloom" rather than block attack
     *
     * @param voicing The voicing to apply timing to
     * @param c Context for phrase position
     * @return Modified voicing with populated timing offsets
     */
    RhVoicing applyMicroTiming(const RhVoicing& voicing, const Context& c) const;

    /**
     * Calculate phrase-position timing offset in beats.
     * Returns -0.02 to +0.03 beats based on phrase position.
     */
    double phraseTimingOffset(const Context& c) const;

    // ========== Constructor ==========
    explicit RhVoicingGenerator(const virtuoso::ontology::OntologyRegistry* ont = nullptr);
    
    // ========== Main Generation ==========
    
    /// Generate a Drop-2 voicing
    RhVoicing generateDrop2(const Context& c) const;
    
    /// Generate a close-position triad
    RhVoicing generateTriad(const Context& c) const;
    
    /// Generate a dyad (2 notes, typically guide + color)
    RhVoicing generateDyad(const Context& c) const;
    
    /// Generate a single melodic note
    RhVoicing generateSingle(const Context& c) const;

    /// Generate a melodic dyad (walking 3rds/6ths for Evans-style motion)
    /// Creates parallel motion rather than isolated chord tones
    RhVoicing generateMelodicDyad(const Context& c, int direction = 0) const;

    /// Generate a unison voicing (RH chord synced with LH for reinforced texture)
    /// Used for Evans-style "locked hands" moments without full block chords
    RhVoicing generateUnisonVoicing(const Context& c, const QVector<int>& lhMidi) const;

    /// Generate upper portion of block chord (climax moments)
    /// Coordinated with LH's generateBlockLower()
    RhVoicing generateBlockUpper(const Context& c, int targetTopMidi) const;

    // ========== Stage 9: New Voicing Types ==========

    /// Generate harmonized dyad: melody note + parallel 3rd or 6th below
    /// Creates singing, vocal-like texture (Evans/Freeman accompaniment style)
    RhVoicing generateHarmonizedDyad(const Context& c) const;

    /// Generate octave-doubled melody: powerful, singing sound
    /// Used at climax moments for emphasis
    RhVoicing generateOctaveDouble(const Context& c) const;

    /// Generate blues grace approach: main note with b3/b7 grace note
    /// Adds bluesy inflection to dominant chords
    RhVoicing generateBluesGrace(const Context& c) const;

    /// Generate an Upper Structure Triad voicing
    RhVoicing generateUST(const Context& c) const;
    
    /// Get available UST options for a chord (sorted by consonance)
    QVector<UpperStructureTriad> getUpperStructureTriads(const music::ChordSymbol& chord) const;
    
    /// Build a specific UST voicing
    RhVoicing buildUstVoicing(const Context& c, const UpperStructureTriad& ust) const;
    
    /// Choose the best voicing type for current context
    RhVoicing generateBest(const Context& c) const;
    
    /// Get activity level (0-4 hits per beat) based on context
    int activityLevel(const Context& c, quint32 hash) const;
    
    /// Select next melodic target for stepwise motion
    int selectNextMelodicTarget(const Context& c) const;
    
    // ========== Ornaments ==========
    
    /// Should we add an ornament for current context?
    bool shouldAddOrnament(const Context& c, quint32 hash) const;
    
    /// Generate an ornament for a given target note
    Ornament generateOrnament(const Context& c, int targetMidi, quint32 hash) const;
    
    // ========== Voice Leading ==========
    
    /// Calculate voice-leading cost
    double voiceLeadingCost(const QVector<int>& prev, const QVector<int>& next) const;
    
    /// Realize pitch classes to MIDI within register
    QVector<int> realizePcsToMidi(const QVector<int>& pcs, int lo, int hi,
                                  const QVector<int>& prevVoicing,
                                  int targetTopMidi = -1) const;
    
    /// Select melodic top note (stepwise preferred, avoid large leaps)
    int selectMelodicTopNote(const QVector<int>& candidatePcs, int rhLo, int rhHi,
                             int lastTopMidi, const Context& c) const;
    
    // ========== State Management ==========
    State& state() { return m_state; }
    const State& state() const { return m_state; }
    void setState(const State& s) { m_state = s; }
    
private:
    // ========== Helpers ==========
    static int pcForDegree(const music::ChordSymbol& c, int degree);
    static int thirdInterval(music::ChordQuality q);
    static int fifthInterval(music::ChordQuality q);
    static int seventhInterval(const music::ChordSymbol& c);
    static int nearestMidiForPc(int pc, int around, int lo, int hi);
    
    /// Determine what chord degree a pitch class represents
    int getDegreeForPc(int pc, const music::ChordSymbol& chord) const;

    /// Update melodic direction target based on phrase position
    /// Called at start of generateBest() to set m_state.targetMelodicDirection
    void updateMelodicDirection(const Context& c) const;

    /// Apply direction-aware top note selection
    /// Returns the best top MIDI note considering voice-leading, direction, and repetition
    int selectDirectionAwareTop(const QVector<int>& candidatePcs, int lo, int hi,
                                int lastTopMidi, int targetDir, int repetitionCount) const;

    // ========== State ==========
    mutable State m_state;
    const virtuoso::ontology::OntologyRegistry* m_ont = nullptr;
};

} // namespace playback
