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
    resize(400, 350);

    buildUi();
}

void SnappingWindow::setPlaybackEngine(playback::VirtuosoBalladMvpPlaybackEngine* engine)
{
    m_engine = engine;
    if (!m_engine) return;

    // Sync UI to current engine state
    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        // Lead mode combo
        if (m_leadModeCombo) {
            const int modeInt = static_cast<int>(snap->leadMode());
            for (int i = 0; i < m_leadModeCombo->count(); ++i) {
                if (m_leadModeCombo->itemData(i).toInt() == modeInt) {
                    m_leadModeCombo->setCurrentIndex(i);
                    break;
                }
            }
        }

        // Harmony mode combo
        if (m_harmonyModeCombo) {
            const int modeInt = static_cast<int>(snap->harmonyMode());
            for (int i = 0; i < m_harmonyModeCombo->count(); ++i) {
                if (m_harmonyModeCombo->itemData(i).toInt() == modeInt) {
                    m_harmonyModeCombo->setCurrentIndex(i);
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
        connect(snap, &playback::ScaleSnapProcessor::leadModeChanged,
                this, &SnappingWindow::onEngineLeadModeChanged,
                Qt::UniqueConnection);
        connect(snap, &playback::ScaleSnapProcessor::harmonyModeChanged,
                this, &SnappingWindow::onEngineHarmonyModeChanged,
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

    updateLeadModeDescription();
    updateHarmonyModeDescription();
}

void SnappingWindow::buildUi()
{
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // ===== LEAD MODE GROUP =====
    QGroupBox* leadGroup = new QGroupBox("Lead (Channel 12)", central);
    QVBoxLayout* leadLayout = new QVBoxLayout(leadGroup);
    leadLayout->setContentsMargins(12, 12, 12, 12);
    leadLayout->setSpacing(8);

    // Lead mode combo box
    QHBoxLayout* leadComboRow = new QHBoxLayout();
    leadComboRow->setSpacing(8);

    QLabel* leadLabel = new QLabel("Mode:", leadGroup);
    m_leadModeCombo = new QComboBox(leadGroup);
    m_leadModeCombo->addItem("Off", static_cast<int>(playback::ScaleSnapProcessor::LeadMode::Off));
    m_leadModeCombo->addItem("Original", static_cast<int>(playback::ScaleSnapProcessor::LeadMode::Original));
    m_leadModeCombo->addItem("Constrained", static_cast<int>(playback::ScaleSnapProcessor::LeadMode::Constrained));
    m_leadModeCombo->setMinimumWidth(180);

    leadComboRow->addWidget(leadLabel);
    leadComboRow->addWidget(m_leadModeCombo);
    leadComboRow->addStretch();
    leadLayout->addLayout(leadComboRow);

    // Lead description label
    m_leadDescriptionLabel = new QLabel(leadGroup);
    m_leadDescriptionLabel->setWordWrap(true);
    m_leadDescriptionLabel->setStyleSheet("QLabel { color: #888; padding: 8px; background: #222; border-radius: 4px; }");
    m_leadDescriptionLabel->setMinimumHeight(50);
    leadLayout->addWidget(m_leadDescriptionLabel);

    mainLayout->addWidget(leadGroup);

    // ===== HARMONY MODE GROUP =====
    QGroupBox* harmonyGroup = new QGroupBox("Harmony (Channel 11)", central);
    QVBoxLayout* harmonyLayout = new QVBoxLayout(harmonyGroup);
    harmonyLayout->setContentsMargins(12, 12, 12, 12);
    harmonyLayout->setSpacing(8);

    // Harmony mode combo box
    QHBoxLayout* harmonyComboRow = new QHBoxLayout();
    harmonyComboRow->setSpacing(8);

    QLabel* harmonyLabel = new QLabel("Mode:", harmonyGroup);
    m_harmonyModeCombo = new QComboBox(harmonyGroup);
    m_harmonyModeCombo->addItem("Off", static_cast<int>(playback::ScaleSnapProcessor::HarmonyMode::Off));
    m_harmonyModeCombo->addItem("Smart Thirds", static_cast<int>(playback::ScaleSnapProcessor::HarmonyMode::SmartThirds));
    m_harmonyModeCombo->setMinimumWidth(180);

    harmonyComboRow->addWidget(harmonyLabel);
    harmonyComboRow->addWidget(m_harmonyModeCombo);
    harmonyComboRow->addStretch();
    harmonyLayout->addLayout(harmonyComboRow);

    // Harmony description label
    m_harmonyDescriptionLabel = new QLabel(harmonyGroup);
    m_harmonyDescriptionLabel->setWordWrap(true);
    m_harmonyDescriptionLabel->setStyleSheet("QLabel { color: #888; padding: 8px; background: #222; border-radius: 4px; }");
    m_harmonyDescriptionLabel->setMinimumHeight(50);
    harmonyLayout->addWidget(m_harmonyDescriptionLabel);

    mainLayout->addWidget(harmonyGroup);

    // ===== VOCAL SETTINGS =====
    // Vocal bend checkbox
    m_vocalBendCheckbox = new QCheckBox("Apply Vocal Vibrato as Pitch Bend", central);
    m_vocalBendCheckbox->setToolTip("When enabled, transfers vocal pitch variations to MIDI pitch bend on output channels 11/12.\nThis adds expressiveness by modulating the lead/harmony notes with your voice.");
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
    connect(m_leadModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SnappingWindow::onLeadModeChanged);
    connect(m_harmonyModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SnappingWindow::onHarmonyModeChanged);
    connect(m_vocalBendCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onVocalBendToggled);
    connect(m_vocalVibratoRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SnappingWindow::onVocalVibratoRangeChanged);
    connect(m_vibratoCorrectionCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onVibratoCorrectionToggled);
    connect(m_voiceSustainCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onVoiceSustainToggled);

    updateLeadModeDescription();
    updateHarmonyModeDescription();
}

void SnappingWindow::onLeadModeChanged(int index)
{
    if (!m_engine || !m_leadModeCombo) return;

    const int modeInt = m_leadModeCombo->itemData(index).toInt();
    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        snap->setLeadMode(static_cast<playback::ScaleSnapProcessor::LeadMode>(modeInt));
    }

    updateLeadModeDescription();
}

void SnappingWindow::onHarmonyModeChanged(int index)
{
    if (!m_engine || !m_harmonyModeCombo) return;

    const int modeInt = m_harmonyModeCombo->itemData(index).toInt();
    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        snap->setHarmonyMode(static_cast<playback::ScaleSnapProcessor::HarmonyMode>(modeInt));
    }

    updateHarmonyModeDescription();
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

void SnappingWindow::onEngineLeadModeChanged(playback::ScaleSnapProcessor::LeadMode mode)
{
    if (!m_leadModeCombo) return;

    // Update combo to match (avoid re-triggering onLeadModeChanged)
    const int modeInt = static_cast<int>(mode);
    for (int i = 0; i < m_leadModeCombo->count(); ++i) {
        if (m_leadModeCombo->itemData(i).toInt() == modeInt) {
            if (m_leadModeCombo->currentIndex() != i) {
                m_leadModeCombo->blockSignals(true);
                m_leadModeCombo->setCurrentIndex(i);
                m_leadModeCombo->blockSignals(false);
            }
            break;
        }
    }

    updateLeadModeDescription();
}

void SnappingWindow::onEngineHarmonyModeChanged(playback::ScaleSnapProcessor::HarmonyMode mode)
{
    if (!m_harmonyModeCombo) return;

    // Update combo to match (avoid re-triggering onHarmonyModeChanged)
    const int modeInt = static_cast<int>(mode);
    for (int i = 0; i < m_harmonyModeCombo->count(); ++i) {
        if (m_harmonyModeCombo->itemData(i).toInt() == modeInt) {
            if (m_harmonyModeCombo->currentIndex() != i) {
                m_harmonyModeCombo->blockSignals(true);
                m_harmonyModeCombo->setCurrentIndex(i);
                m_harmonyModeCombo->blockSignals(false);
            }
            break;
        }
    }

    updateHarmonyModeDescription();
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

void SnappingWindow::updateLeadModeDescription()
{
    if (!m_leadDescriptionLabel || !m_leadModeCombo) return;

    const int modeInt = m_leadModeCombo->currentData().toInt();
    const auto mode = static_cast<playback::ScaleSnapProcessor::LeadMode>(modeInt);

    QString desc;
    switch (mode) {
    case playback::ScaleSnapProcessor::LeadMode::Off:
        desc = "Lead output is disabled. No notes are sent to channel 12.";
        break;
    case playback::ScaleSnapProcessor::LeadMode::Original:
        desc = "Pass through guitar notes unchanged to channel 12 (duplicates channel 1 data including CC2).";
        break;
    case playback::ScaleSnapProcessor::LeadMode::Constrained:
        desc = "Snap guitar notes to the nearest scale/chord tone. Output on MIDI channel 12.";
        break;
    }

    m_leadDescriptionLabel->setText(desc);
}

void SnappingWindow::updateHarmonyModeDescription()
{
    if (!m_harmonyDescriptionLabel || !m_harmonyModeCombo) return;

    const int modeInt = m_harmonyModeCombo->currentData().toInt();
    const auto mode = static_cast<playback::ScaleSnapProcessor::HarmonyMode>(modeInt);

    QString desc;
    switch (mode) {
    case playback::ScaleSnapProcessor::HarmonyMode::Off:
        desc = "Harmony output is disabled. No notes are sent to channel 11.";
        break;
    case playback::ScaleSnapProcessor::HarmonyMode::SmartThirds:
        desc = "Generate a harmony note (preferring 3rds and 5ths that are chord/scale tones). Output on MIDI channel 11.";
        break;
    }

    m_harmonyDescriptionLabel->setText(desc);
}
