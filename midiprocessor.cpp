#include "midiprocessor.h"
#include <iostream>
#include <algorithm>

MidiProcessor::MidiProcessor(const Preset& preset, QObject *parent) 
    : QObject(parent), m_preset(preset) {
    
    // Build the map for program trigger notes.
    for (int i = 0; i < m_preset.programs.size(); ++i) {
        m_programRulesMap[m_preset.programs[i].triggerNote] = i;
    }

    // Initialize all track states to be 'off' (false) initially.
    // The correct state will be set when the first program is applied.
    for (const auto& toggle : m_preset.toggles) {
        trackStates[toggle.id.toStdString()] = false;
    }
    
    currentProgramIndex = -1; // Indicates no program is initially active.
}

MidiProcessor::~MidiProcessor() {
    delete midiInGuitar;
    delete midiOut;
    delete midiInVoice;
}

bool MidiProcessor::initialize() {
    midiInGuitar = new RtMidiIn();
    midiOut = new RtMidiOut();
    midiInVoice = new RtMidiIn();

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
        emit logMessage("ERROR: Could not find all MIDI ports. Check names in preset.xml.");
        return false;
    }

    midiInGuitar->openPort(guitarPort);
    midiOut->openPort(outPort);
    midiInVoice->openPort(voicePort);

    midiInGuitar->setCallback(&MidiProcessor::guitarCallback, this);
    midiInVoice->setCallback(&MidiProcessor::voiceCallback, this);
    midiInGuitar->ignoreTypes(true, true, true);
    midiInVoice->ignoreTypes(true, true, true);

    emit logMessage("SUCCESS: MIDI ports opened and listeners attached.");
    
    // Apply the first program and sync the UI.
    applyProgram(0);

    // After applying the first program, emit the current state of all tracks to sync the UI.
    std::lock_guard<std::mutex> lock(stateMutex);
    for (const auto& pair : trackStates) {
        emit trackStateUpdated(pair.first, pair.second);
    }

    return true;
}

void MidiProcessor::applyProgram(int programIndex) {
    if (programIndex < 0 || programIndex >= m_preset.programs.size()) return;

    std::lock_guard<std::mutex> lock(stateMutex);
    const auto& program = m_preset.programs[programIndex];
    currentProgramIndex = programIndex;

    // Send Program Change via specified Control Change
    std::vector<unsigned char> prog_msg = {
        (unsigned char)(0xB0), // CC on Channel 1
        (unsigned char)program.programCC,
        (unsigned char)program.programValue
    };
    midiOut->sendMessage(&prog_msg);

    // Send Volume via specified Control Change
    std::vector<unsigned char> vol_msg = {
        (unsigned char)(0xB0), // CC on Channel 1
        (unsigned char)program.volumeCC,
        (unsigned char)program.volumeValue
    };
    midiOut->sendMessage(&vol_msg);

    emit logMessage("Applied program: " + program.name.toStdString());
    emit programChanged(currentProgramIndex);

    // Set track states based on the program, using defaults as a fallback.
    for (const auto& toggle : m_preset.toggles) {
        std::string toggleIdStd = toggle.id.toStdString();
        bool defaultState = m_preset.settings.defaultTrackStates.value(toggle.id, false);
        bool desiredState = program.initialStates.value(toggle.id, defaultState);
        
        // Use the new, robust setter function. This is the core of the fix.
        setTrackState_unlocked(toggleIdStd, desiredState);
    }
}

void MidiProcessor::sendNoteToggle(int note, int channel, int velocity) {
    if (channel < 1 || channel > 16) return;
    unsigned char chan = channel - 1;

    std::vector<unsigned char> msg;
    msg = {(unsigned char)(0x90 | chan), (unsigned char)note, (unsigned char)velocity};
    midiOut->sendMessage(&msg);
    msg[0] = (0x80 | chan); msg[2] = 0;
    midiOut->sendMessage(&msg);
}

// Public function for manual toggling from the UI.
void MidiProcessor::toggleTrack(const std::string& trackId) {
    std::lock_guard<std::mutex> lock(stateMutex);
    // Determine the desired new state by inverting the current one.
    bool newState = !trackStates.at(trackId);
    // Call the robust setter function with the desired state.
    setTrackState_unlocked(trackId, newState);
}

// NEW: Private helper that robustly SETS a track to a specific state.
void MidiProcessor::setTrackState_unlocked(const std::string& trackId, bool newState) {
    // Only send a MIDI message and update state if the state is actually changing.
    if (trackStates.at(trackId) != newState) {
        for (const auto& toggle : m_preset.toggles) {
            if (toggle.id.toStdString() == trackId) {
                sendNoteToggle(toggle.note, toggle.channel, toggle.velocity);
                trackStates[trackId] = newState; // Set the new state directly.
                emit logMessage("Set track: " + trackId + " to " + (newState ? "ON" : "OFF"));
                emit trackStateUpdated(trackId, newState);
                return;
            }
        }
    }
}

void MidiProcessor::guitarCallback(double deltatime, std::vector<unsigned char>* message, void* userData) {
    MidiProcessor* self = static_cast<MidiProcessor*>(userData);
    unsigned char status = message->at(0) & 0xF0;

    if (status == 0x90 && message->at(2) > 0) {
        int note = message->at(1);
        
        if (note == self->m_preset.settings.commandNote) {
            self->inCommandMode = true;
            return;
        } 
        else if (self->inCommandMode) {
            if (self->m_programRulesMap.count(note)) {
                QMetaObject::invokeMethod(self, [self, note](){
                    self->applyProgram(self->m_programRulesMap.at(note));
                }, Qt::QueuedConnection);
            }
            self->inCommandMode = false;
            return;
        }
    }

    message->at(0) = (message->at(0) & 0xF0) | 0x00; // Force to channel 1
    self->midiOut->sendMessage(message);
}

void MidiProcessor::voiceCallback(double deltatime, std::vector<unsigned char>* message, void* userData) {
    MidiProcessor* self = static_cast<MidiProcessor*>(userData);
    unsigned char status = message->at(0) & 0xF0;
    
    if (status == 0xD0) { // Voice channel pressure (aftertouch)
        int value = message->at(1);
        int breathValue = std::max(0, value - 16);
        std::vector<unsigned char> cc_msg = {0xB0, 2, (unsigned char)breathValue}; // CC 2 (Breath) on channel 1
        self->midiOut->sendMessage(&cc_msg);
    }
}

#include "midiprocessor.moc"