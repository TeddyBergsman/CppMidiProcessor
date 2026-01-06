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
#include <QMediaPlayer>
#include <QStringList>
#include "RtMidi.h"
#include "PresetData.h"

class QAudioOutput;

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
    void playTrack(int index);
    void pauseTrack();
    void seekToPosition(qint64 positionMs);
    void setVoiceControlEnabled(bool enabled);
    void setTranspose(int semitones);
    void applyTranspose(int semitones);
    void loadTrackTimeline(int index);
    // Virtual musician MIDI (thread-safe; enqueued to worker thread)
    void sendVirtualNoteOn(int channel, int note, int velocity);
    void sendVirtualNoteOff(int channel, int note);
    void sendVirtualAllNotesOff(int channel);
    // Virtual musician CC (thread-safe; enqueued to worker thread)
    void sendVirtualCC(int channel, int cc, int value);
    // Emergency stop for shutdown: sends explicit NOTE_OFF for all notes on all channels,
    // plus CC64/CC123/CC120. This is intended for app quit / teardown.
    void panicAllChannels();

private slots:
    void pollLogQueue();
    void onPlayerStateChanged(QMediaPlayer::PlaybackState state);
    // NEW: Thread-safe slots to control the player
    void onInternalPlay(const QUrl& url);
    void onInternalPause();
    void onInternalResume();
    void onPlayerPositionChanged(qint64 position);
    void onPlayerDurationChanged(qint64 duration);


signals:
    void programChanged(int newProgramIndex);
    void trackStateUpdated(const std::string& trackId, bool newState);
    void logMessage(const QString& message);
    void backingTracksLoaded(const QStringList& trackList);
    void backingTrackStateChanged(int trackIndex, QMediaPlayer::PlaybackState state);
    // NEW: Internal signals for cross-thread communication
    void _internal_playTrack(const QUrl& url);
    void _internal_pauseTrack();
    void _internal_resumeTrack();
    void backingTrackPositionChanged(qint64 position);
    void backingTrackDurationChanged(qint64 duration);
    void backingTrackTimelineUpdated(const QString& timelineJson);
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

private:
    enum class EventType { MIDI_MESSAGE, PROGRAM_CHANGE, TRACK_TOGGLE, PLAY_TRACK, PAUSE_TRACK, TRANSPOSE_CHANGE };
    enum class MidiSource { Guitar, VoiceAmp, VoicePitch, VirtualBand };
    struct MidiEvent {
        EventType type;
        std::vector<unsigned char> message;
        MidiSource source;
        int programIndex; // For PROGRAM_CHANGE/PLAY_TRACK this is index; for TRANSPOSE_CHANGE this is semitone amount
        std::string trackId;
    };
    void handleBackingTrackSelection(int note);
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
    
    QTimer* m_logPollTimer;
    std::queue<std::string> m_logQueue;
    std::mutex m_logMutex;

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
    void loadBackingTracks();
    TrackMetadata loadTrackMetadata(const QString& trackPath);
    void emitPitchIfChanged(bool isGuitar);
    void hzToNoteAndCents(double hz, int& noteOut, double& centsOut) const;
    // Defensive MIDI output: never crash due to RtMidi exceptions or null output.
    void safeSendMessage(const std::vector<unsigned char>& msg);

    // --- MIDI Ports ---
    RtMidiIn* midiInGuitar = nullptr;
    RtMidiOut* midiOut = nullptr;
    RtMidiIn* midiInVoice = nullptr;       // Voice amplitude (aftertouch) source
    RtMidiIn* midiInVoicePitch = nullptr;  // Voice accurate pitch/note source
    bool m_voicePitchAvailable = false;

    // --- Audio Player ---
    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
    QStringList m_backingTracks;
    int m_currentlyPlayingTrackIndex = -1;
    bool m_backingTrackSelectionMode = false;


    // --- State (Confined to Worker Thread) ---
    const Preset& m_preset;
    std::map<int, int> m_programRulesMap;
    std::map<std::string, bool> m_trackStates;
    int m_currentProgramIndex;
    bool m_inCommandMode = false;

    std::atomic<bool> m_isVerbose{false};
    std::atomic<bool> m_voiceControlEnabled{true};
    std::atomic<int> m_transposeAmount{0};

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
};

#endif // MIDIPROCESSOR_H