#pragma once

#include <QDialog>
#include "PresetData.h"

QT_BEGIN_NAMESPACE
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QPushButton;
QT_END_NAMESPACE

// Non-modal editor for the audio-track radio-button switching map.
// Row = one audio track in Logic; Columns = Name, Switch Value, Mute CC.
// Emits mapChanged(cc, entries) whenever the user edits anything valid.
// MainWindow wires that signal to MidiProcessor::setAudioTrackSwitch and
// to QSettings persistence.
class AudioTrackSwitchEditor : public QDialog {
    Q_OBJECT

public:
    explicit AudioTrackSwitchEditor(int initialSwitchCC,
                                    const QList<AudioTrackMute>& initialEntries,
                                    QList<AudioTrackMute> presetDefaults,
                                    int presetDefaultSwitchCC,
                                    QWidget* parent = nullptr);

    int switchCC() const;
    QList<AudioTrackMute> entries() const;

signals:
    // Fired after every committed edit (cell change, add/remove, CC change, reset).
    void mapChanged(int switchCC, const QList<AudioTrackMute>& entries);

private slots:
    void onSwitchCCChanged(int value);
    void onCellChanged(int row, int col);
    void onAddRow();
    void onRemoveSelected();
    void onResetToPresetDefaults();

private:
    void buildUi();
    void populateRow(int row, const AudioTrackMute& entry);
    AudioTrackMute readRow(int row) const;
    void emitMapChanged();
    void setSilentCell(int row, int col, const QString& text);

    QSpinBox* m_switchCCSpin = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_resetButton = nullptr;
    QPushButton* m_closeButton = nullptr;

    // Cached defaults (from preset.xml) for the Reset button.
    QList<AudioTrackMute> m_presetDefaults;
    int m_presetDefaultSwitchCC;

    // Guard to suppress cellChanged signals while we populate programmatically.
    bool m_suppressCellChange = false;
};
