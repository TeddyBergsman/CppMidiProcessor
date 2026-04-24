#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class VocalSyncEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VocalSyncEditor(VocalSyncProcessor& processor)
        : AudioProcessorEditor(&processor), m_processor(processor)
    {
        setSize(300, 160);

        m_titleLabel.setText("VocalSync", juce::dontSendNotification);
        m_titleLabel.setFont(juce::Font(20.0f, juce::Font::bold));
        m_titleLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(m_titleLabel);

        m_statusLabel.setJustificationType(juce::Justification::centredLeft);
        m_statusLabel.setFont(juce::Font(14.0f));
        addAndMakeVisible(m_statusLabel);

        startTimerHz(15);
    }

    ~VocalSyncEditor() override { stopTimer(); }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff1a1a2e)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        m_titleLabel.setBounds(area.removeFromTop(30));
        area.removeFromTop(8);
        m_statusLabel.setBounds(area);
    }

private:
    void timerCallback() override
    {
        float shift = m_processor.getShiftSemitones();
        bool active = m_processor.isActive();
        bool connected = m_processor.isMidiConnected();

        juce::String status;
        status << "MIDI: " << (connected ? "Connected" : "Not connected") << "\n";
        if (active)
            status << "Status: Active  Shift: " << juce::String(shift, 0) << " st";
        else
            status << "Status: Passthrough";

        m_statusLabel.setText(status, juce::dontSendNotification);
    }

    VocalSyncProcessor& m_processor;
    juce::Label m_statusLabel;
    juce::Label m_titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalSyncEditor)
};
