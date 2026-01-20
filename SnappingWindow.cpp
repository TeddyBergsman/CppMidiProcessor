#include "SnappingWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QGridLayout>

#include "playback/VirtuosoBalladMvpPlaybackEngine.h"

SnappingWindow::SnappingWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Snapping Settings");
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(500, 450);

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

        // Apply multi-voice defaults to engine (from current UI state)
        // This ensures the engine matches the UI defaults
        for (int voiceIdx = 0; voiceIdx < 4; ++voiceIdx) {
            if (m_voiceModeCombo[voiceIdx] && m_voiceRangeCombo[voiceIdx]) {
                const int modeInt = m_voiceModeCombo[voiceIdx]->currentData().toInt();
                snap->setVoiceMotionType(voiceIdx, static_cast<playback::VoiceMotionType>(modeInt));

                const int encoded = m_voiceRangeCombo[voiceIdx]->currentData().toInt();
                const int minNote = encoded / 1000;
                const int maxNote = encoded % 1000;
                snap->setVoiceRange(voiceIdx, minNote, maxNote);
            }
        }

        // Sync multi-voice harmony UI (in case engine had different values)
        syncMultiVoiceUiToEngine();

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

        if (m_harmonyVibratoCheckbox) {
            m_harmonyVibratoCheckbox->setChecked(snap->harmonyVibratoEnabled());
        }

        if (m_harmonyHumanizationCheckbox) {
            m_harmonyHumanizationCheckbox->setChecked(snap->harmonyHumanizationEnabled());
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
        connect(snap, &playback::ScaleSnapProcessor::harmonyVibratoEnabledChanged,
                this, &SnappingWindow::onEngineHarmonyVibratoChanged,
                Qt::UniqueConnection);
        connect(snap, &playback::ScaleSnapProcessor::harmonyHumanizationEnabledChanged,
                this, &SnappingWindow::onEngineHarmonyHumanizationChanged,
                Qt::UniqueConnection);
        connect(snap, &playback::ScaleSnapProcessor::voiceSustainEnabledChanged,
                this, &SnappingWindow::onEngineVoiceSustainChanged,
                Qt::UniqueConnection);
    }

    updateLeadModeDescription();
}

// Helper to populate mode combo items
static void populateModeCombo(QComboBox* combo) {
    combo->addItem("Off", static_cast<int>(playback::VoiceMotionType::OFF));
    combo->addItem("Smart Thirds", static_cast<int>(playback::VoiceMotionType::PARALLEL));
    combo->addItem("Contrary", static_cast<int>(playback::VoiceMotionType::CONTRARY));
    combo->addItem("Similar", static_cast<int>(playback::VoiceMotionType::SIMILAR));
    combo->addItem("Oblique", static_cast<int>(playback::VoiceMotionType::OBLIQUE));
}

// Helper to populate range combo items (reusing existing instrument ranges)
static void populateRangeCombo(QComboBox* combo) {
    // Store min,max as a pair encoded in user data (min * 1000 + max)
    combo->addItem("Full Range (C-1 to G9)", 0 * 1000 + 127);
    combo->addItem("Trumpet (E3-C6)", 52 * 1000 + 84);
    combo->addItem("Alto Sax (Db3-Ab5)", 49 * 1000 + 80);
    combo->addItem("Tenor Sax (Ab2-E5)", 44 * 1000 + 76);
    combo->addItem("Violin (G3-E7)", 55 * 1000 + 100);
    combo->addItem("Flute (C4-C7)", 60 * 1000 + 96);
    combo->addItem("Clarinet (E3-C7)", 52 * 1000 + 96);
    combo->addItem("Trombone (E2-Bb4)", 40 * 1000 + 70);
    combo->addItem("Voice Soprano (C4-C6)", 60 * 1000 + 84);
    combo->addItem("Voice Alto (F3-F5)", 53 * 1000 + 77);
    combo->addItem("Voice Tenor (C3-C5)", 48 * 1000 + 72);
    combo->addItem("Voice Bass (E2-E4)", 40 * 1000 + 64);
}

// Helper to find combo index by encoded range value
static int findRangeIndex(QComboBox* combo, int encodedValue) {
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toInt() == encodedValue) {
            return i;
        }
    }
    return 0; // Default to first item
}

// Helper to find combo index by mode value
static int findModeIndex(QComboBox* combo, int modeValue) {
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toInt() == modeValue) {
            return i;
        }
    }
    return 0; // Default to first item (Off)
}

void SnappingWindow::buildUi()
{
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // ===== LEAD MODE GROUP =====
    QGroupBox* leadGroup = new QGroupBox("Lead (Channel 1)", central);
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
    m_leadModeCombo->addItem("Conformed", static_cast<int>(playback::ScaleSnapProcessor::LeadMode::Conformed));
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

    // ===== HARMONY GROUP (4 voices on channels 12-15) =====
    m_harmonyGroup = new QGroupBox("Harmony (Channels 12-15)", central);
    QVBoxLayout* harmonyLayout = new QVBoxLayout(m_harmonyGroup);
    harmonyLayout->setContentsMargins(12, 12, 12, 12);
    harmonyLayout->setSpacing(8);

    // Grid layout for 4 voice rows
    QGridLayout* voiceGrid = new QGridLayout();
    voiceGrid->setSpacing(8);

    // Header row
    voiceGrid->addWidget(new QLabel("Channel", m_harmonyGroup), 0, 0);
    voiceGrid->addWidget(new QLabel("Mode", m_harmonyGroup), 0, 1);
    voiceGrid->addWidget(new QLabel("Range", m_harmonyGroup), 0, 2);

    // Default configurations as specified by user (for TESTING):
    // Channel 12: Contrary, Voice Bass (E2-E4)
    // Channel 13: Smart Thirds, Tenor Sax (Ab2-E5)
    // Channel 14: Similar, Trumpet (E3-C6)
    // Channel 15: Oblique, Clarinet (E3-C7)
    struct VoiceDefault {
        int channel;
        playback::VoiceMotionType mode;
        int rangeEncoded;  // min * 1000 + max
    };
    const VoiceDefault defaults[4] = {
        {12, playback::VoiceMotionType::CONTRARY, 40 * 1000 + 64},  // Voice Bass (E2-E4)
        {13, playback::VoiceMotionType::PARALLEL, 44 * 1000 + 76},  // Tenor Sax (Ab2-E5)
        {14, playback::VoiceMotionType::SIMILAR, 52 * 1000 + 84},   // Trumpet (E3-C6)
        {15, playback::VoiceMotionType::OBLIQUE, 52 * 1000 + 96}    // Clarinet (E3-C7)
    };

    for (int voiceIdx = 0; voiceIdx < 4; ++voiceIdx) {
        int row = voiceIdx + 1;
        int channel = 12 + voiceIdx;

        // Channel label
        QLabel* channelLabel = new QLabel(QString("Ch. %1").arg(channel), m_harmonyGroup);
        voiceGrid->addWidget(channelLabel, row, 0);

        // Mode combo
        m_voiceModeCombo[voiceIdx] = new QComboBox(m_harmonyGroup);
        populateModeCombo(m_voiceModeCombo[voiceIdx]);
        m_voiceModeCombo[voiceIdx]->setMinimumWidth(120);

        // Set default mode
        int defaultModeIdx = findModeIndex(m_voiceModeCombo[voiceIdx], static_cast<int>(defaults[voiceIdx].mode));
        m_voiceModeCombo[voiceIdx]->setCurrentIndex(defaultModeIdx);

        voiceGrid->addWidget(m_voiceModeCombo[voiceIdx], row, 1);

        // Range combo
        m_voiceRangeCombo[voiceIdx] = new QComboBox(m_harmonyGroup);
        populateRangeCombo(m_voiceRangeCombo[voiceIdx]);
        m_voiceRangeCombo[voiceIdx]->setMinimumWidth(150);

        // Set default range
        int defaultRangeIdx = findRangeIndex(m_voiceRangeCombo[voiceIdx], defaults[voiceIdx].rangeEncoded);
        m_voiceRangeCombo[voiceIdx]->setCurrentIndex(defaultRangeIdx);

        voiceGrid->addWidget(m_voiceRangeCombo[voiceIdx], row, 2);

        // Connect signals with lambda to capture voice index
        connect(m_voiceModeCombo[voiceIdx], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, voiceIdx](int comboIdx) { onVoiceModeChanged(voiceIdx, comboIdx); });
        connect(m_voiceRangeCombo[voiceIdx], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, voiceIdx](int comboIdx) { onVoiceRangeChanged(voiceIdx, comboIdx); });
    }

    harmonyLayout->addLayout(voiceGrid);
    mainLayout->addWidget(m_harmonyGroup);

    // ===== VOCAL SETTINGS =====
    // Vocal bend checkbox
    m_vocalBendCheckbox = new QCheckBox("Apply Vocal Vibrato as Pitch Bend", central);
    m_vocalBendCheckbox->setToolTip("When enabled, transfers vocal pitch variations to MIDI pitch bend on output channels.\nThis adds expressiveness by modulating the lead/harmony notes with your voice.");
    mainLayout->addWidget(m_vocalBendCheckbox);

    // Vocal vibrato range combo
    QHBoxLayout* vibratoRow = new QHBoxLayout();
    vibratoRow->setSpacing(8);
    QLabel* vibratoLabel = new QLabel("Vocal Vibrato Range:", central);
    m_vocalVibratoRangeCombo = new QComboBox(central);
    m_vocalVibratoRangeCombo->addItem(QString::fromUtf8("\u00b1200 cents (default)"), 200.0);
    m_vocalVibratoRangeCombo->addItem(QString::fromUtf8("\u00b1100 cents"), 100.0);
    m_vocalVibratoRangeCombo->setToolTip(QString::fromUtf8("Maximum vocal pitch deviation that affects pitch bend.\n\u00b1200 cents = \u00b12 semitones, \u00b1100 cents = \u00b11 semitone."));
    vibratoRow->addWidget(vibratoLabel);
    vibratoRow->addWidget(m_vocalVibratoRangeCombo);
    vibratoRow->addStretch();
    mainLayout->addLayout(vibratoRow);

    // Vibrato correction checkbox
    m_vibratoCorrectionCheckbox = new QCheckBox("Vibrato Correction", central);
    m_vibratoCorrectionCheckbox->setToolTip("Filters out pitch drift from the voice signal, keeping only the vibrato oscillation.\nThis keeps the output perfectly centered around the guitar note, even if you sing slightly flat or sharp.");
    mainLayout->addWidget(m_vibratoCorrectionCheckbox);

    // Harmony vibrato checkbox
    m_harmonyVibratoCheckbox = new QCheckBox("Harmony Vibrato", central);
    m_harmonyVibratoCheckbox->setToolTip("Apply vocal vibrato pitch bend to harmony voices.\nWhen disabled (default), harmony voices stay at a fixed pitch without vibrato wobble.");
    mainLayout->addWidget(m_harmonyVibratoCheckbox);

    // Harmony humanization checkbox
    m_harmonyHumanizationCheckbox = new QCheckBox("Harmony Humanization", central);
    m_harmonyHumanizationCheckbox->setToolTip("Add BPM-constrained timing offsets to harmony voices.\nCreates more natural, human-like timing variation between voices.");
    m_harmonyHumanizationCheckbox->setChecked(true);  // Default on
    mainLayout->addWidget(m_harmonyHumanizationCheckbox);

    // Voice sustain checkbox
    m_voiceSustainCheckbox = new QCheckBox("Voice Sustain", central);
    m_voiceSustainCheckbox->setToolTip("Sustain guitar notes for as long as you're singing (CC2 active).\nNotes ring out even after the guitar string stops, allowing longer sustained tones controlled by your voice.");
    mainLayout->addWidget(m_voiceSustainCheckbox);

    mainLayout->addStretch();

    // Connect widgets
    connect(m_leadModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SnappingWindow::onLeadModeChanged);
    connect(m_vocalBendCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onVocalBendToggled);
    connect(m_vocalVibratoRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SnappingWindow::onVocalVibratoRangeChanged);
    connect(m_vibratoCorrectionCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onVibratoCorrectionToggled);
    connect(m_harmonyVibratoCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onHarmonyVibratoToggled);
    connect(m_harmonyHumanizationCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onHarmonyHumanizationToggled);
    connect(m_voiceSustainCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onVoiceSustainToggled);

    updateLeadModeDescription();

    // Apply default voice configurations to engine (if available)
    // This will be called again in setPlaybackEngine, but we set up defaults here
    // so the UI reflects them even before the engine is connected
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

void SnappingWindow::onVoiceModeChanged(int voiceIndex, int modeComboIndex)
{
    if (!m_engine || voiceIndex < 0 || voiceIndex >= 4 || !m_voiceModeCombo[voiceIndex]) return;

    const int modeInt = m_voiceModeCombo[voiceIndex]->itemData(modeComboIndex).toInt();
    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        snap->setVoiceMotionType(voiceIndex, static_cast<playback::VoiceMotionType>(modeInt));
    }
}

void SnappingWindow::onVoiceRangeChanged(int voiceIndex, int rangeComboIndex)
{
    if (!m_engine || voiceIndex < 0 || voiceIndex >= 4 || !m_voiceRangeCombo[voiceIndex]) return;

    // Decode the min/max from the stored value (min * 1000 + max)
    const int encoded = m_voiceRangeCombo[voiceIndex]->itemData(rangeComboIndex).toInt();
    const int minNote = encoded / 1000;
    const int maxNote = encoded % 1000;

    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        snap->setVoiceRange(voiceIndex, minNote, maxNote);
    }
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

void SnappingWindow::onHarmonyVibratoToggled(bool checked)
{
    if (!m_engine) return;

    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        snap->setHarmonyVibratoEnabled(checked);
    }
}

void SnappingWindow::onHarmonyHumanizationToggled(bool checked)
{
    if (!m_engine) return;

    auto* snap = m_engine->scaleSnapProcessor();
    if (snap) {
        snap->setHarmonyHumanizationEnabled(checked);
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

void SnappingWindow::onEngineHarmonyModeChanged(playback::HarmonyMode /*mode*/)
{
    // Multi-voice mode doesn't use HarmonyMode directly
    // Sync UI to engine state
    syncMultiVoiceUiToEngine();
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

void SnappingWindow::onEngineHarmonyVibratoChanged(bool enabled)
{
    if (!m_harmonyVibratoCheckbox) return;

    if (m_harmonyVibratoCheckbox->isChecked() != enabled) {
        m_harmonyVibratoCheckbox->blockSignals(true);
        m_harmonyVibratoCheckbox->setChecked(enabled);
        m_harmonyVibratoCheckbox->blockSignals(false);
    }
}

void SnappingWindow::onEngineHarmonyHumanizationChanged(bool enabled)
{
    if (!m_harmonyHumanizationCheckbox) return;

    if (m_harmonyHumanizationCheckbox->isChecked() != enabled) {
        m_harmonyHumanizationCheckbox->blockSignals(true);
        m_harmonyHumanizationCheckbox->setChecked(enabled);
        m_harmonyHumanizationCheckbox->blockSignals(false);
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
        desc = "Lead output is disabled. Guitar notes pass through normally to channel 1.";
        break;
    case playback::ScaleSnapProcessor::LeadMode::Original:
        desc = "Pass through guitar notes unchanged to channel 1, with vocal bend and vibrato correction applied.";
        break;
    case playback::ScaleSnapProcessor::LeadMode::Conformed:
        desc = "Apply gravity-based pitch conformance to snap notes toward chord/scale tones. Output on MIDI channel 1.";
        break;
    }

    m_leadDescriptionLabel->setText(desc);
}

void SnappingWindow::syncMultiVoiceUiToEngine()
{
    if (!m_engine) return;

    auto* snap = m_engine->scaleSnapProcessor();
    if (!snap) return;

    for (int voiceIdx = 0; voiceIdx < 4; ++voiceIdx) {
        const auto& config = snap->voiceConfig(voiceIdx);

        // Sync mode combo
        if (m_voiceModeCombo[voiceIdx]) {
            int modeIdx = findModeIndex(m_voiceModeCombo[voiceIdx], static_cast<int>(config.motionType));
            if (m_voiceModeCombo[voiceIdx]->currentIndex() != modeIdx) {
                m_voiceModeCombo[voiceIdx]->blockSignals(true);
                m_voiceModeCombo[voiceIdx]->setCurrentIndex(modeIdx);
                m_voiceModeCombo[voiceIdx]->blockSignals(false);
            }
        }

        // Sync range combo
        if (m_voiceRangeCombo[voiceIdx]) {
            int encodedRange = config.rangeMin * 1000 + config.rangeMax;
            int rangeIdx = findRangeIndex(m_voiceRangeCombo[voiceIdx], encodedRange);
            if (m_voiceRangeCombo[voiceIdx]->currentIndex() != rangeIdx) {
                m_voiceRangeCombo[voiceIdx]->blockSignals(true);
                m_voiceRangeCombo[voiceIdx]->setCurrentIndex(rangeIdx);
                m_voiceRangeCombo[voiceIdx]->blockSignals(false);
            }
        }
    }
}
