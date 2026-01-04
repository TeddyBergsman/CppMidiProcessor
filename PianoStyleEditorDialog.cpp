#include "PianoStyleEditorDialog.h"

#include "music/PianoPresets.h"
#include "playback/BandPlaybackEngine.h"

#include <QtWidgets>

PianoStyleEditorDialog::PianoStyleEditorDialog(const music::PianoProfile& initial,
                                               playback::BandPlaybackEngine* playback,
                                               QWidget* parent)
    : QDialog(parent),
      m_initial(initial),
      m_playback(playback) {
    setWindowTitle("Piano Style");
    setModal(false);
    buildUi();
    setUiFromProfile(initial);
    emitPreview();
}

void PianoStyleEditorDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto makeSpin = [](int lo, int hi) {
        auto* s = new QSpinBox;
        s->setRange(lo, hi);
        return s;
    };
    auto makeD = [](double lo, double hi, double step, int decimals = 2) {
        auto* d = new QDoubleSpinBox;
        d->setRange(lo, hi);
        d->setSingleStep(step);
        d->setDecimals(decimals);
        return d;
    };

    m_enabled = new QCheckBox("Enable piano");

    // Presets row
    {
        auto* presetsRow = new QWidget(this);
        auto* h = new QHBoxLayout(presetsRow);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);

        auto* presetLbl = new QLabel("Preset:", presetsRow);
        presetLbl->setStyleSheet("QLabel { color: #ddd; }");
        m_presetCombo = new QComboBox(presetsRow);
        m_presetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        const auto presets = music::PianoPresets::all();
        for (const auto& p : presets) {
            m_presetCombo->addItem(p.name, p.id);
        }

        m_loadPresetBtn = new QPushButton("Load", presetsRow);
        m_loadPresetBtn->setFixedWidth(64);
        m_keepRanges = new QCheckBox("Keep ranges", presetsRow);
        m_keepRanges->setChecked(true);
        m_keepEnable = new QCheckBox("Keep enable/channel", presetsRow);
        m_keepEnable->setChecked(true);

        h->addWidget(presetLbl, 0);
        h->addWidget(m_presetCombo, 1);
        h->addWidget(m_loadPresetBtn, 0);
        h->addWidget(m_keepRanges, 0);
        h->addWidget(m_keepEnable, 0);
        presetsRow->setLayout(h);
        root->addWidget(presetsRow);
    }

    // Routing
    auto* routingBox = new QGroupBox("Routing");
    auto* routingForm = new QFormLayout(routingBox);
    m_channel = makeSpin(1, 16);
    routingForm->addRow("MIDI channel", m_channel);

    // Ranges
    auto* rangeBox = new QGroupBox("Ranges");
    auto* rangeForm = new QFormLayout(rangeBox);
    m_lhMin = makeSpin(0, 127);
    m_lhMax = makeSpin(0, 127);
    m_rhMin = makeSpin(0, 127);
    m_rhMax = makeSpin(0, 127);
    rangeForm->addRow("LH min note", m_lhMin);
    rangeForm->addRow("LH max note", m_lhMax);
    rangeForm->addRow("RH min note", m_rhMin);
    rangeForm->addRow("RH max note", m_rhMax);

    // Feel/timing
    auto* feelBox = new QGroupBox("Feel & Timing");
    auto* feelForm = new QFormLayout(feelBox);
    m_feelStyle = new QComboBox(feelBox);
    m_feelStyle->addItem("Swing", (int)music::PianoFeelStyle::Swing);
    m_feelStyle->addItem("Ballad", (int)music::PianoFeelStyle::Ballad);
    m_jitterMs = makeSpin(0, 50);
    m_laidBackMs = makeSpin(-60, 60);
    m_pushMs = makeSpin(-60, 60);
    m_driftMaxMs = makeSpin(0, 120);
    m_driftRate = makeD(0.0, 1.0, 0.01, 2);
    feelForm->addRow("Feel style", m_feelStyle);
    feelForm->addRow("Micro jitter (ms +/-)", m_jitterMs);
    feelForm->addRow("Laid back (ms)", m_laidBackMs);
    feelForm->addRow("Push (ms)", m_pushMs);
    feelForm->addRow("Timing drift max (ms)", m_driftMaxMs);
    feelForm->addRow("Timing drift rate", m_driftRate);

    // Dynamics
    auto* dynBox = new QGroupBox("Dynamics");
    auto* dynForm = new QFormLayout(dynBox);
    m_baseVel = makeSpin(1, 127);
    m_velVar = makeSpin(0, 64);
    m_accentDown = makeD(0.1, 2.0, 0.02, 2);
    m_accentBack = makeD(0.1, 2.0, 0.02, 2);
    dynForm->addRow("Base velocity", m_baseVel);
    dynForm->addRow("Velocity variance (+/-)", m_velVar);
    dynForm->addRow("Accent downbeat", m_accentDown);
    dynForm->addRow("Accent backbeat", m_accentBack);

    // Rhythm
    auto* rhythmBox = new QGroupBox("Comping Rhythm");
    auto* rhythmForm = new QFormLayout(rhythmBox);
    m_compDensity = makeD(0.0, 1.0, 0.01, 2);
    m_anticipation = makeD(0.0, 1.0, 0.01, 2);
    m_syncop = makeD(0.0, 1.0, 0.01, 2);
    m_restProb = makeD(0.0, 1.0, 0.01, 2);
    rhythmForm->addRow("Comp density", m_compDensity);
    rhythmForm->addRow("Anticipation prob", m_anticipation);
    rhythmForm->addRow("Syncopation prob", m_syncop);
    rhythmForm->addRow("Rest prob", m_restProb);

    // Voicing
    auto* voiceBox = new QGroupBox("Voicings & Voice-leading");
    auto* voiceForm = new QFormLayout(voiceBox);
    m_preferRootless = new QCheckBox("Prefer rootless voicings");
    m_rootlessProb = makeD(0.0, 1.0, 0.01, 2);
    m_drop2Prob = makeD(0.0, 1.0, 0.01, 2);
    m_quartalProb = makeD(0.0, 1.0, 0.01, 2);
    m_clusterProb = makeD(0.0, 1.0, 0.01, 2);
    m_tensionProb = makeD(0.0, 1.0, 0.01, 2);
    m_avoidRootProb = makeD(0.0, 1.0, 0.01, 2);
    m_avoidThirdProb = makeD(0.0, 1.0, 0.01, 2);
    m_maxHandLeap = makeSpin(0, 36);
    m_voiceLeading = makeD(0.0, 1.0, 0.01, 2);
    m_repeatPenalty = makeD(0.0, 1.0, 0.01, 2);
    voiceForm->addRow(m_preferRootless);
    voiceForm->addRow("Rootless probability", m_rootlessProb);
    voiceForm->addRow("Drop-2 probability", m_drop2Prob);
    voiceForm->addRow("Quartal probability", m_quartalProb);
    voiceForm->addRow("Cluster probability", m_clusterProb);
    voiceForm->addRow("Tension probability", m_tensionProb);
    voiceForm->addRow("Avoid root probability", m_avoidRootProb);
    voiceForm->addRow("Avoid 3rd probability", m_avoidThirdProb);
    voiceForm->addRow("Max hand leap (semitones)", m_maxHandLeap);
    voiceForm->addRow("Voice-leading strength", m_voiceLeading);
    voiceForm->addRow("Repetition penalty", m_repeatPenalty);

    // Fills
    auto* fillsBox = new QGroupBox("RH Fills");
    auto* fillsForm = new QFormLayout(fillsBox);
    m_fillPhraseEnd = makeD(0.0, 1.0, 0.01, 2);
    m_fillAnyBeat = makeD(0.0, 1.0, 0.01, 2);
    m_phraseBars = makeSpin(1, 16);
    m_fillMaxNotes = makeSpin(0, 16);
    m_fillMinNote = makeSpin(0, 127);
    m_fillMaxNote = makeSpin(0, 127);
    fillsForm->addRow("Fill prob (phrase end)", m_fillPhraseEnd);
    fillsForm->addRow("Fill prob (any beat)", m_fillAnyBeat);
    fillsForm->addRow("Phrase length (bars)", m_phraseBars);
    fillsForm->addRow("Max notes per fill", m_fillMaxNotes);
    fillsForm->addRow("Fill min note", m_fillMinNote);
    fillsForm->addRow("Fill max note", m_fillMaxNote);

    // Pedal
    auto* pedalBox = new QGroupBox("Sustain Pedal (CC64)");
    auto* pedalForm = new QFormLayout(pedalBox);
    m_pedalEnabled = new QCheckBox("Enable sustain pedal");
    m_pedalReleaseOnChange = new QCheckBox("Release on chord change");
    m_pedalDown = makeSpin(0, 127);
    m_pedalUp = makeSpin(0, 127);
    m_pedalMinHoldMs = makeSpin(0, 5000);
    m_pedalMaxHoldMs = makeSpin(0, 8000);
    m_pedalChangeProb = makeD(0.0, 1.0, 0.01, 2);
    pedalForm->addRow(m_pedalEnabled);
    pedalForm->addRow(m_pedalReleaseOnChange);
    pedalForm->addRow("Pedal down value", m_pedalDown);
    pedalForm->addRow("Pedal up value", m_pedalUp);
    pedalForm->addRow("Min hold (ms)", m_pedalMinHoldMs);
    pedalForm->addRow("Max hold (ms)", m_pedalMaxHoldMs);
    pedalForm->addRow("Change probability", m_pedalChangeProb);

    // Layout grid
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    grid->addWidget(routingBox, 0, 0);
    grid->addWidget(rangeBox, 0, 1);
    grid->addWidget(feelBox, 1, 0);
    grid->addWidget(dynBox, 1, 1);
    grid->addWidget(rhythmBox, 2, 0);
    grid->addWidget(voiceBox, 2, 1);
    grid->addWidget(fillsBox, 3, 0);
    grid->addWidget(pedalBox, 3, 1);

    root->addWidget(m_enabled);

    QWidget* content = new QWidget(this);
    content->setLayout(grid);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    // Live log (opt-in, throttled)
    {
        auto* box = new QGroupBox("Live output log (what/why the piano just played)");
        auto* v = new QVBoxLayout(box);
        v->setContentsMargins(10, 8, 10, 10);
        v->setSpacing(6);

        auto* top = new QWidget(box);
        auto* h = new QHBoxLayout(top);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);

        m_reasoningLogEnabled = new QCheckBox("Enable live reasoning log", top);
        m_clearLogBtn = new QPushButton("Clear", top);
        m_clearLogBtn->setFixedWidth(64);

        h->addWidget(m_reasoningLogEnabled, 0);
        h->addStretch(1);
        h->addWidget(m_clearLogBtn, 0);
        top->setLayout(h);

        m_liveLog = new QListWidget(box);
        m_liveLog->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_liveLog->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_liveLog->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_liveLog->setWordWrap(false);
        m_liveLog->setMinimumHeight(140);
        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setPointSize(std::max(9, f.pointSize()));
        m_liveLog->setFont(f);
        m_liveLog->setStyleSheet("QListWidget { background-color: #0b0b0b; color: #e6e6e6; border: 1px solid #333; }");
        m_liveLog->setContextMenuPolicy(Qt::CustomContextMenu);

        v->addWidget(top);
        v->addWidget(m_liveLog, 1);
        box->setLayout(v);
        root->addWidget(box, 0);

        m_logFlushTimer = new QTimer(this);
        m_logFlushTimer->setInterval(50);
        m_logFlushTimer->setSingleShot(false);
        connect(m_logFlushTimer, &QTimer::timeout, this, &PianoStyleEditorDialog::flushPendingLog);

        connect(m_clearLogBtn, &QPushButton::clicked, this, [this]() {
            if (m_liveLog) m_liveLog->clear();
        });

        auto copySelectedLog = [this]() {
            if (!m_liveLog) return;
            const auto items = m_liveLog->selectedItems();
            if (items.isEmpty()) return;
            QStringList lines;
            lines.reserve(items.size());
            for (auto* it : items) {
                if (!it) continue;
                lines.push_back(it->text());
            }
            QGuiApplication::clipboard()->setText(lines.join('\n'));
        };

        auto* copySc = new QShortcut(QKeySequence::Copy, m_liveLog);
        connect(copySc, &QShortcut::activated, this, copySelectedLog);
        connect(m_liveLog, &QListWidget::customContextMenuRequested, this, [this, copySelectedLog](const QPoint& pos) {
            if (!m_liveLog) return;
            QMenu menu(m_liveLog);
            QAction* copy = menu.addAction("Copy");
            copy->setShortcut(QKeySequence::Copy);
            connect(copy, &QAction::triggered, this, copySelectedLog);
            menu.exec(m_liveLog->viewport()->mapToGlobal(pos));
        });

        connect(m_reasoningLogEnabled, &QCheckBox::toggled, this, [this](bool on) {
            setLiveLogActive(on);
        });
    }

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    root->addWidget(m_buttons);

    auto hook = [this]() { emitPreview(); };
    const auto widgets = findChildren<QWidget*>();
    for (QWidget* w : widgets) {
        if (auto* sb = qobject_cast<QSpinBox*>(w)) connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, hook);
        else if (auto* db = qobject_cast<QDoubleSpinBox*>(w)) connect(db, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, hook);
        else if (auto* cb = qobject_cast<QCheckBox*>(w)) connect(cb, &QCheckBox::toggled, this, hook);
        else if (auto* combo = qobject_cast<QComboBox*>(w)) connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { emitPreview(); });
    }

    connect(m_buttons, &QDialogButtonBox::accepted, this, [this]() {
        const auto p = profileFromUi();
        emit profileCommitted(p);
        accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, [this]() { reject(); });
    connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        const auto p = profileFromUi();
        emit profileCommitted(p);
    });

    connect(m_loadPresetBtn, &QPushButton::clicked, this, [this]() {
        if (!m_presetCombo) return;
        const QString id = m_presetCombo->currentData().toString();
        music::PianoPreset preset;
        if (!music::PianoPresets::getById(id, preset)) return;

        music::PianoProfile cur = profileFromUi();
        music::PianoProfile p = preset.profile;
        p.name = preset.name;
        p.humanizeSeed = cur.humanizeSeed;

        if (m_keepEnable && m_keepEnable->isChecked()) {
            p.enabled = cur.enabled;
            p.midiChannel = cur.midiChannel;
        }
        if (m_keepRanges && m_keepRanges->isChecked()) {
            p.lhMinMidiNote = cur.lhMinMidiNote;
            p.lhMaxMidiNote = cur.lhMaxMidiNote;
            p.rhMinMidiNote = cur.rhMinMidiNote;
            p.rhMaxMidiNote = cur.rhMaxMidiNote;
            p.fillMinMidiNote = cur.fillMinMidiNote;
            p.fillMaxMidiNote = cur.fillMaxMidiNote;
        }

        setUiFromProfile(p);
        emitPreview();
    });
}

void PianoStyleEditorDialog::setUiFromProfile(const music::PianoProfile& p) {
    if (m_presetCombo) {
        music::PianoPreset found;
        if (music::PianoPresets::getByName(p.name, found)) {
            const int idx = m_presetCombo->findData(found.id);
            if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
        }
    }

    m_enabled->setChecked(p.enabled);
    m_channel->setValue(p.midiChannel);
    if (m_feelStyle) {
        const int idx = m_feelStyle->findData((int)p.feelStyle);
        if (idx >= 0) m_feelStyle->setCurrentIndex(idx);
    }

    m_lhMin->setValue(p.lhMinMidiNote);
    m_lhMax->setValue(p.lhMaxMidiNote);
    m_rhMin->setValue(p.rhMinMidiNote);
    m_rhMax->setValue(p.rhMaxMidiNote);

    m_jitterMs->setValue(p.microJitterMs);
    m_laidBackMs->setValue(p.laidBackMs);
    m_pushMs->setValue(p.pushMs);
    m_driftMaxMs->setValue(p.driftMaxMs);
    m_driftRate->setValue(p.driftRate);

    m_baseVel->setValue(p.baseVelocity);
    m_velVar->setValue(p.velocityVariance);
    m_accentDown->setValue(p.accentDownbeat);
    m_accentBack->setValue(p.accentBackbeat);

    m_compDensity->setValue(p.compDensity);
    m_anticipation->setValue(p.anticipationProb);
    m_syncop->setValue(p.syncopationProb);
    m_restProb->setValue(p.restProb);

    m_preferRootless->setChecked(p.preferRootless);
    m_rootlessProb->setValue(p.rootlessProb);
    m_drop2Prob->setValue(p.drop2Prob);
    m_quartalProb->setValue(p.quartalProb);
    m_clusterProb->setValue(p.clusterProb);
    m_tensionProb->setValue(p.tensionProb);
    m_avoidRootProb->setValue(p.avoidRootProb);
    m_avoidThirdProb->setValue(p.avoidThirdProb);
    m_maxHandLeap->setValue(p.maxHandLeap);
    m_voiceLeading->setValue(p.voiceLeadingStrength);
    m_repeatPenalty->setValue(p.repetitionPenalty);

    m_fillPhraseEnd->setValue(p.fillProbPhraseEnd);
    m_fillAnyBeat->setValue(p.fillProbAnyBeat);
    m_phraseBars->setValue(p.phraseLengthBars);
    m_fillMaxNotes->setValue(p.fillMaxNotes);
    m_fillMinNote->setValue(p.fillMinMidiNote);
    m_fillMaxNote->setValue(p.fillMaxMidiNote);

    m_pedalEnabled->setChecked(p.pedalEnabled);
    m_pedalReleaseOnChange->setChecked(p.pedalReleaseOnChordChange);
    m_pedalDown->setValue(p.pedalDownValue);
    m_pedalUp->setValue(p.pedalUpValue);
    m_pedalMinHoldMs->setValue(p.pedalMinHoldMs);
    m_pedalMaxHoldMs->setValue(p.pedalMaxHoldMs);
    m_pedalChangeProb->setValue(p.pedalChangeProb);

    // Do not auto-activate live log during show (user can re-enable explicitly).
    if (m_reasoningLogEnabled) {
        const bool prev = m_reasoningLogEnabled->blockSignals(true);
        m_reasoningLogEnabled->setChecked(p.reasoningLogEnabled);
        m_reasoningLogEnabled->blockSignals(prev);
    }
    setLiveLogActive(false);
}

music::PianoProfile PianoStyleEditorDialog::profileFromUi() const {
    music::PianoProfile p = m_initial;
    p.enabled = m_enabled->isChecked();
    p.midiChannel = m_channel->value();
    if (m_feelStyle) p.feelStyle = (music::PianoFeelStyle)m_feelStyle->currentData().toInt();

    p.lhMinMidiNote = m_lhMin->value();
    p.lhMaxMidiNote = m_lhMax->value();
    if (p.lhMinMidiNote > p.lhMaxMidiNote) std::swap(p.lhMinMidiNote, p.lhMaxMidiNote);
    p.rhMinMidiNote = m_rhMin->value();
    p.rhMaxMidiNote = m_rhMax->value();
    if (p.rhMinMidiNote > p.rhMaxMidiNote) std::swap(p.rhMinMidiNote, p.rhMaxMidiNote);

    p.microJitterMs = m_jitterMs->value();
    p.laidBackMs = m_laidBackMs->value();
    p.pushMs = m_pushMs->value();
    p.driftMaxMs = m_driftMaxMs->value();
    p.driftRate = m_driftRate->value();

    p.baseVelocity = m_baseVel->value();
    p.velocityVariance = m_velVar->value();
    p.accentDownbeat = m_accentDown->value();
    p.accentBackbeat = m_accentBack->value();

    p.compDensity = m_compDensity->value();
    p.anticipationProb = m_anticipation->value();
    p.syncopationProb = m_syncop->value();
    p.restProb = m_restProb->value();

    p.preferRootless = m_preferRootless->isChecked();
    p.rootlessProb = m_rootlessProb->value();
    p.drop2Prob = m_drop2Prob->value();
    p.quartalProb = m_quartalProb->value();
    p.clusterProb = m_clusterProb->value();
    p.tensionProb = m_tensionProb->value();
    p.avoidRootProb = m_avoidRootProb->value();
    p.avoidThirdProb = m_avoidThirdProb->value();
    p.maxHandLeap = m_maxHandLeap->value();
    p.voiceLeadingStrength = m_voiceLeading->value();
    p.repetitionPenalty = m_repeatPenalty->value();

    p.fillProbPhraseEnd = m_fillPhraseEnd->value();
    p.fillProbAnyBeat = m_fillAnyBeat->value();
    p.phraseLengthBars = m_phraseBars->value();
    p.fillMaxNotes = m_fillMaxNotes->value();
    p.fillMinMidiNote = m_fillMinNote->value();
    p.fillMaxMidiNote = m_fillMaxNote->value();
    if (p.fillMinMidiNote > p.fillMaxMidiNote) std::swap(p.fillMinMidiNote, p.fillMaxMidiNote);

    p.pedalEnabled = m_pedalEnabled->isChecked();
    p.pedalReleaseOnChordChange = m_pedalReleaseOnChange->isChecked();
    p.pedalDownValue = m_pedalDown->value();
    p.pedalUpValue = m_pedalUp->value();
    p.pedalMinHoldMs = m_pedalMinHoldMs->value();
    p.pedalMaxHoldMs = m_pedalMaxHoldMs->value();
    if (p.pedalMinHoldMs > p.pedalMaxHoldMs) std::swap(p.pedalMinHoldMs, p.pedalMaxHoldMs);
    p.pedalChangeProb = m_pedalChangeProb->value();

    if (m_presetCombo) p.name = m_presetCombo->currentText().trimmed();
    if (m_reasoningLogEnabled) p.reasoningLogEnabled = m_reasoningLogEnabled->isChecked();
    return p;
}

void PianoStyleEditorDialog::emitPreview() {
    emit profilePreview(profileFromUi());
}

void PianoStyleEditorDialog::appendLiveLogLine(const QString& line) {
    if (!m_reasoningLogEnabled || !m_reasoningLogEnabled->isChecked()) return;
    const QString t = line.trimmed();
    if (t.isEmpty()) return;
    m_pendingLog.push_back(t);
}

void PianoStyleEditorDialog::setLiveLogActive(bool active) {
    if (m_logConn) {
        QObject::disconnect(m_logConn);
        m_logConn = QMetaObject::Connection{};
    }
    if (m_logFlushTimer) {
        if (!active) m_logFlushTimer->stop();
        else if (!m_logFlushTimer->isActive()) m_logFlushTimer->start();
    }
    if (!active) {
        m_pendingLog.clear();
        return;
    }
    if (!m_playback) return;
    m_logConn = connect(m_playback, &playback::BandPlaybackEngine::pianoLogLine,
                        this, &PianoStyleEditorDialog::appendLiveLogLine, Qt::QueuedConnection);
}

void PianoStyleEditorDialog::flushPendingLog() {
    if (!m_liveLog || !m_reasoningLogEnabled || !m_reasoningLogEnabled->isChecked()) return;
    if (m_pendingLog.isEmpty()) return;

    constexpr int kMaxDrain = 40;
    const int n = std::min<int>(kMaxDrain, int(m_pendingLog.size()));
    for (int i = 0; i < n; ++i) {
        m_liveLog->addItem(m_pendingLog[i]);
    }
    m_pendingLog.erase(m_pendingLog.begin(), m_pendingLog.begin() + n);

    constexpr int kMaxLines = 300;
    while (m_liveLog->count() > kMaxLines) {
        delete m_liveLog->takeItem(0);
    }
    m_liveLog->scrollToBottom();
}

