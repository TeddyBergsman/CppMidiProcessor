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
        bool phraseEndBar = false;
        double cadence01 = 0.0;
        
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
        int topNoteMidi = -1;            // The melodic line note
        int melodicDirection = 0;        // -1=down, 0=hold, +1=up
        QString ontologyKey;             // e.g., "piano_rh_drop2", "piano_ust_bIII"
        VoicingType type = VoicingType::Dyad;
        bool isColorTone = false;        // Uses extensions (9/11/13)?
        double cost = 0.0;               // Voice-leading cost
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
    };
    
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
    
    // ========== State ==========
    mutable State m_state;
    const virtuoso::ontology::OntologyRegistry* m_ont = nullptr;
};

} // namespace playback
