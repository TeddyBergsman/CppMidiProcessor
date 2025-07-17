#include "midiprocessor.h"
#include <iostream>
#include <algorithm>

// --- Configuration ---
const std::string GUITAR_IN_PORT_NAME = "IAC Driver MG3 Guitar";
const std::string VOICE_IN_PORT_NAME  = "IAC Driver MG3 Voice";
const std::string CONTROLLER_OUT_PORT_NAME = "IAC Driver Controller";
const int COMMAND_NOTE = 86;
const int PROGRAM_SELECT_CC = 119;

// The actual DATA for the vectors remains here.
const std::vector<ProgramConfig> programConfigs = {
    { "Program 1", 85, 0, 127, { {"track1", false}, {"track2", true},  {"track3", true} } },
    { "Program 2", 84, 1, 96,  { {"track1", false}, {"track2", true},  {"track3", false} } },
    { "Program 3", 83, 2, 112, {} },
    { "Program 4", 82, 3, 70,  {} },
    { "Program 5", 81, 4, 70,  {} },
    { "Program 6", 80, 5, 64,  {} },
    { "Program 7", 79, 6, 108, {} },
    { "Program 8", 78, 7, 112, {} },
    { "Program 9", 77, 8, 125, {} },
    { "Program 10", 76, 9, 125,{} }
};
const std::map<std::string, int> trackToggleNotes = { {"track1", 0}, {"track2", 1}, {"track3", 2} };
const std::map<std::string, bool> defaultProgramTrackStates = { {"track1", true}, {"track2", false}, {"track3", false} };
std::map<int, int> programRulesMap;


// --- Implementation ---

MidiProcessor::MidiProcessor(QObject *parent) : QObject(parent) {
    // Initialize state
    trackStates = { {"track1", false}, {"track2", true}, {"track3", true} };
    currentProgramIndex = 0;
    for (size_t i = 0; i < programConfigs.size(); ++i) {
        programRulesMap[programConfigs[i].note] = i;
    }
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

    // You can remove this debugging code now if you wish
    std::cout << "\n--- Available MIDI Input Ports ---" << std::endl;
    for (unsigned int i = 0; i < midiInGuitar->getPortCount(); ++i) {
        std::cout << "Input Port " << i << ": " << midiInGuitar->getPortName(i) << std::endl;
    }

    std::cout << "\n--- Available MIDI Output Ports ---" << std::endl;
    for (unsigned int i = 0; i < midiOut->getPortCount(); ++i) {
        std::cout << "Output Port " << i << ": " << midiOut->getPortName(i) << std::endl;
    }
    std::cout << "------------------------------------\n" << std::endl;


    auto find_port = [](RtMidi& midi, const std::string& name) {
        for (unsigned int i = 0; i < midi.getPortCount(); i++) {
            if (midi.getPortName(i).find(name) != std::string::npos) return (int)i;
        }
        return -1;
    };

    int guitarPort = find_port(*midiInGuitar, GUITAR_IN_PORT_NAME);
    int voicePort = find_port(*midiInVoice, VOICE_IN_PORT_NAME);
    int outPort = find_port(*midiOut, CONTROLLER_OUT_PORT_NAME);

    if (guitarPort == -1 || outPort == -1 || voicePort == -1) {
        emit logMessage("ERROR: Could not find all MIDI ports. Check names in code.");
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
    applyProgram(0);
    return true;
}

void MidiProcessor::applyProgram(int programIndex) {
    if (programIndex >= programConfigs.size()) return;

    std::lock_guard<std::mutex> lock(stateMutex); // Lock is acquired ONCE here.
    const auto& rule = programConfigs[programIndex];
    currentProgramIndex = programIndex;

    std::vector<unsigned char> msg;
    msg.push_back(0xB0); msg.push_back(PROGRAM_SELECT_CC); msg.push_back(rule.program);
    midiOut->sendMessage(&msg);

    msg[1] = 7; msg[2] = rule.volume;
    midiOut->sendMessage(&msg);

    emit logMessage("Applied program: " + rule.name);
    emit programChanged(currentProgramIndex);

    const auto& desiredStates = rule.trackStates.empty() ? defaultProgramTrackStates : rule.trackStates;
    for (const auto& pair : desiredStates) {
        if (trackStates.at(pair.first) != pair.second) {
            // *** CHANGED: Call the unlocked helper function ***
            toggleTrack_unlocked(pair.first);
        }
    }
}

void MidiProcessor::sendNoteToggle(int noteNumber) {
    std::vector<unsigned char> msg;
    msg.push_back(0x9F); msg.push_back(noteNumber); msg.push_back(100);
    midiOut->sendMessage(&msg);
    msg[0] = 0x8F; msg[2] = 0;
    midiOut->sendMessage(&msg);
}

// *** CHANGED: The public toggleTrack now just locks and calls the helper ***
void MidiProcessor::toggleTrack(const std::string& trackId) {
    std::lock_guard<std::mutex> lock(stateMutex);
    toggleTrack_unlocked(trackId);
}

// *** NEW: The private helper contains the core logic without locking ***
void MidiProcessor::toggleTrack_unlocked(const std::string& trackId) {
    // This function assumes the mutex is already locked by the calling function.
    sendNoteToggle(trackToggleNotes.at(trackId));
    trackStates[trackId] = !trackStates[trackId];
    emit logMessage("Toggled track: " + trackId);
    emit trackStateUpdated(trackId, trackStates[trackId]);
}


// --- Callbacks ---
void MidiProcessor::guitarCallback(double deltatime, std::vector<unsigned char>* message, void* userData) {
    MidiProcessor* self = static_cast<MidiProcessor*>(userData);

    // This is running on a high-priority thread.
    // We must pass MIDI through with ZERO delay.
    unsigned char status = message->at(0) & 0xF0;
    message->at(0) = status | 0x00; // Force to channel 1
    self->midiOut->sendMessage(message);

    // Now, handle command logic
    if (status == 0x90 && message->at(2) > 0) { // Note On
        int note = message->at(1);
        if (note == COMMAND_NOTE) {
            self->inCommandMode = true;
        } else if (self->inCommandMode) {
            if (programRulesMap.count(note)) {
                // We use QMetaObject::invokeMethod to safely call it on the main GUI thread.
                QMetaObject::invokeMethod(self, [=](){
                    self->applyProgram(programRulesMap.at(note));
                }, Qt::QueuedConnection);
            }
            self->inCommandMode = false;
        }
    }
}

void MidiProcessor::voiceCallback(double deltatime, std::vector<unsigned char>* message, void* userData) {
    MidiProcessor* self = static_cast<MidiProcessor*>(userData);
    unsigned char status = message->at(0) & 0xF0;
    
    if (status == 0xD0) { // Aftertouch
        int value = message->at(1);
        int breathValue = std::max(0, value - 16);
        std::vector<unsigned char> cc_msg = {0xB0, 2, (unsigned char)breathValue};
        self->midiOut->sendMessage(&cc_msg);
    }
}