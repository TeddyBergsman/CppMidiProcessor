#pragma once

#include <QMainWindow>
#include <array>
#include "playback/ScaleSnapProcessor.h"

class QComboBox;
class QCheckBox;
class QLabel;
class QGroupBox;
class MidiProcessor;

namespace playback { class VirtuosoBalladMvpPlaybackEngine; }

/**
 * SnappingWindow - Settings window for scale/chord snapping behavior
 *
 * Provides controls for configuring how guitar notes are snapped to scale/chord tones
 * and harmony generation. Accessible via Window -> Snapping menu.
 *
 * Multi-voice harmony: 4 independent harmony voices on MIDI channels 12-15.
 * Each voice has its own Mode (Off/Smart Thirds/Contrary/Similar/Oblique) and
 * Range (instrument range constraint) settings.
 */
class SnappingWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit SnappingWindow(QWidget* parent = nullptr);

    // Set the playback engine to control (must be called before showing)
    void setPlaybackEngine(playback::VirtuosoBalladMvpPlaybackEngine* engine);

private slots:
    void onLeadModeChanged(int index);
    void onVocalBendToggled(bool checked);
    void onVocalVibratoRangeChanged(int index);
    void onVibratoCorrectionToggled(bool checked);
    void onVoiceSustainToggled(bool checked);
    void onEngineLeadModeChanged(playback::ScaleSnapProcessor::LeadMode mode);
    void onEngineHarmonyModeChanged(playback::HarmonyMode mode);
    void onEngineVocalBendChanged(bool enabled);
    void onEngineVocalVibratoRangeChanged(double cents);
    void onEngineVibratoCorrectionChanged(bool enabled);
    void onEngineVoiceSustainChanged(bool enabled);

    // Multi-voice harmony slots
    void onVoiceModeChanged(int voiceIndex, int modeComboIndex);
    void onVoiceRangeChanged(int voiceIndex, int rangeComboIndex);

private:
    void buildUi();
    void updateLeadModeDescription();
    void syncMultiVoiceUiToEngine();

    playback::VirtuosoBalladMvpPlaybackEngine* m_engine = nullptr;

    QComboBox* m_leadModeCombo = nullptr;
    QCheckBox* m_vocalBendCheckbox = nullptr;
    QComboBox* m_vocalVibratoRangeCombo = nullptr;
    QCheckBox* m_vibratoCorrectionCheckbox = nullptr;
    QCheckBox* m_voiceSustainCheckbox = nullptr;
    QLabel* m_leadDescriptionLabel = nullptr;

    // Multi-voice harmony UI elements (4 voices, channels 12-15)
    QGroupBox* m_harmonyGroup = nullptr;
    std::array<QComboBox*, 4> m_voiceModeCombo = {nullptr, nullptr, nullptr, nullptr};
    std::array<QComboBox*, 4> m_voiceRangeCombo = {nullptr, nullptr, nullptr, nullptr};
};
