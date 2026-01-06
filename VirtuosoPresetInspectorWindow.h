#pragma once

#include <QMainWindow>

class QComboBox;
class QSpinBox;
class QTextEdit;
class QTableWidget;
class QLabel;
class QPushButton;
class QTimer;

class MidiProcessor;

#include "virtuoso/ui/GrooveTimelineWidget.h"
namespace virtuoso::groove { class GrooveRegistry; }

// A glass-box inspector for style presets:
// - Shows groove template offsets, per-instrument groove profiles, and articulation notes
// - Shows FluffyAudio Brushes drum mapping table
// - Shows planner tuning knobs (reference-track tuning) and a simple generated preview
class VirtuosoPresetInspectorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit VirtuosoPresetInspectorWindow(MidiProcessor* midi, QWidget* parent = nullptr);

private slots:
    void onPresetChanged();
    void onBpmChanged(int);
    void onGeneratePreview();
    void onAuditionStartStop();
    void onAuditionTick();
    void onTimelineEventClicked(const QString& lane, int note, int velocity, const QString& label);

private:
    void rebuildPresetCombo();
    void rebuildDrumMapTable();
    void refreshAll();

    void refreshPresetSummary();
    void refreshGrooveTemplateTable();
    void refreshInstrumentProfilesTable();
    void refreshReferenceTuningPanel();

    QString currentPresetKey() const;

    MidiProcessor* m_midi = nullptr; // not owned
    virtuoso::groove::GrooveRegistry* m_regOwned = nullptr;

    QComboBox* m_presetCombo = nullptr;
    QSpinBox* m_bpm = nullptr;
    QLabel* m_presetSummary = nullptr;

    QTableWidget* m_grooveOffsets = nullptr;
    QTableWidget* m_profiles = nullptr;
    QTableWidget* m_drumMap = nullptr;

    QTextEdit* m_tuningText = nullptr;

    // Visual preview + audition
    virtuoso::ui::GrooveTimelineWidget* m_timeline = nullptr;
    QPushButton* m_auditionBtn = nullptr;
    QTimer* m_auditionTimer = nullptr;
    qint64 m_auditionStartMs = 0;
    int m_previewBars = 4;
    int m_subdivPerBeat = 2;
    QVector<virtuoso::ui::GrooveTimelineWidget::LaneEvent> m_previewEvents;
};

