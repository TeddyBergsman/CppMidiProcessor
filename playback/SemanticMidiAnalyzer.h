#pragma once

#include <QVector>
#include <QSet>
#include <QtGlobal>
#include <array>

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
    struct Settings {
        // Sliding window length for density (ms)
        // LOWERED: Jazz ensemble needs quick reaction to user activity
        int densityWindowMs = 600;  // Was 1200 - now 0.6 sec for snappier response
        // Silence threshold (ms since last note-on)
        // LOWERED: Detect silence quickly so piano can fill gaps tastefully
        int silenceMs = 800;  // Was 1400 - now 0.8 sec (still enough to avoid false positives)

        // Intent thresholds
        // LOWERED: Trigger "user busy" at lower activity levels for more responsive backing
        double densityHighNotesPerSec = 2.5;  // Was 6.0 - now triggers at 2.5 notes/sec
        int registerHighCenterMidi = 72; // C5-ish
        // CC2 (breath/intensity) drives "Intensity Peak" (vocal energy), not voice note events.
        int intensityPeakCc2 = 55;      // Was 65 - more sensitive to vocal intensity
        int cc2ActivityFloor = 8;       // Was 10 - counts as "user active" (not silence)

        // Outside detection
        int outsideWindowNotes = 24;
        double outsideRatioThreshold = 0.40;
    };

    struct IntentState {
        // raw metrics
        double notesPerSec = 0.0;
        int registerCenterMidi = 60;
        int lastGuitarVelocity = 0;
        int lastCc2 = 0;
        qint64 msSinceLastGuitarNoteOn = 0;
        qint64 msSinceLastActivity = 0; // max(guitar attack, cc2 activity)
        int lastVoiceMidi = -1;         // tracked for future call/response (NOT used for density)
        qint64 msSinceLastVoiceNoteOn = 0;
        double outsideRatio = 0.0;

        // intent flags
        bool densityHigh = false;
        bool registerHigh = false;
        bool intensityPeak = false;
        bool playingOutside = false;
        bool silence = false;

        // Phrase/interaction events (derived, deterministic; no internal state required).
        // silenceOnset is true briefly when transitioning into SILENCE.
        bool silenceOnset = false;
        // questionEnded is a higher-level heuristic: user phrase ended and the band should respond.
        bool questionEnded = false;
    };

    SemanticMidiAnalyzer() = default;
    explicit SemanticMidiAnalyzer(const Settings& s) : m_s(s) {}

    void reset();

    // Guitar note attacks (used for density/register/outside)
    void ingestGuitarNoteOn(int midiNote, int velocity, qint64 timestampMs);
    void ingestGuitarNoteOff(int midiNote, qint64 timestampMs);

    // Vocal intensity (CC2 / breath) drives intensityPeak and also counts as "activity" to prevent SILENCE.
    void ingestCc2(int value, qint64 timestampMs);

    // Vocal melody tracking (for future interaction features). Not used for density/register.
    void ingestVoiceNoteOn(int midiNote, int velocity, qint64 timestampMs);
    void ingestVoiceNoteOff(int midiNote, qint64 timestampMs);

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

    int m_lastGuitarVelocity = 0;
    int m_lastCc2 = 0;
    qint64 m_lastGuitarNoteOnMs = -1;
    qint64 m_lastActivityMs = -1;
    int m_lastVoiceMidi = -1;
    qint64 m_lastVoiceNoteOnMs = -1;
    int m_registerEma = 60; // simple EMA for register center

    // Active guitar notes (dedupe repeated NOTE_ON spam while key is held).
    std::array<bool, 128> m_guitarActive{};

    // Harmonic context (not owned)
    QSet<int> m_allowedPcs;
};

} // namespace playback

