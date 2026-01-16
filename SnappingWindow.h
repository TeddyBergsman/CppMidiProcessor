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
    void onModeChanged(int index);
    void onVocalBendToggled(bool checked);
    void onVocalVibratoRangeChanged(int index);
    void onEngineModeChanged(playback::ScaleSnapProcessor::Mode mode);
    void onEngineVocalBendChanged(bool enabled);
    void onEngineVocalVibratoRangeChanged(double cents);

private:
    void buildUi();
    void updateModeDescription();

    playback::VirtuosoBalladMvpPlaybackEngine* m_engine = nullptr;

    QComboBox* m_modeCombo = nullptr;
    QCheckBox* m_vocalBendCheckbox = nullptr;
    QComboBox* m_vocalVibratoRangeCombo = nullptr;
    QLabel* m_descriptionLabel = nullptr;
};
