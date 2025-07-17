#ifndef MIDIPROCESSOR_H
#define MIDIPROCESSOR_H

#include <QObject>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <mutex>
#include "RtMidi.h"

// Define the struct here so it can be shared.
struct ProgramConfig {
    std::string name;
    int note;
    int program;
    int volume;
    std::map<std::string, bool> trackStates;
};

// Declare the global configuration vectors as 'extern'.
// This tells other files "these variables exist somewhere else".
extern const std::vector<ProgramConfig> programConfigs;
extern const std::map<std::string, int> trackToggleNotes;

// Inheriting from QObject is necessary to use Qt's signal and slot system.
class MidiProcessor : public QObject {
    Q_OBJECT // This macro must be included for any object that has signals or slots.

public:
    explicit MidiProcessor(QObject *parent = nullptr);
    ~MidiProcessor();

    bool initialize();
    void applyProgram(int programIndex);
    void toggleTrack(const std::string& trackId);

signals:
    // Signals are messages the MIDI thread can safely send to the GUI thread.
    void programChanged(int newProgramIndex);
    void trackStateUpdated(const std::string& trackId, bool newState);
    void logMessage(const std::string& message);

private:
    // MIDI Ports
    RtMidiIn* midiInGuitar = nullptr;
    RtMidiOut* midiOut = nullptr;
    RtMidiIn* midiInVoice = nullptr;

    // State Variables
    std::atomic<bool> inCommandMode{false};
    std::mutex stateMutex;
    std::map<std::string, bool> trackStates;
    int currentProgramIndex;
    
    // NEW: A helper function that assumes the mutex is already locked
    void toggleTrack_unlocked(const std::string& trackId);

    // Static MIDI callback functions
    static void guitarCallback(double deltatime, std::vector<unsigned char>* message, void* userData);
    static void voiceCallback(double deltatime, std::vector<unsigned char>* message, void* userData);

    void sendNoteToggle(int noteNumber);
};

#endif // MIDIPROCESSOR_H