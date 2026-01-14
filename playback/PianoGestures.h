#pragma once

#include <QVector>
#include <QString>
#include "music/ChordSymbol.h"

namespace playback {

/**
 * PianoGestures - Library of pre-composed pianistic figures
 *
 * Bill Evans-style gestures for expressive moments:
 * - Waterfalls: Descending arpeggios at phrase endings
 * - Scale runs: Ascending/descending scalar passages
 * - Octave bells: High single notes for color/sparkle
 * - Grace approaches: Chromatic/diatonic approach to target
 *
 * Each gesture returns a sequence of notes with timing and velocity.
 * The orchestrator decides WHEN to trigger gestures based on context.
 */
class PianoGestures {
public:
    PianoGestures() = default;

    // A single note in a gesture
    struct GestureNote {
        int midiNote;
        int offsetMs;       // Timing offset from gesture start (can be negative)
        int durationMs;     // Note duration
        int velocity;       // Absolute velocity (not delta)
    };

    // Complete gesture result
    struct Gesture {
        QVector<GestureNote> notes;
        int totalDurationMs;    // How long the gesture takes
        QString hand;           // "LH", "RH", or "Both"
        QString type;           // For logging/debugging
    };

    // Context for gesture generation
    struct Context {
        music::ChordSymbol chord;
        int keyTonicPc = 0;
        int keyMode = 0;        // 0 = major, 1 = minor
        double energy = 0.5;
        int bpm = 90;
        int registerLow = 48;   // Suggested register bounds
        int registerHigh = 72;
        int previousTopNote = -1;  // For voice-leading
    };

    // ========================================================================
    // WATERFALL - Descending arpeggio at phrase endings
    // ========================================================================
    // Bill Evans' signature fill: a cascading descent through chord tones
    // from a high note down to the mid-register. Creates a "release" feeling.
    //
    // Best used at:
    // - Phrase endings when user is silent
    // - Low-mid energy (expressive, not climactic)
    // - After sustained chords (creates movement)
    //
    // Parameters:
    // - startMidi: Highest note of the waterfall
    // - numNotes: How many notes in the cascade (3-6 typical)
    // - spreadMs: Total time for the cascade
    // ========================================================================
    Gesture generateWaterfall(const Context& ctx, int startMidi, int numNotes = 4) const;

    // ========================================================================
    // SCALE RUN - Ascending or descending scalar passage
    // ========================================================================
    // Short scale fragments that create forward motion.
    // Uses chord scale (mixolydian for dom7, dorian for min7, etc.)
    //
    // Best used at:
    // - Approaching cadences
    // - Building tension
    // - Mid energy
    // ========================================================================
    Gesture generateScaleRun(const Context& ctx, int startMidi, int direction, int numNotes = 4) const;

    // ========================================================================
    // OCTAVE BELL - High single note for sparkle
    // ========================================================================
    // A single high note that rings out, adding color and space.
    // Evans often used this at phrase starts or during sustained moments.
    //
    // Best used at:
    // - Phrase starts
    // - Very low energy
    // - When user is holding a note
    // ========================================================================
    Gesture generateOctaveBell(const Context& ctx, int targetPc) const;

    // ========================================================================
    // GRACE APPROACH - Quick chromatic or diatonic approach to target
    // ========================================================================
    // 1-2 quick notes that "lean into" a target note.
    // Creates expressiveness without being ornate.
    // ========================================================================
    Gesture generateGraceApproach(const Context& ctx, int targetMidi, bool chromatic = true) const;

private:
    // Get chord tones in the given register, sorted descending
    QVector<int> getChordTonesDescending(const music::ChordSymbol& chord, int highMidi, int lowMidi) const;

    // Get scale notes for the chord
    QVector<int> getChordScale(const music::ChordSymbol& chord, int keyTonicPc, int keyMode) const;

    // Calculate velocity based on energy and position in gesture
    int velocityForGesture(double energy, int noteIndex, int totalNotes, bool isDescending) const;

    // Calculate note duration based on tempo and gesture type
    int noteDurationMs(int bpm, double beatFraction) const;
};

} // namespace playback
