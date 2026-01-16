#pragma once

#include <QMainWindow>
#include "playback/ScaleSnapProcessor.h"

class QComboBox;
class QCheckBox;
class QLabel;
class MidiProcessor;

namespace playback { class VirtuosoBalladMvpPlaybackEngine; }

/**
 * SnappingWindow - Settings window for scale/chord snapping behavior
 *
 * Provides controls for configuring how guitar notes are snapped to scale/chord tones
 * and harmony generation. Accessible via Window -> Snapping menu.
 */
class SnappingWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit SnappingWindow(QWidget* parent = nullptr);

    // Set the playback engine to control (must be called before showing)
    void setPlaybackEngine(playback::VirtuosoBalladMvpPlaybackEngine* engine);

private slots:
    void onLeadModeChanged(int index);
    void onHarmonyModeChanged(int index);
    void onVocalBendToggled(bool checked);
    void onVocalVibratoRangeChanged(int index);
    void onVibratoCorrectionToggled(bool checked);
    void onVoiceSustainToggled(bool checked);
    void onEngineLeadModeChanged(playback::ScaleSnapProcessor::LeadMode mode);
    void onEngineHarmonyModeChanged(playback::ScaleSnapProcessor::HarmonyMode mode);
    void onEngineVocalBendChanged(bool enabled);
    void onEngineVocalVibratoRangeChanged(double cents);
    void onEngineVibratoCorrectionChanged(bool enabled);
    void onEngineVoiceSustainChanged(bool enabled);

private:
    void buildUi();
    void updateLeadModeDescription();
    void updateHarmonyModeDescription();

    playback::VirtuosoBalladMvpPlaybackEngine* m_engine = nullptr;

    QComboBox* m_leadModeCombo = nullptr;
    QComboBox* m_harmonyModeCombo = nullptr;
    QCheckBox* m_vocalBendCheckbox = nullptr;
    QComboBox* m_vocalVibratoRangeCombo = nullptr;
    QCheckBox* m_vibratoCorrectionCheckbox = nullptr;
    QCheckBox* m_voiceSustainCheckbox = nullptr;
    QLabel* m_leadDescriptionLabel = nullptr;
    QLabel* m_harmonyDescriptionLabel = nullptr;
};
