#pragma once

#include <QVector>
#include <QSet>
#include <QtGlobal>

namespace music { struct ChordSymbol; }

namespace playback {

// Module 4.1 (Listening MVP): deterministic semantic interpretation of incoming user MIDI.
// This is intentionally small but high-leverage: it converts raw note-ons into intent flags.
//
// Determinism contract:
// - Given the same input event stream (note, velocity, timestampMs) + chord context, outputs are deterministic.
// - No RNG is used.
class SemanticMidiAnalyzer {
public:
    enum class Source { Guitar, Voice };

    struct Settings {
        // Sliding window length for density (ms)
        int densityWindowMs = 1200;
        // Silence threshold (ms since last note-on)
        int silenceMs = 1400;

        // Intent thresholds
        double densityHighNotesPerSec = 6.0;
        int registerHighCenterMidi = 72; // C5-ish
        int intensityPeakVelocity = 105;
        double intensityPeakNotesPerSec = 7.5;

        // Outside detection
        int outsideWindowNotes = 24;
        double outsideRatioThreshold = 0.40;
    };

    struct IntentState {
        // raw metrics
        double notesPerSec = 0.0;
        int registerCenterMidi = 60;
        int lastVelocity = 0;
        qint64 msSinceLastNoteOn = 0;
        double outsideRatio = 0.0;

        // intent flags
        bool densityHigh = false;
        bool registerHigh = false;
        bool intensityPeak = false;
        bool playingOutside = false;
        bool silence = false;
    };

    SemanticMidiAnalyzer() = default;
    explicit SemanticMidiAnalyzer(const Settings& s) : m_s(s) {}

    void reset();

    void ingestNoteOn(Source src, int midiNote, int velocity, qint64 timestampMs);
    void ingestNoteOff(Source src, int midiNote, qint64 timestampMs);

    // Provide current harmonic context for "playing outside" classification.
    // For MVP, allowed pitch classes are derived from chord degrees (incl. alterations).
    void setChordContext(const music::ChordSymbol& chord);
    IntentState compute(qint64 nowMs) const;

private:
    static int clampMidi(int m) { return (m < 0) ? 0 : (m > 127 ? 127 : m); }
    static QSet<int> allowedPitchClassesForChord(const music::ChordSymbol& c);

    Settings m_s;

    // Recent note-ons (for density)
    QVector<qint64> m_noteOnTimesMs;
    // Recent pitch classes (for outside ratio)
    QVector<int> m_recentPitchClasses;

    int m_lastVelocity = 0;
    qint64 m_lastNoteOnMs = -1;
    int m_registerEma = 60; // simple EMA for register center

    // Harmonic context (not owned)
    QSet<int> m_allowedPcs;
};

} // namespace playback

