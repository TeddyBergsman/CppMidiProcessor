#pragma once

#include <QString>
#include <QVector>

class QSettings;

namespace music {

// High-level feel selector (in addition to detailed knobs below).
enum class PianoFeelStyle {
    Ballad = 0,
    Swing = 1,
};

// Per-song “human musician” configuration for the jazz piano generator.
// Versioned and persisted via QSettings.
struct PianoProfile {
    int version = 3;
    QString name; // optional label, e.g. "Bill Evans (Default)"

    // High-level feel / density mode.
    PianoFeelStyle feelStyle = PianoFeelStyle::Swing;
    bool enabled = true;

    // MIDI routing
    int midiChannel = 4; // 1..16 (reserved: 4 = piano)

    // Register / hand ranges (MIDI notes)
    int lhMinMidiNote = 36; // C2 (warmer range)
    int lhMaxMidiNote = 72; // C5
    int rhMinMidiNote = 60; // C4
    int rhMaxMidiNote = 100; // E7

    // Timing / humanization
    int microJitterMs = 4;   // +/- per event
    int laidBackMs = 8;      // constant behind-the-beat
    int pushMs = 0;          // constant ahead-of-the-beat
    int driftMaxMs = 14;     // slow timing drift max (+/-) across bars
    double driftRate = 0.18; // 0..1 random-walk rate per bar
    quint32 humanizeSeed = 1;

    // Dynamics
    int baseVelocity = 62;       // 1..127
    int velocityVariance = 14;   // +/- per event
    double accentDownbeat = 1.08; // beat 1 emphasis
    double accentBackbeat = 0.95; // beats 2/4 (swing comp)

    // Comping rhythm
    double compDensity = 0.55;       // 0..1 how often to comp on a beat
    double anticipationProb = 0.14;  // 0..1 anticipate next beat (upbeat)
    double syncopationProb = 0.18;   // 0..1 offbeat comp placement
    double restProb = 0.12;          // 0..1 skip a weak-beat comp

    // Voicing language
    bool preferRootless = true;      // default Bill Evans-ish: rootless shells + extensions
    double rootlessProb = 0.80;      // 0..1
    double drop2Prob = 0.35;         // 0..1
    double quartalProb = 0.18;       // 0..1
    double clusterProb = 0.10;       // 0..1 (tight 2nds)
    double tensionProb = 0.75;       // 0..1 include 9/11/13 when idiomatic
    double avoidRootProb = 0.65;     // 0..1 avoid playing chord root if bass implies it
    double avoidThirdProb = 0.10;    // 0..1 occasional sus/ambiguous color

    // Voice-leading / motion
    int maxHandLeap = 10;            // semitones; larger movement penalized
    double voiceLeadingStrength = 0.75; // 0..1 prioritize minimal motion
    double repetitionPenalty = 0.45;    // 0..1 avoid exact voicing repeats

    // RH fills (short melodic fragments)
    double fillProbPhraseEnd = 0.22; // 0..1
    double fillProbAnyBeat = 0.06;   // 0..1
    int phraseLengthBars = 4;        // typical jazz phrasing (used for phrase-end fills)
    int fillMaxNotes = 4;            // per beat window
    int fillMinMidiNote = 64;        // E4
    int fillMaxMidiNote = 108;       // C8

    // Sustain pedal (CC64)
    bool pedalEnabled = true;
    bool pedalReleaseOnChordChange = true;
    int pedalDownValue = 127;
    int pedalUpValue = 0;
    int pedalMinHoldMs = 180;
    int pedalMaxHoldMs = 620;
    double pedalChangeProb = 0.80;   // 0..1 probability to refresh pedal on harmony change

    // Explainability / UI
    bool reasoningLogEnabled = false;
};

PianoProfile defaultPianoProfile();

// Persist/load profile under a prefix like "<overrideGroup>/pianoProfile".
PianoProfile loadPianoProfile(QSettings& settings, const QString& prefix);
void savePianoProfile(QSettings& settings, const QString& prefix, const PianoProfile& p);

} // namespace music

