#pragma once

#include <QDialog>
#include "PresetData.h"

QT_BEGIN_NAMESPACE
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QPushButton;
class QComboBox;
class QCheckBox;
class QLabel;
QT_END_NAMESPACE

// Bundled harmony state for the editor's "Harmony" section. Mirrors the
// QSettings-persisted state in MainWindow.
struct HarmonyEditorState {
    int toggleCC = 33;
    int rootStepCC = 34;
    int accidentalStepCC = 35;
    int qualityStepCC = 36;
    int rootIndex = 6;        // 0=C, 1=D, 2=E, 3=F, 4=G, 5=A, 6=B
    int accidentalIndex = 2;  // 0=natural, 1=sharp, 2=flat
    int qualityIndex = 0;     // 0=maj, 1=min, 2=dim, 3=aug, 4=sus2, 5=sus4,
                              // 6=maj7, 7=m7, 8=7, 9=m7b5
    bool enabledOnStartup = false;
    bool speakChanges = true;
};

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
                                    const HarmonyEditorState& initialHarmony,
                                    QWidget* parent = nullptr);

    int switchCC() const;
    QList<AudioTrackMute> entries() const;
    HarmonyEditorState harmonyState() const;

    // Two-way binding: when a footswitch steps the chord (or toggles harmony),
    // MainWindow calls this to mirror the live state into the dropdowns and
    // the live-state label without echoing back as a user edit.
    void setLiveChord(int rootIdx, int accidentalIdx, int qualityIdx);
    void setLiveHarmonyEnabled(bool enabled);
    // Plain-text summary of the scale currently driving harmonies, e.g.
    // "B♭ Ionian (Major) — B♭ C D E♭ F G A". Empty hides the label.
    void setLiveScaleSummary(const QString& summary);

signals:
    // Fired after every committed edit (cell change, add/remove, CC change, reset).
    void mapChanged(int switchCC, const QList<AudioTrackMute>& entries);

    // Harmony-section signals (one per logical setting so MainWindow can
    // persist + push to the engine without re-parsing a giant payload).
    void harmonyCCsChanged(int toggleCC, int rootStepCC, int accStepCC, int qualityStepCC);
    void defaultChordChanged(int rootIdx, int accidentalIdx, int qualityIdx);
    void enabledOnStartupChanged(bool enabled);
    void speakChangesChanged(bool enabled);

private slots:
    void onSwitchCCChanged(int value);
    void onCellChanged(int row, int col);
    void onAddRow();
    void onRemoveSelected();
    void onResetToPresetDefaults();
    void onHarmonyCCChanged();
    void onChordDropdownChanged();
    void onEnabledOnStartupToggled(bool checked);
    void onSpeakChangesToggled(bool checked);

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

    // Harmony section widgets.
    QSpinBox* m_harmonyToggleCCSpin = nullptr;
    QSpinBox* m_harmonyRootStepCCSpin = nullptr;
    QSpinBox* m_harmonyAccStepCCSpin = nullptr;
    QSpinBox* m_harmonyQualityStepCCSpin = nullptr;
    QComboBox* m_rootCombo = nullptr;
    QComboBox* m_accidentalCombo = nullptr;
    QComboBox* m_qualityCombo = nullptr;
    QCheckBox* m_enabledOnStartupCheck = nullptr;
    QCheckBox* m_speakChangesCheck = nullptr;
    QLabel* m_liveChordLabel = nullptr;
    QLabel* m_liveStateLabel = nullptr;
    QLabel* m_liveScaleLabel = nullptr;

    // Cached defaults (from preset.xml) for the Reset button.
    QList<AudioTrackMute> m_presetDefaults;
    int m_presetDefaultSwitchCC;

    // Guard to suppress cellChanged / dropdown-changed signals while we
    // populate programmatically.
    bool m_suppressCellChange = false;
    bool m_suppressHarmonySignals = false;
};
