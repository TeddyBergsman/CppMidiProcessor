#include "SnappingWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QGridLayout>
#include <QSlider>
#include <QSettings>

#include "playback/VirtuosoBalladMvpPlaybackEngine.h"

SnappingWindow::SnappingWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Snapping Settings");
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(500, 450);

    buildUi();
}

playback::ScaleSnapProcessor* SnappingWindow::activeSnap() const
{
    if (m_directSnap) return m_directSnap;
    if (m_engine) return m_engine->scaleSnapProcessor();
    return nullptr;
}

void SnappingWindow::setScaleSnapProcessor(playback::ScaleSnapProcessor* snap)
{
    m_directSnap = snap;
    if (!m_directSnap) return;

    // Sync UI to current snap state (same as setPlaybackEngine but using snap directly)
    if (m_leadModeCombo) {
        const int modeInt = static_cast<int>(snap->leadMode());
        for (int i = 0; i < m_leadModeCombo->count(); ++i) {
            if (m_leadModeCombo->itemData(i).toInt() == modeInt) {
                m_leadModeCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    // Apply multi-voice defaults to snap
    for (int voiceIdx = 0; voiceIdx < 4; ++voiceIdx) {
        if (m_voiceModeCombo[voiceIdx] && m_voiceRangeCombo[voiceIdx]) {
            const int modeInt = m_voiceModeCombo[voiceIdx]->currentData().toInt();
            snap->setVoiceMotionType(voiceIdx, static_cast<playback::VoiceMotionType>(modeInt));
            const int encoded = m_voiceRangeCombo[voiceIdx]->currentData().toInt();
            snap->setVoiceRange(voiceIdx, encoded / 1000, encoded % 1000);
        }
    }

    syncMultiVoiceUiToEngine();

    if (m_vocalBendCheckbox) m_vocalBendCheckbox->setChecked(snap->vocalBendEnabled());
    if (m_vocalVibratoRangeCombo) {
        const double cents = snap->vocalVibratoRangeCents();
        for (int i = 0; i < m_vocalVibratoRangeCombo->count(); ++i) {
            if (qAbs(m_vocalVibratoRangeCombo->itemData(i).toDouble() - cents) < 1.0) {
                m_vocalVibratoRangeCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    if (m_vibratoCorrectionCheckbox) m_vibratoCorrectionCheckbox->setChecked(snap->vibratoCorrectionEnabled());
    if (m_harmonyVibratoCheckbox) m_harmonyVibratoCheckbox->setChecked(snap->harmonyVibratoEnabled());
    if (m_harmonyHumanizationCheckbox) m_harmonyHumanizationCheckbox->setChecked(snap->harmonyHumanizationEnabled());
    if (m_voiceSustainCheckbox) m_voiceSustainCheckbox->setChecked(snap->voiceSustainEnabled());

    // Apply persisted sustain smoothing and release bend prevention settings to the processor
    if (m_sustainSmoothingCheckbox) {
        snap->setSustainSmoothingEnabled(m_sustainSmoothingCheckbox->isChecked());
    }
    if (m_sustainSmoothingSlider) {
        snap->setSustainSmoothingMs(m_sustainSmoothingSlider->value());
    }
    if (m_releaseBendPreventionCheckbox) {
        snap->setReleaseBendPreventionEnabled(m_releaseBendPreventionCheckbox->isChecked());
    }
    if (m_octaveGuardCheckbox) {
        snap->setOctaveGuardEnabled(m_octaveGuardCheckbox->isChecked());
    }
    if (m_voiceSustainThresholdSlider) {
        snap->setVoiceSustainThreshold(m_voiceSustainThresholdSlider->value());
    }

    // Connect to changes from the processor
    connect(snap, &playback::ScaleSnapProcessor::leadModeChanged,
            this, &SnappingWindow::onEngineLeadModeChanged, Qt::UniqueConnection);
    connect(snap, &playback::ScaleSnapProcessor::harmonyModeChanged,
            this, &SnappingWindow::onEngineHarmonyModeChanged, Qt::UniqueConnection);
    connect(snap, &playback::ScaleSnapProcessor::vocalBendEnabledChanged,
            this, &SnappingWindow::onEngineVocalBendChanged, Qt::UniqueConnection);
    connect(snap, &playback::ScaleSnapProcessor::vocalVibratoRangeCentsChanged,
            this, &SnappingWindow::onEngineVocalVibratoRangeChanged, Qt::UniqueConnection);
    connect(snap, &playback::ScaleSnapProcessor::vibratoCorrectionEnabledChanged,
            this, &SnappingWindow::onEngineVibratoCorrectionChanged, Qt::UniqueConnection);
    connect(snap, &playback::ScaleSnapProcessor::harmonyVibratoEnabledChanged,
            this, &SnappingWindow::onEngineHarmonyVibratoChanged, Qt::UniqueConnection);
    connect(snap, &playback::ScaleSnapProcessor::harmonyHumanizationEnabledChanged,
            this, &SnappingWindow::onEngineHarmonyHumanizationChanged, Qt::UniqueConnection);
    connect(snap, &playback::ScaleSnapProcessor::voiceSustainEnabledChanged,
            this, &SnappingWindow::onEngineVoiceSustainChanged, Qt::UniqueConnection);
    connect(snap, &playback::ScaleSnapProcessor::sustainSmoothingEnabledChanged,
            this, &SnappingWindow::onEngineSustainSmoothingChanged, Qt::UniqueConnection);
    connect(snap, &playback::ScaleSnapProcessor::sustainSmoothingMsChanged,
            this, &SnappingWindow::onEngineSustainSmoothingMsChanged, Qt::UniqueConnection);
    connect(snap, &playback::ScaleSnapProcessor::releaseBendPreventionEnabledChanged,
            this, &SnappingWindow::onEngineReleaseBendPreventionChanged, Qt::UniqueConnection);
    connect(snap, &playback::ScaleSnapProcessor::voiceSustainThresholdChanged,
            this, &SnappingWindow::onEngineVoiceSustainThresholdChanged, Qt::UniqueConnection);

    // Show/hide glissando controls and apply settings based on current lead mode
    if (m_glissandoGroup) {
        const bool isVocalSync = (snap->leadMode() == playback::ScaleSnapProcessor::LeadMode::VocalSync);
        m_glissandoGroup->setVisible(isVocalSync);
        if (isVocalSync) {
            if (m_glissandoEnabledCheckbox) snap->setGlissandoEnabled(m_glissandoEnabledCheckbox->isChecked());
            if (m_glissandoRateSlider) snap->setGlissandoRateStPerSec(static_cast<float>(m_glissandoRateSlider->value()));
            if (m_glissandoThresholdSlider) snap->setGlissandoIntervalThresholdSt(static_cast<float>(m_glissandoThresholdSlider->value()));
            if (m_glissandoCurveCombo) snap->setGlissandoCurveExponent(m_glissandoCurveCombo->currentData().toFloat());
        }
    }

    updateLeadModeDescription();
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

        // Apply persisted sustain smoothing and release bend prevention settings to the processor
        if (m_sustainSmoothingCheckbox) {
            snap->setSustainSmoothingEnabled(m_sustainSmoothingCheckbox->isChecked());
        }
        if (m_sustainSmoothingSlider) {
            snap->setSustainSmoothingMs(m_sustainSmoothingSlider->value());
        }
        if (m_releaseBendPreventionCheckbox) {
            snap->setReleaseBendPreventionEnabled(m_releaseBendPreventionCheckbox->isChecked());
        }
        if (m_voiceSustainThresholdSlider) {
            snap->setVoiceSustainThreshold(m_voiceSustainThresholdSlider->value());
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
        connect(snap, &playback::ScaleSnapProcessor::sustainSmoothingEnabledChanged,
                this, &SnappingWindow::onEngineSustainSmoothingChanged,
                Qt::UniqueConnection);
        connect(snap, &playback::ScaleSnapProcessor::sustainSmoothingMsChanged,
                this, &SnappingWindow::onEngineSustainSmoothingMsChanged,
                Qt::UniqueConnection);
        connect(snap, &playback::ScaleSnapProcessor::releaseBendPreventionEnabledChanged,
                this, &SnappingWindow::onEngineReleaseBendPreventionChanged,
                Qt::UniqueConnection);
        connect(snap, &playback::ScaleSnapProcessor::voiceSustainThresholdChanged,
                this, &SnappingWindow::onEngineVoiceSustainThresholdChanged,
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
    m_leadModeCombo->addItem("VocalSync", static_cast<int>(playback::ScaleSnapProcessor::LeadMode::VocalSync));
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

    // ===== GLISSANDO GROUP (VocalSync mode only) =====
    m_glissandoGroup = new QGroupBox("Glissando (VocalSync)", central);
    QVBoxLayout* glissandoLayout = new QVBoxLayout(m_glissandoGroup);
    glissandoLayout->setContentsMargins(12, 12, 12, 12);
    glissandoLayout->setSpacing(8);

    m_glissandoEnabledCheckbox = new QCheckBox("Enable Glissando", m_glissandoGroup);
    m_glissandoEnabledCheckbox->setToolTip("Smooth pitch transitions between guitar notes.\nWhen disabled, pitch changes snap instantly.");
    glissandoLayout->addWidget(m_glissandoEnabledCheckbox);

    // Glide rate slider
    auto* rateRow = new QHBoxLayout();
    m_glissandoRateLabel = new QLabel("Glide Rate: 100 st/s", m_glissandoGroup);
    m_glissandoRateSlider = new QSlider(Qt::Horizontal, m_glissandoGroup);
    m_glissandoRateSlider->setRange(10, 500);
    m_glissandoRateSlider->setValue(100);
    m_glissandoRateSlider->setTickInterval(50);
    m_glissandoRateSlider->setTickPosition(QSlider::TicksBelow);
    m_glissandoRateSlider->setToolTip("Base glide speed in semitones per second.\nLower = slower, more vocal portamento feel.\nHigher = faster transitions.");
    rateRow->addWidget(m_glissandoRateLabel);
    rateRow->addWidget(m_glissandoRateSlider, 1);
    glissandoLayout->addLayout(rateRow);

    // Snap threshold slider
    auto* thresholdGlissRow = new QHBoxLayout();
    m_glissandoThresholdLabel = new QLabel("Snap Threshold: 7 st", m_glissandoGroup);
    m_glissandoThresholdSlider = new QSlider(Qt::Horizontal, m_glissandoGroup);
    m_glissandoThresholdSlider->setRange(1, 12);
    m_glissandoThresholdSlider->setValue(7);
    m_glissandoThresholdSlider->setTickInterval(1);
    m_glissandoThresholdSlider->setTickPosition(QSlider::TicksBelow);
    m_glissandoThresholdSlider->setToolTip("Intervals larger than this glide proportionally faster.\nSmall intervals (steps) glide smoothly, large leaps snap quickly.");
    thresholdGlissRow->addWidget(m_glissandoThresholdLabel);
    thresholdGlissRow->addWidget(m_glissandoThresholdSlider, 1);
    glissandoLayout->addLayout(thresholdGlissRow);

    // Curve combo
    auto* curveRow = new QHBoxLayout();
    QLabel* curveLabel = new QLabel("Curve:", m_glissandoGroup);
    m_glissandoCurveCombo = new QComboBox(m_glissandoGroup);
    m_glissandoCurveCombo->addItem("Linear", 1.0f);
    m_glissandoCurveCombo->addItem("Smooth (default)", 1.5f);
    m_glissandoCurveCombo->addItem("Exponential", 2.0f);
    m_glissandoCurveCombo->setCurrentIndex(1); // Smooth by default
    m_glissandoCurveCombo->setToolTip("Glide curve shape.\nLinear = constant speed.\nSmooth = natural vocal feel.\nExponential = accelerates into the target note.");
    curveRow->addWidget(curveLabel);
    curveRow->addWidget(m_glissandoCurveCombo);
    curveRow->addStretch();
    glissandoLayout->addLayout(curveRow);

    // Load persisted glissando settings
    {
        QSettings settings;
        m_glissandoEnabledCheckbox->setChecked(settings.value("snapping/glissandoEnabled", true).toBool());
        int rate = settings.value("snapping/glissandoRate", 100).toInt();
        m_glissandoRateSlider->setValue(rate);
        m_glissandoRateLabel->setText(QString("Glide Rate: %1 st/s").arg(rate));
        int threshold = settings.value("snapping/glissandoThreshold", 7).toInt();
        m_glissandoThresholdSlider->setValue(threshold);
        m_glissandoThresholdLabel->setText(QString("Snap Threshold: %1 st").arg(threshold));
        int curveIdx = settings.value("snapping/glissandoCurve", 1).toInt();
        m_glissandoCurveCombo->setCurrentIndex(qBound(0, curveIdx, 2));
    }

    // Glissando connections
    connect(m_glissandoEnabledCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings().setValue("snapping/glissandoEnabled", checked);
        auto* snap = activeSnap();
        if (snap) snap->setGlissandoEnabled(checked);
    });
    connect(m_glissandoRateSlider, &QSlider::valueChanged, this, [this](int value) {
        m_glissandoRateLabel->setText(QString("Glide Rate: %1 st/s").arg(value));
        QSettings().setValue("snapping/glissandoRate", value);
        auto* snap = activeSnap();
        if (snap) snap->setGlissandoRateStPerSec(static_cast<float>(value));
    });
    connect(m_glissandoThresholdSlider, &QSlider::valueChanged, this, [this](int value) {
        m_glissandoThresholdLabel->setText(QString("Snap Threshold: %1 st").arg(value));
        QSettings().setValue("snapping/glissandoThreshold", value);
        auto* snap = activeSnap();
        if (snap) snap->setGlissandoIntervalThresholdSt(static_cast<float>(value));
    });
    connect(m_glissandoCurveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        QSettings().setValue("snapping/glissandoCurve", index);
        auto* snap = activeSnap();
        if (snap && m_glissandoCurveCombo) {
            float exponent = m_glissandoCurveCombo->itemData(index).toFloat();
            snap->setGlissandoCurveExponent(exponent);
        }
    });

    // Initially hidden - shown only when VocalSync mode is selected
    m_glissandoGroup->setVisible(false);
    mainLayout->addWidget(m_glissandoGroup);

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

    // Voice sustain sensitivity slider
    auto* thresholdRow = new QHBoxLayout();
    m_voiceSustainThresholdLabel = new QLabel("Sensitivity: 5", central);
    m_voiceSustainThresholdSlider = new QSlider(Qt::Horizontal, central);
    m_voiceSustainThresholdSlider->setRange(1, 10);
    m_voiceSustainThresholdSlider->setValue(5);
    m_voiceSustainThresholdSlider->setTickInterval(1);
    m_voiceSustainThresholdSlider->setTickPosition(QSlider::TicksBelow);
    m_voiceSustainThresholdSlider->setInvertedAppearance(true);  // Left = high threshold (less sensitive), right = low (more sensitive)
    m_voiceSustainThresholdSlider->setToolTip("Voice sustain sensitivity (CC2 threshold).\nHigher = more sensitive (softer singing triggers sustain).\nLower = less sensitive (requires stronger voice signal).");
    thresholdRow->addWidget(m_voiceSustainThresholdLabel);
    thresholdRow->addWidget(m_voiceSustainThresholdSlider, 1);
    mainLayout->addLayout(thresholdRow);

    // Sustain smoothing checkbox + slider
    m_sustainSmoothingCheckbox = new QCheckBox("Sustain Smoothing", central);
    m_sustainSmoothingCheckbox->setToolTip("Hold sustain briefly after voice drops out to survive short silences.\nPrevents notes from cutting off during brief pauses in singing.");
    mainLayout->addWidget(m_sustainSmoothingCheckbox);

    auto* smoothingRow = new QHBoxLayout();
    m_sustainSmoothingLabel = new QLabel("Hold: 500 ms", central);
    m_sustainSmoothingSlider = new QSlider(Qt::Horizontal, central);
    m_sustainSmoothingSlider->setRange(50, 2000);
    m_sustainSmoothingSlider->setValue(500);
    m_sustainSmoothingSlider->setTickInterval(250);
    m_sustainSmoothingSlider->setTickPosition(QSlider::TicksBelow);
    m_sustainSmoothingSlider->setToolTip("How long to hold sustain after voice drops out (50-2000 ms).");
    smoothingRow->addWidget(m_sustainSmoothingLabel);
    smoothingRow->addWidget(m_sustainSmoothingSlider, 1);
    mainLayout->addLayout(smoothingRow);

    // Release bend prevention checkbox
    m_releaseBendPreventionCheckbox = new QCheckBox("Release Bend Prevention", central);
    m_releaseBendPreventionCheckbox->setToolTip("Freeze pitch bend when a note is voice-sustained.\nPrevents the pitch droop from guitar string release (MIDI Guitar 3) from making sustained notes go flat.");
    mainLayout->addWidget(m_releaseBendPreventionCheckbox);

    // Octave guard checkbox
    m_octaveGuardCheckbox = new QCheckBox("Octave Guard", central);
    m_octaveGuardCheckbox->setToolTip("Reject sudden octave jumps from voice MIDI tracking errors.\nLarge pitch jumps (>9 semitones) must be stable for ~30ms before accepted.\nPrevents brief false octave spikes from MG2.");
    mainLayout->addWidget(m_octaveGuardCheckbox);

    // Load persisted sustain smoothing and release bend prevention settings
    {
        QSettings settings;
        m_sustainSmoothingCheckbox->setChecked(settings.value("snapping/sustainSmoothingEnabled", true).toBool());
        int ms = settings.value("snapping/sustainSmoothingMs", 500).toInt();
        m_sustainSmoothingSlider->setValue(ms);
        m_sustainSmoothingLabel->setText(QString("Hold: %1 ms").arg(ms));
        m_releaseBendPreventionCheckbox->setChecked(settings.value("snapping/releaseBendPreventionEnabled", true).toBool());
        m_octaveGuardCheckbox->setChecked(settings.value("snapping/octaveGuardEnabled", true).toBool());
        int threshold = settings.value("snapping/voiceSustainThreshold", 5).toInt();
        m_voiceSustainThresholdSlider->setValue(threshold);
        m_voiceSustainThresholdLabel->setText(QString("Sensitivity: %1").arg(threshold));
    }

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
    connect(m_sustainSmoothingCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onSustainSmoothingToggled);
    connect(m_sustainSmoothingSlider, &QSlider::valueChanged,
            this, &SnappingWindow::onSustainSmoothingMsChanged);
    connect(m_releaseBendPreventionCheckbox, &QCheckBox::toggled,
            this, &SnappingWindow::onReleaseBendPreventionToggled);
    connect(m_octaveGuardCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        auto* snap = activeSnap();
        if (snap) snap->setOctaveGuardEnabled(checked);
        QSettings().setValue("snapping/octaveGuardEnabled", checked);
    });
    connect(m_voiceSustainThresholdSlider, &QSlider::valueChanged,
            this, &SnappingWindow::onVoiceSustainThresholdChanged);

    updateLeadModeDescription();

    // Apply default voice configurations to engine (if available)
    // This will be called again in setPlaybackEngine, but we set up defaults here
    // so the UI reflects them even before the engine is connected
}

void SnappingWindow::onLeadModeChanged(int index)
{
    if (!m_leadModeCombo) return;
    auto* snap = activeSnap();
    if (!snap) return;

    const int modeInt = m_leadModeCombo->itemData(index).toInt();
    const auto mode = static_cast<playback::ScaleSnapProcessor::LeadMode>(modeInt);
    snap->setLeadMode(mode);

    // Show/hide glissando controls based on VocalSync mode
    if (m_glissandoGroup) {
        m_glissandoGroup->setVisible(mode == playback::ScaleSnapProcessor::LeadMode::VocalSync);
    }

    // Apply persisted glissando settings when entering VocalSync mode
    if (mode == playback::ScaleSnapProcessor::LeadMode::VocalSync) {
        if (m_glissandoEnabledCheckbox) snap->setGlissandoEnabled(m_glissandoEnabledCheckbox->isChecked());
        if (m_glissandoRateSlider) snap->setGlissandoRateStPerSec(static_cast<float>(m_glissandoRateSlider->value()));
        if (m_glissandoThresholdSlider) snap->setGlissandoIntervalThresholdSt(static_cast<float>(m_glissandoThresholdSlider->value()));
        if (m_glissandoCurveCombo) snap->setGlissandoCurveExponent(m_glissandoCurveCombo->currentData().toFloat());
    }

    updateLeadModeDescription();
}

void SnappingWindow::onVoiceModeChanged(int voiceIndex, int modeComboIndex)
{
    if (voiceIndex < 0 || voiceIndex >= 4 || !m_voiceModeCombo[voiceIndex]) return;
    auto* snap = activeSnap();
    if (!snap) return;

    const int modeInt = m_voiceModeCombo[voiceIndex]->itemData(modeComboIndex).toInt();
    snap->setVoiceMotionType(voiceIndex, static_cast<playback::VoiceMotionType>(modeInt));
}

void SnappingWindow::onVoiceRangeChanged(int voiceIndex, int rangeComboIndex)
{
    if (voiceIndex < 0 || voiceIndex >= 4 || !m_voiceRangeCombo[voiceIndex]) return;
    auto* snap = activeSnap();
    if (!snap) return;

    const int encoded = m_voiceRangeCombo[voiceIndex]->itemData(rangeComboIndex).toInt();
    snap->setVoiceRange(voiceIndex, encoded / 1000, encoded % 1000);
}

void SnappingWindow::onVocalBendToggled(bool checked)
{
    auto* snap = activeSnap();
    if (snap) snap->setVocalBendEnabled(checked);
}

void SnappingWindow::onVocalVibratoRangeChanged(int index)
{
    if (!m_vocalVibratoRangeCombo) return;
    auto* snap = activeSnap();
    if (!snap) return;

    const double cents = m_vocalVibratoRangeCombo->itemData(index).toDouble();
    snap->setVocalVibratoRangeCents(cents);
}

void SnappingWindow::onVibratoCorrectionToggled(bool checked)
{
    auto* snap = activeSnap();
    if (snap) snap->setVibratoCorrectionEnabled(checked);
}

void SnappingWindow::onHarmonyVibratoToggled(bool checked)
{
    auto* snap = activeSnap();
    if (snap) snap->setHarmonyVibratoEnabled(checked);
}

void SnappingWindow::onHarmonyHumanizationToggled(bool checked)
{
    auto* snap = activeSnap();
    if (snap) snap->setHarmonyHumanizationEnabled(checked);
}

void SnappingWindow::onVoiceSustainToggled(bool checked)
{
    auto* snap = activeSnap();
    if (snap) snap->setVoiceSustainEnabled(checked);
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

void SnappingWindow::onSustainSmoothingToggled(bool checked)
{
    auto* snap = activeSnap();
    if (snap) snap->setSustainSmoothingEnabled(checked);

    QSettings settings;
    settings.setValue("snapping/sustainSmoothingEnabled", checked);
}

void SnappingWindow::onSustainSmoothingMsChanged(int value)
{
    auto* snap = activeSnap();
    if (snap) snap->setSustainSmoothingMs(value);

    if (m_sustainSmoothingLabel) {
        m_sustainSmoothingLabel->setText(QString("Hold: %1 ms").arg(value));
    }

    QSettings settings;
    settings.setValue("snapping/sustainSmoothingMs", value);
}

void SnappingWindow::onEngineSustainSmoothingChanged(bool enabled)
{
    if (!m_sustainSmoothingCheckbox) return;

    if (m_sustainSmoothingCheckbox->isChecked() != enabled) {
        m_sustainSmoothingCheckbox->blockSignals(true);
        m_sustainSmoothingCheckbox->setChecked(enabled);
        m_sustainSmoothingCheckbox->blockSignals(false);
    }
}

void SnappingWindow::onEngineSustainSmoothingMsChanged(int ms)
{
    if (!m_sustainSmoothingSlider) return;

    if (m_sustainSmoothingSlider->value() != ms) {
        m_sustainSmoothingSlider->blockSignals(true);
        m_sustainSmoothingSlider->setValue(ms);
        m_sustainSmoothingSlider->blockSignals(false);
    }
    if (m_sustainSmoothingLabel) {
        m_sustainSmoothingLabel->setText(QString("Hold: %1 ms").arg(ms));
    }
}

void SnappingWindow::onReleaseBendPreventionToggled(bool checked)
{
    auto* snap = activeSnap();
    if (snap) snap->setReleaseBendPreventionEnabled(checked);

    QSettings settings;
    settings.setValue("snapping/releaseBendPreventionEnabled", checked);
}

void SnappingWindow::onEngineReleaseBendPreventionChanged(bool enabled)
{
    if (!m_releaseBendPreventionCheckbox) return;

    if (m_releaseBendPreventionCheckbox->isChecked() != enabled) {
        m_releaseBendPreventionCheckbox->blockSignals(true);
        m_releaseBendPreventionCheckbox->setChecked(enabled);
        m_releaseBendPreventionCheckbox->blockSignals(false);
    }
}

void SnappingWindow::onVoiceSustainThresholdChanged(int value)
{
    auto* snap = activeSnap();
    if (snap) snap->setVoiceSustainThreshold(value);

    if (m_voiceSustainThresholdLabel) {
        m_voiceSustainThresholdLabel->setText(QString("Sensitivity: %1").arg(value));
    }

    QSettings settings;
    settings.setValue("snapping/voiceSustainThreshold", value);
}

void SnappingWindow::onEngineVoiceSustainThresholdChanged(int threshold)
{
    if (!m_voiceSustainThresholdSlider) return;

    if (m_voiceSustainThresholdSlider->value() != threshold) {
        m_voiceSustainThresholdSlider->blockSignals(true);
        m_voiceSustainThresholdSlider->setValue(threshold);
        m_voiceSustainThresholdSlider->blockSignals(false);
    }
    if (m_voiceSustainThresholdLabel) {
        m_voiceSustainThresholdLabel->setText(QString("Sensitivity: %1").arg(threshold));
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
    case playback::ScaleSnapProcessor::LeadMode::VocalSync:
        desc = "Output guitar pitch target on MIDI channel 1 for the VocalSync AU plugin. "
               "Guitar notes set the target pitch, glissando smooths transitions, "
               "and vocal vibrato is layered on top via pitch bend.";
        break;
    }

    m_leadDescriptionLabel->setText(desc);
}

void SnappingWindow::syncMultiVoiceUiToEngine()
{
    auto* snap = activeSnap();
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
