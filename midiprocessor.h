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
#include <deque>
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
    void setVoiceControlEnabled(bool enabled);
    void setTranspose(int semitones);
    void applyTranspose(int semitones);
    // Audio-track radio-button switching (CC-27 fan-out). Replaces the active
    // switching CC number and mute map atomically; safe to call from any thread.
    void setAudioTrackSwitch(int switchCC, const QList<AudioTrackMute>& entries);
    // Read-only accessors for UI (snapshot copies).
    int audioTrackSwitchCC() const { return m_audioTrackSwitchCC.load(); }
    QList<AudioTrackMute> audioTrackMutes() const;

    // Harmony footswitch CCs (default 33 toggle, 34/35/36 root/acc/quality
    // step). Receivers connect to the signals below. All atomic / lock-free
    // for the worker thread.
    void setHarmonyCCs(int toggleCC, int rootStepCC, int accStepCC, int qualityStepCC);
    // Tell the MIDI worker what the current harmony master state is. Each
    // future CC-33 press will then flip starting from this value, keeping
    // the footswitch and the engine in lockstep.
    void setHarmonyToggleStateForFlip(bool state);
    int harmonyToggleCC() const { return m_harmonyToggleCC.load(); }
    // Public log entry point — lets non-Qt-friendly subsystems (e.g.
    // ScaleSnapProcessor) push diagnostic messages into the shared in-app
    // console without needing a signal/slot of their own.
    void pushLog(const QString& message);
    int harmonyRootStepCC() const { return m_harmonyRootStepCC.load(); }
    int harmonyAccidentalStepCC() const { return m_harmonyAccidentalStepCC.load(); }
    int harmonyQualityStepCC() const { return m_harmonyQualityStepCC.load(); }
    // Suppress guitar passthrough to channel 1 (used by ScaleSnapProcessor when Lead mode is active)
    void setSuppressGuitarPassthrough(bool suppress);
    bool suppressGuitarPassthrough() const { return m_suppressGuitarPassthrough.load(); }
    // Suppress voice pitch passthrough to channel 2 (used by VocalSync mode to free Ch 2 for pitch targets)
    void setSuppressVoicePassthrough(bool suppress);
    bool suppressVoicePassthrough() const { return m_suppressVoicePassthrough.load(); }
    // Virtual musician MIDI (thread-safe; enqueued to worker thread)
    void sendVirtualNoteOn(int channel, int note, int velocity);
    void sendVirtualNoteOff(int channel, int note);
    void sendVirtualAllNotesOff(int channel);
    // Virtual musician CC (thread-safe; enqueued to worker thread)
    void sendVirtualCC(int channel, int cc, int value);
    // Virtual musician pitch bend (thread-safe; enqueued to worker thread)
    void sendVirtualPitchBend(int channel, int bendValue);
    // VocalSync dedicated output (bypasses main output, goes to separate IAC bus)
    void sendVocalSyncNoteOn(int note, int velocity);
    void sendVocalSyncNoteOff(int note);
    void sendVocalSyncCC(int cc, int value);
    void sendVocalSyncPitchBend(int bendValue); // 14-bit (0-16383, center 8192)
    // Emergency stop for shutdown: sends explicit NOTE_OFF for all notes on all channels,
    // plus CC64/CC123/CC120. This is intended for app quit / teardown.
    void panicAllChannels();

private slots:
    void pollLogQueue();

signals:
    void programChanged(int newProgramIndex);
    void trackStateUpdated(const std::string& trackId, bool newState);
    void logMessage(const QString& message);
    // Low-latency pitch updates for UI
    void guitarPitchUpdated(int midiNote, double cents);
    void voicePitchUpdated(int midiNote, double cents);
    // Wave visualizer updates
    void guitarHzUpdated(double hz);
    void voiceHzUpdated(double hz);
    void guitarAftertouchUpdated(int value); // 0-127 channel pressure
    void voiceCc2Updated(int value);         // 0-127 breath (CC2)
    // Unthrottled CC2 stream (emitted for every incoming voice aftertouch->CC2 conversion).
    // Use this for interaction/vibe detection; UI can still use voiceCc2Updated (throttled).
    void voiceCc2Stream(int value);
    void guitarVelocityUpdated(int value);   // 0-127 note velocity

    // --- Live performance note events (for Virtuoso Listening MVP) ---
    // These reflect the *transposed* notes that are actually sent to the synth output.
    // They are NOT emitted for command/backing-track selection notes.
    void guitarNoteOn(int midiNote, int velocity);
    void guitarNoteOff(int midiNote);
    void voiceNoteOn(int midiNote, int velocity);
    void voiceNoteOff(int midiNote);

    // --- Ampero-driven harmony control ---
    // Toggle: level-based (value > 63 = on, ≤ 63 = off). Step signals are
    // emitted on rising edge only (≤63 → >63), so a momentary footswitch
    // sending 127-on-press / 0-on-release advances exactly once per press.
    void harmonyToggleRequested(bool enabled);
    void harmonyRootStepRequested();
    void harmonyAccidentalStepRequested();
    void harmonyQualityStepRequested();

private:
    enum class EventType { MIDI_MESSAGE, PROGRAM_CHANGE, TRACK_TOGGLE, TRANSPOSE_CHANGE };
    enum class MidiSource { Guitar, VoiceAmp, VoicePitch, VirtualBand, Ampero };
    struct MidiEvent {
        EventType type;
        std::vector<unsigned char> message;
        MidiSource source;
        int programIndex; // For PROGRAM_CHANGE/PLAY_TRACK this is index; for TRANSPOSE_CHANGE this is semitone amount
        std::string trackId;
    };
    bool tryEnqueueEvent(MidiEvent&& ev);
    static bool isCriticalMidiEvent(const MidiEvent& ev);

    // --- Threading & Queues ---
    std::thread m_workerThread;
    std::deque<MidiEvent> m_eventQueue; // bounded (see tryEnqueueEvent)
    std::mutex m_eventMutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_isRunning{false};

    // Backpressure: prevent unbounded growth when VirtualBand + live MIDI arrive together.
    static constexpr size_t kMaxEventQueue = 16384;
    std::atomic<quint64> m_droppedMidiEvents{0};

    // Suppress guitar passthrough: when true, guitar notes/CC are NOT passed through to channel 1.
    // ScaleSnapProcessor sets this when Lead mode is active so it can output processed notes instead.
    std::atomic<bool> m_suppressGuitarPassthrough{false};
    // Suppress voice pitch passthrough: when true, voice notes/pitch bend are NOT passed through to channel 2.
    // VocalSync mode sets this so it can output pitch targets on channel 2 instead.
    std::atomic<bool> m_suppressVoicePassthrough{false};

    QTimer* m_logPollTimer;
    std::queue<std::string> m_logQueue;
    std::mutex m_logMutex;

    // File log: every console message also goes to
    // ~/Library/Logs/CppMidiProcessor/harmony.log so the user can
    // `tail -f` it in performance mode (where the in-app console is hidden).
    QString m_logFilePath;

    // --- Worker Thread Methods ---
    void workerLoop();
    void processMidiEvent(const MidiEvent& event);
    void processProgramChange(int programIndex);
    void setTrackState(const std::string& trackId, bool newState);
    void sendNoteToggle(int note, int channel, int velocity);
    void panicSilence();
    void sendChannelAllNotesOff(int zeroBasedChannel);
    void updatePitch(const std::vector<unsigned char>& message, bool isGuitar);
    void processPitchBend();
    double noteToFrequency(int note) const;
    void precalculateRatios();
    void emitPitchIfChanged(bool isGuitar);
    void hzToNoteAndCents(double hz, int& noteOut, double& centsOut) const;
    // Defensive MIDI output: never crash due to RtMidi exceptions or null output.
    void safeSendMessage(const std::vector<unsigned char>& msg);
    // VocalSync-dedicated output: sends on a separate IAC bus to avoid flooding the AU plugin
    void safeSendVocalSync(const std::vector<unsigned char>& msg);

    // --- MIDI Ports ---
    RtMidiIn* midiInGuitar = nullptr;
    RtMidiOut* midiOut = nullptr;
    RtMidiOut* midiOutVocalSync = nullptr;  // Dedicated output for VocalSync AU plugin
    RtMidiIn* midiInVoice = nullptr;       // Voice amplitude (aftertouch) source
    RtMidiIn* midiInVoicePitch = nullptr;  // Voice accurate pitch/note source
    RtMidiIn* midiInAmpero = nullptr;      // Ampero Control USB (footswitch CCs)
    bool m_voicePitchAvailable = false;
    bool m_amperoAvailable = false;


    // --- State (Confined to Worker Thread) ---
    const Preset& m_preset;
    std::map<int, int> m_programRulesMap;
    std::map<std::string, bool> m_trackStates;
    int m_currentProgramIndex;
    bool m_inCommandMode = false;

    std::atomic<bool> m_isVerbose{false};
    std::atomic<bool> m_voiceControlEnabled{true};
    std::atomic<int> m_transposeAmount{0};

    // Live audio-track switching state. Seeded from the preset in ctor, then
    // replaced at runtime by the Audio Track Switch editor. The atomic CC
    // number lets the worker skip the mutex on every non-matching CC message.
    std::atomic<int> m_audioTrackSwitchCC{27};
    mutable std::mutex m_audioTrackMutesMutex;
    QList<AudioTrackMute> m_audioTrackMutesList;

    // Harmony footswitch CCs. Worker-thread-only "last value" trackers
    // implement rising-edge detection on the step CCs.
    std::atomic<int> m_harmonyToggleCC{33};
    std::atomic<int> m_harmonyRootStepCC{34};
    std::atomic<int> m_harmonyAccidentalStepCC{35};
    std::atomic<int> m_harmonyQualityStepCC{36};
    int m_lastHarmonyToggleValue = -1;
    bool m_harmonyToggleState = false;  // flipped per Ampero CC33 press
    int m_lastHarmonyRootStepValue = 0;
    int m_lastHarmonyAccidentalStepValue = 0;
    int m_lastHarmonyQualityStepValue = 0;

    // Pitch state
    int m_lastGuitarNote = -1;
    int m_lastVoiceNote = -1;
    double m_lastGuitarPitchHz = 0.0;
    double m_lastVoicePitchHz = 0.0;
    double m_lastEmittedGuitarHz = -1.0;
    double m_lastEmittedVoiceHz = -1.0;
    int m_lastGuitarAftertouch = -1;
    int m_lastVoiceCc2 = -1;
    int m_lastGuitarVelocity = 0;
    int m_lastEmittedGuitarNote = -2;
    double m_lastEmittedGuitarCents = 0.0;
    int m_lastEmittedVoiceNote = -2;
    double m_lastEmittedVoiceCents = 0.0;
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
    static void voiceAmpCallback(double deltatime, std::vector<unsigned char>* message, void* userData);
    static void voicePitchCallback(double deltatime, std::vector<unsigned char>* message, void* userData);
    static void amperoCallback(double deltatime, std::vector<unsigned char>* message, void* userData);
};

#endif // MIDIPROCESSOR_H