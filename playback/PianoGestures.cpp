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
// SCALE RUN - Ascending or descending scalar passage
// ============================================================================
PianoGestures::Gesture PianoGestures::generateScaleRun(
    const Context& ctx, int startMidi, int direction, int numNotes) const {

    Gesture result;
    result.hand = "RH";
    result.type = direction > 0 ? "scale_run_up" : "scale_run_down";

    QVector<int> scalePcs = getChordScale(ctx.chord, ctx.keyTonicPc, ctx.keyMode);
    if (scalePcs.isEmpty()) {
        return result;
    }

    // Build scale notes in register
    QVector<int> scaleNotes;
    int low = direction > 0 ? startMidi : startMidi - 12;
    int high = direction > 0 ? startMidi + 12 : startMidi;

    for (int midi = low; midi <= high; ++midi) {
        int pc = midi % 12;
        if (scalePcs.contains(pc)) {
            scaleNotes.append(midi);
        }
    }

    if (scaleNotes.size() < numNotes) {
        return result;  // Not enough notes
    }

    // Find starting position
    int startIdx = 0;
    for (int i = 0; i < scaleNotes.size(); ++i) {
        if (scaleNotes[i] >= startMidi) {
            startIdx = i;
            break;
        }
    }

    // Calculate timing - scale runs are quick
    double beatMs = 60000.0 / ctx.bpm;
    int noteSpacingMs = int(beatMs * 0.15);  // 16th-ish notes
    noteSpacingMs = qBound(60, noteSpacingMs, 120);

    // Generate notes
    for (int i = 0; i < numNotes; ++i) {
        int idx = startIdx + (direction > 0 ? i : -i);
        if (idx < 0 || idx >= scaleNotes.size()) break;

        GestureNote note;
        note.midiNote = scaleNotes[idx];
        note.offsetMs = i * noteSpacingMs;
        note.durationMs = noteDurationMs(ctx.bpm, 0.25);
        note.velocity = velocityForGesture(ctx.energy, i, numNotes, direction < 0);

        result.notes.append(note);
    }

    if (!result.notes.isEmpty()) {
        result.totalDurationMs = result.notes.last().offsetMs + result.notes.last().durationMs;
    }

    return result;
}

// ============================================================================
// OCTAVE BELL - High single note for sparkle
// ============================================================================
PianoGestures::Gesture PianoGestures::generateOctaveBell(
    const Context& ctx, int targetPc) const {

    Gesture result;
    result.hand = "RH";
    result.type = "octave_bell";

    // Find the target pitch class in the high register (C5-C6 range)
    int midiNote = -1;
    for (int midi = 84; midi >= 72; --midi) {  // C6 down to C5
        if (midi % 12 == targetPc) {
            midiNote = midi;
            break;
        }
    }

    if (midiNote < 0) {
        return result;
    }

    GestureNote note;
    note.midiNote = midiNote;
    note.offsetMs = 0;
    note.durationMs = noteDurationMs(ctx.bpm, 2.0);  // Let it ring (2 beats)
    note.velocity = 40 + int(ctx.energy * 25);  // Soft but audible

    result.notes.append(note);
    result.totalDurationMs = note.durationMs;

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
