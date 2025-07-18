#ifndef MIDIPROCESSOR_H
#define MIDIPROCESSOR_H

#include <QObject>
#include <QTimer>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include "RtMidi.h"
#include "PresetData.h"

class MidiProcessor : public QObject {
    Q_OBJECT

public:
    explicit MidiProcessor(const Preset& preset, QObject *parent = nullptr);
    ~MidiProcessor();

    bool initialize();

public slots:
    void applyProgram(int programIndex);
    void toggleTrack(const std::string& trackId);
    void setVerbose(bool verbose);

private slots:
    void pollLogQueue();

signals:
    void programChanged(int newProgramIndex);
    void trackStateUpdated(const std::string& trackId, bool newState);
    void logMessage(const QString& message);

private:
    enum class EventType { MIDI_MESSAGE, PROGRAM_CHANGE, TRACK_TOGGLE };
    struct MidiEvent {
        EventType type;
        std::vector<unsigned char> message;
        bool isGuitar;
        int programIndex;
        std::string trackId;
    };

    // --- Threading & Queues ---
    std::thread m_workerThread;
    std::queue<MidiEvent> m_eventQueue;
    std::mutex m_eventMutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_isRunning{false};
    
    QTimer* m_logPollTimer;
    std::queue<std::string> m_logQueue;
    std::mutex m_logMutex;

    // --- Worker Thread Methods ---
    void workerLoop();
    void processMidiEvent(const MidiEvent& event);
    void processProgramChange(int programIndex);
    void setTrackState(const std::string& trackId, bool newState);
    void sendNoteToggle(int note, int channel, int velocity);
    void updatePitch(const std::vector<unsigned char>& message, bool isGuitar);
    void processPitchBend();
    double noteToFrequency(int note) const;
    void precalculateRatios();

    // --- MIDI Ports ---
    RtMidiIn* midiInGuitar = nullptr;
    RtMidiOut* midiOut = nullptr;
    RtMidiIn* midiInVoice = nullptr;

    // --- State (Confined to Worker Thread) ---
    const Preset& m_preset;
    std::map<int, int> m_programRulesMap;
    std::map<std::string, bool> m_trackStates;
    int m_currentProgramIndex;
    bool m_inCommandMode = false;
    std::atomic<bool> m_isVerbose{false};

    // Pitch state
    int m_lastGuitarNote = -1;
    int m_lastVoiceNote = -1;
    double m_lastGuitarPitchHz = 0.0;
    double m_lastVoicePitchHz = 0.0;
    const int BEND_DOWN_CC = 102;
    const int BEND_UP_CC = 103;

    // Pre-calculated ratios for performance
    double m_ratioUpDeadZone;
    double m_ratioDownDeadZone;

    // GUARANTEED FIX: State for value throttling
    int m_lastCC102Value = -1;
    int m_lastCC103Value = -1;

    // --- Static Callbacks (Producers) ---
    static void guitarCallback(double deltatime, std::vector<unsigned char>* message, void* userData);
    static void voiceCallback(double deltatime, std::vector<unsigned char>* message, void* userData);
};

#endif // MIDIPROCESSOR_H