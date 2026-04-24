#include "AudioTrackSwitchEditor.h"

#include <QFormLayout>
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
}

AudioTrackSwitchEditor::AudioTrackSwitchEditor(int initialSwitchCC,
                                               const QList<AudioTrackMute>& initialEntries,
                                               QList<AudioTrackMute> presetDefaults,
                                               int presetDefaultSwitchCC,
                                               QWidget* parent)
    : QDialog(parent),
      m_presetDefaults(std::move(presetDefaults)),
      m_presetDefaultSwitchCC(presetDefaultSwitchCC) {
    setWindowTitle("Audio Track Switch");
    setModal(false);
    resize(520, 360);

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

void AudioTrackSwitchEditor::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);

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
    rootLayout->addLayout(topRow);

    // Explanatory caption.
    auto* caption = new QLabel(
        "Each row is one audio track in Logic. When the Ampero sends the "
        "Switching CC with a row's Switch Value, that track is unmuted and all "
        "others are muted.");
    caption->setWordWrap(true);
    caption->setStyleSheet("QLabel { color: #888; }");
    rootLayout->addWidget(caption);

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
    rootLayout->addWidget(m_table, /*stretch*/ 1);

    // Row-editing buttons.
    auto* rowButtons = new QHBoxLayout;
    m_addButton = new QPushButton("Add Track");
    m_removeButton = new QPushButton("Remove Selected");
    rowButtons->addWidget(m_addButton);
    rowButtons->addWidget(m_removeButton);
    rowButtons->addStretch(1);
    rootLayout->addLayout(rowButtons);

    // Footer: Reset + Close.
    auto* footer = new QHBoxLayout;
    m_resetButton = new QPushButton("Reset to Preset Defaults");
    m_closeButton = new QPushButton("Close");
    footer->addWidget(m_resetButton);
    footer->addStretch(1);
    footer->addWidget(m_closeButton);
    rootLayout->addLayout(footer);

    connect(m_switchCCSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &AudioTrackSwitchEditor::onSwitchCCChanged);
    connect(m_table, &QTableWidget::cellChanged,
            this, &AudioTrackSwitchEditor::onCellChanged);
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
