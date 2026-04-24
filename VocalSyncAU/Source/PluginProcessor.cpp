#include "PluginProcessor.h"
#include "PluginEditor.h"

VocalSyncProcessor::VocalSyncProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::mono(), true)
                     .withOutput("Output", juce::AudioChannelSet::mono(), true))
{
    // Connect to the dedicated VocalSync IAC bus
    auto devices = juce::MidiInput::getAvailableDevices();
    for (const auto& device : devices) {
        if (device.name.containsIgnoreCase("MG3 Bass")) {
            m_midiInput = juce::MidiInput::openDevice(device.identifier, this);
            if (m_midiInput)
                m_midiInput->start();
            break;
        }
    }
}

VocalSyncProcessor::~VocalSyncProcessor()
{
    if (m_midiInput) {
        m_midiInput->stop();
        m_midiInput.reset();
    }
}

void VocalSyncProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    m_pitchShifter.prepare(sampleRate, samplesPerBlock);
    m_inputCopy.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
    m_active.store(false);
    m_shiftSemitones.store(0.0f);
}

void VocalSyncProcessor::releaseResources() {}

void VocalSyncProcessor::handleIncomingMidiMessage(juce::MidiInput*,
                                                    const juce::MidiMessage& message)
{
    // Pitch bend = shift amount with 14-bit precision (center 8192 = no shift, ±24 semitones)
    if (message.isPitchWheel()) {
        float normalized = (static_cast<float>(message.getPitchWheelValue()) - 8192.0f) / 8192.0f;
        m_shiftSemitones.store(normalized * 24.0f);
        m_active.store(true);
    }
}

void VocalSyncProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    midiMessages.clear();

    auto* channelData = buffer.getWritePointer(0);
    int numSamples = buffer.getNumSamples();

    float semitones = m_shiftSemitones.load();

    if (static_cast<int>(m_inputCopy.size()) < numSamples)
        m_inputCopy.resize(static_cast<size_t>(numSamples));
    std::copy(channelData, channelData + numSamples, m_inputCopy.data());

    m_pitchShifter.process(m_inputCopy.data(), channelData, numSamples, semitones);
}

juce::AudioProcessorEditor* VocalSyncProcessor::createEditor()
{
    return new VocalSyncEditor(*this);
}

void VocalSyncProcessor::getStateInformation(juce::MemoryBlock&) {}
void VocalSyncProcessor::setStateInformation(const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalSyncProcessor();
}
