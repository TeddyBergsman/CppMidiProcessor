#ifndef PRESETDATA_H
#define PRESETDATA_H

#include <QString>
#include <QList>
#include <QMap>

// Represents a toggleable track with its full MIDI note definition
struct Toggle {
    QString id;
    QString name;
    int note;
    int channel;
    int velocity;
};

// Represents one audio track in the radio-button switching map.
// When the switching CC arrives with `switchValue`, this track's mute CC is
// sent with value 127 (unmute); for any other switching-CC value the track's
// mute CC is sent with value 0 (mute). This polarity matches the observed
// behavior of Logic's learned Controller Assignments on the mute button.
struct AudioTrackMute {
    int switchValue; // Matching value of the switching CC (e.g. CC 27 value)
    int muteCC;      // CC number mapped to this track's mute in Logic (Cmd+K)
    QString name;    // Informational only (used in logs)
};

// Maps a single value of a "direct chord" Ampero CC to a specific chord. The
// chord text is parsed by music::parseChordSymbol, so anything that parser
// understands works ("A", "Bb", "C#m7", "Gbsus4"). The user-facing default
// preset just lists the 12 major chords, but the format is general.
struct HarmonyDirectChord {
    int     value; // CC value (0..127) the footswitch sends
    QString chord; // Chord text (e.g. "A", "Bb", "C#m7")
    QString name;  // Optional human label for logs
};

// Represents a single program, with explicit CCs for program/volume
struct Program {
    QString name;
    QString quickSwitch;  // Name of program to switch to when "quick switch" command is received
    int triggerNote;
    int programCC = -1;
    int programValue = -1;
    int volumeCC = -1;
    int volumeValue = -1;
    QMap<QString, bool> initialStates; // Defines which toggles are on for this program
    QStringList tags; // Voice command aliases for this program
};

// Represents the global settings for the application
struct Settings {
    QMap<QString, QString> ports; // e.g., {"GUITAR_IN": "IAC Driver..."}
    int commandNote = -1;
    QMap<QString, bool> defaultTrackStates; // NEW: Holds the default state for each toggle

    // NEW: Pitch bend settings
    int pitchBendDeadZoneCents = 50;   // +/- cents before pitch bend CCs are generated
    int pitchBendDownRangeCents = 200; // Cents below deadzone for CC102 to reach 127
    int pitchBendUpRangeCents = 200;   // Cents above deadzone for CC103 to reach 127

    // Voice control settings
    bool voiceControlEnabled = true;   // Enable/disable voice control
    double voiceConfidenceThreshold = 0.8; // Minimum confidence for voice commands
    QString rtSttSocketPath = "/tmp/rt-stt.sock"; // RT-STT daemon socket path

    // Audio-track radio-button switching (Ampero CC 27 → per-track mute CCs in Logic).
    // When a CC with number == audioTrackSwitchCC arrives on channel 1, MidiProcessor
    // fans out a mute CC for every entry in audioTrackMutes: value 0 (unmute) if the
    // track's switchValue matches, else value 127 (mute).
    int audioTrackSwitchCC = 27;
    QList<AudioTrackMute> audioTrackMutes;

    // Direct-chord lookup: when a CC with number == harmonyDirectChordCC arrives,
    // its value is matched against harmonyDirectChordMap and the chord text from
    // the matched entry is applied as the harmony chord. Set CC to -1 (default) to
    // disable. Designed for an Ampero footswitch sending a unique value per chord
    // (e.g. value 0..11 → A, Bb, B, ..., G#) so the player can jump directly to
    // any chord without root/acc/quality stepping.
    int harmonyDirectChordCC = -1;
    QList<HarmonyDirectChord> harmonyDirectChordMap;
};

// Top-level container for the entire preset file
struct Preset {
    QString name;
    Settings settings;
    QList<Toggle> toggles;
    QList<Program> programs;
    bool isValid = false; // Flag to check if loading was successful
};

#endif // PRESETDATA_H