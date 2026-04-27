#include "AudioTrackSwitchEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
constexpr int kColName = 0;
constexpr int kColSwitchValue = 1;
constexpr int kColMuteCC = 2;

// Roots, accidentals, qualities — kept short here, matched by index across
// the editor and MainWindow. (Updates here require matching changes in
// MainWindow's chord-builder.)
const QStringList kRootLabels   = { "C", "D", "E", "F", "G", "A", "B" };
// Order: ♮ → ♭ → ♯ → ♮ … (flat before sharp; matches per-feedback ordering).
const QStringList kAccidentals  = { QString::fromUtf8("\u266E"),    // ♮
                                    QString::fromUtf8("\u266D"),    // ♭
                                    QString::fromUtf8("\u266F") };  // ♯
const QStringList kQualityLabels = {
    "maj", "min", "dim", "aug", "sus2", "sus4", "maj7", "m7", "7", "m7♭5"
};
}

AudioTrackSwitchEditor::AudioTrackSwitchEditor(int initialSwitchCC,
                                               const QList<AudioTrackMute>& initialEntries,
                                               QList<AudioTrackMute> presetDefaults,
                                               int presetDefaultSwitchCC,
                                               const HarmonyEditorState& initialHarmony,
                                               QWidget* parent)
    : QDialog(parent),
      m_presetDefaults(std::move(presetDefaults)),
      m_presetDefaultSwitchCC(presetDefaultSwitchCC) {
    setWindowTitle("Audio Track Switch");
    setModal(false);
    resize(620, 540);

    buildUi();

    m_switchCCSpin->blockSignals(true);
    m_switchCCSpin->setValue(initialSwitchCC);
    m_switchCCSpin->blockSignals(false);

    m_suppressCellChange = true;
    m_table->setRowCount(initialEntries.size());
    for (int i = 0; i < initialEntries.size(); ++i) {
        populateRow(i, initialEntries[i]);
    }
    m_suppressCellChange = false;

    // Seed the harmony section without firing edit signals.
    m_suppressHarmonySignals = true;
    m_harmonyToggleCCSpin->setValue(initialHarmony.toggleCC);
    m_harmonyRootStepCCSpin->setValue(initialHarmony.rootStepCC);
    m_harmonyAccStepCCSpin->setValue(initialHarmony.accidentalStepCC);
    m_harmonyQualityStepCCSpin->setValue(initialHarmony.qualityStepCC);
    m_rootCombo->setCurrentIndex(initialHarmony.rootIndex);
    m_accidentalCombo->setCurrentIndex(initialHarmony.accidentalIndex);
    m_qualityCombo->setCurrentIndex(initialHarmony.qualityIndex);
    m_enabledOnStartupCheck->setChecked(initialHarmony.enabledOnStartup);
    m_speakChangesCheck->setChecked(initialHarmony.speakChanges);
    m_suppressHarmonySignals = false;

    setLiveChord(initialHarmony.rootIndex, initialHarmony.accidentalIndex,
                 initialHarmony.qualityIndex);
    setLiveHarmonyEnabled(initialHarmony.enabledOnStartup);
}

int AudioTrackSwitchEditor::switchCC() const {
    return m_switchCCSpin->value();
}

QList<AudioTrackMute> AudioTrackSwitchEditor::entries() const {
    QList<AudioTrackMute> out;
    out.reserve(m_table->rowCount());
    for (int r = 0; r < m_table->rowCount(); ++r) {
        out.append(readRow(r));
    }
    return out;
}

HarmonyEditorState AudioTrackSwitchEditor::harmonyState() const {
    HarmonyEditorState s;
    s.toggleCC = m_harmonyToggleCCSpin->value();
    s.rootStepCC = m_harmonyRootStepCCSpin->value();
    s.accidentalStepCC = m_harmonyAccStepCCSpin->value();
    s.qualityStepCC = m_harmonyQualityStepCCSpin->value();
    s.rootIndex = m_rootCombo->currentIndex();
    s.accidentalIndex = m_accidentalCombo->currentIndex();
    s.qualityIndex = m_qualityCombo->currentIndex();
    s.enabledOnStartup = m_enabledOnStartupCheck->isChecked();
    s.speakChanges = m_speakChangesCheck->isChecked();
    return s;
}

void AudioTrackSwitchEditor::setLiveChord(int rootIdx, int accidentalIdx, int qualityIdx) {
    m_suppressHarmonySignals = true;
    if (rootIdx >= 0 && rootIdx < kRootLabels.size()) m_rootCombo->setCurrentIndex(rootIdx);
    if (accidentalIdx >= 0 && accidentalIdx < kAccidentals.size()) m_accidentalCombo->setCurrentIndex(accidentalIdx);
    if (qualityIdx >= 0 && qualityIdx < kQualityLabels.size()) m_qualityCombo->setCurrentIndex(qualityIdx);
    m_suppressHarmonySignals = false;

    // Update the human-readable live-chord label, e.g. "B♭ maj".
    QString text = kRootLabels.value(rootIdx);
    if (accidentalIdx > 0) text += kAccidentals.value(accidentalIdx);
    text += " " + kQualityLabels.value(qualityIdx);
    m_liveChordLabel->setText(text);
}

void AudioTrackSwitchEditor::setLiveHarmonyEnabled(bool enabled) {
    m_liveStateLabel->setText(enabled ? "Harmonies: ON" : "Harmonies: OFF");
    m_liveStateLabel->setStyleSheet(enabled
        ? "QLabel { color: #6c3; font-weight: bold; }"
        : "QLabel { color: #888; }");
}

void AudioTrackSwitchEditor::setLiveScaleSummary(const QString& summary) {
    if (!m_liveScaleLabel) return;
    if (summary.isEmpty()) {
        m_liveScaleLabel->setText("Scale: —");
    } else {
        m_liveScaleLabel->setText("Scale: " + summary);
    }
}

void AudioTrackSwitchEditor::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);

    // ============================================================
    // Harmony group (sits above the audio-track table).
    // ============================================================
    auto* harmonyBox = new QGroupBox("Harmony");
    auto* harmonyLayout = new QVBoxLayout(harmonyBox);

    // Live state row + checkboxes.
    auto* harmonyTopRow = new QHBoxLayout;
    m_liveStateLabel = new QLabel("Harmonies: OFF");
    m_liveStateLabel->setStyleSheet("QLabel { color: #888; }");
    m_enabledOnStartupCheck = new QCheckBox("Enable on app startup");
    m_speakChangesCheck = new QCheckBox("Speak chord changes");
    harmonyTopRow->addWidget(m_liveStateLabel);
    harmonyTopRow->addStretch(1);
    harmonyTopRow->addWidget(m_enabledOnStartupCheck);
    harmonyTopRow->addWidget(m_speakChangesCheck);
    harmonyLayout->addLayout(harmonyTopRow);

    // CC numbers (4 spinboxes in a grid).
    auto* ccGrid = new QGridLayout;
    auto makeCCSpin = [](const QString& tooltip) {
        auto* s = new QSpinBox;
        s->setRange(0, 127);
        s->setToolTip(tooltip);
        return s;
    };
    m_harmonyToggleCCSpin = makeCCSpin("Footswitch CC that toggles all harmonies on/off (default 33).");
    m_harmonyRootStepCCSpin = makeCCSpin("Footswitch CC that steps the root forward (default 34). Pressing also resets accidental → natural and quality → maj.");
    m_harmonyAccStepCCSpin = makeCCSpin("Footswitch CC that steps the accidental forward (♮ → ♯ → ♭ → …, default 35).");
    m_harmonyQualityStepCCSpin = makeCCSpin("Footswitch CC that steps the quality forward (default 36).");
    ccGrid->addWidget(new QLabel("Toggle CC:"),       0, 0);
    ccGrid->addWidget(m_harmonyToggleCCSpin,          0, 1);
    ccGrid->addWidget(new QLabel("Root step CC:"),    0, 2);
    ccGrid->addWidget(m_harmonyRootStepCCSpin,        0, 3);
    ccGrid->addWidget(new QLabel("Accidental step CC:"), 1, 0);
    ccGrid->addWidget(m_harmonyAccStepCCSpin,         1, 1);
    ccGrid->addWidget(new QLabel("Quality step CC:"), 1, 2);
    ccGrid->addWidget(m_harmonyQualityStepCCSpin,     1, 3);
    ccGrid->setColumnStretch(4, 1);
    harmonyLayout->addLayout(ccGrid);

    // Default chord dropdowns + live readout.
    auto* chordRow = new QHBoxLayout;
    chordRow->addWidget(new QLabel("Default chord:"));
    m_rootCombo = new QComboBox;
    m_rootCombo->addItems(kRootLabels);
    m_accidentalCombo = new QComboBox;
    m_accidentalCombo->addItems(kAccidentals);
    m_qualityCombo = new QComboBox;
    m_qualityCombo->addItems(kQualityLabels);
    chordRow->addWidget(m_rootCombo);
    chordRow->addWidget(m_accidentalCombo);
    chordRow->addWidget(m_qualityCombo);
    chordRow->addSpacing(12);
    chordRow->addWidget(new QLabel("Live:"));
    m_liveChordLabel = new QLabel("—");
    m_liveChordLabel->setStyleSheet("QLabel { font-weight: bold; }");
    chordRow->addWidget(m_liveChordLabel);
    chordRow->addStretch(1);
    harmonyLayout->addLayout(chordRow);

    // Scale summary label (debug aid): shows the actual scale being used
    // to conform harmonies, e.g. "Scale: B♭ Ionian (Major) — B♭ C D E♭ F G A".
    m_liveScaleLabel = new QLabel("Scale: —");
    m_liveScaleLabel->setStyleSheet("QLabel { color: #888; }");
    m_liveScaleLabel->setWordWrap(true);
    harmonyLayout->addWidget(m_liveScaleLabel);

    rootLayout->addWidget(harmonyBox);

    // ============================================================
    // Audio-track section.
    // ============================================================
    auto* audioBox = new QGroupBox("Audio Tracks");
    auto* audioLayout = new QVBoxLayout(audioBox);

    // Top row: switching CC number.
    auto* topRow = new QHBoxLayout;
    auto* ccLabel = new QLabel("Switching CC:");
    m_switchCCSpin = new QSpinBox;
    m_switchCCSpin->setRange(0, 127);
    m_switchCCSpin->setValue(27);
    m_switchCCSpin->setToolTip("The CC number the app listens to from the Ampero (default: 27).");
    topRow->addWidget(ccLabel);
    topRow->addWidget(m_switchCCSpin);
    topRow->addStretch(1);
    audioLayout->addLayout(topRow);

    // Explanatory caption.
    auto* caption = new QLabel(
        "Each row is one audio track in Logic. When the Ampero sends the "
        "Switching CC with a row's Switch Value, that track is unmuted and all "
        "others are muted.");
    caption->setWordWrap(true);
    caption->setStyleSheet("QLabel { color: #888; }");
    audioLayout->addWidget(caption);

    // Table of entries.
    m_table = new QTableWidget(0, 3, this);
    QStringList headers;
    headers << "Name" << "Switch Value" << "Mute CC";
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColSwitchValue, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColMuteCC, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    audioLayout->addWidget(m_table, /*stretch*/ 1);

    // Row-editing buttons.
    auto* rowButtons = new QHBoxLayout;
    m_addButton = new QPushButton("Add Track");
    m_removeButton = new QPushButton("Remove Selected");
    rowButtons->addWidget(m_addButton);
    rowButtons->addWidget(m_removeButton);
    rowButtons->addStretch(1);
    audioLayout->addLayout(rowButtons);

    rootLayout->addWidget(audioBox, /*stretch*/ 1);

    // Footer: Reset + Close (sit at dialog root, not inside the audio group).
    auto* footer = new QHBoxLayout;
    m_resetButton = new QPushButton("Reset Audio Tracks to Preset Defaults");
    m_closeButton = new QPushButton("Close");
    footer->addWidget(m_resetButton);
    footer->addStretch(1);
    footer->addWidget(m_closeButton);
    rootLayout->addLayout(footer);

    connect(m_switchCCSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &AudioTrackSwitchEditor::onSwitchCCChanged);
    connect(m_table, &QTableWidget::cellChanged,
            this, &AudioTrackSwitchEditor::onCellChanged);

    // Harmony connections.
    auto connectCCSpin = [this](QSpinBox* s) {
        connect(s, qOverload<int>(&QSpinBox::valueChanged),
                this, &AudioTrackSwitchEditor::onHarmonyCCChanged);
    };
    connectCCSpin(m_harmonyToggleCCSpin);
    connectCCSpin(m_harmonyRootStepCCSpin);
    connectCCSpin(m_harmonyAccStepCCSpin);
    connectCCSpin(m_harmonyQualityStepCCSpin);
    connect(m_rootCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AudioTrackSwitchEditor::onChordDropdownChanged);
    connect(m_accidentalCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AudioTrackSwitchEditor::onChordDropdownChanged);
    connect(m_qualityCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AudioTrackSwitchEditor::onChordDropdownChanged);
    connect(m_enabledOnStartupCheck, &QCheckBox::toggled,
            this, &AudioTrackSwitchEditor::onEnabledOnStartupToggled);
    connect(m_speakChangesCheck, &QCheckBox::toggled,
            this, &AudioTrackSwitchEditor::onSpeakChangesToggled);
    connect(m_addButton, &QPushButton::clicked,
            this, &AudioTrackSwitchEditor::onAddRow);
    connect(m_removeButton, &QPushButton::clicked,
            this, &AudioTrackSwitchEditor::onRemoveSelected);
    connect(m_resetButton, &QPushButton::clicked,
            this, &AudioTrackSwitchEditor::onResetToPresetDefaults);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);
}

void AudioTrackSwitchEditor::populateRow(int row, const AudioTrackMute& entry) {
    setSilentCell(row, kColName, entry.name);
    setSilentCell(row, kColSwitchValue, QString::number(entry.switchValue));
    setSilentCell(row, kColMuteCC, QString::number(entry.muteCC));
}

void AudioTrackSwitchEditor::setSilentCell(int row, int col, const QString& text) {
    auto* item = new QTableWidgetItem(text);
    m_table->setItem(row, col, item);
}

AudioTrackMute AudioTrackSwitchEditor::readRow(int row) const {
    AudioTrackMute out{};
    auto* nameItem = m_table->item(row, kColName);
    auto* svItem = m_table->item(row, kColSwitchValue);
    auto* muteItem = m_table->item(row, kColMuteCC);
    out.name = nameItem ? nameItem->text() : QString();
    out.switchValue = svItem ? svItem->text().toInt() : 0;
    out.muteCC = muteItem ? muteItem->text().toInt() : 0;
    // Clamp to valid CC range; the UI accepts typos but the runtime must be safe.
    if (out.switchValue < 0) out.switchValue = 0;
    if (out.switchValue > 127) out.switchValue = 127;
    if (out.muteCC < 0) out.muteCC = 0;
    if (out.muteCC > 127) out.muteCC = 127;
    return out;
}

void AudioTrackSwitchEditor::onSwitchCCChanged(int) {
    emitMapChanged();
}

void AudioTrackSwitchEditor::onCellChanged(int, int) {
    if (m_suppressCellChange) return;
    emitMapChanged();
}

void AudioTrackSwitchEditor::onAddRow() {
    const int row = m_table->rowCount();
    AudioTrackMute seed;
    seed.name = "New Track";
    // Seed the next switch value from the current max + 1, to reduce manual typing.
    int nextSwitch = 0;
    int nextMute = 0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const auto e = readRow(r);
        if (e.switchValue > nextSwitch) nextSwitch = e.switchValue;
        if (e.muteCC > nextMute) nextMute = e.muteCC;
    }
    seed.switchValue = std::min(127, nextSwitch + 1);
    seed.muteCC = std::min(127, nextMute + 1);

    m_suppressCellChange = true;
    m_table->insertRow(row);
    populateRow(row, seed);
    m_suppressCellChange = false;

    m_table->selectRow(row);
    m_table->editItem(m_table->item(row, kColName));
    emitMapChanged();
}

void AudioTrackSwitchEditor::onRemoveSelected() {
    const auto ranges = m_table->selectedRanges();
    if (ranges.isEmpty()) return;
    // SingleSelection mode: just remove the first selected row.
    const int row = ranges.first().topRow();
    m_suppressCellChange = true;
    m_table->removeRow(row);
    m_suppressCellChange = false;
    emitMapChanged();
}

void AudioTrackSwitchEditor::onResetToPresetDefaults() {
    m_switchCCSpin->blockSignals(true);
    m_switchCCSpin->setValue(m_presetDefaultSwitchCC);
    m_switchCCSpin->blockSignals(false);

    m_suppressCellChange = true;
    m_table->setRowCount(0);
    m_table->setRowCount(m_presetDefaults.size());
    for (int i = 0; i < m_presetDefaults.size(); ++i) {
        populateRow(i, m_presetDefaults[i]);
    }
    m_suppressCellChange = false;

    emitMapChanged();
}

void AudioTrackSwitchEditor::emitMapChanged() {
    emit mapChanged(switchCC(), entries());
}

void AudioTrackSwitchEditor::onHarmonyCCChanged() {
    if (m_suppressHarmonySignals) return;
    emit harmonyCCsChanged(m_harmonyToggleCCSpin->value(),
                           m_harmonyRootStepCCSpin->value(),
                           m_harmonyAccStepCCSpin->value(),
                           m_harmonyQualityStepCCSpin->value());
}

void AudioTrackSwitchEditor::onChordDropdownChanged() {
    if (m_suppressHarmonySignals) return;
    const int rootIdx = m_rootCombo->currentIndex();
    const int accIdx = m_accidentalCombo->currentIndex();
    const int qualIdx = m_qualityCombo->currentIndex();
    emit defaultChordChanged(rootIdx, accIdx, qualIdx);
    // Refresh the live label immediately for tight feedback.
    setLiveChord(rootIdx, accIdx, qualIdx);
}

void AudioTrackSwitchEditor::onEnabledOnStartupToggled(bool checked) {
    if (m_suppressHarmonySignals) return;
    emit enabledOnStartupChanged(checked);
}

void AudioTrackSwitchEditor::onSpeakChangesToggled(bool checked) {
    if (m_suppressHarmonySignals) return;
    emit speakChangesChanged(checked);
}
