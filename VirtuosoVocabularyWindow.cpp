#include "VirtuosoVocabularyWindow.h"

#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QTimer>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QCloseEvent>

#include <algorithm>
#include <limits>

#include "midiprocessor.h"

VirtuosoVocabularyWindow::VirtuosoVocabularyWindow(MidiProcessor* midi,
                                                   Instrument instrument,
                                                   QWidget* parent)
    : QMainWindow(parent)
    , m_midi(midi)
    , m_instrument(instrument) {
    setWindowTitle(QString("Virtuoso Player — %1").arg(instrumentName(m_instrument)));
    resize(1180, 680);
    buildUi();
    refreshTagList();
    rebuildTimelineFromLivePlan();
}

void VirtuosoVocabularyWindow::closeEvent(QCloseEvent* e) {
    stopAuditionNow();
    QMainWindow::closeEvent(e);
}

QString VirtuosoVocabularyWindow::instrumentName(Instrument i) {
    switch (i) {
        case Instrument::Piano: return "Piano";
        case Instrument::Bass: return "Bass";
        case Instrument::Drums: return "Drums";
    }
    return "Instrument";
}

int VirtuosoVocabularyWindow::defaultMidiChannel(Instrument i) {
    // Match MVP runner defaults for consistent routing.
    switch (i) {
        case Instrument::Drums: return 6;
        case Instrument::Bass: return 3;
        case Instrument::Piano: return 4;
    }
    return 1;
}

void VirtuosoVocabularyWindow::buildUi() {
    QWidget* root = new QWidget(this);
    setCentralWidget(root);

    QVBoxLayout* main = new QVBoxLayout(root);
    main->setSpacing(10);

    // --- Controls bar ---
    QHBoxLayout* controls = new QHBoxLayout;
    controls->setSpacing(10);

    controls->addWidget(new QLabel("Energy x:", this));
    m_energyMultSlider = new QSlider(Qt::Horizontal, this);
    m_energyMultSlider->setRange(0, 200);
    m_energyMultSlider->setValue(100);
    m_energyMultSlider->setToolTip("Per-instrument energy multiplier (0..2). Multiplies global Energy.");
    m_energyMultLabel = new QLabel("1.00", this);
    m_energyMultLabel->setMinimumWidth(44);
    controls->addWidget(m_energyMultSlider, 1);
    controls->addWidget(m_energyMultLabel, 0);

    auto* refreshBtn = new QPushButton("Refresh (song)", this);
    refreshBtn->setToolTip("Request a 4-bar lookahead plan from the current song, even if playback is stopped.");
    controls->addWidget(refreshBtn);

    controls->addStretch(1);

    m_auditionBtn = new QPushButton("Audition", this);
    controls->addWidget(m_auditionBtn);

    main->addLayout(controls);

    // --- Main split: list | details/timeline/live ---
    QHBoxLayout* split = new QHBoxLayout;
    split->setSpacing(10);

    m_list = new QListWidget(this);
    m_list->setMinimumWidth(320);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    split->addWidget(m_list, 0);

    QVBoxLayout* right = new QVBoxLayout;
    right->setSpacing(10);

    m_detailTable = new QTableWidget(this);
    m_detailTable->setColumnCount(2);
    m_detailTable->setHorizontalHeaderLabels({"Field", "Value"});
    m_detailTable->horizontalHeader()->setStretchLastSection(true);
    m_detailTable->verticalHeader()->setVisible(false);
    m_detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_detailTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_detailTable->setMinimumHeight(120);
    right->addWidget(m_detailTable, 0);

    m_timeline = new virtuoso::ui::GrooveTimelineWidget(this);
    m_timeline->setMinimumHeight(220);
    right->addWidget(m_timeline, 1);

    QGroupBox* liveBox = new QGroupBox("Live (from Theory stream)", this);
    QVBoxLayout* lv = new QVBoxLayout(liveBox);
    m_liveHeader = new QLabel("—", liveBox);
    m_liveHeader->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_liveHeader->setStyleSheet("QLabel { font-family: Menlo, monospace; font-size: 10pt; }");
    m_liveLog = new QTextEdit(liveBox);
    m_liveLog->setReadOnly(true);
    m_liveLog->setMinimumHeight(140);
    m_liveLog->setStyleSheet("QTextEdit { background: #0b0b0b; color: #ddd; font-family: Menlo, monospace; font-size: 9pt; }");
    lv->addWidget(m_liveHeader);
    lv->addWidget(m_liveLog);
    right->addWidget(liveBox, 0);

    split->addLayout(right, 1);
    main->addLayout(split, 1);

    // Audition timer
    m_auditionTimer = new QTimer(this);
    m_auditionTimer->setInterval(5);

    // Live decay timer: if planned events stop, we return to Preview mode and enable audition.
    m_liveDecayTimer = new QTimer(this);
    m_liveDecayTimer->setSingleShot(true);
    connect(m_liveDecayTimer, &QTimer::timeout, this, [this]() {
        m_liveMode = false;
        if (m_auditionBtn) m_auditionBtn->setEnabled(true);
        rebuildTimelineFromLivePlan();
    });

    // Connections
    connect(m_list, &QListWidget::currentRowChanged, this, &VirtuosoVocabularyWindow::onSelectionChanged);
    connect(m_auditionBtn, &QPushButton::clicked, this, &VirtuosoVocabularyWindow::onAuditionStartStop);
    connect(m_auditionTimer, &QTimer::timeout, this, &VirtuosoVocabularyWindow::onAuditionTick);

    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        emit requestSongPreview();
    });
    connect(m_energyMultSlider, &QSlider::valueChanged, this, [this](int v) {
        const double mult = qBound(0.0, double(v) / 100.0, 2.0);
        if (m_energyMultLabel) m_energyMultLabel->setText(QString::number(mult, 'f', 2));
        emit agentEnergyMultiplierChanged(instrumentName(m_instrument), mult);
    });
}

void VirtuosoVocabularyWindow::refreshTagList() {
    if (!m_list) return;
    const QString prev = m_list->currentItem() ? m_list->currentItem()->text() : QString();
    QSet<QString> tags;
    for (const auto& e : m_liveBuf) {
        const QString t = e.logic.trimmed();
        if (!t.isEmpty()) tags.insert(t);
    }
    QVector<QString> sorted = tags.values().toVector();
    std::sort(sorted.begin(), sorted.end());
    m_list->clear();
    m_list->addItem("All");
    for (const auto& t : sorted) m_list->addItem(t);
    if (!prev.isEmpty()) highlightPatternId(prev);
    if (!m_list->currentItem()) m_list->setCurrentRow(0);
}

void VirtuosoVocabularyWindow::onSelectionChanged() {
    rebuildTimelineFromLivePlan();
}

void VirtuosoVocabularyWindow::rebuildTimelineFromLivePlan() {
    if (!m_timeline) return;

    const QString lane = instrumentName(m_instrument);
    m_displayEvents.clear();

    if (!m_liveBuf.isEmpty()) {
        qint64 baseMs = std::numeric_limits<qint64>::max();
        for (const auto& e : m_liveBuf) baseMs = qMin(baseMs, e.onMs);
        if (baseMs == std::numeric_limits<qint64>::max()) baseMs = 0;

        const QString sel = m_list && m_list->currentItem() ? m_list->currentItem()->text() : QString("All");
        const bool filter = (!sel.isEmpty() && sel != "All");

        // Precompute a 4-bar window end for pedal interval visualization.
        const double quarterMs = 60000.0 / double(qMax(1, m_liveBpm));
        const double beatMs = quarterMs * (4.0 / double(qMax(1, m_liveTsDen)));
        const double barMs = beatMs * double(qMax(1, m_liveTsNum));
        const qint64 previewEndMsAbs = baseMs + qint64(llround(barMs * 4.0));

        // Sustain reconstruction: treat CC64>=64 as down, <64 as up, and draw intervals on a "Pedal" lane.
        QVector<virtuoso::ui::GrooveTimelineWidget::LaneEvent> pedalEvents;
        if (m_instrument == Instrument::Piano) {
            // Collect CC64 changes in time order.
            struct CcEv { qint64 t; int v; QString logic; };
            QVector<CcEv> ccs;
            for (const auto& e : m_liveBuf) {
                if (e.kind != "cc") continue;
                if (e.cc != 64) continue;
                if (filter && e.logic != sel) continue;
                ccs.push_back({e.onMs, e.ccValue, e.logic});
            }
            std::sort(ccs.begin(), ccs.end(), [](const CcEv& a, const CcEv& b) { return a.t < b.t; });

            bool down = false;
            qint64 downT = 0;
            QString downLogic;
            for (const auto& ce : ccs) {
                const bool isDown = (ce.v >= 64);
                if (isDown && !down) {
                    down = true;
                    downT = ce.t;
                    downLogic = ce.logic;
                } else if (!isDown && down) {
                    // Close interval.
                    const qint64 upT = ce.t;
                    virtuoso::ui::GrooveTimelineWidget::LaneEvent pev;
                    pev.lane = "Pedal";
                    pev.note = 64;
                    pev.velocity = 127;
                    pev.onMs = qMax<qint64>(0, downT - baseMs);
                    pev.offMs = qMax<qint64>(pev.onMs + 6, upT - baseMs);
                    pev.label = downLogic.isEmpty() ? QString("Sustain") : downLogic;
                    pedalEvents.push_back(pev);
                    down = false;
                }
            }
            if (down) {
                virtuoso::ui::GrooveTimelineWidget::LaneEvent pev;
                pev.lane = "Pedal";
                pev.note = 64;
                pev.velocity = 127;
                pev.onMs = qMax<qint64>(0, downT - baseMs);
                pev.offMs = qMax<qint64>(pev.onMs + 6, previewEndMsAbs - baseMs);
                pev.label = downLogic.isEmpty() ? QString("Sustain") : downLogic;
                pedalEvents.push_back(pev);
            }
        }

        for (const auto& e : m_liveBuf) {
            if (filter && e.logic != sel) continue;
            if (e.kind == "cc") continue; // CC visualized separately (pedal intervals)
            virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
            ev.lane = lane;
            ev.note = e.note;
            ev.velocity = qBound(1, e.velocity, 127);
            ev.onMs = qMax<qint64>(0, e.onMs - baseMs);
            ev.offMs = qMax<qint64>(0, e.offMs - baseMs);
            ev.label = e.logic;
            m_displayEvents.push_back(ev);
        }

        for (const auto& pev : pedalEvents) m_displayEvents.push_back(pev);
    }

    m_timeline->setTempoAndSignature(m_liveBpm, m_liveTsNum, m_liveTsDen);
    m_timeline->setPreviewBars(4);
    m_timeline->setSubdivision(4);
    if (m_instrument == Instrument::Piano) {
        m_timeline->setLanes(QStringList() << lane << "Pedal");
    } else {
        m_timeline->setLanes(QStringList() << lane);
    }
    m_timeline->setEvents(m_displayEvents);
    m_timeline->setPlayheadMs(-1);

    if (m_detailTable) {
        m_detailTable->setRowCount(0);
        auto addRow = [&](const QString& k, const QString& v) {
            const int r = m_detailTable->rowCount();
            m_detailTable->insertRow(r);
            m_detailTable->setItem(r, 0, new QTableWidgetItem(k));
            m_detailTable->setItem(r, 1, new QTableWidgetItem(v));
        };
        const QString sel = m_list && m_list->currentItem() ? m_list->currentItem()->text() : QString("All");
        addRow("Agent", lane);
        addRow("Mode", m_liveMode ? "Live" : "Preview");
        addRow("Filter", sel.isEmpty() ? "All" : sel);
        addRow("Events", QString::number(m_displayEvents.size()));
        addRow("Tempo/TS", QString("%1 bpm  %2/%3").arg(m_liveBpm).arg(m_liveTsNum).arg(m_liveTsDen));
    }

    if (m_liveHeader) {
        const QString sel = m_list && m_list->currentItem() ? m_list->currentItem()->text() : QString("All");
        const QString mode = m_liveMode ? "Live" : "Preview";
        m_liveHeader->setText(QString("%1  bpm=%2  ts=%3/%4  filter=%5  events=%6")
                                  .arg(mode)
                                  .arg(m_liveBpm)
                                  .arg(m_liveTsNum)
                                  .arg(m_liveTsDen)
                                  .arg(sel.isEmpty() ? "All" : sel)
                                  .arg(m_displayEvents.size()));
    }
}

void VirtuosoVocabularyWindow::onAuditionStartStop() {
    if (!m_auditionBtn || !m_auditionTimer || !m_midi) return;
    if (m_liveMode) return; // disabled during live playback
    if (m_auditionTimer->isActive()) {
        stopAuditionNow();
        return;
    }
    if (m_displayEvents.isEmpty()) {
        emit requestSongPreview();
        return;
    }
    m_auditionStartMs = QDateTime::currentMSecsSinceEpoch();
    m_auditionLastPlayMs = -1;
    m_auditionBtn->setText("Stop");
    m_auditionTimer->start();
}

void VirtuosoVocabularyWindow::stopAuditionNow() {
    if (m_auditionTimer) m_auditionTimer->stop();
    if (m_auditionBtn) m_auditionBtn->setText("Audition");
    if (m_timeline) m_timeline->setPlayheadMs(-1);
    m_auditionLastPlayMs = -1;
    if (!m_midi) return;
    m_midi->sendVirtualAllNotesOff(defaultMidiChannel(m_instrument));
}

void VirtuosoVocabularyWindow::onAuditionTick() {
    if (!m_auditionTimer || !m_auditionTimer->isActive() || !m_timeline) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 rel = now - m_auditionStartMs;
    const int bpm = qMax(1, m_liveBpm);
    const int bars = 4;
    qint64 totalMs = qint64(llround(double(bars) * (60000.0 / double(bpm)) * 4.0));
    if (totalMs <= 0) totalMs = 1;
    const qint64 play = rel % totalMs;
    m_timeline->setPlayheadMs(play);

    if (m_auditionLastPlayMs < 0) m_auditionLastPlayMs = play;
    const bool wrapped = play < m_auditionLastPlayMs;

    const int ch = defaultMidiChannel(m_instrument);
    for (const auto& ev : m_displayEvents) {
        if (ev.lane != instrumentName(m_instrument)) continue;
        const bool hit = wrapped
            ? (ev.onMs >= m_auditionLastPlayMs || ev.onMs <= play)
            : (ev.onMs >= m_auditionLastPlayMs && ev.onMs <= play);
        if (!hit) continue;
        m_midi->sendVirtualNoteOn(ch, ev.note, qBound(1, ev.velocity, 127));
        const int durMs = qBound(40, int(ev.offMs - ev.onMs), 8000);
        QTimer::singleShot(durMs, this, [this, ch, n = ev.note]() {
            if (!m_midi) return;
            m_midi->sendVirtualNoteOff(ch, n);
        });
    }
    m_auditionLastPlayMs = play;
}

void VirtuosoVocabularyWindow::highlightPatternId(const QString& id) {
    if (!m_list || id.trimmed().isEmpty()) return;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->text() == id) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void VirtuosoVocabularyWindow::ingestTheoryEventJson(const QString& json) {
    // This is a full-plan replacement stream; drop duplicates to avoid UI churn.
    if (!m_lastPlanJson.isEmpty() && json == m_lastPlanJson) return;
    m_lastPlanJson = json;

    const QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
    if (!d.isArray()) return;

    // Lookahead plan: replace buffer with the entire next-4-bars plan.
    m_liveBuf.clear();
    const auto arr = d.array();
    QStringList logLines;
    logLines.reserve(arr.size());
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        const auto o = v.toObject();
        const QString agent = o.value("agent").toString();
        if (agent.trimmed() != instrumentName(m_instrument)) continue;
        const QString kind = o.value("event_kind").toString().trimmed();
        const QString grid = o.value("grid_pos").toString(o.value("timestamp").toString());
        const QString logic = o.value("logic_tag").toString();
        const QString target = o.value("target_note").toString();
        const int note = o.value("note").toInt(-1);
        const int vel = o.value("dynamic_marking").toString().toInt();
        const int cc = o.value("cc").toInt(-1);
        const int ccValue = o.value("cc_value").toInt(-1);
        const qint64 onMs = qint64(o.value("on_ms").toDouble(0.0));
        const qint64 offMs = qint64(o.value("off_ms").toDouble(0.0));
        const int bpm = o.value("tempo_bpm").toInt(0);
        const int tsNum = o.value("ts_num").toInt(0);
        const int tsDen = o.value("ts_den").toInt(0);
        const qint64 engineNowMs = qint64(o.value("engine_now_ms").toDouble(0.0));
        if (bpm > 0) m_liveBpm = bpm;
        if (tsNum > 0) m_liveTsNum = tsNum;
        if (tsDen > 0) m_liveTsDen = tsDen;
        const bool isCc = (kind == "cc") || (cc >= 0);
        if (onMs > 0 && (isCc || offMs > onMs)) {
            LiveEv ev;
            ev.onMs = onMs;
            ev.offMs = isCc ? onMs : offMs;
            ev.kind = isCc ? "cc" : "note";
            ev.note = note;
            ev.velocity = vel;
            ev.cc = cc;
            ev.ccValue = ccValue;
            ev.logic = logic;
            ev.grid = grid;
            ev.engineNowMs = engineNowMs;
            m_liveBuf.push_back(ev);
        }
        if (isCc && cc == 64) {
            logLines.push_back(QString("%1  %2  sustain=%3").arg(grid, logic, QString::number(ccValue)));
        } else {
            logLines.push_back(QString("%1  %2  %3").arg(grid, logic, target));
        }
    }

    if (m_liveLog) {
        // Replace the log each time (cheap) instead of incremental appends (expensive).
        const bool prev = m_liveLog->blockSignals(true);
        m_liveLog->setPlainText(logLines.join("\n"));
        m_liveLog->blockSignals(prev);
        m_liveLog->moveCursor(QTextCursor::End);
    }

    refreshTagList();
    rebuildTimelineFromLivePlan();

    // Live-follow: disable audition during live playback updates.
    m_liveMode = true;
    if (m_auditionTimer && m_auditionTimer->isActive()) stopAuditionNow();
    if (m_auditionBtn) m_auditionBtn->setEnabled(false);
    if (m_liveDecayTimer) m_liveDecayTimer->start(1600);
}

