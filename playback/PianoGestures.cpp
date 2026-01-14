#include "PianoGestures.h"
#include <algorithm>
#include <QtMath>

namespace playback {

// Helper: Get pitch class for a chord degree
// Returns -1 if the degree is not present in the chord
static int pcForDegree(const music::ChordSymbol& chord, int degree) {
    int root = chord.rootPc;
    switch (degree) {
        case 1: return root;
        case 3: {
            // Major 3rd or minor 3rd based on chord quality
            if (chord.quality == music::ChordQuality::Minor ||
                chord.quality == music::ChordQuality::HalfDiminished ||
                chord.quality == music::ChordQuality::Diminished) {
                return (root + 3) % 12;  // Minor 3rd
            }
            return (root + 4) % 12;  // Major 3rd
        }
        case 5: {
            // Perfect 5th, diminished, or augmented
            if (chord.quality == music::ChordQuality::Diminished ||
                chord.quality == music::ChordQuality::HalfDiminished) {
                return (root + 6) % 12;  // Diminished 5th
            }
            if (chord.quality == music::ChordQuality::Augmented) {
                return (root + 8) % 12;  // Augmented 5th
            }
            return (root + 7) % 12;  // Perfect 5th
        }
        case 7: {
            // Only include 7th if the chord actually has one
            if (chord.seventh == music::SeventhQuality::None) {
                return -1;  // No 7th in this chord
            }
            if (chord.seventh == music::SeventhQuality::Major7) {
                return (root + 11) % 12;  // Major 7th
            }
            if (chord.seventh == music::SeventhQuality::Dim7) {
                return (root + 9) % 12;  // Diminished 7th
            }
            return (root + 10) % 12;  // Minor/Dominant 7th
        }
        case 9: {
            // Only include 9th if chord has extension >= 9
            if (chord.extension < 9) {
                return -1;  // No 9th in this chord
            }
            return (root + 2) % 12;
        }
        case 11: {
            if (chord.extension < 11) {
                return -1;
            }
            return (root + 5) % 12;
        }
        case 13: {
            if (chord.extension < 13) {
                return -1;
            }
            return (root + 9) % 12;
        }
        default: return root;
    }
}

QVector<int> PianoGestures::getChordTonesDescending(
    const music::ChordSymbol& chord, int highMidi, int lowMidi) const {

    QVector<int> result;

    // Get the pitch classes for chord tones (only those present in the chord)
    QVector<int> chordPcs;
    for (int degree : {1, 3, 5, 7, 9}) {
        int pc = pcForDegree(chord, degree);
        if (pc >= 0) {  // Only add if the degree exists in this chord
            chordPcs.append(pc);
        }
    }

    // Find all instances of chord tones in the register
    for (int midi = highMidi; midi >= lowMidi; --midi) {
        int pc = midi % 12;
        if (chordPcs.contains(pc)) {
            result.append(midi);
        }
    }

    return result;  // Already sorted descending
}

QVector<int> PianoGestures::getChordScale(
    const music::ChordSymbol& chord, int keyTonicPc, int keyMode) const {

    QVector<int> scale;
    int root = chord.rootPc;

    // Determine scale based on chord quality
    if (chord.quality == music::ChordQuality::Dominant) {
        // Dominant: Mixolydian
        scale = {0, 2, 4, 5, 7, 9, 10};
    } else if (chord.quality == music::ChordQuality::Minor) {
        // Minor 7: Dorian
        scale = {0, 2, 3, 5, 7, 9, 10};
    } else if (chord.quality == music::ChordQuality::Major &&
               chord.seventh == music::SeventhQuality::Major7) {
        // Major 7: Ionian or Lydian
        scale = {0, 2, 4, 6, 7, 9, 11};  // Lydian for Evans sound
    } else if (chord.quality == music::ChordQuality::HalfDiminished) {
        // Half-diminished: Locrian natural 2
        scale = {0, 2, 3, 5, 6, 8, 10};
    } else if (chord.quality == music::ChordQuality::Diminished) {
        // Diminished: Whole-half diminished
        scale = {0, 2, 3, 5, 6, 8, 9, 11};
    } else {
        // Default to major scale
        scale = {0, 2, 4, 5, 7, 9, 11};
    }

    // Transpose to chord root
    for (int& interval : scale) {
        interval = (interval + root) % 12;
    }

    return scale;
}

int PianoGestures::velocityForGesture(
    double energy, int noteIndex, int totalNotes, bool isDescending) const {

    // Base velocity from energy
    int base = 45 + int(energy * 35);  // 45-80 range

    // Contour: descending gestures get softer, ascending get louder
    if (isDescending) {
        // First note loudest, fade out
        double fade = 1.0 - (double(noteIndex) / totalNotes) * 0.3;
        base = int(base * fade);
    } else {
        // Build toward last note
        double build = 0.8 + (double(noteIndex) / totalNotes) * 0.2;
        base = int(base * build);
    }

    return qBound(35, base, 90);
}

int PianoGestures::noteDurationMs(int bpm, double beatFraction) const {
    double beatMs = 60000.0 / bpm;
    return int(beatMs * beatFraction);
}

// ============================================================================
// WATERFALL - Descending arpeggio
// ============================================================================
PianoGestures::Gesture PianoGestures::generateWaterfall(
    const Context& ctx, int startMidi, int numNotes) const {

    Gesture result;
    result.hand = "RH";
    result.type = "waterfall";

    // Get chord tones descending from start note
    int lowBound = qMax(ctx.registerLow, startMidi - 24);  // Max 2 octaves
    QVector<int> chordTones = getChordTonesDescending(ctx.chord, startMidi, lowBound);

    if (chordTones.isEmpty()) {
        return result;  // No valid notes
    }

    // Limit to requested number of notes
    int actualNotes = qMin(numNotes, chordTones.size());
    if (actualNotes < 3) {
        return result;  // Need at least 3 notes for a waterfall
    }

    // Calculate timing based on tempo
    // Waterfall should feel unhurried - about 1 beat total at slow tempo
    double beatMs = 60000.0 / ctx.bpm;
    int totalSpreadMs = int(beatMs * 0.8);  // ~80% of a beat
    totalSpreadMs = qBound(200, totalSpreadMs, 600);  // 200-600ms range

    int noteSpacingMs = totalSpreadMs / (actualNotes - 1);

    // Generate notes
    for (int i = 0; i < actualNotes; ++i) {
        GestureNote note;
        note.midiNote = chordTones[i];
        note.offsetMs = i * noteSpacingMs;
        note.durationMs = noteDurationMs(ctx.bpm, 0.75);  // Hold ~3/4 beat
        note.velocity = velocityForGesture(ctx.energy, i, actualNotes, true);

        result.notes.append(note);
    }

    result.totalDurationMs = totalSpreadMs + result.notes.last().durationMs;

    return result;
}

// ============================================================================
// MELODIC FILL - Arpeggio-based fill with dyads/triads, musical contour
// ============================================================================
PianoGestures::Gesture PianoGestures::generateScaleRun(
    const Context& ctx, int startMidi, int direction, int /*numNotes*/) const {

    Gesture result;
    result.hand = "RH";
    result.type = direction > 0 ? "melodic_fill_up" : "melodic_fill_down";

    double beatMs = 60000.0 / ctx.bpm;
    int patternHash = (ctx.chord.rootPc * 17 + startMidi * 7 + ctx.bpm) % 100;

    // ========================================================================
    // BUILD CHORD TONES in register (arpeggio basis)
    // ========================================================================
    int root = ctx.chord.rootPc;
    int third = pcForDegree(ctx.chord, 3);
    int fifth = pcForDegree(ctx.chord, 5);
    int seventh = pcForDegree(ctx.chord, 7);
    int ninth = pcForDegree(ctx.chord, 9);

    // Build available chord tones across register
    QVector<int> chordTones;
    for (int midi = startMidi - 12; midi <= startMidi + 12; ++midi) {
        int pc = midi % 12;
        if (pc == root || pc == third || pc == fifth ||
            (seventh >= 0 && pc == seventh) || (ninth >= 0 && pc == ninth)) {
            chordTones.append(midi);
        }
    }

    if (chordTones.size() < 4) {
        return result;
    }

    // Find starting position
    int startIdx = 0;
    for (int i = 0; i < chordTones.size(); ++i) {
        if (chordTones[i] >= startMidi) {
            startIdx = i;
            break;
        }
    }

    // ========================================================================
    // MELODIC PATTERNS - Arpeggio shapes with character
    // ========================================================================
    // Each pattern is: {noteIndex, dyadBelow} where dyadBelow adds harmony
    struct FillNote {
        int chordToneOffset;  // Relative to current position in chord tones
        bool addDyad;         // Add a note a 3rd/4th below
        double timingMult;    // Timing relative to base
    };

    QVector<QVector<FillNote>> patterns = {
        // Pattern 0: Simple ascending arpeggio with final dyad
        {{0, false, 1.0}, {1, false, 1.0}, {2, false, 1.2}, {3, true, 1.5}},
        // Pattern 1: Up-down turn ending on root
        {{1, false, 0.9}, {2, false, 1.0}, {1, false, 1.1}, {0, true, 1.8}},
        // Pattern 2: Leap up, step down to resolution
        {{2, false, 1.0}, {3, false, 0.8}, {2, true, 1.4}, {1, false, 1.0}, {0, true, 2.0}},
        // Pattern 3: Descending with dyads
        {{2, true, 1.2}, {1, false, 1.0}, {0, true, 1.8}},
        // Pattern 4: Wide arpeggio
        {{0, false, 1.0}, {2, false, 1.0}, {4, false, 1.2}, {2, true, 1.5}},
        // Pattern 5: Gentle turn
        {{1, false, 1.1}, {0, false, 0.9}, {1, false, 1.0}, {2, true, 1.6}},
        // Pattern 6: Rising with passing tone feel
        {{0, false, 0.8}, {1, false, 0.9}, {1, false, 1.0}, {2, false, 1.1}, {3, true, 1.5}},
        // Pattern 7: Bell-like - high note then settle
        {{3, false, 1.3}, {2, false, 1.0}, {1, true, 1.2}, {0, true, 2.0}},
    };

    int patternIdx = patternHash % patterns.size();
    if (direction < 0) {
        // Use different patterns for descending
        patternIdx = (patternIdx + 4) % patterns.size();
    }
    const auto& pattern = patterns[patternIdx];

    // ========================================================================
    // TIMING - Relaxed, melodic feel (~2-3 notes per beat)
    // ========================================================================
    double baseNoteMs = beatMs / (2.0 + (patternHash % 20) / 20.0);  // 2-3 notes per beat

    // ========================================================================
    // GENERATE NOTES
    // ========================================================================
    double currentTimeMs = 0.0;
    int currentIdx = startIdx;

    for (int i = 0; i < pattern.size(); ++i) {
        const auto& p = pattern[i];

        // Move in chord tones
        int targetIdx = currentIdx + (direction > 0 ? p.chordToneOffset : -p.chordToneOffset);
        targetIdx = qBound(0, targetIdx, chordTones.size() - 1);

        int noteMidi = chordTones[targetIdx];

        // Main note
        GestureNote note;
        note.midiNote = noteMidi;
        note.offsetMs = int(currentTimeMs);
        note.durationMs = int(baseNoteMs * p.timingMult * 0.9);

        // Velocity: gentle arc
        int baseVel = 48 + int(ctx.energy * 22);
        double velMult = (i == 0) ? 0.9 : (i == pattern.size() - 1) ? 1.05 : 1.0;
        note.velocity = qBound(40, int(baseVel * velMult), 75);

        result.notes.append(note);

        // Add dyad below if requested (3rd or 4th below)
        if (p.addDyad && targetIdx > 0) {
            int dyadIdx = targetIdx - 1;  // One chord tone below
            if (dyadIdx >= 0) {
                GestureNote dyad;
                dyad.midiNote = chordTones[dyadIdx];
                dyad.offsetMs = int(currentTimeMs) + 5;  // Slight spread
                dyad.durationMs = note.durationMs;
                dyad.velocity = note.velocity - 8;  // Softer
                result.notes.append(dyad);
            }
        }

        currentTimeMs += baseNoteMs * p.timingMult;
        currentIdx = targetIdx;
    }

    // ========================================================================
    // ENSURE RESOLUTION - Last note should be root, 3rd, or 5th
    // ========================================================================
    if (!result.notes.isEmpty()) {
        int lastPc = result.notes.last().midiNote % 12;
        bool resolved = (lastPc == root || lastPc == third || lastPc == fifth);

        if (!resolved) {
            // Find nearest resolution tone
            int lastMidi = result.notes.last().midiNote;
            for (int delta = 1; delta <= 4; ++delta) {
                int upPc = (lastMidi + delta) % 12;
                int downPc = (lastMidi - delta + 12) % 12;
                if (upPc == root || upPc == third || upPc == fifth) {
                    GestureNote resolve;
                    resolve.midiNote = lastMidi + delta;
                    resolve.offsetMs = int(currentTimeMs);
                    resolve.durationMs = int(beatMs * 0.6);
                    resolve.velocity = 55 + int(ctx.energy * 15);
                    result.notes.append(resolve);
                    currentTimeMs += beatMs * 0.4;
                    break;
                }
                if (downPc == root || downPc == third || downPc == fifth) {
                    GestureNote resolve;
                    resolve.midiNote = lastMidi - delta;
                    resolve.offsetMs = int(currentTimeMs);
                    resolve.durationMs = int(beatMs * 0.6);
                    resolve.velocity = 55 + int(ctx.energy * 15);
                    result.notes.append(resolve);
                    currentTimeMs += beatMs * 0.4;
                    break;
                }
            }
        }

        result.totalDurationMs = int(currentTimeMs) + result.notes.last().durationMs;
    }

    return result;
}

// ============================================================================
// OCTAVE BELL - High single note for sparkle (Evans-style)
// ============================================================================
// A clear, ringing note in the high register that adds color and space.
// Can optionally include octave doubling below for richer texture.
// Best used at phrase starts, during held chords, or very low energy moments.
// ============================================================================
PianoGestures::Gesture PianoGestures::generateOctaveBell(
    const Context& ctx, int targetPc) const {

    Gesture result;
    result.hand = "RH";
    result.type = "octave_bell";

    // Voice leading: prefer note close to previous top note if available
    int midiNote = -1;
    int bestDist = 999;

    // Search in the sparkle register (C5-C6 range: MIDI 72-84)
    for (int midi = 84; midi >= 72; --midi) {
        if (midi % 12 == targetPc) {
            int dist = (ctx.previousTopNote > 0) ? qAbs(midi - ctx.previousTopNote) : 0;
            if (dist < bestDist) {
                bestDist = dist;
                midiNote = midi;
            }
        }
    }

    if (midiNote < 0) {
        return result;
    }

    // Duration varies with tempo - let it ring
    int durationMs = noteDurationMs(ctx.bpm, 2.0);  // 2 beats
    int velocity = 45 + int(ctx.energy * 20);  // Soft but clear

    // Main bell note (high)
    GestureNote bellNote;
    bellNote.midiNote = midiNote;
    bellNote.offsetMs = 0;
    bellNote.durationMs = durationMs;
    bellNote.velocity = velocity;
    result.notes.append(bellNote);

    // Octave doubling below (optional, adds richness)
    // Use at slightly lower energy for more intimate sound
    int octaveBelow = midiNote - 12;
    if (octaveBelow >= 60 && ctx.energy < 0.5) {  // Only if it fits and low energy
        GestureNote lowerNote;
        lowerNote.midiNote = octaveBelow;
        lowerNote.offsetMs = 15;  // Slight delay for "rolled" effect
        lowerNote.durationMs = durationMs;
        lowerNote.velocity = velocity - 8;  // Softer than top note
        result.notes.append(lowerNote);
        result.type = "octave_bell_doubled";
    }

    result.totalDurationMs = durationMs;

    return result;
}

// ============================================================================
// GRACE APPROACH - Quick chromatic or diatonic approach
// ============================================================================
PianoGestures::Gesture PianoGestures::generateGraceApproach(
    const Context& ctx, int targetMidi, bool chromatic) const {

    Gesture result;
    result.hand = "RH";
    result.type = chromatic ? "grace_chromatic" : "grace_diatonic";

    // Grace note from below
    int graceNote = chromatic ? (targetMidi - 1) : (targetMidi - 2);

    if (graceNote < ctx.registerLow) {
        return result;
    }

    double beatMs = 60000.0 / ctx.bpm;
    int graceDurMs = int(beatMs * 0.1);  // Very short
    graceDurMs = qBound(30, graceDurMs, 80);

    // Grace note
    GestureNote grace;
    grace.midiNote = graceNote;
    grace.offsetMs = 0;
    grace.durationMs = graceDurMs;
    grace.velocity = 35 + int(ctx.energy * 20);  // Soft
    result.notes.append(grace);

    // Target note
    GestureNote target;
    target.midiNote = targetMidi;
    target.offsetMs = graceDurMs;
    target.durationMs = noteDurationMs(ctx.bpm, 0.5);
    target.velocity = 50 + int(ctx.energy * 30);
    result.notes.append(target);

    result.totalDurationMs = target.offsetMs + target.durationMs;

    return result;
}

} // namespace playback
