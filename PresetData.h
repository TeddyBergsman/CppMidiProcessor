#ifndef PRESETDATA_H
#define PRESETDATA_H

#include <QString>
#include <QList>
#include <QMap>
#include <string>

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
    int triggerNote;
    int programCC = -1;
    int programValue = -1;
    int volumeCC = -1;
    int volumeValue = -1;
    QMap<QString, bool> initialStates; // Defines which toggles are on for this program
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