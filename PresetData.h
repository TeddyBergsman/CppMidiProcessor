#ifndef PRESETDATA_H
#define PRESETDATA_H

#include <QString>
#include <QList>
#include <QMap>

// Timeline event structures
struct BarMarker {
    double bar;      // Bar number (1-based, supports fractional bars like 5.5)
    qint64 timeMs;   // Timestamp in milliseconds
};

struct SectionMarker {
    QString label;   // Section label (e.g., "A", "B", "Intro")
    qint64 timeMs;   // Timestamp in milliseconds
    double bar;      // Optional bar number
};

struct ChordEvent {
    double bar;      // Bar where chord changes
    QString chord;   // Chord name (e.g., "Emaj7", "Bm7")
};

struct ProgramChangeEvent {
    double bar;      // Bar where program changes
    QString programName; // Program name to switch to
};

struct TransposeToggleEvent {
    double bar;      // Bar where transpose toggles
    bool on;         // true = transpose on, false = transpose off
};

struct LyricLine {
    double startBar; // Bar where line starts
    double endBar;   // Bar where line ends
    QString text;    // Lyric text
    // Optional word-level timing
    struct Word {
        QString text;
        double startFraction; // 0.0-1.0 fraction of line duration
        double endFraction;
    };
    QList<Word> words;
};

struct ScaleChangeEvent {
    double bar;
    QString scale;   // e.g., "C harmonic minor"
};

struct KeyChangeEvent {
    double bar;
    QString key;     // e.g., "C minor"
};

struct TempoChangeEvent {
    double bar;
    int bpm;
};

struct TimeSignatureChangeEvent {
    double bar;
    int numerator;
    int denominator;
};

// Represents metadata for a backing track
struct TrackMetadata {
    double volume = 0.5;        // Volume level (0.0 to 1.0)
    int tempo = 120;            // BPM (deprecated - use tempoChanges)
    QString key = "C";          // Musical key (deprecated - use keyChanges)
    int program = 1;            // Program to activate when track starts (1-based)
    int barWindowSize = 4;      // Number of bars to show in the 4-bar window (configurable)
    
    // Timeline data
    QList<BarMarker> barMarkers;
    QList<SectionMarker> sections;
    QList<ChordEvent> chordEvents;
    QList<ProgramChangeEvent> programChanges;
    QList<TransposeToggleEvent> transposeToggles;
    QList<LyricLine> lyricLines;
    QList<ScaleChangeEvent> scaleChanges;
    QList<KeyChangeEvent> keyChanges;
    QList<TempoChangeEvent> tempoChanges;
    QList<TimeSignatureChangeEvent> timeSignatureChanges;
};

// Represents a toggleable track with its full MIDI note definition
struct Toggle {
    QString id;
    QString name;
    int note;
    int channel;
    int velocity;
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
    int backingTrackCommandNote = -1;
    QString backingTrackDirectory;
    QMap<QString, bool> defaultTrackStates; // NEW: Holds the default state for each toggle

    // NEW: Pitch bend settings
    int pitchBendDeadZoneCents = 50;   // +/- cents before pitch bend CCs are generated
    int pitchBendDownRangeCents = 200; // Cents below deadzone for CC102 to reach 127
    int pitchBendUpRangeCents = 200;   // Cents above deadzone for CC103 to reach 127

    // Voice control settings
    bool voiceControlEnabled = true;   // Enable/disable voice control
    double voiceConfidenceThreshold = 0.8; // Minimum confidence for voice commands
    QString rtSttSocketPath = "/tmp/rt-stt.sock"; // RT-STT daemon socket path
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