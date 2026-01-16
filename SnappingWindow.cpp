#include "SnappingWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>

#include "playback/VirtuosoBalladMvpPlaybackEngine.h"

SnappingWindow::SnappingWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Snapping Settings");
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(400, 250);

    buildUi();
}

void SnappingWindow::setPlaybackEngine(playback::VirtuosoBalladMvpPlaybackEngine* engine)
{
    m_engine = engine;
    if (!m_engine) return;

    // Sync UI to current engine state
    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        if (m_modeCombo) {
            const int modeInt = static_cast<int>(snap->mode());
            for (int i = 0; i < m_modeCombo->count(); ++i) {
                if (m_modeCombo->itemData(i).toInt() == modeInt) {
                    m_modeCombo->setCurrentIndex(i);
                    break;
                }
            }
        }

        if (m_vocalBendCheckbox) {
            m_vocalBendCheckbox->setChecked(snap->vocalBendEnabled());
        }

        if (m_vocalVibratoRangeCombo) {
            // Match combo index to current cents value
            const double cents = snap->vocalVibratoRangeCents();
            for (int i = 0; i < m_vocalVibratoRangeCombo->count(); ++i) {
                if (qAbs(m_vocalVibratoRangeCombo->itemData(i).toDouble() - cents) < 1.0) {
                    m_vocalVibratoRangeCombo->setCurrentIndex(i);
                    break;
                }
            }
        }

        if (m_vibratoCorrectionCheckbox) {
            m_vibratoCorrectionCheckbox->setChecked(snap->vibratoCorrectionEnabled());
        }

        if (m_voiceSustainCheckbox) {
            m_voiceSustainCheckbox->setChecked(snap->voiceSustainEnabled());
        }

        // Connect to changes from the processor (in case it changes elsewhere)
        connect(snap, &playback::ScaleSnapProcessor::modeChanged,
                this, &SnappingWindow::onEngineModeChanged,
                Qt::UniqueConnection);
        connect(snap, &playback::ScaleSnapProcessor::vocalBendEnabledChanged,
                this, &SnappingWindow::onEngineVocalBendChanged,
                Qt::UniqueConnection);
        connect(snap, &playback::ScaleSnapProcessor::vocalVibratoRangeCentsChanged,
                this, &SnappingWindow::onEngineVocalVibratoRangeChanged,
                Qt::UniqueConnection);
        connect(snap, &playback::ScaleSnapProcessor::vibratoCorrectionEnabledChanged,
                this, &SnappingWindow::onEngineVibratoCorrectionChanged,
                Qt::UniqueConnection);
        connect(snap, &playback::ScaleSnapProcessor::voiceSustainEnabledChanged,
                this, &SnappingWindow::onEngineVoiceSustainChanged,
                Qt::UniqueConnection);
    }

    updateModeDescription();
}

void SnappingWindow::buildUi()
{
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Mode selection group
    QGroupBox* modeGroup = new QGroupBox("Snap Mode", central);
    QVBoxLayout* modeLayout = new QVBoxLayout(modeGroup);
    modeLayout->setContentsMargins(12, 12, 12, 12);
    modeLayout->setSpacing(8);

    // Mode combo box
    QHBoxLayout* comboRow = new QHBoxLayout();
    comboRow->setSpacing(8);

    QLabel* modeLabel = new QLabel("Mode:", modeGroup);
    m_modeCombo = new QComboBox(modeGroup);
    m_modeCombo->addItem("Off", static_cast<int>(playback::ScaleSnapProcessor::Mode::Off));
    m_modeCombo->addItem("Original", static_cast<int>(playback::ScaleSnapProcessor::Mode::Original));
    m_modeCombo->addItem("As Played", static_cast<int>(playback::ScaleSnapProcessor::Mode::AsPlayed));
    m_modeCombo->addItem("Harmony", static_cast<int>(playback::ScaleSnapProcessor::Mode::Harmony));
    m_modeCombo->addItem("Both", static_cast<int>(playback::ScaleSnapProcessor::Mode::AsPlayedPlusHarmony));
    m_modeCombo->setMinimumWidth(180);

    comboRow->addWidget(modeLabel);
    comboRow->addWidget(m_modeCombo);
    comboRow->addStretch();
    modeLayout->addLayout(comboRow);

    // Description label
    m_descriptionLabel = new QLabel(modeGroup);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setStyleSheet("QLabel { color: #888; padding: 8px; background: #222; border-radius: 4px; }");
    m_descriptionLabel->setMinimumHeight(60);
    modeLayout->addWidget(m_descriptionLabel);

    mainLayout->addWidget(modeGroup);

    // Vocal bend checkbox (outside mode group)
    m_vocalBendCheckbox = new QCheckBox("Apply Vocal Vibrato as Pitch Bend", central);
    m_vocalBendCheckbox->setToolTip("When enabled, transfers vocal pitch variations to MIDI pitch bend on output channels 11/12.\nThis adds expressiveness by modulating the snapped/harmony notes with your voice.");
    mainLayout->addWidget(m_vocalBendCheckbox);

    // Vocal vibrato range combo
    QHBoxLayout* vibratoRow = new QHBoxLayout();
    vibratoRow->setSpacing(8);
    QLabel* vibratoLabel = new QLabel("Vocal Vibrato Range:", central);
    m_vocalVibratoRangeCombo = new QComboBox(central);
    m_vocalVibratoRangeCombo->addItem("±200 cents (default)", 200.0);
    m_vocalVibratoRangeCombo->addItem("±100 cents", 100.0);
    m_vocalVibratoRangeCombo->setToolTip("Maximum vocal pitch deviation that affects pitch bend.\n±200 cents = ±2 semitones, ±100 cents = ±1 semitone.");
    vibratoRow->addWidget(vibratoLabel);
    vibratoRow->addWidget(m_vocalVibratoRangeCombo);
    vibratoRow->addStretch();
    mainLayout->addLayout(vibratoRow);

    // Vibrato correction checkbox
    m_vibratoCorrectionCheckbox = new QCheckBox("Vibrato Correction", central);
    m_vibratoCorrectionCheckbox->setToolTip("Filters out pitch drift from the voice signal, keeping only the vibrato oscillation.\nThis keeps the output perfectly centered around the guitar note, even if you sing slightly flat or sharp.");
    mainLayout->addWidget(m_vibratoCorrectionCheckbox);

    // Voice sustain checkbox
    m_voiceSustainCheckbox = new QCheckBox("Voice Sustain", central);
    m_voiceSustainCheckbox->setToolTip("Sustain guitar notes for as long as you're singing (CC2 active).\nNotes ring out even after the guitar string stops, allowing longer sustained tones controlled by your voice.");
    mainLayout->addWidget(m_voiceSustainCheckbox);

    mainLayout->addStretch();

    // Connect widgets
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SnappingWindow::onModeChanged);
    connect(m_vocalBendCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onVocalBendToggled);
    connect(m_vocalVibratoRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SnappingWindow::onVocalVibratoRangeChanged);
    connect(m_vibratoCorrectionCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onVibratoCorrectionToggled);
    connect(m_voiceSustainCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onVoiceSustainToggled);

    updateModeDescription();
}

void SnappingWindow::onModeChanged(int index)
{
    if (!m_engine || !m_modeCombo) return;

    const int modeInt = m_modeCombo->itemData(index).toInt();
    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        snap->setMode(static_cast<playback::ScaleSnapProcessor::Mode>(modeInt));
    }

    updateModeDescription();
}

void SnappingWindow::onVocalBendToggled(bool checked)
{
    if (!m_engine) return;

    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        snap->setVocalBendEnabled(checked);
    }
}

void SnappingWindow::onVocalVibratoRangeChanged(int index)
{
    if (!m_engine || !m_vocalVibratoRangeCombo) return;

    const double cents = m_vocalVibratoRangeCombo->itemData(index).toDouble();
    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        snap->setVocalVibratoRangeCents(cents);
    }
}

void SnappingWindow::onVibratoCorrectionToggled(bool checked)
{
    if (!m_engine) return;

    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        snap->setVibratoCorrectionEnabled(checked);
    }
}

void SnappingWindow::onVoiceSustainToggled(bool checked)
{
    if (!m_engine) return;

    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        snap->setVoiceSustainEnabled(checked);
    }
}

void SnappingWindow::onEngineModeChanged(playback::ScaleSnapProcessor::Mode mode)
{
    if (!m_modeCombo) return;

    // Update combo to match (avoid re-triggering onModeChanged)
    const int modeInt = static_cast<int>(mode);
    for (int i = 0; i < m_modeCombo->count(); ++i) {
        if (m_modeCombo->itemData(i).toInt() == modeInt) {
            if (m_modeCombo->currentIndex() != i) {
                m_modeCombo->blockSignals(true);
                m_modeCombo->setCurrentIndex(i);
                m_modeCombo->blockSignals(false);
            }
            break;
        }
    }

    updateModeDescription();
}

void SnappingWindow::onEngineVocalBendChanged(bool enabled)
{
    if (!m_vocalBendCheckbox) return;

    if (m_vocalBendCheckbox->isChecked() != enabled) {
        m_vocalBendCheckbox->blockSignals(true);
        m_vocalBendCheckbox->setChecked(enabled);
        m_vocalBendCheckbox->blockSignals(false);
    }
}

void SnappingWindow::onEngineVocalVibratoRangeChanged(double cents)
{
    if (!m_vocalVibratoRangeCombo) return;

    // Find matching combo index
    for (int i = 0; i < m_vocalVibratoRangeCombo->count(); ++i) {
        if (qAbs(m_vocalVibratoRangeCombo->itemData(i).toDouble() - cents) < 1.0) {
            if (m_vocalVibratoRangeCombo->currentIndex() != i) {
                m_vocalVibratoRangeCombo->blockSignals(true);
                m_vocalVibratoRangeCombo->setCurrentIndex(i);
                m_vocalVibratoRangeCombo->blockSignals(false);
            }
            break;
        }
    }
}

void SnappingWindow::onEngineVibratoCorrectionChanged(bool enabled)
{
    if (!m_vibratoCorrectionCheckbox) return;

    if (m_vibratoCorrectionCheckbox->isChecked() != enabled) {
        m_vibratoCorrectionCheckbox->blockSignals(true);
        m_vibratoCorrectionCheckbox->setChecked(enabled);
        m_vibratoCorrectionCheckbox->blockSignals(false);
    }
}

void SnappingWindow::onEngineVoiceSustainChanged(bool enabled)
{
    if (!m_voiceSustainCheckbox) return;

    if (m_voiceSustainCheckbox->isChecked() != enabled) {
        m_voiceSustainCheckbox->blockSignals(true);
        m_voiceSustainCheckbox->setChecked(enabled);
        m_voiceSustainCheckbox->blockSignals(false);
    }
}

void SnappingWindow::updateModeDescription()
{
    if (!m_descriptionLabel || !m_modeCombo) return;

    const int modeInt = m_modeCombo->currentData().toInt();
    const auto mode = static_cast<playback::ScaleSnapProcessor::Mode>(modeInt);

    QString desc;
    switch (mode) {
    case playback::ScaleSnapProcessor::Mode::Off:
        desc = "Snapping is disabled. Guitar notes are not duplicated to channels 11/12.";
        break;
    case playback::ScaleSnapProcessor::Mode::Original:
        desc = "Pass through guitar notes unchanged to channel 12 (duplicates channel 1 data including CC2).";
        break;
    case playback::ScaleSnapProcessor::Mode::AsPlayed:
        desc = "Snap guitar notes to the nearest scale/chord tone. Output on MIDI channel 12.";
        break;
    case playback::ScaleSnapProcessor::Mode::Harmony:
        desc = "Generate a harmony note (3rd, 4th, or 5th above) for each guitar note. Output on MIDI channel 12.";
        break;
    case playback::ScaleSnapProcessor::Mode::AsPlayedPlusHarmony:
        desc = "Output both snapped notes (channel 11) and harmony notes (channel 12) simultaneously.";
        break;
    }

    m_descriptionLabel->setText(desc);
}
