#include "ChordOntology.h"
#include <algorithm>
#include <cmath>

namespace playback {

// ============================================================================
// Singleton
// ============================================================================

ChordOntology& ChordOntology::instance() {
    static ChordOntology instance;
    return instance;
}

// ============================================================================
// Constructor
// ============================================================================

ChordOntology::ChordOntology() = default;

// ============================================================================
// Set Ontology Registry
// ============================================================================

void ChordOntology::setOntologyRegistry(const virtuoso::ontology::OntologyRegistry* ontology) {
    m_ontology = ontology;
}

// ============================================================================
// Create ActiveChord from ontology keys
// ============================================================================

ActiveChord ChordOntology::createActiveChord(
    int rootPc,
    const QString& chordKey,
    const QString& scaleKey
) const {
    if (!m_ontology) {
        // Return empty chord if ontology not set
        ActiveChord chord;
        chord.rootPc = normalizePc(rootPc);
        chord.ontologyChordKey = chordKey;
        chord.ontologyScaleKey = scaleKey;
        // At minimum, root is always a chord tone
        chord.tier1Absolute.insert(chord.rootPc);
        return chord;
    }

    const auto* chordDef = m_ontology->chord(chordKey);
    const auto* scaleDef = m_ontology->scale(scaleKey);

    return createActiveChord(rootPc, chordDef, scaleDef);
}

// ============================================================================
// Create ActiveChord from ChordDef and ScaleDef pointers
// ============================================================================

ActiveChord ChordOntology::createActiveChord(
    int rootPc,
    const virtuoso::ontology::ChordDef* chordDef,
    const virtuoso::ontology::ScaleDef* scaleDef
) const {
    // Delegate to the version with separate key root, using chord root as key root
    // (backwards compatible behavior)
    return createActiveChord(rootPc, rootPc, chordDef, scaleDef);
}

ActiveChord ChordOntology::createActiveChord(
    int chordRootPc,
    int keyRootPc,
    const virtuoso::ontology::ChordDef* chordDef,
    const virtuoso::ontology::ScaleDef* scaleDef
) const {
    ActiveChord chord;
    chord.rootPc = normalizePc(chordRootPc);

    if (chordDef) {
        chord.ontologyChordKey = chordDef->key;
    }
    if (scaleDef) {
        chord.ontologyScaleKey = scaleDef->key;
    }

    // Build T1 (chord tones) from chord intervals - relative to CHORD root
    if (chordDef) {
        for (int interval : chordDef->intervals) {
            // Normalize to 0-11 (intervals can be >12 for extensions like 13th)
            int pc = normalizePc(chord.rootPc + interval);
            chord.tier1Absolute.insert(pc);
        }
    } else {
        // Fallback: at minimum root is a chord tone
        chord.tier1Absolute.insert(chord.rootPc);
    }

    // Build scale tones set - relative to KEY root (not chord root!)
    std::set<int> scaleTones;
    if (scaleDef) {
        int keyRoot = normalizePc(keyRootPc);
        for (int interval : scaleDef->intervals) {
            int pc = normalizePc(keyRoot + interval);
            scaleTones.insert(pc);
        }
    }

    // Compute T2 (tensions) - scale tones that are 9th, 11th, 13th from CHORD root
    chord.tier2Absolute = computeTensions(chord.rootPc, chord.tier1Absolute, scaleTones);

    // Build T3 (remaining scale tones, excluding T1 and T2)
    for (int pc : scaleTones) {
        if (chord.tier1Absolute.count(pc) == 0 && chord.tier2Absolute.count(pc) == 0) {
            chord.tier3Absolute.insert(pc);
        }
    }

    // Compute avoid notes
    chord.avoidAbsolute = computeAvoidNotes(chord.rootPc, chord.tier1Absolute, scaleTones);

    return chord;
}

ActiveChord ChordOntology::createActiveChord(
    int chordRootPc,
    const virtuoso::ontology::ChordDef* chordDef,
    const QVector<const virtuoso::ontology::ScaleDef*>& scaleDefs
) const {
    ActiveChord chord;
    chord.rootPc = normalizePc(chordRootPc);

    if (chordDef) {
        chord.ontologyChordKey = chordDef->key;
    }

    // Build T1 (chord tones) from chord intervals - relative to CHORD root
    if (chordDef) {
        for (int interval : chordDef->intervals) {
            int pc = normalizePc(chord.rootPc + interval);
            chord.tier1Absolute.insert(pc);
        }
    } else {
        chord.tier1Absolute.insert(chord.rootPc);
    }

    // Build scale tones set by unioning ALL compatible scales (from chord root)
    std::set<int> allScaleTones;
    for (const auto* scaleDef : scaleDefs) {
        if (!scaleDef) continue;
        // Store first scale key for reference
        if (chord.ontologyScaleKey.isEmpty()) {
            chord.ontologyScaleKey = scaleDef->key;
        }
        // Add all scale intervals (from chord root)
        for (int interval : scaleDef->intervals) {
            int pc = normalizePc(chord.rootPc + interval);
            allScaleTones.insert(pc);
        }
    }

    // Compute T2 (tensions) - notes that are 9th, 11th, 13th from chord root
    chord.tier2Absolute = computeTensions(chord.rootPc, chord.tier1Absolute, allScaleTones);

    // Build T3 (remaining scale tones, excluding T1 and T2)
    for (int pc : allScaleTones) {
        if (chord.tier1Absolute.count(pc) == 0 && chord.tier2Absolute.count(pc) == 0) {
            chord.tier3Absolute.insert(pc);
        }
    }

    // Compute avoid notes
    chord.avoidAbsolute = computeAvoidNotes(chord.rootPc, chord.tier1Absolute, allScaleTones);

    return chord;
}

// ============================================================================
// Compute Tensions
// ============================================================================

std::set<int> ChordOntology::computeTensions(
    int rootPc,
    const std::set<int>& chordTones,
    const std::set<int>& scaleTones
) const {
    std::set<int> tensions;

    // Standard tension intervals from root (as pitch classes)
    // 9th = 2 semitones (or 14 mod 12)
    // 11th = 5 semitones (or 17 mod 12)
    // 13th = 9 semitones (or 21 mod 12)
    const int ninth = normalizePc(rootPc + 2);
    const int eleventh = normalizePc(rootPc + 5);
    const int thirteenth = normalizePc(rootPc + 9);

    // Also consider altered tensions
    const int flatNinth = normalizePc(rootPc + 1);
    const int sharpNinth = normalizePc(rootPc + 3);
    const int sharpEleventh = normalizePc(rootPc + 6);
    const int flatThirteenth = normalizePc(rootPc + 8);

    // Add standard tensions if they're in the scale but not chord tones
    if (scaleTones.count(ninth) && !chordTones.count(ninth)) {
        tensions.insert(ninth);
    }
    if (scaleTones.count(eleventh) && !chordTones.count(eleventh)) {
        tensions.insert(eleventh);
    }
    if (scaleTones.count(thirteenth) && !chordTones.count(thirteenth)) {
        tensions.insert(thirteenth);
    }

    // Add altered tensions if they're in the scale
    if (scaleTones.count(flatNinth) && !chordTones.count(flatNinth)) {
        tensions.insert(flatNinth);
    }
    if (scaleTones.count(sharpNinth) && !chordTones.count(sharpNinth)) {
        tensions.insert(sharpNinth);
    }
    if (scaleTones.count(sharpEleventh) && !chordTones.count(sharpEleventh)) {
        tensions.insert(sharpEleventh);
    }
    if (scaleTones.count(flatThirteenth) && !chordTones.count(flatThirteenth)) {
        tensions.insert(flatThirteenth);
    }

    return tensions;
}

// ============================================================================
// Compute Avoid Notes
// ============================================================================

std::set<int> ChordOntology::computeAvoidNotes(
    int rootPc,
    const std::set<int>& chordTones,
    const std::set<int>& scaleTones
) const {
    std::set<int> avoidNotes;

    // MUSIC THEORY RULES FOR AVOID NOTES:
    //
    // 1. Natural 4th (interval 5) is an avoid note when:
    //    - Chord has a major 3rd (interval 4)
    //    - Because 4th creates a minor 2nd clash with 3rd
    //
    // 2. Natural 6th (interval 9) can be avoid note when:
    //    - Chord has a minor 7th (interval 10) in minor context
    //    - Creates tension in Dorian context
    //
    // 3. b2 (interval 1) is often avoid when:
    //    - Creates minor 2nd with root

    const int major3rd = normalizePc(rootPc + 4);
    const int minor3rd = normalizePc(rootPc + 3);
    const int natural4th = normalizePc(rootPc + 5);
    const int minor7th = normalizePc(rootPc + 10);

    // Rule 1: If chord has major 3rd, natural 4th is avoid
    bool hasMajor3rd = chordTones.count(major3rd) > 0;
    bool hasMinor3rd = chordTones.count(minor3rd) > 0;

    if (hasMajor3rd && !hasMinor3rd) {
        // Natural 4th is avoid if it's in the scale (not #4)
        if (scaleTones.count(natural4th) && !chordTones.count(natural4th)) {
            avoidNotes.insert(natural4th);
        }
    }

    // Rule 2: In minor context (has m3 and m7), b6 can be avoid
    bool hasMinor7th = chordTones.count(minor7th) > 0;
    if (hasMinor3rd && hasMinor7th) {
        // In Dorian-style context, b6 creates tension
        // But this is often stylistic - we'll be conservative
        // (Not adding b6 as avoid for now - it's valid in Aeolian)
    }

    // Rule 3: Sus4 chords - major 3rd is avoid (defeats the sus)
    // Check if chord has 4th as chord tone (sus4)
    bool hasFourthAsChordTone = chordTones.count(natural4th) > 0;
    if (hasFourthAsChordTone) {
        // Major 3rd would clash with the sus4 character
        if (scaleTones.count(major3rd)) {
            avoidNotes.insert(major3rd);
        }
    }

    // Rule 4: Sus2 chords - major 3rd is also avoid
    const int second = normalizePc(rootPc + 2);
    bool hasSecondAsChordTone = chordTones.count(second) > 0 && !chordTones.count(major3rd);
    if (hasSecondAsChordTone && !chordTones.count(natural4th)) {
        // Likely a sus2 - major 3rd defeats it
        if (scaleTones.count(major3rd)) {
            avoidNotes.insert(major3rd);
        }
    }

    return avoidNotes;
}

// ============================================================================
// Get Tier
// ============================================================================

int ChordOntology::getTier(int pitchClass, const ActiveChord& chord) const {
    int pc = normalizePc(pitchClass);

    if (chord.tier1Absolute.count(pc)) return 1;  // Chord tone
    if (chord.tier2Absolute.count(pc)) return 2;  // Tension
    if (chord.tier3Absolute.count(pc)) return 3;  // Scale tone
    return 4;  // Chromatic
}

// ============================================================================
// Utility Functions
// ============================================================================

int ChordOntology::normalizePc(int pc) {
    pc = pc % 12;
    if (pc < 0) pc += 12;
    return pc;
}

int ChordOntology::minDistance(int from, int to) {
    from = normalizePc(from);
    to = normalizePc(to);
    int diff = std::abs(to - from);
    return std::min(diff, 12 - diff);
}

int ChordOntology::signedDistance(int from, int to) {
    from = normalizePc(from);
    to = normalizePc(to);

    int diff = to - from;

    // Normalize to -6 to +5 range (prefer smaller absolute values)
    if (diff > 6) diff -= 12;
    if (diff < -6) diff += 12;

    return diff;
}

int ChordOntology::findNearestInOctave(int referenceMidi, int targetPc) {
    targetPc = normalizePc(targetPc);

    // Find the octave of the reference
    int refOctave = referenceMidi / 12;

    // Try the three closest octaves
    int candidates[3] = {
        (refOctave - 1) * 12 + targetPc,
        refOctave * 12 + targetPc,
        (refOctave + 1) * 12 + targetPc
    };

    int bestCandidate = candidates[1];
    int bestDist = std::abs(referenceMidi - candidates[1]);

    for (int c : candidates) {
        if (c >= 0 && c <= 127) {
            int dist = std::abs(referenceMidi - c);
            if (dist < bestDist) {
                bestDist = dist;
                bestCandidate = c;
            }
        }
    }

    // Clamp to valid MIDI range
    return std::max(0, std::min(127, bestCandidate));
}

int ChordOntology::placeBelow(int pitchClass, int referenceMidi) {
    pitchClass = normalizePc(pitchClass);

    // Find pitch class at or below reference
    int refPc = normalizePc(referenceMidi);
    int refOctave = referenceMidi / 12;

    int result;
    if (pitchClass <= refPc) {
        result = refOctave * 12 + pitchClass;
    } else {
        result = (refOctave - 1) * 12 + pitchClass;
    }

    // Clamp to valid MIDI range
    return std::max(0, std::min(127, result));
}

int ChordOntology::placeInRange(int pitchClass, int minMidi, int maxMidi) {
    pitchClass = normalizePc(pitchClass);

    // Find all valid placements
    int minOctave = minMidi / 12;
    int maxOctave = maxMidi / 12;

    for (int octave = minOctave; octave <= maxOctave; ++octave) {
        int candidate = octave * 12 + pitchClass;
        if (candidate >= minMidi && candidate <= maxMidi) {
            return candidate;
        }
    }

    // Fallback: find closest valid placement
    int below = minOctave * 12 + pitchClass;
    int above = (maxOctave + 1) * 12 + pitchClass;

    if (below >= 0 && below >= minMidi - 12) return std::max(0, below);
    if (above <= 127 && above <= maxMidi + 12) return std::min(127, above);

    return std::max(0, std::min(127, minMidi));
}

} // namespace playback
