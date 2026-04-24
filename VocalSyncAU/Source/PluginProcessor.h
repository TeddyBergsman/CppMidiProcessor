#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "PsolaProcessor.h"

/**
 * VocalSyncProcessor - AU plugin for guitar-controlled vocal pitch correction.
 *
 * Receives pitch shift via CC85 on a DEDICATED IAC Driver bus ("IAC Driver MG3 Bass")
 * that only carries VocalSync messages — no flooding from other MIDI traffic.
 *
 * Protocol:
 * - Note ON:  activate shifting
 * - Note OFF: deactivate (passthrough)
 * - CC 85:    shift in semitones (value + 24 offset, so 24=0, 0=-24, 48=+24)
 */
class VocalSyncProcessor : public juce::AudioProcessor,
                           private juce::MidiInputCallback
{
public:
    VocalSyncProcessor();
    ~VocalSyncProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VocalSync"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Diagnostics
    float getShiftSemitones() const { return m_shiftSemitones.load(); }
    bool isActive() const { return m_active.load(); }
    bool isMidiConnected() const { return m_midiInput != nullptr; }

private:
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    std::unique_ptr<juce::MidiInput> m_midiInput;

    // State (atomic: MIDI callback thread writes, audio thread reads)
    std::atomic<bool> m_active{false};
    std::atomic<float> m_shiftSemitones{0.0f};

    // DSP
    PitchShifter m_pitchShifter;
    std::vector<float> m_inputCopy;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalSyncProcessor)
};
