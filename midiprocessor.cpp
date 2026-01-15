#include "midiprocessor.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <QStringBuilder>
#include <exception>
#include <deque>

MidiProcessor::MidiProcessor(const Preset& preset, QObject *parent) 
    : QObject(parent), m_preset(preset), m_currentProgramIndex(-1) {

    for (int i = 0; i < m_preset.programs.size(); ++i) {
        m_programRulesMap[m_preset.programs[i].triggerNote] = i;
    }
    for (const auto& toggle : m_preset.toggles) {
        m_trackStates[toggle.id.toStdString()] = true;
    }
    
    // Initialize voice control state from preset
    m_voiceControlEnabled.store(preset.settings.voiceControlEnabled);

    m_logPollTimer = new QTimer(this);
    connect(m_logPollTimer, &QTimer::timeout, this, &MidiProcessor::pollLogQueue);
    m_logPollTimer->start(33);

    // Nothing extra here
}

MidiProcessor::~MidiProcessor() {
    m_isRunning = false;
    m_condition.notify_one();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    // Guarantee silence on teardown. Many samplers require explicit NOTE_OFF to stop loops.
    // Do this AFTER the worker thread is stopped (no concurrent midiOut access),
    // and BEFORE midiOut is destroyed.
    panicAllChannels();
    
    delete midiInGuitar;
    delete midiOut;
    delete midiInVoice;
    delete midiInVoicePitch;
}

void MidiProcessor::safeSendMessage(const std::vector<unsigned char>& msg) {
    if (!midiOut) return;
    if (msg.empty()) return;
    try {
        midiOut->sendMessage(&msg);
    } catch (const RtMidiError& e) {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logQueue.push(std::string("ERROR: RtMidi sendMessage failed: ") + e.getMessage());
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logQueue.push(std::string("ERROR: MIDI sendMessage threw: ") + e.what());
    } catch (...) {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logQueue.push("ERROR: MIDI sendMessage threw unknown exception");
    }
}

bool MidiProcessor::isCriticalMidiEvent(const MidiEvent& ev) {
    // Always keep control events.
    if (ev.type != EventType::MIDI_MESSAGE) return true;
    const auto& m = ev.message;
    if (m.empty()) return false;
    const unsigned char st = m[0] & 0xF0;
    const unsigned char stRaw = m[0];
    if (stRaw >= 0xF0) return false;
    if (m.size() < 3) return false;
    const int d1 = int(m[1]);
    const int d2 = int(m[2]);
    // NOTE_OFF (incl note-on vel=0) is critical to avoid stuck notes.
    if (st == 0x80) return true;
    if (st == 0x90 && d2 == 0) return true;
    // "All notes off / sound off / sustain off" kills are critical.
    if (st == 0xB0) {
        const int cc = d1;
        const int val = d2;
        if ((cc == 64 || cc == 120 || cc == 123) && val == 0) return true;
    }
    return false;
}

bool MidiProcessor::tryEnqueueEvent(MidiEvent&& ev) {
    // Must be called with m_eventMutex held.
    if (m_eventQueue.size() < kMaxEventQueue) {
        m_eventQueue.emplace_back(std::move(ev));
        return true;
    }

    // Queue full: keep critical events if possible; otherwise drop.
    const bool critical = isCriticalMidiEvent(ev);
    if (!critical) {
        const quint64 dropped = ++m_droppedMidiEvents;
        // Log occasionally to avoid flooding.
        if ((dropped % 1024u) == 1u) {
            std::lock_guard<std::mutex> lock(m_logMutex);
            m_logQueue.push(QString("WARN: Dropping MIDI events due to overload (dropped=%1, q=%2)")
                                .arg(qulonglong(dropped))
                                .arg(qulonglong(m_eventQueue.size()))
                                .toStdString());
        }
        return false;
    }

    // Try to make room by dropping one non-critical event from the back (cheapest to lose).
    for (auto it = m_eventQueue.rbegin(); it != m_eventQueue.rend(); ++it) {
        if (!isCriticalMidiEvent(*it)) {
            // Erase reverse iterator.
            m_eventQueue.erase(std::next(it).base());
            m_eventQueue.emplace_back(std::move(ev));
            return true;
        }
    }

    // Everything in the queue is critical. Drop the oldest one to ensure progress.
    m_eventQueue.pop_front();
    m_eventQueue.emplace_back(std::move(ev));
    return true;
}

void MidiProcessor::panicAllChannels() {
    if (!midiOut) return;
    for (int ch = 0; ch < 16; ++ch) {
        for (int n = 0; n < 128; ++n) {
            std::vector<unsigned char> offMsg = { (unsigned char)(0x80 | (unsigned char)ch), (unsigned char)n, 0 };
            safeSendMessage(offMsg);
            // Some hosts prefer NoteOn velocity=0 as note-off
            std::vector<unsigned char> on0Msg = { (unsigned char)(0x90 | (unsigned char)ch), (unsigned char)n, 0 };
            safeSendMessage(on0Msg);
        }
        sendChannelAllNotesOff(ch);
    }
}

bool MidiProcessor::initialize() {
    midiInGuitar = new RtMidiIn();
    midiOut = new RtMidiOut();
    midiInVoice = new RtMidiIn();
    midiInVoicePitch = new RtMidiIn();

    auto find_port = [](RtMidi& midi, const std::string& name) {
        for (unsigned int i = 0; i < midi.getPortCount(); i++) {
            if (midi.getPortName(i).find(name) != std::string::npos) return (int)i;
        }
        return -1;
    };

    int guitarPort = find_port(*midiInGuitar, m_preset.settings.ports["GUITAR_IN"].toStdString());
    int voicePort = find_port(*midiInVoice, m_preset.settings.ports["VOICE_IN"].toStdString());
    // Voice pitch port: prefer VOICE_PITCH_IN override; else try default literal
    QString voicePitchName = m_preset.settings.ports.contains("VOICE_PITCH_IN")
        ? m_preset.settings.ports["VOICE_PITCH_IN"]
        : QString("IAC Driver MG3 Voice Pitch");
    int voicePitchPort = find_port(*midiInVoicePitch, voicePitchName.toStdString());
    int outPort = find_port(*midiOut, m_preset.settings.ports["CONTROLLER_OUT"].toStdString());

    if (guitarPort == -1 || outPort == -1 || voicePort == -1) {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logQueue.push("ERROR: Could not find all MIDI ports. Check names in preset.xml.");
        return false;
    }

    midiInGuitar->openPort(guitarPort);
    midiOut->openPort(outPort);
    midiInVoice->openPort(voicePort);
    if (voicePitchPort != -1) {
        midiInVoicePitch->openPort(voicePitchPort);
        m_voicePitchAvailable = true;
    } else {
        // Fallback: keep using VOICE_IN for pitch if separate pitch port not found
        m_voicePitchAvailable = false;
        {
            std::lock_guard<std::mutex> lock(m_logMutex);
            m_logQueue.push("WARN: VOICE_PITCH_IN port not found; using VOICE_IN for pitch.");
        }
    }

    midiInGuitar->setCallback(&MidiProcessor::guitarCallback, this);
    midiInVoice->setCallback(&MidiProcessor::voiceAmpCallback, this);
    if (m_voicePitchAvailable) {
        midiInVoicePitch->setCallback(&MidiProcessor::voicePitchCallback, this);
    }
    
    // We don't need SysEx/timing/sensing from live inputs; dropping them avoids
    // edge cases where system messages get accidentally mangled/forwarded.
    midiInGuitar->ignoreTypes(true, true, true);
    midiInVoice->ignoreTypes(true, true, true);
    if (m_voicePitchAvailable) {
        midiInVoicePitch->ignoreTypes(true, true, true);
    }

    precalculateRatios();
    m_isRunning = true;

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
    tryEnqueueEvent({EventType::PROGRAM_CHANGE, std::vector<unsigned char>(), MidiSource::Guitar, programIndex, ""});
    m_condition.notify_one();
}

void MidiProcessor::applyTranspose(int semitones) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    // Use programIndex field to carry semitone value for TRANSPOSE_CHANGE
    tryEnqueueEvent({EventType::TRANSPOSE_CHANGE, std::vector<unsigned char>(), MidiSource::Guitar, semitones, ""});
    m_condition.notify_one();
}

void MidiProcessor::sendVirtualNoteOn(int channel, int note, int velocity) {
    if (channel < 1 || channel > 16) return;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    if (velocity < 1) velocity = 1;
    if (velocity > 127) velocity = 127;
    const unsigned char chan = (unsigned char)(channel - 1);
    std::vector<unsigned char> msg = { (unsigned char)(0x90 | chan), (unsigned char)note, (unsigned char)velocity };
    std::lock_guard<std::mutex> lock(m_eventMutex);
    tryEnqueueEvent({EventType::MIDI_MESSAGE, msg, MidiSource::VirtualBand, -1, ""});
    m_condition.notify_one();
}

void MidiProcessor::sendVirtualNoteOff(int channel, int note) {
    if (channel < 1 || channel > 16) return;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    const unsigned char chan = (unsigned char)(channel - 1);
    std::lock_guard<std::mutex> lock(m_eventMutex);
    // Some VSTs/hosts are more reliable with "NoteOn velocity=0" as note-off.
    // Send BOTH forms to avoid stuck notes / "infinite sustain" symptoms.
    std::vector<unsigned char> msgOff = { (unsigned char)(0x80 | chan), (unsigned char)note, 0 };
    std::vector<unsigned char> msgOn0 = { (unsigned char)(0x90 | chan), (unsigned char)note, 0 };
    tryEnqueueEvent({EventType::MIDI_MESSAGE, msgOff, MidiSource::VirtualBand, -1, ""});
    tryEnqueueEvent({EventType::MIDI_MESSAGE, msgOn0, MidiSource::VirtualBand, -1, ""});
    m_condition.notify_one();
}

void MidiProcessor::sendVirtualAllNotesOff(int channel) {
    if (channel < 1 || channel > 16) return;
    const unsigned char chan = (unsigned char)(channel - 1);
    std::vector<unsigned char> sustainOff = { (unsigned char)(0xB0 | chan), 64, 0 };
    std::vector<unsigned char> allNotesOff = { (unsigned char)(0xB0 | chan), 123, 0 };
    std::vector<unsigned char> allSoundOff = { (unsigned char)(0xB0 | chan), 120, 0 };
    std::lock_guard<std::mutex> lock(m_eventMutex);
    tryEnqueueEvent({EventType::MIDI_MESSAGE, sustainOff, MidiSource::VirtualBand, -1, ""});
    tryEnqueueEvent({EventType::MIDI_MESSAGE, allNotesOff, MidiSource::VirtualBand, -1, ""});
    tryEnqueueEvent({EventType::MIDI_MESSAGE, allSoundOff, MidiSource::VirtualBand, -1, ""});
    m_condition.notify_one();
}

void MidiProcessor::sendVirtualCC(int channel, int cc, int value) {
    if (channel < 1 || channel > 16) return;
    if (cc < 0) cc = 0;
    if (cc > 127) cc = 127;
    if (value < 0) value = 0;
    if (value > 127) value = 127;
    const unsigned char chan = (unsigned char)(channel - 1);
    std::vector<unsigned char> msg = { (unsigned char)(0xB0 | chan), (unsigned char)cc, (unsigned char)value };
    std::lock_guard<std::mutex> lock(m_eventMutex);
    tryEnqueueEvent({EventType::MIDI_MESSAGE, msg, MidiSource::VirtualBand, -1, ""});
    m_condition.notify_one();
}

void MidiProcessor::sendVirtualPitchBend(int channel, int bendValue) {
    if (channel < 1 || channel > 16) return;
    // MIDI pitch bend: 14-bit value, 0-16383, center at 8192
    if (bendValue < 0) bendValue = 0;
    if (bendValue > 16383) bendValue = 16383;
    const unsigned char chan = (unsigned char)(channel - 1);
    // Pitch bend message: status byte 0xE0 | channel, LSB (7 bits), MSB (7 bits)
    unsigned char lsb = (unsigned char)(bendValue & 0x7F);
    unsigned char msb = (unsigned char)((bendValue >> 7) & 0x7F);
    std::vector<unsigned char> msg = { (unsigned char)(0xE0 | chan), lsb, msb };
    std::lock_guard<std::mutex> lock(m_eventMutex);
    tryEnqueueEvent({EventType::MIDI_MESSAGE, msg, MidiSource::VirtualBand, -1, ""});
    m_condition.notify_one();
}

void MidiProcessor::toggleTrack(const std::string& trackId) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    tryEnqueueEvent({EventType::TRACK_TOGGLE, std::vector<unsigned char>(), MidiSource::Guitar, -1, trackId});
    m_condition.notify_one();
}

void MidiProcessor::setVerbose(bool verbose) {
    m_isVerbose.store(verbose);
}

void MidiProcessor::setVoiceControlEnabled(bool enabled) {
    m_voiceControlEnabled.store(enabled);
}

void MidiProcessor::setTranspose(int semitones) {
    m_transposeAmount.store(semitones);
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logQueue.push("Transpose set to: " + std::to_string(semitones) + " semitones");
    }
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

void MidiProcessor::guitarCallback(double deltatime, std::vector<unsigned char>* message, void* userData) {
    MidiProcessor* self = static_cast<MidiProcessor*>(userData);
    if (!self->m_isRunning) return;
    if (!message) return;
    {
        std::lock_guard<std::mutex> lock(self->m_eventMutex);
        self->tryEnqueueEvent({EventType::MIDI_MESSAGE, *message, MidiSource::Guitar, -1, ""});
    }
    self->m_condition.notify_one();
}

void MidiProcessor::voiceAmpCallback(double deltatime, std::vector<unsigned char>* message, void* userData) {
    MidiProcessor* self = static_cast<MidiProcessor*>(userData);
    if (!self->m_isRunning) return;
    if (!message) return;
    {
        std::lock_guard<std::mutex> lock(self->m_eventMutex);
        self->tryEnqueueEvent({EventType::MIDI_MESSAGE, *message, MidiSource::VoiceAmp, -1, ""});
    }
    self->m_condition.notify_one();
}

void MidiProcessor::voicePitchCallback(double deltatime, std::vector<unsigned char>* message, void* userData) {
    MidiProcessor* self = static_cast<MidiProcessor*>(userData);
    if (!self->m_isRunning) return;
    if (!message) return;
    {
        std::lock_guard<std::mutex> lock(self->m_eventMutex);
        self->tryEnqueueEvent({EventType::MIDI_MESSAGE, *message, MidiSource::VoicePitch, -1, ""});
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
            
            event = std::move(m_eventQueue.front());
            m_eventQueue.pop_front();
        }
        
        processMidiEvent(event);
    }
}

void MidiProcessor::processMidiEvent(const MidiEvent& event) {
    switch(event.type) {
        case EventType::MIDI_MESSAGE:
            {
                const auto& message = event.message;
                // Defensive: some MIDI sources can produce empty packets (or get truncated by routers).
                // Never crash the app on malformed input; just ignore.
                if (message.empty()) {
                    return;
                }
                unsigned char status = message[0] & 0xF0;
                const unsigned char statusRaw = message[0];

                // Defensive: ignore system common / real-time messages coming from live inputs.
                // These are not part of our performance/control protocol, and rewriting their
                // low nibble (channel) is invalid and can destabilize downstream MIDI drivers.
                if (statusRaw >= 0xF0 && event.source != MidiSource::VirtualBand) {
                    return;
                }

                if (event.source == MidiSource::Guitar) {
                    int inputNote = message.size() > 1 ? message[1] : 0;
                    int velocity = message.size() > 2 ? message[2] : 0;
                    if (status == 0x90 && velocity > 0) {
                        // Emit velocity for visualizer fallback amplitude
                        if (velocity != m_lastGuitarVelocity) {
                            m_lastGuitarVelocity = velocity;
                            emit guitarVelocityUpdated(velocity);
                        }
                        // Only process MIDI commands if voice control is disabled
                        if (!m_voiceControlEnabled.load()) {
                            // Adjust command note thresholds based on transpose amount
                            int transposeAmount = m_transposeAmount.load();
                            int adjustedCommandNote = m_preset.settings.commandNote + transposeAmount;
                            
                            if (inputNote == adjustedCommandNote) { m_inCommandMode = true; return; }
                            else if (m_inCommandMode) {
                                if (m_programRulesMap.count(inputNote)) { 
                                    panicSilence();
                                    processProgramChange(m_programRulesMap.at(inputNote));
                                }
                                m_inCommandMode = false;
                                return;
                            }
                        }
                    }
                    std::vector<unsigned char> passthroughMsg = message;
                    // Only channel messages (0x8*..0xE*) should have their channel nibble rewritten.
                    if (passthroughMsg[0] < 0xF0) {
                        passthroughMsg[0] = (passthroughMsg[0] & 0xF0) | 0x00;
                    }
                    
                    // Capture guitar channel pressure as amplitude for visualizer
                    if (status == 0xD0 && message.size() > 1) {
                        int aftertouch = message[1];
                        if (aftertouch != m_lastGuitarAftertouch) {
                            m_lastGuitarAftertouch = aftertouch;
                            emit guitarAftertouchUpdated(aftertouch);
                        }
                    }
                    
                    // Apply transpose to note on/off messages
                    if ((status == 0x90 || status == 0x80) && !m_inCommandMode) {
                        int transposeAmount = m_transposeAmount.load();
                        if (transposeAmount != 0 && passthroughMsg.size() > 1) {
                            int transposedNote = passthroughMsg[1] + transposeAmount;
                            // Clamp to valid MIDI range
                            if (transposedNote < 0) transposedNote = 0;
                            if (transposedNote > 127) transposedNote = 127;
                            passthroughMsg[1] = (unsigned char)transposedNote;
                        }
                    }

                    // Listening MVP hook: emit transposed performance note events (ignore command/backing selection notes).
                    if (!m_inCommandMode && (status == 0x90 || status == 0x80) && passthroughMsg.size() >= 3) {
                        const int note = int(passthroughMsg[1]);
                        const int vel = int(passthroughMsg[2]);
                        if (status == 0x90 && vel > 0) emit guitarNoteOn(note, vel);
                        else if (status == 0x80 || (status == 0x90 && vel == 0)) emit guitarNoteOff(note);
                    }
                    
                    safeSendMessage(passthroughMsg);
                    
                } else if (event.source == MidiSource::VoiceAmp) {
                    // Handle aftertouch → CC2 and CC104; ignore voice notes here to avoid duplicates
                    if (status == 0xD0 && message.size() > 1) { // Aftertouch
                        int value = message[1];
                        int breathValue = std::max(0, value - 16);
                        
                        std::vector<unsigned char> cc2_msg = {0xB0, 2, (unsigned char)breathValue};
                        safeSendMessage(cc2_msg);

                        std::vector<unsigned char> cc104_msg = {0xB0, 104, (unsigned char)breathValue};
                        safeSendMessage(cc104_msg);

                        // Unthrottled stream for interaction/vibe detection.
                        emit voiceCc2Stream(breathValue);
                        
                        // Emit breath (CC2) amplitude for visualizer if changed
                        if (breathValue != m_lastVoiceCc2) {
                            m_lastVoiceCc2 = breathValue;
                            emit voiceCc2Updated(breathValue);
                        }
                    }
                    // Do not call updatePitch here; pitch comes from VoicePitch source
                } else if (event.source == MidiSource::VoicePitch) {
                    // Use accurate pitch notes for visualization and optionally for output
                    if (status == 0x90 || status == 0x80) { // Note on/off for voice
                        std::vector<unsigned char> voiceMsg = message;
                        if (voiceMsg[0] < 0xF0) {
                            voiceMsg[0] = (voiceMsg[0] & 0xF0) | 0x01; // Set to channel 2
                        }
                        
                        // Apply transpose to voice notes
                        int transposeAmount = m_transposeAmount.load();
                        if (transposeAmount != 0 && voiceMsg.size() > 1) {
                            int transposedNote = voiceMsg[1] + transposeAmount;
                            // Clamp to valid MIDI range
                            if (transposedNote < 0) transposedNote = 0;
                            if (transposedNote > 127) transposedNote = 127;
                            voiceMsg[1] = (unsigned char)transposedNote;
                        }

                        // Listening MVP hook: emit voice note events (transposed).
                        if (voiceMsg.size() >= 3) {
                            const int note = int(voiceMsg[1]);
                            const int vel = int(voiceMsg[2]);
                            if (status == 0x90 && vel > 0) emit voiceNoteOn(note, vel);
                            else if (status == 0x80 || (status == 0x90 && vel == 0)) emit voiceNoteOff(note);
                        }
                        safeSendMessage(voiceMsg);
                    } else if (status != 0xD0) {
                        // Forward other non-aftertouch messages as-is on channel 2
                        std::vector<unsigned char> voiceMsg = message;
                        if (voiceMsg[0] < 0xF0) {
                            voiceMsg[0] = (voiceMsg[0] & 0xF0) | 0x01;
                        }
                        safeSendMessage(voiceMsg);
                    }
                } else if (event.source == MidiSource::VirtualBand) {
                    // Virtual musicians: forward as-is (no transpose, no channel remap).
                    if (m_isVerbose.load()) {
                        // Log note events so we can verify keyswitch/FX output is actually happening.
                        if (event.message.size() >= 3) {
                            const unsigned char st = event.message[0] & 0xF0;
                            const int ch = int(event.message[0] & 0x0F) + 1;
                            const int note = int(event.message[1]);
                            const int vel = int(event.message[2]);
                            if (st == 0x90 && vel > 0) {
                                std::lock_guard<std::mutex> lock(m_logMutex);
                                m_logQueue.push(QString("VirtualBand NOTE_ON  ch%1 note=%2 vel=%3")
                                                    .arg(ch).arg(note).arg(vel).toStdString());
                            } else if (st == 0x80 || (st == 0x90 && vel == 0)) {
                                std::lock_guard<std::mutex> lock(m_logMutex);
                                m_logQueue.push(QString("VirtualBand NOTE_OFF ch%1 note=%2")
                                                    .arg(ch).arg(note).toStdString());
                            }
                        }
                    }
                    safeSendMessage(event.message);
                }

                // Update pitch for guitar always; for voice only from VoicePitch source
                if (status == 0x90 || status == 0x80 || status == 0xE0) {
                    if (event.source == MidiSource::Guitar) {
                        updatePitch(message, true);
                    } else if (event.source == MidiSource::VoicePitch) {
                        updatePitch(message, false);
                    } else {
                        // VoiceAmp: skip pitch updates to avoid mixing inaccurate notes
                    }
                }
            }
            break;
        case EventType::PROGRAM_CHANGE:
            // Silence any sounding notes before switching programs to avoid stuck notes
            panicSilence();
            processProgramChange(event.programIndex);
            break;
        case EventType::TRANSPOSE_CHANGE:
            // Silence before changing transpose to ensure on/off pairs match
            panicSilence();
            m_transposeAmount.store(event.programIndex);
            {
                std::lock_guard<std::mutex> lock(m_logMutex);
                m_logQueue.push("Transpose set to: " + std::to_string(event.programIndex) + " semitones");
            }
            break;
        case EventType::TRACK_TOGGLE:
            if (m_trackStates.count(event.trackId)) {
                setTrackState(event.trackId, !m_trackStates.at(event.trackId));
            }
            break;
    }
}

void MidiProcessor::processProgramChange(int programIndex) {
    if (programIndex < 0 || programIndex >= m_preset.programs.size()) return;
    const auto& program = m_preset.programs[programIndex];
    m_currentProgramIndex = programIndex;

    if (program.programCC != -1 && program.programValue != -1) {
        std::vector<unsigned char> prog_msg = { 0xB0, (unsigned char)program.programCC, (unsigned char)program.programValue };
        safeSendMessage(prog_msg);
    }
    
    if (program.volumeCC != -1 && program.volumeValue != -1) {
        std::vector<unsigned char> vol_msg = { 0xB0, (unsigned char)program.volumeCC, (unsigned char)program.volumeValue };
        safeSendMessage(vol_msg);
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

void MidiProcessor::sendNoteToggle(int note, int channel, int velocity) {
    if (channel < 1 || channel > 16) return;
    unsigned char chan = channel - 1;
    std::vector<unsigned char> msg = {(unsigned char)(0x90 | chan), (unsigned char)note, (unsigned char)velocity};
    safeSendMessage(msg);
    msg[0] = (0x80 | chan); msg[2] = 0;
    safeSendMessage(msg);
}

void MidiProcessor::sendChannelAllNotesOff(int zeroBasedChannel) {
    if (zeroBasedChannel < 0 || zeroBasedChannel > 15) return;
    unsigned char chan = (unsigned char)zeroBasedChannel;
    // Sustain Off (CC64 = 0)
    std::vector<unsigned char> sustainOff = { (unsigned char)(0xB0 | chan), 64, 0 };
    safeSendMessage(sustainOff);
    // All Notes Off (CC123 = 0)
    std::vector<unsigned char> allNotesOff = { (unsigned char)(0xB0 | chan), 123, 0 };
    safeSendMessage(allNotesOff);
    // All Sound Off (CC120 = 0)
    std::vector<unsigned char> allSoundOff = { (unsigned char)(0xB0 | chan), 120, 0 };
    safeSendMessage(allSoundOff);
}

void MidiProcessor::panicSilence() {
    // Hard kill per note for channels 1 and 2 to be extra safe, then send CC kills
    for (int ch = 0; ch < 2; ++ch) {
        for (int n = 0; n < 128; ++n) {
            std::vector<unsigned char> offMsg = { (unsigned char)(0x80 | ch), (unsigned char)n, 0 };
            safeSendMessage(offMsg);
        }
        sendChannelAllNotesOff(ch);
    }
}

void MidiProcessor::updatePitch(const std::vector<unsigned char>& message, bool isGuitar) {
    // Defensive: some devices/routers can emit short MIDI packets (e.g., running status edge cases).
    // We must never crash the app because pitch tracking is "best effort".
    if (message.empty()) return;
    unsigned char status = message[0] & 0xF0;

    // Note on/off and pitch bend are 3-byte messages. If we don't have enough data, ignore.
    if ((status == 0x90 || status == 0x80 || status == 0xE0) && message.size() < 3) {
        return;
    }
    
    if (status == 0x90 && message[2] > 0) {
        int note = message[1];
        if (isGuitar) { m_lastGuitarNote = note; m_lastGuitarPitchHz = noteToFrequency(note); }
        else { m_lastVoiceNote = note; m_lastVoicePitchHz = noteToFrequency(note); }
    } else if (status == 0x80 || (status == 0x90 && message[2] == 0)) {
        int note = message[1];
        if (isGuitar && m_lastGuitarNote == note) { m_lastGuitarPitchHz = 0.0; }
        else if (!isGuitar && m_lastVoiceNote == note) { m_lastVoicePitchHz = 0.0; }
    } else if (status == 0xE0) {
        int bendValue = ((message[1] | (message[2] << 7))) - 8192;
        double centsOffset = (static_cast<double>(bendValue) / 8192.0) * 200.0;
        int baseNote = isGuitar ? m_lastGuitarNote : m_lastVoiceNote;
        if (baseNote != -1) {
            double bentFreq = noteToFrequency(baseNote) * pow(2.0, centsOffset / 1200.0);
            if (isGuitar) { m_lastGuitarPitchHz = bentFreq; }
            else { m_lastVoicePitchHz = bentFreq; }
        }
    } else {
        // Not a pitch-relevant message.
        return;
    }
    
    processPitchBend();
    emitPitchIfChanged(isGuitar);

    // PERFORMANCE FIX: Increased Hz threshold from 0.1 to 1.0 Hz.
    // The old 0.1 Hz threshold was too sensitive during live performance.
    // 1.0 Hz change is barely perceptible but reduces signal rate significantly.
    constexpr double hzThreshold = 1.0;
    if (isGuitar) {
        double hz = m_lastGuitarPitchHz;
        if ((m_lastEmittedGuitarHz < 0 && hz > 0) || (hz <= 0 && m_lastEmittedGuitarHz > 0) ||
            std::fabs(hz - m_lastEmittedGuitarHz) >= hzThreshold) {
            m_lastEmittedGuitarHz = hz;
            emit guitarHzUpdated(hz);
        }
    } else {
        double hz = m_lastVoicePitchHz;
        if ((m_lastEmittedVoiceHz < 0 && hz > 0) || (hz <= 0 && m_lastEmittedVoiceHz > 0) ||
            std::fabs(hz - m_lastEmittedVoiceHz) >= hzThreshold) {
            m_lastEmittedVoiceHz = hz;
            emit voiceHzUpdated(hz);
        }
    }
}

void MidiProcessor::precalculateRatios() {
    // Defensive: presets can accidentally set these to 0/negative; never allow UB downstream.
    const int deadZoneCents = std::max(0, m_preset.settings.pitchBendDeadZoneCents);
    m_ratioUpDeadZone = pow(2.0, double(deadZoneCents) / 1200.0);
    m_ratioDownDeadZone = pow(2.0, -double(deadZoneCents) / 1200.0);
}

void MidiProcessor::processPitchBend() {
    double guitarHz = m_lastGuitarPitchHz;
    double voiceHz = m_lastVoicePitchHz;

    if (guitarHz <= 1.0 || voiceHz <= 1.0) {
        if (m_lastCC102Value != 0 || m_lastCC103Value != 0) {
            std::vector<unsigned char> msg102 = { 0xB0, (unsigned char)BEND_DOWN_CC, 0 };
            safeSendMessage(msg102);
            std::vector<unsigned char> msg103 = { 0xB0, (unsigned char)BEND_UP_CC, 0 };
            safeSendMessage(msg103);
            m_lastCC102Value = 0;
            m_lastCC103Value = 0;
        }
        return;
    }

    double currentRatio = voiceHz / guitarHz;
    if (!std::isfinite(currentRatio) || currentRatio <= 0.0) {
        return;
    }
    int cc102_val = 0;
    int cc103_val = 0;

    const int deadZoneCents = std::max(0, m_preset.settings.pitchBendDeadZoneCents);
    const double downRange = double(std::max(1, m_preset.settings.pitchBendDownRangeCents));
    const double upRange = double(std::max(1, m_preset.settings.pitchBendUpRangeCents));

    if (currentRatio < m_ratioDownDeadZone) {
        double diffCents = -1200.0 * log2(currentRatio);
        double deviation = diffCents - double(deadZoneCents);
        cc102_val = static_cast<int>((deviation / downRange) * 127.0);
    } else if (currentRatio > m_ratioUpDeadZone) {
        double diffCents = 1200.0 * log2(currentRatio);
        double deviation = diffCents - double(deadZoneCents);
        cc103_val = static_cast<int>((deviation / upRange) * 127.0);
    }
    
    cc102_val = std::min(127, std::max(0, cc102_val));
    cc103_val = std::min(127, std::max(0, cc103_val));

    if (cc102_val != m_lastCC102Value) {
        std::vector<unsigned char> msg = { 0xB0, (unsigned char)BEND_DOWN_CC, (unsigned char)cc102_val };
        safeSendMessage(msg);
        m_lastCC102Value = cc102_val;
    }
    if (cc103_val != m_lastCC103Value) {
        std::vector<unsigned char> msg = { 0xB0, (unsigned char)BEND_UP_CC, (unsigned char)cc103_val };
        safeSendMessage(msg);
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

void MidiProcessor::hzToNoteAndCents(double hz, int& noteOut, double& centsOut) const {
    if (hz <= 1.0) {
        noteOut = -1;
        centsOut = 0.0;
        return;
    }
    double n = 69.0 + 12.0 * log2(hz / 440.0);
    int nearest = static_cast<int>(std::llround(n));
    double nearestHz = noteToFrequency(nearest);
    double cents = 1200.0 * log2(hz / nearestHz);
    // Constrain cents to [-50, 50] by construction (nearest note), but clamp for safety
    if (cents > 50.0) cents = 50.0;
    if (cents < -50.0) cents = -50.0;
    noteOut = nearest;
    centsOut = cents;
}

void MidiProcessor::emitPitchIfChanged(bool isGuitar) {
    int note;
    double cents;
    const double hz = isGuitar ? m_lastGuitarPitchHz : m_lastVoicePitchHz;
    hzToNoteAndCents(hz, note, cents);

    // PERFORMANCE FIX: Increased threshold from 0.5 to 3.0 cents.
    // The old 0.5 cent threshold caused excessive signal emission during live performance
    // (pitch trackers can jitter sub-cent variations at 100+ Hz). 3 cents is still
    // perceptually "in tune" (JND for pitch is ~5-10 cents) while dramatically
    // reducing signal rate. This prevents UI thread congestion that was causing
    // playback lag during guitar+voice performance.
    constexpr double centsThreshold = 3.0;
    if (isGuitar) {
        if (note != m_lastEmittedGuitarNote || std::fabs(cents - m_lastEmittedGuitarCents) >= centsThreshold) {
            m_lastEmittedGuitarNote = note;
            m_lastEmittedGuitarCents = cents;
            emit guitarPitchUpdated(note, cents);
        }
    } else {
        if (note != m_lastEmittedVoiceNote || std::fabs(cents - m_lastEmittedVoiceCents) >= centsThreshold) {
            m_lastEmittedVoiceNote = note;
            m_lastEmittedVoiceCents = cents;
            emit voicePitchUpdated(note, cents);
        }
    }
}