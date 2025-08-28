#include "midiprocessor.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <QStringBuilder>
#include <QDir>
#include <QFileInfo>
#include <QAudioOutput>
#include <QUrl>

MidiProcessor::MidiProcessor(const Preset& preset, QObject *parent) 
    : QObject(parent), m_preset(preset), m_currentProgramIndex(-1) {

    for (int i = 0; i < m_preset.programs.size(); ++i) {
        m_programRulesMap[m_preset.programs[i].triggerNote] = i;
    }
    for (const auto& toggle : m_preset.toggles) {
        m_trackStates[toggle.id.toStdString()] = true;
    }

    m_logPollTimer = new QTimer(this);
    connect(m_logPollTimer, &QTimer::timeout, this, &MidiProcessor::pollLogQueue);
    m_logPollTimer->start(33);
}

MidiProcessor::~MidiProcessor() {
    m_isRunning = false;
    m_condition.notify_one();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    
    delete midiInGuitar;
    delete midiOut;
    delete midiInVoice;
    delete m_player;
    delete m_audioOutput;
}

bool MidiProcessor::initialize() {
    midiInGuitar = new RtMidiIn();
    midiOut = new RtMidiOut();
    midiInVoice = new RtMidiIn();
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);

    auto find_port = [](RtMidi& midi, const std::string& name) {
        for (unsigned int i = 0; i < midi.getPortCount(); i++) {
            if (midi.getPortName(i).find(name) != std::string::npos) return (int)i;
        }
        return -1;
    };

    int guitarPort = find_port(*midiInGuitar, m_preset.settings.ports["GUITAR_IN"].toStdString());
    int voicePort = find_port(*midiInVoice, m_preset.settings.ports["VOICE_IN"].toStdString());
    int outPort = find_port(*midiOut, m_preset.settings.ports["CONTROLLER_OUT"].toStdString());

    if (guitarPort == -1 || outPort == -1 || voicePort == -1) {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logQueue.push("ERROR: Could not find all MIDI ports. Check names in preset.xml.");
        return false;
    }

    midiInGuitar->openPort(guitarPort);
    midiOut->openPort(outPort);
    midiInVoice->openPort(voicePort);

    midiInGuitar->setCallback(&MidiProcessor::guitarCallback, this);
    midiInVoice->setCallback(&MidiProcessor::voiceCallback, this);
    
    // Connect player state changes to the main window
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &MidiProcessor::onPlayerStateChanged);

    // *** THE FIX: Connect internal signals to slots using a QueuedConnection ***
    connect(this, &MidiProcessor::_internal_playTrack, this, &MidiProcessor::onInternalPlay, Qt::QueuedConnection);
    connect(this, &MidiProcessor::_internal_pauseTrack, this, &MidiProcessor::onInternalPause, Qt::QueuedConnection);
    connect(this, &MidiProcessor::_internal_resumeTrack, this, &MidiProcessor::onInternalResume, Qt::QueuedConnection);


    midiInGuitar->ignoreTypes(false, true, true);
    midiInVoice->ignoreTypes(false, true, true);

    precalculateRatios();
    m_isRunning = true;

    loadBackingTracks();

    m_workerThread = std::thread(&MidiProcessor::workerLoop, this);

    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logQueue.push("SUCCESS: MIDI ports opened and worker thread started.");
    }
    
    applyProgram(0);

    return true;
}

void MidiProcessor::applyProgram(int programIndex) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_eventQueue.push({EventType::PROGRAM_CHANGE, {}, false, programIndex, ""});
    m_condition.notify_one();
}

void MidiProcessor::toggleTrack(const std::string& trackId) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_eventQueue.push({EventType::TRACK_TOGGLE, {}, false, -1, trackId});
    m_condition.notify_one();
}
 
void MidiProcessor::playTrack(int index) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_eventQueue.push({EventType::PLAY_TRACK, {}, false, index, ""});
    m_condition.notify_one();
}

void MidiProcessor::pauseTrack() {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_eventQueue.push({EventType::PAUSE_TRACK, {}, false, -1, ""});
    m_condition.notify_one();
}

void MidiProcessor::setVerbose(bool verbose) {
    m_isVerbose.store(verbose);
}

void MidiProcessor::pollLogQueue() {
    QString allMessages;
    std::lock_guard<std::mutex> lock(m_logMutex);
    if (m_logQueue.empty()) return;
    
    while(!m_logQueue.empty()) {
        allMessages.append(QString::fromStdString(m_logQueue.front())).append('\n');
        m_logQueue.pop();
    }
    emit logMessage(allMessages.trimmed());
}

void MidiProcessor::onPlayerStateChanged(QMediaPlayer::PlaybackState state) {
    emit backingTrackStateChanged(m_currentlyPlayingTrackIndex, state);
}

// These slots are now executed safely on the main thread
void MidiProcessor::onInternalPlay(const QUrl& url) {
    m_player->stop();
    m_player->setSource(url);
    m_player->play();
}

void MidiProcessor::onInternalPause() {
    m_player->pause();
}

void MidiProcessor::onInternalResume() {
    m_player->play();
}

void MidiProcessor::guitarCallback(double deltatime, std::vector<unsigned char>* message, void* userData) {
    MidiProcessor* self = static_cast<MidiProcessor*>(userData);
    if (!self->m_isRunning) return;
    {
        std::lock_guard<std::mutex> lock(self->m_eventMutex);
        self->m_eventQueue.push({EventType::MIDI_MESSAGE, *message, true, -1, ""});
    }
    self->m_condition.notify_one();
}

void MidiProcessor::voiceCallback(double deltatime, std::vector<unsigned char>* message, void* userData) {
    MidiProcessor* self = static_cast<MidiProcessor*>(userData);
    if (!self->m_isRunning) return;
    {
        std::lock_guard<std::mutex> lock(self->m_eventMutex);
        self->m_eventQueue.push({EventType::MIDI_MESSAGE, *message, false, -1, ""});
    }
    self->m_condition.notify_one();
}

void MidiProcessor::workerLoop() {
    while (m_isRunning) {
        MidiEvent event;
        {
            std::unique_lock<std::mutex> lock(m_eventMutex);
            m_condition.wait(lock, [this] { return !m_eventQueue.empty() || !m_isRunning; });
            
            if (!m_isRunning && m_eventQueue.empty()) break;
            
            event = m_eventQueue.front();
            m_eventQueue.pop();
        }
        
        processMidiEvent(event);
    }
}

void MidiProcessor::processMidiEvent(const MidiEvent& event) {
    switch(event.type) {
        case EventType::MIDI_MESSAGE:
            {
                const auto& message = event.message;
                unsigned char status = message[0] & 0xF0;

                if (event.isGuitar) {
                    int note = message.size() > 1 ? message[1] : 0;
                    int velocity = message.size() > 2 ? message[2] : 0;
                    if (status == 0x90 && velocity > 0) {
                        if (note == m_preset.settings.commandNote) { m_inCommandMode = true; return; }
                        else if (m_inCommandMode) {
                            if (m_programRulesMap.count(note)) processProgramChange(m_programRulesMap.at(note));
                            m_inCommandMode = false;
                            return;
                        } else if (note == m_preset.settings.backingTrackCommandNote) { 
                            m_backingTrackSelectionMode = true; return;
                        } else if (m_backingTrackSelectionMode) {
                            handleBackingTrackSelection(note);
                            m_backingTrackSelectionMode = false;
                            return;
                        }
                    }
                    std::vector<unsigned char> passthroughMsg = message;
                    passthroughMsg[0] = (passthroughMsg[0] & 0xF0) | 0x00;
                    midiOut->sendMessage(&passthroughMsg);
                } else { // Voice
                    if (status == 0xD0) { // Aftertouch
                        int value = message[1];
                        int breathValue = std::max(0, value - 16);
                        
                        std::vector<unsigned char> cc2_msg = {0xB0, 2, (unsigned char)breathValue};
                        midiOut->sendMessage(&cc2_msg);

                        std::vector<unsigned char> cc104_msg = {0xB0, 104, (unsigned char)breathValue};
                        midiOut->sendMessage(&cc104_msg);
                    }
                }

                if (status == 0x90 || status == 0x80 || status == 0xE0) {
                    updatePitch(message, event.isGuitar);
                }
            }
            break;
        case EventType::PROGRAM_CHANGE:
            processProgramChange(event.programIndex);
            break;
        case EventType::TRACK_TOGGLE:
            if (m_trackStates.count(event.trackId)) {
                setTrackState(event.trackId, !m_trackStates.at(event.trackId));
            }
            break;
        case EventType::PLAY_TRACK:
             if (event.programIndex >= 0 && event.programIndex < m_backingTracks.size()) {
                if (m_currentlyPlayingTrackIndex == event.programIndex) {
                    // It's the same track, so just resume if paused
                    if (m_player->playbackState() == QMediaPlayer::PausedState) {
                        emit _internal_resumeTrack();
                    }
                } else {
                    // It's a new track, so play it from the start
                    m_currentlyPlayingTrackIndex = event.programIndex;
                    emit _internal_playTrack(QUrl::fromLocalFile(m_backingTracks.at(event.programIndex)));
                }
            }
            break;
        case EventType::PAUSE_TRACK:
            if (m_player->playbackState() == QMediaPlayer::PlayingState) {
                emit _internal_pauseTrack();
            }
            break;
    }
}
 
void MidiProcessor::loadBackingTracks() {
    QDir backingTracksDir(m_preset.settings.backingTrackDirectory);
    QStringList absolutePaths;
    // Get a list of QFileInfo objects
    QFileInfoList fileInfoList = backingTracksDir.entryInfoList(QStringList() << "*.mp3", QDir::Files);

    // Populate the list of absolute paths
    for (const QFileInfo &fileInfo : fileInfoList) {
        absolutePaths.append(fileInfo.absoluteFilePath());
    }
    m_backingTracks = absolutePaths;
    m_backingTracks.sort();
    emit backingTracksLoaded(m_backingTracks);
}

void MidiProcessor::processProgramChange(int programIndex) {
    if (programIndex < 0 || programIndex >= m_preset.programs.size()) return;
    const auto& program = m_preset.programs[programIndex];
    m_currentProgramIndex = programIndex;

    if (program.programCC != -1 && program.programValue != -1) {
        std::vector<unsigned char> prog_msg = { 0xB0, (unsigned char)program.programCC, (unsigned char)program.programValue };
        midiOut->sendMessage(&prog_msg);
    }
    
    if (program.volumeCC != -1 && program.volumeValue != -1) {
        std::vector<unsigned char> vol_msg = { 0xB0, (unsigned char)program.volumeCC, (unsigned char)program.volumeValue };
        midiOut->sendMessage(&vol_msg);
    }

    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logQueue.push("Applied program: " + program.name.toStdString());
    }
    emit programChanged(m_currentProgramIndex);

    for (const auto& toggle : m_preset.toggles) {
        setTrackState(toggle.id.toStdString(), program.initialStates.value(toggle.id, m_preset.settings.defaultTrackStates.value(toggle.id, false)));
    }
}

void MidiProcessor::setTrackState(const std::string& trackId, bool newState) {
    if (m_trackStates.count(trackId) && m_trackStates.at(trackId) != newState) {
        for (const auto& toggle : m_preset.toggles) {
            if (toggle.id.toStdString() == trackId) {
                sendNoteToggle(toggle.note, toggle.channel, toggle.velocity);
                m_trackStates[trackId] = newState;
                {
                    std::lock_guard<std::mutex> lock(m_logMutex);
                    m_logQueue.push("Set track: " + trackId + " to " + (newState ? "ON" : "OFF"));
                }
                emit trackStateUpdated(trackId, newState);
                return;
            }
        }
    }
}

void MidiProcessor::handleBackingTrackSelection(int note) {
    if (note > 86) return;

    int trackIndex = 86 - note;

    if (trackIndex < 0 || trackIndex >= m_backingTracks.size()) {
        {
            std::lock_guard<std::mutex> lock(m_logMutex);
            m_logQueue.push("Backing track index " + std::to_string(trackIndex) + " is out of range.");
        }
        return;
    }

    if (m_currentlyPlayingTrackIndex == trackIndex) { // Same track selected
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            emit _internal_pauseTrack();
        } else {
            emit _internal_resumeTrack();
        }
    } else { // New track selected
        m_currentlyPlayingTrackIndex = trackIndex;
        emit _internal_playTrack(QUrl::fromLocalFile(m_backingTracks.at(trackIndex)));
    }
}

void MidiProcessor::sendNoteToggle(int note, int channel, int velocity) {
    if (channel < 1 || channel > 16) return;
    unsigned char chan = channel - 1;
    std::vector<unsigned char> msg = {(unsigned char)(0x90 | chan), (unsigned char)note, (unsigned char)velocity};
    midiOut->sendMessage(&msg);
    msg[0] = (0x80 | chan); msg[2] = 0;
    midiOut->sendMessage(&msg);
}

void MidiProcessor::updatePitch(const std::vector<unsigned char>& message, bool isGuitar) {
    unsigned char status = message.at(0) & 0xF0;
    
    if (status == 0x90 && message.at(2) > 0) {
        int note = message.at(1);
        if (isGuitar) { m_lastGuitarNote = note; m_lastGuitarPitchHz = noteToFrequency(note); }
        else { m_lastVoiceNote = note; m_lastVoicePitchHz = noteToFrequency(note); }
    } else if (status == 0x80 || (status == 0x90 && message.at(2) == 0)) {
        int note = message.at(1);
        if (isGuitar && m_lastGuitarNote == note) { m_lastGuitarPitchHz = 0.0; }
        else if (!isGuitar && m_lastVoiceNote == note) { m_lastVoicePitchHz = 0.0; }
    } else if (status == 0xE0) {
        int bendValue = ((message.at(1) | (message.at(2) << 7))) - 8192;
        double centsOffset = (static_cast<double>(bendValue) / 8192.0) * 200.0;
        int baseNote = isGuitar ? m_lastGuitarNote : m_lastVoiceNote;
        if (baseNote != -1) {
            double bentFreq = noteToFrequency(baseNote) * pow(2.0, centsOffset / 1200.0);
            if (isGuitar) { m_lastGuitarPitchHz = bentFreq; }
            else { m_lastVoicePitchHz = bentFreq; }
        }
    }
    
    processPitchBend();
}

void MidiProcessor::precalculateRatios() {
    m_ratioUpDeadZone = pow(2.0, m_preset.settings.pitchBendDeadZoneCents / 1200.0);
    m_ratioDownDeadZone = pow(2.0, -m_preset.settings.pitchBendDeadZoneCents / 1200.0);
}

void MidiProcessor::processPitchBend() {
    double guitarHz = m_lastGuitarPitchHz;
    double voiceHz = m_lastVoicePitchHz;

    if (guitarHz <= 1.0 || voiceHz <= 1.0) {
        if (m_lastCC102Value != 0 || m_lastCC103Value != 0) {
            std::vector<unsigned char> msg102 = { 0xB0, (unsigned char)BEND_DOWN_CC, 0 };
            midiOut->sendMessage(&msg102);
            std::vector<unsigned char> msg103 = { 0xB0, (unsigned char)BEND_UP_CC, 0 };
            midiOut->sendMessage(&msg103);
            m_lastCC102Value = 0;
            m_lastCC103Value = 0;
        }
        return;
    }

    double currentRatio = voiceHz / guitarHz;
    int cc102_val = 0;
    int cc103_val = 0;

    if (currentRatio < m_ratioDownDeadZone) {
        double diffCents = -1200.0 * log2(currentRatio);
        double deviation = diffCents - m_preset.settings.pitchBendDeadZoneCents;
        cc102_val = static_cast<int>((deviation / m_preset.settings.pitchBendDownRangeCents) * 127.0);
    } else if (currentRatio > m_ratioUpDeadZone) {
        double diffCents = 1200.0 * log2(currentRatio);
        double deviation = diffCents - m_preset.settings.pitchBendDeadZoneCents;
        cc103_val = static_cast<int>((deviation / m_preset.settings.pitchBendUpRangeCents) * 127.0);
    }
    
    cc102_val = std::min(127, std::max(0, cc102_val));
    cc103_val = std::min(127, std::max(0, cc103_val));

    if (cc102_val != m_lastCC102Value) {
        std::vector<unsigned char> msg = { 0xB0, (unsigned char)BEND_DOWN_CC, (unsigned char)cc102_val };
        midiOut->sendMessage(&msg);
        m_lastCC102Value = cc102_val;
    }
    if (cc103_val != m_lastCC103Value) {
        std::vector<unsigned char> msg = { 0xB0, (unsigned char)BEND_UP_CC, (unsigned char)cc103_val };
        midiOut->sendMessage(&msg);
        m_lastCC103Value = cc103_val;
    }

    if (m_isVerbose.load()) {
        char buffer[100];
        snprintf(buffer, sizeof(buffer), "Pitch Bend CCs -> Down (102): %d, Up (103): %d", m_lastCC102Value, m_lastCC103Value);
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logQueue.push(std::string(buffer));
    }
}

double MidiProcessor::noteToFrequency(int note) const {
    if (note < 0) return 0.0;
    return 440.0 * pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}