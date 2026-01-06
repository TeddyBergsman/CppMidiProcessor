#include "GrooveLabWindow.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include "midiprocessor.h"

using namespace virtuoso;

GrooveLabWindow::GrooveLabWindow(MidiProcessor* midi, QWidget* parent)
    : QMainWindow(parent)
    , m_midi(midi)
    , m_engine(this) {
    m_grooveRegistry = groove::GrooveRegistry::builtins();
    setWindowTitle("Groove Lab");
    resize(960, 640);
    buildUi();
    wireEngineOutputs();
    resetPatternState();
}

void GrooveLabWindow::buildUi() {
    QWidget* root = new QWidget(this);
    setCentralWidget(root);

    QHBoxLayout* main = new QHBoxLayout(root);

    // Left controls
    QScrollArea* leftScroll = new QScrollArea(this);
    leftScroll->setWidgetResizable(true);
    leftScroll->setMinimumWidth(380);
    leftScroll->setFrameShape(QFrame::NoFrame);

    QWidget* left = new QWidget(this);
    left->setMinimumWidth(360);
    QVBoxLayout* l = new QVBoxLayout(left);
    leftScroll->setWidget(left);

    auto mkBox = [&](const QString& title) -> QGroupBox* {
        QGroupBox* b = new QGroupBox(title, this);
        b->setStyleSheet("QGroupBox { font-weight: bold; }");
        return b;
    };

    // Transport
    {
        QGroupBox* box = mkBox("Transport");
        QHBoxLayout* row = new QHBoxLayout(box);
        m_start = new QPushButton("Start", this);
        m_stop = new QPushButton("Stop", this);
        row->addWidget(m_start);
        row->addWidget(m_stop);
        row->addStretch(1);
        l->addWidget(box);

        connect(m_start, &QPushButton::clicked, this, &GrooveLabWindow::onStart);
        connect(m_stop, &QPushButton::clicked, this, &GrooveLabWindow::onStop);
    }

    // Global timing
    {
        QGroupBox* box = mkBox("Global timing");
        QGridLayout* g = new QGridLayout(box);

        m_bpm = new QSpinBox(this);
        m_bpm->setRange(30, 300);
        m_bpm->setValue(120);

        m_tsNum = new QSpinBox(this);
        m_tsNum->setRange(1, 32);
        m_tsNum->setValue(4);
        m_tsDen = new QSpinBox(this);
        m_tsDen->setRange(1, 32);
        m_tsDen->setValue(4);

        g->addWidget(new QLabel("BPM:", this), 0, 0);
        g->addWidget(m_bpm, 0, 1);
        g->addWidget(new QLabel("Time sig:", this), 1, 0);
        QWidget* tsw = new QWidget(this);
        QHBoxLayout* tsL = new QHBoxLayout(tsw);
        tsL->setContentsMargins(0, 0, 0, 0);
        tsL->addWidget(m_tsNum);
        tsL->addWidget(new QLabel("/", this));
        tsL->addWidget(m_tsDen);
        g->addWidget(tsw, 1, 1);

        l->addWidget(box);
    }

    // Jazz preset (optional)
    {
        QGroupBox* box = mkBox("Jazz preset");
        QGridLayout* g = new QGridLayout(box);

        m_preset = new QComboBox(this);
        m_preset->addItem("(none)", "");
        const auto presets = m_grooveRegistry.allStylePresets();
        for (const auto* p : presets) {
            if (!p) continue;
            m_preset->addItem(p->name, p->key);
        }

        g->addWidget(new QLabel("Preset:", this), 0, 0);
        g->addWidget(m_preset, 0, 1);

        l->addWidget(box);
    }

    // Preset notes (driver hooks)
    {
        QGroupBox* box = mkBox("Preset notes (driver hooks)");
        QVBoxLayout* v = new QVBoxLayout(box);
        m_presetNotes = new QLabel(this);
        m_presetNotes->setWordWrap(true);
        m_presetNotes->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_presetNotes->setStyleSheet("QLabel { font-weight: normal; font-size: 10pt; color: #ddd; }");
        m_presetNotes->setText("(select a preset)");
        v->addWidget(m_presetNotes);
        l->addWidget(box);
    }

    // Groove template
    {
        QGroupBox* box = mkBox("Groove template");
        QGridLayout* g = new QGridLayout(box);

        m_template = new QComboBox(this);
        const auto tpls = m_grooveRegistry.allGrooveTemplates();
        for (const auto* t : tpls) {
            if (!t) continue;
            m_template->addItem(QString("%1 — %2").arg(t->category, t->name), t->key);
        }
        const int idx = m_template->findData("jazz_swing_2to1");
        m_template->setCurrentIndex(idx >= 0 ? idx : 0);

        m_templateAmount = new QDoubleSpinBox(this);
        m_templateAmount->setRange(0.0, 1.0);
        m_templateAmount->setSingleStep(0.05);
        m_templateAmount->setValue(0.80);

        g->addWidget(new QLabel("Template:", this), 0, 0);
        g->addWidget(m_template, 0, 1);
        g->addWidget(new QLabel("Amount:", this), 1, 0);
        g->addWidget(m_templateAmount, 1, 1);

        l->addWidget(box);
    }
    // Instrument groove profile
    {
        QGroupBox* box = mkBox("Agent lanes");
        QVBoxLayout* outer = new QVBoxLayout(box);

        // ---- Lane A ----
        QGroupBox* laneA = mkBox("Lane A");
        QGridLayout* g = new QGridLayout(laneA);

        m_agent = new QComboBox(this);
        m_agent->addItems({"Piano", "Bass", "Drums", "Guitar"});
        m_agent->setCurrentText("Piano");

        m_channel = new QSpinBox(this);
        m_channel->setRange(1, 16);
        m_channel->setValue(4);

        m_seed = new QSpinBox(this);
        m_seed->setRange(1, 9999999);
        m_seed->setValue(1);

        m_pushMs = new QSpinBox(this);
        m_pushMs->setRange(0, 80);
        m_pushMs->setValue(0);

        m_laidBackMs = new QSpinBox(this);
        m_laidBackMs->setRange(0, 80);
        m_laidBackMs->setValue(8);

        m_jitterMs = new QSpinBox(this);
        m_jitterMs->setRange(0, 30);
        m_jitterMs->setValue(4);

        m_attackVarMs = new QSpinBox(this);
        m_attackVarMs->setRange(0, 30);
        m_attackVarMs->setValue(3);

        m_driftMaxMs = new QSpinBox(this);
        m_driftMaxMs->setRange(0, 80);
        m_driftMaxMs->setValue(12);

        m_driftRate = new QDoubleSpinBox(this);
        m_driftRate->setRange(0.0, 1.0);
        m_driftRate->setSingleStep(0.05);
        m_driftRate->setValue(0.18);

        m_baseVel = new QSpinBox(this);
        m_baseVel->setRange(1, 127);
        m_baseVel->setValue(72);

        m_velJitter = new QSpinBox(this);
        m_velJitter->setRange(0, 40);
        m_velJitter->setValue(12);

        m_accentDownbeat = new QDoubleSpinBox(this);
        m_accentDownbeat->setRange(0.5, 1.8);
        m_accentDownbeat->setSingleStep(0.05);
        m_accentDownbeat->setValue(1.08);

        m_accentBackbeat = new QDoubleSpinBox(this);
        m_accentBackbeat->setRange(0.5, 1.8);
        m_accentBackbeat->setSingleStep(0.05);
        m_accentBackbeat->setValue(0.95);

        m_gatePct = new QDoubleSpinBox(this);
        m_gatePct->setRange(0.10, 1.00);
        m_gatePct->setSingleStep(0.05);
        m_gatePct->setValue(0.80);

        int r = 0;
        g->addWidget(new QLabel("Agent:", this), r, 0); g->addWidget(m_agent, r, 1); r++;
        g->addWidget(new QLabel("MIDI ch:", this), r, 0); g->addWidget(m_channel, r, 1); r++;
        g->addWidget(new QLabel("Seed:", this), r, 0); g->addWidget(m_seed, r, 1); r++;
        g->addWidget(new QLabel("Push ms:", this), r, 0); g->addWidget(m_pushMs, r, 1); r++;
        g->addWidget(new QLabel("Laid-back ms:", this), r, 0); g->addWidget(m_laidBackMs, r, 1); r++;
        g->addWidget(new QLabel("Jitter ms:", this), r, 0); g->addWidget(m_jitterMs, r, 1); r++;
        g->addWidget(new QLabel("Attack var ms:", this), r, 0); g->addWidget(m_attackVarMs, r, 1); r++;
        g->addWidget(new QLabel("Drift max ms:", this), r, 0); g->addWidget(m_driftMaxMs, r, 1); r++;
        g->addWidget(new QLabel("Drift rate:", this), r, 0); g->addWidget(m_driftRate, r, 1); r++;
        g->addWidget(new QLabel("Base vel:", this), r, 0); g->addWidget(m_baseVel, r, 1); r++;
        g->addWidget(new QLabel("Vel jitter:", this), r, 0); g->addWidget(m_velJitter, r, 1); r++;
        g->addWidget(new QLabel("Accent beat1:", this), r, 0); g->addWidget(m_accentDownbeat, r, 1); r++;
        g->addWidget(new QLabel("Accent 2/4:", this), r, 0); g->addWidget(m_accentBackbeat, r, 1); r++;
        g->addWidget(new QLabel("Gate %:", this), r, 0); g->addWidget(m_gatePct, r, 1); r++;

        outer->addWidget(laneA);

        // ---- Lane B ----
        QGroupBox* laneB = mkBox("Lane B");
        QGridLayout* gb = new QGridLayout(laneB);

        m_laneBEnabled = new QCheckBox("Enable Lane B", this);
        m_laneBEnabled->setChecked(true);

        m_agentB = new QComboBox(this);
        m_agentB->addItems({"Piano", "Bass", "Drums", "Guitar"});
        m_agentB->setCurrentText("Bass");

        m_channelB = new QSpinBox(this);
        m_channelB->setRange(1, 16);
        m_channelB->setValue(3);

        m_seedB = new QSpinBox(this);
        m_seedB->setRange(1, 9999999);
        m_seedB->setValue(2);

        m_pushMsB = new QSpinBox(this);
        m_pushMsB->setRange(0, 80);
        m_pushMsB->setValue(0);

        m_laidBackMsB = new QSpinBox(this);
        m_laidBackMsB->setRange(0, 80);
        m_laidBackMsB->setValue(2);

        m_jitterMsB = new QSpinBox(this);
        m_jitterMsB->setRange(0, 30);
        m_jitterMsB->setValue(2);

        m_attackVarMsB = new QSpinBox(this);
        m_attackVarMsB->setRange(0, 30);
        m_attackVarMsB->setValue(2);

        m_driftMaxMsB = new QSpinBox(this);
        m_driftMaxMsB->setRange(0, 80);
        m_driftMaxMsB->setValue(8);

        m_driftRateB = new QDoubleSpinBox(this);
        m_driftRateB->setRange(0.0, 1.0);
        m_driftRateB->setSingleStep(0.05);
        m_driftRateB->setValue(0.15);

        m_baseVelB = new QSpinBox(this);
        m_baseVelB->setRange(1, 127);
        m_baseVelB->setValue(82);

        m_velJitterB = new QSpinBox(this);
        m_velJitterB->setRange(0, 40);
        m_velJitterB->setValue(10);

        m_accentDownbeatB = new QDoubleSpinBox(this);
        m_accentDownbeatB->setRange(0.5, 1.8);
        m_accentDownbeatB->setSingleStep(0.05);
        m_accentDownbeatB->setValue(1.10);

        m_accentBackbeatB = new QDoubleSpinBox(this);
        m_accentBackbeatB->setRange(0.5, 1.8);
        m_accentBackbeatB->setSingleStep(0.05);
        m_accentBackbeatB->setValue(0.85);

        m_gatePctB = new QDoubleSpinBox(this);
        m_gatePctB->setRange(0.10, 1.00);
        m_gatePctB->setSingleStep(0.05);
        m_gatePctB->setValue(0.85);

        int rb = 0;
        gb->addWidget(m_laneBEnabled, rb, 0, 1, 2); rb++;
        gb->addWidget(new QLabel("Agent:", this), rb, 0); gb->addWidget(m_agentB, rb, 1); rb++;
        gb->addWidget(new QLabel("MIDI ch:", this), rb, 0); gb->addWidget(m_channelB, rb, 1); rb++;
        gb->addWidget(new QLabel("Seed:", this), rb, 0); gb->addWidget(m_seedB, rb, 1); rb++;
        gb->addWidget(new QLabel("Push ms:", this), rb, 0); gb->addWidget(m_pushMsB, rb, 1); rb++;
        gb->addWidget(new QLabel("Laid-back ms:", this), rb, 0); gb->addWidget(m_laidBackMsB, rb, 1); rb++;
        gb->addWidget(new QLabel("Jitter ms:", this), rb, 0); gb->addWidget(m_jitterMsB, rb, 1); rb++;
        gb->addWidget(new QLabel("Attack var ms:", this), rb, 0); gb->addWidget(m_attackVarMsB, rb, 1); rb++;
        gb->addWidget(new QLabel("Drift max ms:", this), rb, 0); gb->addWidget(m_driftMaxMsB, rb, 1); rb++;
        gb->addWidget(new QLabel("Drift rate:", this), rb, 0); gb->addWidget(m_driftRateB, rb, 1); rb++;
        gb->addWidget(new QLabel("Base vel:", this), rb, 0); gb->addWidget(m_baseVelB, rb, 1); rb++;
        gb->addWidget(new QLabel("Vel jitter:", this), rb, 0); gb->addWidget(m_velJitterB, rb, 1); rb++;
        gb->addWidget(new QLabel("Accent beat1:", this), rb, 0); gb->addWidget(m_accentDownbeatB, rb, 1); rb++;
        gb->addWidget(new QLabel("Accent 2/4:", this), rb, 0); gb->addWidget(m_accentBackbeatB, rb, 1); rb++;
        gb->addWidget(new QLabel("Gate %:", this), rb, 0); gb->addWidget(m_gatePctB, rb, 1); rb++;

        outer->addWidget(laneB);
        l->addWidget(box);
    }

    // Test pattern
    {
        QGroupBox* box = mkBox("Test pattern");
        QGridLayout* g = new QGridLayout(box);

        m_pattern = new QComboBox(this);
        m_pattern->addItem(patternName(PatternKind::QuarterClick), int(PatternKind::QuarterClick));
        m_pattern->addItem(patternName(PatternKind::SwingEighths), int(PatternKind::SwingEighths));
        m_pattern->addItem(patternName(PatternKind::TripletEighths), int(PatternKind::TripletEighths));
        m_pattern->setCurrentIndex(1);

        m_testMidi = new QSpinBox(this);
        m_testMidi->setRange(0, 127);
        m_testMidi->setValue(60);

        m_testMidiB = new QSpinBox(this);
        m_testMidiB->setRange(0, 127);
        m_testMidiB->setValue(43); // G1-ish (bass-friendly) for default audition

        m_lookaheadMs = new QSpinBox(this);
        m_lookaheadMs->setRange(50, 2000);
        m_lookaheadMs->setValue(300);

        g->addWidget(new QLabel("Pattern:", this), 0, 0);
        g->addWidget(m_pattern, 0, 1);
        g->addWidget(new QLabel("Test MIDI:", this), 1, 0);
        g->addWidget(m_testMidi, 1, 1);
        g->addWidget(new QLabel("Test MIDI (B):", this), 2, 0);
        g->addWidget(m_testMidiB, 2, 1);
        g->addWidget(new QLabel("Lookahead ms:", this), 3, 0);
        g->addWidget(m_lookaheadMs, 3, 1);

        l->addWidget(box);
    }

    // Groove lock (Lane B -> Lane A)
    {
        QGroupBox* box = mkBox("Groove lock (Lane B → Lane A)");
        QGridLayout* g = new QGridLayout(box);

        m_lockMode = new QComboBox(this);
        m_lockMode->addItems({"Off", "Downbeats only", "All events"});
        m_lockMode->setCurrentText("Downbeats only");

        m_lockStrength = new QDoubleSpinBox(this);
        m_lockStrength->setRange(0.0, 1.0);
        m_lockStrength->setSingleStep(0.05);
        m_lockStrength->setValue(1.0);

        g->addWidget(new QLabel("Mode:", this), 0, 0);
        g->addWidget(m_lockMode, 0, 1);
        g->addWidget(new QLabel("Strength:", this), 1, 0);
        g->addWidget(m_lockStrength, 1, 1);

        l->addWidget(box);
    }

    l->addStretch(1);
    main->addWidget(leftScroll);

    // Right log
    QWidget* right = new QWidget(this);
    QVBoxLayout* r = new QVBoxLayout(right);
    QHBoxLayout* top = new QHBoxLayout;
    top->addWidget(new QLabel("TheoryEvent JSON:", this));
    top->addStretch(1);
    m_clearLog = new QPushButton("Clear", this);
    top->addWidget(m_clearLog);
    r->addLayout(top);

    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setStyleSheet("QTextEdit { font-family: Menlo, Monaco, Consolas; font-size: 10pt; }");
    r->addWidget(m_log, 1);
    main->addWidget(right, 1);

    connect(m_clearLog, &QPushButton::clicked, this, &GrooveLabWindow::onClearLog);

    // Tick timer drives lookahead scheduling.
    m_tick = new QTimer(this);
    m_tick->setInterval(20);
    m_tick->setTimerType(Qt::PreciseTimer);
    connect(m_tick, &QTimer::timeout, this, &GrooveLabWindow::onTickSchedule);

    // Debounced auto-apply: changing any control should immediately apply and (if running) restart.
    m_applyDebounce = new QTimer(this);
    m_applyDebounce->setSingleShot(true);
    m_applyDebounce->setInterval(80);
    m_applyDebounce->setTimerType(Qt::PreciseTimer);
    connect(m_applyDebounce, &QTimer::timeout, this, [this]() { applyNow(/*restartIfRunning=*/true); });

    onAnySettingChanged();
}

void GrooveLabWindow::wireEngineOutputs() {
    if (m_midi) {
        connect(&m_engine, &engine::VirtuosoEngine::noteOn, m_midi, &MidiProcessor::sendVirtualNoteOn, Qt::QueuedConnection);
        connect(&m_engine, &engine::VirtuosoEngine::noteOff, m_midi, &MidiProcessor::sendVirtualNoteOff, Qt::QueuedConnection);
        connect(&m_engine, &engine::VirtuosoEngine::allNotesOff, m_midi, &MidiProcessor::sendVirtualAllNotesOff, Qt::QueuedConnection);
        connect(&m_engine, &engine::VirtuosoEngine::cc, m_midi, &MidiProcessor::sendVirtualCC, Qt::QueuedConnection);
    }
    connect(&m_engine, &engine::VirtuosoEngine::theoryEventJson, this, &GrooveLabWindow::onTheoryJson, Qt::QueuedConnection);
}

void GrooveLabWindow::onStart() {
    applyNow(/*restartIfRunning=*/false);
    m_engine.start();
    resetPatternState();
    m_hA.reset();
    m_hB.reset();
    scheduleAhead();
    m_tick->start();
}

void GrooveLabWindow::onStop() {
    m_tick->stop();
    m_engine.stop();
}

void GrooveLabWindow::onClearLog() {
    if (m_log) m_log->clear();
}

void GrooveLabWindow::onApplySettings() {
    if (!m_bpm || !m_tsNum || !m_tsDen) return;

    // If a preset is selected, apply it by mutating UI fields (then we fall through to apply).
    const QString presetKey = m_preset ? m_preset->currentData().toString() : QString();
    if (!presetKey.trimmed().isEmpty()) {
        const auto* p = m_grooveRegistry.stylePreset(presetKey);
        if (p) {
            if (m_presetNotes) {
                QStringList lines;
                if (p->articulationNotes.contains("Drums")) {
                    lines << QString("Drums: %1").arg(p->articulationNotes.value("Drums"));
                }
                if (p->articulationNotes.contains("Piano")) {
                    lines << QString("Piano: %1").arg(p->articulationNotes.value("Piano"));
                }
                if (p->articulationNotes.contains("Bass")) {
                    lines << QString("Bass: %1").arg(p->articulationNotes.value("Bass"));
                }
                m_presetNotes->setText(lines.isEmpty() ? "(no notes)" : lines.join("\n\n"));
            }

            // Tempo + time signature
            if (m_bpm) m_bpm->setValue(p->defaultBpm);
            if (m_tsNum) m_tsNum->setValue(p->defaultTimeSig.num);
            if (m_tsDen) m_tsDen->setValue(p->defaultTimeSig.den);

            // Template selection
            if (m_template) {
                const int ti = m_template->findData(p->grooveTemplateKey);
                if (ti >= 0) m_template->setCurrentIndex(ti);
            }
            if (m_templateAmount) {
                m_templateAmount->setValue(qBound(0.0, p->templateAmount, 1.0));
            }

            // Instrument profiles: map Piano->LaneA, Bass->LaneB if present.
            if (p->instrumentProfiles.contains("Piano")) {
                const auto ip = p->instrumentProfiles.value("Piano");
                if (m_agent) m_agent->setCurrentText("Piano");
                if (m_seed) m_seed->setValue(int(ip.humanizeSeed == 0 ? 1 : ip.humanizeSeed));
                if (m_laidBackMs) m_laidBackMs->setValue(ip.laidBackMs);
                if (m_pushMs) m_pushMs->setValue(ip.pushMs);
                if (m_jitterMs) m_jitterMs->setValue(ip.microJitterMs);
                if (m_attackVarMs) m_attackVarMs->setValue(ip.attackVarianceMs);
                if (m_driftMaxMs) m_driftMaxMs->setValue(ip.driftMaxMs);
                if (m_driftRate) m_driftRate->setValue(ip.driftRate);
                if (m_velJitter) m_velJitter->setValue(ip.velocityJitter);
                if (m_accentDownbeat) m_accentDownbeat->setValue(ip.accentDownbeat);
                if (m_accentBackbeat) m_accentBackbeat->setValue(ip.accentBackbeat);
            }
            if (p->instrumentProfiles.contains("Bass")) {
                const auto ip = p->instrumentProfiles.value("Bass");
                if (m_laneBEnabled) m_laneBEnabled->setChecked(true);
                if (m_agentB) m_agentB->setCurrentText("Bass");
                if (m_seedB) m_seedB->setValue(int(ip.humanizeSeed == 0 ? 1 : ip.humanizeSeed));
                if (m_laidBackMsB) m_laidBackMsB->setValue(ip.laidBackMs);
                if (m_pushMsB) m_pushMsB->setValue(ip.pushMs);
                if (m_jitterMsB) m_jitterMsB->setValue(ip.microJitterMs);
                if (m_attackVarMsB) m_attackVarMsB->setValue(ip.attackVarianceMs);
                if (m_driftMaxMsB) m_driftMaxMsB->setValue(ip.driftMaxMs);
                if (m_driftRateB) m_driftRateB->setValue(ip.driftRate);
                if (m_velJitterB) m_velJitterB->setValue(ip.velocityJitter);
                if (m_accentDownbeatB) m_accentDownbeatB->setValue(ip.accentDownbeat);
                if (m_accentBackbeatB) m_accentBackbeatB->setValue(ip.accentBackbeat);
            }
        }
    } else {
        if (m_presetNotes) m_presetNotes->setText("(select a preset)");
    }

    m_engine.setTempoBpm(m_bpm->value());
    groove::TimeSignature ts;
    ts.num = m_tsNum->value();
    ts.den = m_tsDen->value();
    if (ts.den <= 0) ts.den = 4;
    m_engine.setTimeSignature(ts);

    // Select groove template
    const groove::GrooveTemplate gt = currentGrooveTemplate();
    m_hA.setGrooveTemplate(gt);
    m_hB.setGrooveTemplate(gt);

    m_engine.setInstrumentGrooveProfile(laneAAgentId(), currentInstrumentProfileLaneA());
    if (m_laneBEnabled && m_laneBEnabled->isChecked()) {
        m_engine.setInstrumentGrooveProfile(laneBAgentId(), currentInstrumentProfileLaneB());
    }

    // Keep local humanizers in sync with UI so we can do deterministic groove-lock blending.
    m_hA.setProfile(currentInstrumentProfileLaneA());
    m_hB.setProfile(currentInstrumentProfileLaneB());
}

void GrooveLabWindow::applyNow(bool restartIfRunning) {
    const bool wasRunning = m_engine.isRunning();
    if (wasRunning && restartIfRunning) {
        m_tick->stop();
        m_engine.stop();
    }
    onApplySettings();
    if (wasRunning && restartIfRunning) {
        m_engine.start();
        resetPatternState();
        m_hA.reset();
        m_hB.reset();
        scheduleAhead();
        m_tick->start();
    }
}

void GrooveLabWindow::onAnySettingChanged() {
    // Hook every control we care about.
    auto spin = [&](QSpinBox* s) {
        if (!s) return;
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [this]() { m_applyDebounce->start(); });
    };
    auto dspin = [&](QDoubleSpinBox* s) {
        if (!s) return;
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this]() { m_applyDebounce->start(); });
    };
    auto combo = [&](QComboBox* c) {
        if (!c) return;
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { m_applyDebounce->start(); });
    };
    auto check = [&](QCheckBox* c) {
        if (!c) return;
        connect(c, &QCheckBox::toggled, this, [this]() { m_applyDebounce->start(); });
    };

    spin(m_bpm);
    spin(m_tsNum);
    spin(m_tsDen);
    combo(m_preset);
    combo(m_template);
    dspin(m_templateAmount);

    combo(m_agent);
    spin(m_channel);
    spin(m_seed);
    spin(m_pushMs);
    spin(m_laidBackMs);
    spin(m_jitterMs);
    spin(m_attackVarMs);
    spin(m_driftMaxMs);
    dspin(m_driftRate);
    spin(m_baseVel);
    spin(m_velJitter);
    dspin(m_accentDownbeat);
    dspin(m_accentBackbeat);
    dspin(m_gatePct);

    check(m_laneBEnabled);
    combo(m_agentB);
    spin(m_channelB);
    spin(m_seedB);
    spin(m_pushMsB);
    spin(m_laidBackMsB);
    spin(m_jitterMsB);
    spin(m_attackVarMsB);
    spin(m_driftMaxMsB);
    dspin(m_driftRateB);
    spin(m_baseVelB);
    spin(m_velJitterB);
    dspin(m_accentDownbeatB);
    dspin(m_accentBackbeatB);
    dspin(m_gatePctB);

    combo(m_pattern);
    spin(m_testMidi);
    spin(m_testMidiB);
    spin(m_lookaheadMs);

    combo(m_lockMode);
    dspin(m_lockStrength);
}

void GrooveLabWindow::onTickSchedule() {
    scheduleAhead();
}

void GrooveLabWindow::onTheoryJson(const QString& json) {
    if (!m_log) return;
    if (json.trimmed().isEmpty()) return;
    m_log->append(json);
}

groove::GrooveTemplate GrooveLabWindow::currentGrooveTemplate() const {
    const QString key = (m_template ? m_template->currentData().toString() : QString("jazz_swing_2to1"));
    const double amt = m_templateAmount ? m_templateAmount->value() : 1.0;
    const auto* base = m_grooveRegistry.grooveTemplate(key);
    groove::GrooveTemplate out = base ? *base : groove::GrooveTemplate{};
    if (out.key.trimmed().isEmpty()) {
        // fallback to a known built-in
        const auto* fb = m_grooveRegistry.grooveTemplate("jazz_swing_2to1");
        if (fb) out = *fb;
    }
    out.amount = amt;
    return out;
}

QString GrooveLabWindow::laneAAgentId() const {
    const QString inst = m_agent ? m_agent->currentText() : QString("Piano");
    return inst + "#A";
}

QString GrooveLabWindow::laneBAgentId() const {
    const QString inst = m_agentB ? m_agentB->currentText() : QString("Bass");
    return inst + "#B";
}

groove::InstrumentGrooveProfile GrooveLabWindow::currentInstrumentProfileLaneA() const {
    groove::InstrumentGrooveProfile p;
    p.instrument = m_agent ? m_agent->currentText() : QString("Piano");
    p.humanizeSeed = quint32(m_seed ? m_seed->value() : 1);

    p.pushMs = m_pushMs ? m_pushMs->value() : 0;
    p.laidBackMs = m_laidBackMs ? m_laidBackMs->value() : 0;
    p.microJitterMs = m_jitterMs ? m_jitterMs->value() : 0;
    p.attackVarianceMs = m_attackVarMs ? m_attackVarMs->value() : 0;
    p.driftMaxMs = m_driftMaxMs ? m_driftMaxMs->value() : 0;
    p.driftRate = m_driftRate ? m_driftRate->value() : 0.0;

    p.velocityJitter = m_velJitter ? m_velJitter->value() : 0;
    p.accentDownbeat = m_accentDownbeat ? m_accentDownbeat->value() : 1.0;
    p.accentBackbeat = m_accentBackbeat ? m_accentBackbeat->value() : 1.0;

    // Groove Lab wants audible differences: increase clamp dynamically so timing changes are not subtle.
    // (This does not change core defaults; only the harness profile.)
    const int roughMax = (p.pushMs + p.laidBackMs + p.microJitterMs + p.attackVarianceMs + p.driftMaxMs + 10);
    p.clampMsLoose = qBound(32, roughMax, 140);
    p.clampMsStructural = qBound(18, roughMax / 2, 100);

    return p;
}

groove::InstrumentGrooveProfile GrooveLabWindow::currentInstrumentProfileLaneB() const {
    groove::InstrumentGrooveProfile p;
    p.instrument = m_agentB ? m_agentB->currentText() : QString("Bass");
    p.humanizeSeed = quint32(m_seedB ? m_seedB->value() : 2);

    p.pushMs = m_pushMsB ? m_pushMsB->value() : 0;
    p.laidBackMs = m_laidBackMsB ? m_laidBackMsB->value() : 0;
    p.microJitterMs = m_jitterMsB ? m_jitterMsB->value() : 0;
    p.attackVarianceMs = m_attackVarMsB ? m_attackVarMsB->value() : 0;
    p.driftMaxMs = m_driftMaxMsB ? m_driftMaxMsB->value() : 0;
    p.driftRate = m_driftRateB ? m_driftRateB->value() : 0.0;

    p.velocityJitter = m_velJitterB ? m_velJitterB->value() : 0;
    p.accentDownbeat = m_accentDownbeatB ? m_accentDownbeatB->value() : 1.0;
    p.accentBackbeat = m_accentBackbeatB ? m_accentBackbeatB->value() : 1.0;

    const int roughMax = (p.pushMs + p.laidBackMs + p.microJitterMs + p.attackVarianceMs + p.driftMaxMs + 10);
    p.clampMsLoose = qBound(32, roughMax, 140);
    p.clampMsStructural = qBound(18, roughMax / 2, 100);

    return p;
}

GrooveLabWindow::PatternKind GrooveLabWindow::currentPattern() const {
    if (!m_pattern) return PatternKind::SwingEighths;
    return PatternKind(m_pattern->currentData().toInt());
}

QString GrooveLabWindow::patternName(PatternKind k) {
    switch (k) {
    case PatternKind::QuarterClick: return "Quarter notes (click)";
    case PatternKind::SwingEighths: return "Eighths (swing test)";
    case PatternKind::TripletEighths: return "Triplet grid (3)";
    }
    return "Pattern";
}

void GrooveLabWindow::resetPatternState() {
    m_nextPos.barIndex = 0;
    m_nextPos.withinBarWhole = groove::Rational(0, 1);
    m_lastScheduledOnMs = -1;
}

groove::Rational GrooveLabWindow::stepWholeFor(PatternKind k, const groove::TimeSignature& ts) {
    const groove::Rational beat = groove::GrooveGrid::beatDurationWhole(ts);
    switch (k) {
    case PatternKind::QuarterClick:
        return beat;
    case PatternKind::SwingEighths:
        return beat / 2;
    case PatternKind::TripletEighths:
        return beat / 3;
    }
    return beat;
}

void GrooveLabWindow::advancePos(groove::GridPos& p,
                                 const groove::Rational& stepWhole,
                                 const groove::TimeSignature& ts) {
    const groove::Rational bar = groove::GrooveGrid::barDurationWhole(ts);
    p.withinBarWhole = p.withinBarWhole + stepWhole;
    while (p.withinBarWhole >= bar) {
        p.barIndex += 1;
        p.withinBarWhole = p.withinBarWhole - bar;
    }
}

void GrooveLabWindow::scheduleAhead() {
    if (!m_engine.isRunning()) return;

    groove::TimeSignature ts;
    ts.num = m_tsNum ? m_tsNum->value() : 4;
    ts.den = m_tsDen ? m_tsDen->value() : 4;

    const int bpm = m_bpm ? m_bpm->value() : 120;
    const int lookaheadMs = m_lookaheadMs ? m_lookaheadMs->value() : 300;
    const qint64 now = m_engine.elapsedMs();
    const qint64 horizon = now + lookaheadMs;

    const PatternKind pat = currentPattern();
    const groove::Rational step = stepWholeFor(pat, ts);
    const double gatePct = m_gatePct ? m_gatePct->value() : 0.80;
    groove::Rational dur = step;
    // dur = step * gatePct (approx as rational by using 1000ths)
    const int gate1000 = qBound(50, int(llround(gatePct * 1000.0)), 1000);
    dur = dur * gate1000 / 1000;

    const QString agent = laneAAgentId();
    const int ch = m_channel ? m_channel->value() : 4;
    const int midi = m_testMidi ? m_testMidi->value() : 60;
    const int baseVel = m_baseVel ? m_baseVel->value() : 72;

    const bool laneBOn = (m_laneBEnabled && m_laneBEnabled->isChecked());
    const QString agentB = laneBAgentId();
    const int chB = m_channelB ? m_channelB->value() : 3;
    const int midiB = m_testMidiB ? m_testMidiB->value() : 43;
    const int baseVelB = m_baseVelB ? m_baseVelB->value() : 82;
    const double gatePctB = m_gatePctB ? m_gatePctB->value() : 0.85;
    groove::Rational durB = step;
    const int gate1000B = qBound(50, int(llround(gatePctB * 1000.0)), 1000);
    durB = durB * gate1000B / 1000;

    const QString lockMode = m_lockMode ? m_lockMode->currentText() : QString("Off");
    const double lockStrength = m_lockStrength ? m_lockStrength->value() : 1.0;

    // Schedule until the next on-time exceeds horizon.
    for (int guard = 0; guard < 2048; ++guard) {
        const qint64 onMs = groove::GrooveGrid::posToMs(m_nextPos, ts, bpm);
        // If we fell behind (e.g., UI stalled), skip forward without scheduling late events.
        if (onMs + 5 < now) {
            advancePos(m_nextPos, step, ts);
            continue;
        }
        if (onMs > horizon) break;

        // Tighten structural timing on downbeats.
        int beatInBar = 0;
        groove::Rational withinBeat{0, 1};
        groove::GrooveGrid::splitWithinBar(m_nextPos, ts, beatInBar, withinBeat);
        const bool structural = (beatInBar == 0 && withinBeat.num == 0);

        // Humanize locally so groove-lock blending is exact and deterministic.
        const auto heA = m_hA.humanizeNote(m_nextPos, ts, bpm, baseVel, dur, structural);
        m_engine.scheduleHumanizedNote(agent, ch, midi, heA, /*logicTag=*/QString());

        if (laneBOn) {
            auto heB = m_hB.humanizeNote(m_nextPos, ts, bpm, baseVelB, durB, structural);

            bool doLock = false;
            if (lockMode == "All events") doLock = true;
            else if (lockMode == "Downbeats only" && structural) doLock = true;

            QString tag;
            if (doLock && lockStrength > 0.0) {
                const double a = qBound(0.0, lockStrength, 1.0);
                const qint64 durMs = heB.offMs - heB.onMs;
                const qint64 newOn = qint64(llround(double(heB.onMs) * (1.0 - a) + double(heA.onMs) * a));
                const qint64 delta = newOn - heB.onMs;
                heB.onMs = newOn;
                heB.offMs = newOn + durMs;
                heB.timing_offset_ms += int(delta);
                tag = QString("GrooveLock(%1,%2)")
                          .arg(structural ? "Downbeat" : "All")
                          .arg(a, 0, 'f', 2);
            }

            m_engine.scheduleHumanizedNote(agentB, chB, midiB, heB, tag);
        }
        m_lastScheduledOnMs = onMs;

        advancePos(m_nextPos, step, ts);
    }
}

