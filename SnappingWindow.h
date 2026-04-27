#pragma once

#include <QMainWindow>
#include <array>
#include "playback/ScaleSnapProcessor.h"

class QComboBox;
class QCheckBox;
class QLabel;
class QGroupBox;
class QSlider;
class QSpinBox;
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
    // Performance mode: set ScaleSnapProcessor directly (no full engine needed)
    void setScaleSnapProcessor(playback::ScaleSnapProcessor* snap);

private slots:
    void onLeadModeChanged(int index);
    void onVocalBendToggled(bool checked);
    void onVocalVibratoRangeChanged(int index);
    void onVibratoCorrectionToggled(bool checked);
    void onHarmonyVibratoToggled(bool checked);
    void onHarmonyHumanizationToggled(bool checked);
    void onVoiceSustainToggled(bool checked);
    void onSustainSmoothingToggled(bool checked);
    void onSustainSmoothingMsChanged(int value);
    void onReleaseBendPreventionToggled(bool checked);
    void onVoiceSustainThresholdChanged(int value);
    void onEngineLeadModeChanged(playback::ScaleSnapProcessor::LeadMode mode);
    void onEngineHarmonyModeChanged(playback::HarmonyMode mode);
    void onEngineVocalBendChanged(bool enabled);
    void onEngineVocalVibratoRangeChanged(double cents);
    void onEngineVibratoCorrectionChanged(bool enabled);
    void onEngineHarmonyVibratoChanged(bool enabled);
    void onEngineHarmonyHumanizationChanged(bool enabled);
    void onEngineVoiceSustainChanged(bool enabled);
    void onEngineSustainSmoothingChanged(bool enabled);
    void onEngineSustainSmoothingMsChanged(int ms);
    void onEngineReleaseBendPreventionChanged(bool enabled);
    void onEngineVoiceSustainThresholdChanged(int threshold);

    // Multi-voice harmony slots
    void onVoiceModeChanged(int voiceIndex, int modeComboIndex);
    void onVoiceRangeChanged(int voiceIndex, int rangeComboIndex);
    void onVoiceValueChanged(int voiceIndex, int value);
    void onVoicingPresetChanged(int comboIdx);

private:
    void buildUi();
    void updateLeadModeDescription();
    void syncMultiVoiceUiToEngine();

    // Helper: returns the active ScaleSnapProcessor from either direct or engine path
    playback::ScaleSnapProcessor* activeSnap() const;

    playback::VirtuosoBalladMvpPlaybackEngine* m_engine = nullptr;
    playback::ScaleSnapProcessor* m_directSnap = nullptr;

    QComboBox* m_leadModeCombo = nullptr;
    QCheckBox* m_vocalBendCheckbox = nullptr;
    QComboBox* m_vocalVibratoRangeCombo = nullptr;
    QCheckBox* m_vibratoCorrectionCheckbox = nullptr;
    QCheckBox* m_harmonyVibratoCheckbox = nullptr;
    QCheckBox* m_harmonyHumanizationCheckbox = nullptr;
    QCheckBox* m_voiceSustainCheckbox = nullptr;
    QCheckBox* m_sustainSmoothingCheckbox = nullptr;
    QSlider* m_sustainSmoothingSlider = nullptr;
    QLabel* m_sustainSmoothingLabel = nullptr;
    QCheckBox* m_releaseBendPreventionCheckbox = nullptr;
    QCheckBox* m_octaveGuardCheckbox = nullptr;
    QSlider* m_voiceSustainThresholdSlider = nullptr;
    QLabel* m_voiceSustainThresholdLabel = nullptr;
    QLabel* m_leadDescriptionLabel = nullptr;

    // Glissando UI elements (VocalSync mode only)
    QGroupBox* m_glissandoGroup = nullptr;
    QCheckBox* m_glissandoEnabledCheckbox = nullptr;
    QSlider* m_glissandoRateSlider = nullptr;
    QLabel* m_glissandoRateLabel = nullptr;
    QSlider* m_glissandoThresholdSlider = nullptr;
    QLabel* m_glissandoThresholdLabel = nullptr;
    QComboBox* m_glissandoCurveCombo = nullptr;

    // Multi-voice harmony UI elements (4 voices, channels 12-15)
    QGroupBox* m_harmonyGroup = nullptr;
    std::array<QComboBox*, 4> m_voiceModeCombo = {nullptr, nullptr, nullptr, nullptr};
    std::array<QComboBox*, 4> m_voiceRangeCombo = {nullptr, nullptr, nullptr, nullptr};
    // Per-voice numeric value: semitones for PARALLEL_FIXED, octave for DRONE.
    // Disabled (greyed) when the current mode doesn't use it.
    std::array<QSpinBox*, 4>  m_voiceValueSpin  = {nullptr, nullptr, nullptr, nullptr};

    // Voicing-preset selector (one-click setups for the 4 harmony voices).
    // See kHarmonyPresets in SnappingWindow.cpp.
    QComboBox* m_voicingPresetCombo = nullptr;
};
