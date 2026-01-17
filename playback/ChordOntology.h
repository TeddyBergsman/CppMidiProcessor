#pragma once

#include "HarmonyTypes.h"
#include "virtuoso/ontology/OntologyRegistry.h"
#include <set>
#include <map>
#include <array>
#include <QString>

namespace playback {

// ============================================================================
// Active Chord - Runtime chord instance with tier classification
//
// This extends the existing OntologyRegistry chord data with:
// - Tier classification (T1=chord tones, T2=tensions, T3=scale, T4=chromatic)
// - Avoid note identification
// - Gravity-based conformance support
// ============================================================================

struct ActiveChord {
    int rootPc = 0;                 // Root pitch class (0-11, 0=C)
    QString ontologyChordKey;       // Key into OntologyRegistry (e.g., "maj7", "min7")
    QString ontologyScaleKey;       // Key into OntologyRegistry (e.g., "ionian", "dorian")

    // Precomputed pitch class sets (absolute, transposed from root)
    std::set<int> tier1Absolute;    // Chord tones (from OntologyRegistry chord intervals)
    std::set<int> tier2Absolute;    // Tensions (9th, 11th, 13th not in chord)
    std::set<int> tier3Absolute;    // Scale tones (from scale, excluding T1 & T2)
    std::set<int> avoidAbsolute;    // Avoid notes (subset of T3 that clash)

    // Check if a pitch class is an avoid note
    bool isAvoidNote(int pitchClass) const {
        return avoidAbsolute.count(pitchClass) > 0;
    }

    // Check if pitch class is in any valid tier (T1, T2, or T3)
    bool isValidScaleTone(int pitchClass) const {
        return tier1Absolute.count(pitchClass) ||
               tier2Absolute.count(pitchClass) ||
               tier3Absolute.count(pitchClass);
    }
};

// ============================================================================
// Chord Ontology - Tier classification layer on top of OntologyRegistry
//
// This class does NOT duplicate chord/scale definitions. Instead it:
// 1. Uses OntologyRegistry for chord intervals and scale intervals
// 2. Adds tier classification logic (T1/T2/T3/T4)
// 3. Adds avoid note rules based on music theory
// ============================================================================

class ChordOntology {
public:
    // Get singleton instance
    static ChordOntology& instance();

    // Set the ontology registry to use (must be called before use)
    void setOntologyRegistry(const virtuoso::ontology::OntologyRegistry* ontology);

    // Create an ActiveChord from a root pitch class and ontology keys
    // Uses OntologyRegistry to look up chord/scale intervals
    ActiveChord createActiveChord(
        int rootPc,
        const QString& chordKey,    // e.g., "maj7", "min7", "7"
        const QString& scaleKey     // e.g., "ionian", "dorian", "mixolydian"
    ) const;

    // Convenience: create from ChordDef and ScaleDef pointers
    ActiveChord createActiveChord(
        int rootPc,
        const virtuoso::ontology::ChordDef* chordDef,
        const virtuoso::ontology::ScaleDef* scaleDef
    ) const;

    // Get the tier (1-4) of a pitch class relative to an active chord
    // Returns: 1 = chord tone, 2 = tension, 3 = scale tone, 4 = chromatic
    int getTier(int pitchClass, const ActiveChord& chord) const;

    // ========================================================================
    // Utility functions
    // ========================================================================

    // Normalize pitch class to 0-11
    static int normalizePc(int pc);

    // Get minimum distance on pitch class circle (0 to 6)
    static int minDistance(int from, int to);

    // Get signed distance on pitch class circle (-6 to +6, prefers smaller absolute)
    static int signedDistance(int from, int to);

    // Find MIDI note with target pitch class, nearest to reference
    static int findNearestInOctave(int referenceMidi, int targetPc);

    // Place pitch class in octave at or below reference MIDI note
    static int placeBelow(int pitchClass, int referenceMidi);

    // Place pitch class within a MIDI range
    static int placeInRange(int pitchClass, int minMidi, int maxMidi);

private:
    ChordOntology();

    // Determine avoid notes based on chord structure
    // Rule: If chord has major 3rd (interval 4), the natural 4th (interval 5)
    // is an avoid note because it creates a minor 2nd clash
    std::set<int> computeAvoidNotes(
        int rootPc,
        const std::set<int>& chordTones,
        const std::set<int>& scaleTones
    ) const;

    // Determine tensions (9th, 11th, 13th that aren't chord tones)
    std::set<int> computeTensions(
        int rootPc,
        const std::set<int>& chordTones,
        const std::set<int>& scaleTones
    ) const;

    const virtuoso::ontology::OntologyRegistry* m_ontology = nullptr;
};

// ============================================================================
// Convenience function (uses singleton)
// ============================================================================

inline int getTier(int pitchClass, const ActiveChord& chord) {
    return ChordOntology::instance().getTier(pitchClass, chord);
}

} // namespace playback
