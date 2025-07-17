#ifndef MIDIPROCESSOR_H
#define MIDIPROCESSOR_H

#include <QObject>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <mutex>
#include "RtMidi.h"
#include "PresetData.h"

class MidiProcessor : public QObject {
    Q_OBJECT

public:
    explicit MidiProcessor(const Preset& preset, QObject *parent = nullptr);
    ~MidiProcessor();

    bool initialize();
    void applyProgram(int programIndex);
    void toggleTrack(const std::string& trackId);

signals:
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
    
    // Loaded Configuration
    const Preset& m_preset;
    std::map<int, int> m_programRulesMap;

    void toggleTrack_unlocked(const std::string& trackId);

    // Static MIDI callback functions
    static void guitarCallback(double deltatime, std::vector<unsigned char>* message, void* userData);
    static void voiceCallback(double deltatime, std::vector<unsigned char>* message, void* userData);

    void sendNoteToggle(int note, int channel, int velocity);
};

#endif // MIDIPROCESSOR_H