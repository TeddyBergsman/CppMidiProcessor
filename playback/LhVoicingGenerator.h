#pragma once

#include <QVector>
#include <QString>
#include "music/ChordSymbol.h"
#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/control/PerformanceWeightsV2.h"
#include "virtuoso/theory/FunctionalHarmony.h"

namespace playback {

/**
 * LhVoicingGenerator - Left Hand Voicing Generator (Bill Evans style)
 *
 * Generates left-hand voicings for jazz ballad piano:
 * - Rootless Type A (3-5-7-9) and Type B (7-9-3-5)
 * - Shell voicings (3-7 guide tones)
 * - Quartal voicings (stacked 4ths, McCoy Tyner style)
 * - Inner voice movement for sustained chords
 *
 * Operates in register MIDI 48-68.
 */
class LhVoicingGenerator {
public:
    // ========== Input Context ==========
    struct Context {
        music::ChordSymbol chord;
        int lhLo = 48;
        int lhHi = 64;
        int beatInBar = 0;
        double energy = 0.12;  // Start very low (12%)
        bool chordIsNew = false;
        bool preferShells = true;
        virtuoso::control::PerformanceWeightsV2 weights;
        
        // Key context
        int keyTonicPc = 0;
        virtuoso::theory::KeyMode keyMode = virtuoso::theory::KeyMode::Major;
        
        // Bass coordination
        int bassRegisterHi = 55;
    };
    
    // ========== Output ==========
    struct LhVoicing {
        QVector<int> midiNotes;       // Realized MIDI notes (3-4 notes typically)
        bool isTypeA = true;          // True = Type A (3-5-7-9), False = Type B (7-9-3-5)
        QString ontologyKey;          // e.g., "piano_rootless_a", "piano_lh_quartal"
        double cost = 0.0;            // Voice-leading cost from previous voicing
    };
    
    // ========== State for continuity ==========
    struct State {
        QVector<int> lastLhMidi;         // Previous LH voicing for voice-leading
        bool lastLhWasTypeA = true;      // Alternate Type A/B
        int lastInnerVoiceIndex = 0;     // Which inner voice moved last
        int innerVoiceDirection = 1;     // +1 or -1
    };
    
    // ========== Constructor ==========
    explicit LhVoicingGenerator(const virtuoso::ontology::OntologyRegistry* ont = nullptr);
    
    // ========== Main Generation ==========
    
    /// Generate a rootless voicing (Bill Evans Type A or Type B)
    LhVoicing generateRootless(const Context& c) const;
    
    /// Generate rootless voicing starting from a specific degree (3 or 7)
    /// Used for voice-leading optimization
    LhVoicing generateRootlessFromDegree(const Context& c, int startDegree) const;
    
    /// Generate both Type A (3rd-first) and Type B (7th-first) voicings,
    /// return the one with optimal voice-leading from previous chord
    LhVoicing generateRootlessOptimal(const Context& c) const;
    
    /// Generate voicing with a target for the top note (soprano line)
    /// targetTopMidi: -1 for automatic, or specific MIDI note to aim for
    LhVoicing generateRootlessWithTopTarget(const Context& c, int targetTopMidi) const;
    
    /// Reset voice-leading state to allow register shifts
    void resetVoiceLeadingState() { m_state.lastLhMidi.clear(); }
    
    /// Generate a quartal voicing (McCoy Tyner stacked 4ths)
    LhVoicing generateQuartal(const Context& c) const;
    
    /// Generate a shell voicing (just 3-7 guide tones)
    LhVoicing generateShell(const Context& c) const;
    
    /// Apply inner voice movement to create melodic motion within sustained chords
    /// direction: +1 = up, -1 = down, 0 = automatic
    LhVoicing applyInnerVoiceMovement(const LhVoicing& base, const Context& c, int direction = 0) const;
    
    /// Should LH play on this beat? (sparse: beat 1, sometimes beat 3)
    bool shouldPlayBeat(const Context& c, quint32 hash) const;
    
    /// Choose the best voicing type for current context
    /// Returns: rootless, quartal, or shell based on energy, function, etc.
    LhVoicing generateBest(const Context& c) const;
    
    // ========== Voice Leading ==========
    
    /// Calculate voice-leading cost between two voicings
    double voiceLeadingCost(const QVector<int>& prev, const QVector<int>& next) const;
    
    /// Realize pitch classes to MIDI within register with minimal voice movement
    QVector<int> realizePcsToMidi(const QVector<int>& pcs, int lo, int hi,
                                  const QVector<int>& prevVoicing) const;
    
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
    
    /// Realize a voicing template by stacking intervals (Bill Evans style)
    QVector<int> realizeVoicingTemplate(const QVector<int>& degrees,
                                        const music::ChordSymbol& chord,
                                        int bassMidi, int ceiling) const;
    
    // ========== State ==========
    mutable State m_state;
    const virtuoso::ontology::OntologyRegistry* m_ont = nullptr;
};

} // namespace playback
