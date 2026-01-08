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

namespace {
static QString shortArtLabel(const QString& logic) {
    // Prefer compact labels for the Articulation lane.
    const QString t = logic.trimmed();
    if (t.endsWith(":Sus")) return "Sus";
    if (t.endsWith(":PM")) return "PM";
    if (t.endsWith(":SIO")) return "SIO";
    if (t.endsWith(":LS")) return "LS";
    if (t.endsWith(":HP")) return "HP";
    if (t.endsWith(":art:Sus")) return "Sus";
    if (t.endsWith(":art:PM")) return "PM";
    // Fallback: last token after ':'
    const int idx = t.lastIndexOf(':');
    if (idx >= 0 && idx + 1 < t.size()) return t.mid(idx + 1);
    return t;
}

static QString extractToken(const QString& s, const QString& prefix) {
    const QString t = s.trimmed();
    if (t.startsWith(prefix)) {
        const int end = t.indexOf('|');
        return (end >= 0) ? t.left(end) : t;
    }
    const int idx = t.indexOf(prefix);
    if (idx < 0) return QString();
    const int end = t.indexOf('|', idx);
    return (end >= 0) ? t.mid(idx, end - idx) : t.mid(idx);
}

static bool matchesFilter(const QString& logic, const QString& sel) {
    if (sel.isEmpty() || sel == "All") return true;
    if (logic == sel) return true;
    return logic.contains(sel);
}
} // namespace

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
        if (m_livePlayheadTimer) m_livePlayheadTimer->stop();
        if (m_timeline) m_timeline->setPlayheadMs(-1);
        rebuildTimelineFromLivePlan();
    });

    // Live playhead timer: smooth playhead movement between plan refreshes.
    m_livePlayheadTimer = new QTimer(this);
    m_livePlayheadTimer->setInterval(33); // ~30fps
    connect(m_livePlayheadTimer, &QTimer::timeout, this, [this]() {
        if (!m_liveMode) return;
        if (!m_timeline) return;
        if (m_auditionTimer && m_auditionTimer->isActive()) return; // audition owns playhead
        if (m_lastEngineWallMs <= 0) return;
        const qint64 nowWall = QDateTime::currentMSecsSinceEpoch();
        const qint64 engineNowEst = m_lastEngineNowMs + qMax<qint64>(0, nowWall - m_lastEngineWallMs);
        const qint64 play = engineNowEst - m_liveBaseMs;
        m_timeline->setPlayheadMs(play);
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

    // Load vocab library (for showing underlying pattern definitions when selecting tags/IDs).
    QString err;
    m_vocabLoaded = m_vocab.loadFromResourcePath(":/virtuoso/vocab/cool_jazz_vocabulary.json", &err);
    if (!m_vocabLoaded) m_vocabErr = err;
}

void VirtuosoVocabularyWindow::refreshTagList() {
    if (!m_list) return;
    const QString prev = m_list->currentItem() ? m_list->currentItem()->text() : QString();
    QSet<QString> tags;
    for (const auto& e : m_liveBuf) {
        const QString t = e.logic.trimmed();
        if (!t.isEmpty()) tags.insert(t);
        // Also index library IDs so users can filter by "gesture:ID", "vocab_phrase:ID", etc.
        if (!extractToken(t, "vocab_phrase:").isEmpty()) tags.insert(extractToken(t, "vocab_phrase:"));
        if (!extractToken(t, "vocab:").isEmpty()) tags.insert(extractToken(t, "vocab:"));
        if (!extractToken(t, "topline_phrase:").isEmpty()) tags.insert(extractToken(t, "topline_phrase:"));
        if (!extractToken(t, "gesture:").isEmpty()) tags.insert(extractToken(t, "gesture:"));
        if (!extractToken(t, "pedal:").isEmpty()) tags.insert(extractToken(t, "pedal:"));
    }
    // Browse mode: add all known library IDs (not only those present in the current plan).
    if (m_vocabLoaded && m_instrument == Instrument::Piano) {
        for (const auto& p : m_vocab.pianoPatterns()) tags.insert(QString("vocab:%1").arg(p.id));
        for (const auto& p : m_vocab.pianoPhrasePatterns()) tags.insert(QString("vocab_phrase:%1").arg(p.id));
        for (const auto& p : m_vocab.pianoTopLinePatterns()) tags.insert(QString("topline_phrase:%1").arg(p.id));
        for (const auto& p : m_vocab.pianoGesturePatterns()) tags.insert(QString("gesture:%1").arg(p.id));
        for (const auto& p : m_vocab.pianoPedalPatterns()) tags.insert(QString("pedal:%1").arg(p.id));
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

    auto buildSyntheticPianoPreview = [&](const QString& sel) -> bool {
        if (!m_vocabLoaded) return false;
        if (m_instrument != Instrument::Piano) return false;

        const double quarterMs = 60000.0 / double(qMax(1, m_liveBpm));
        const double beatMs = quarterMs * (4.0 / double(qMax(1, m_liveTsDen)));
        const double barMs = beatMs * double(qMax(1, m_liveTsNum));
        const double wholeMs = quarterMs * 4.0;

        auto emitChord = [&](qint64 onMs, qint64 offMs, const QString& logic) {
            // Simple Cmaj7 shell-ish voicing for audition/debug.
            const QVector<int> notes = {48, 55, 59, 62};
            for (int i = 0; i < notes.size(); ++i) {
                virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
                ev.lane = "Comp";
                ev.note = notes[i];
                ev.velocity = 92 - i * 6;
                ev.onMs = qMax<qint64>(0, onMs);
                ev.offMs = qMax<qint64>(ev.onMs + 40, offMs);
                ev.label = logic;
                m_displayEvents.push_back(ev);
            }
        };
        auto emitTop = [&](qint64 onMs, qint64 offMs, int midi, const QString& logic) {
            virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
            ev.lane = "TopLine";
            ev.note = midi;
            ev.velocity = 80;
            ev.onMs = qMax<qint64>(0, onMs);
            ev.offMs = qMax<qint64>(ev.onMs + 40, offMs);
            ev.label = logic;
            m_displayEvents.push_back(ev);
        };

        if (sel.startsWith("vocab:")) {
            const QString id = sel.mid(QString("vocab:").size()).trimmed();
            for (const auto& b : m_vocab.pianoPatterns()) {
                if (b.id.trimmed() != id) continue;
                for (int bar = 0; bar < 4; ++bar) {
                    for (int beat = 0; beat < m_liveTsNum; ++beat) {
                        if (!b.beats.contains(beat)) continue;
                        for (const auto& h : b.hits) {
                            const double on = double(bar) * barMs + double(beat) * beatMs + (double(h.sub) / double(qMax(1, h.count))) * beatMs;
                            const double dur = (double(qMax(1, h.dur_num)) / double(qMax(1, h.dur_den))) * wholeMs;
                            emitChord(qint64(llround(on)), qint64(llround(on + dur)), QString("vocab:%1").arg(id));
                        }
                    }
                }
                return true;
            }
            return false;
        }
        if (sel.startsWith("vocab_phrase:")) {
            const QString id = sel.mid(QString("vocab_phrase:").size()).trimmed();
            for (const auto& p : m_vocab.pianoPhrasePatterns()) {
                if (p.id.trimmed() != id) continue;
                for (const auto& ph : p.hits) {
                    if (ph.barOffset < 0 || ph.barOffset >= 4) continue;
                    const int bar = ph.barOffset;
                    const int beat = ph.beatInBar;
                    const auto& h = ph.hit;
                    const double on = double(bar) * barMs + double(beat) * beatMs + (double(h.sub) / double(qMax(1, h.count))) * beatMs;
                    const double dur = (double(qMax(1, h.dur_num)) / double(qMax(1, h.dur_den))) * wholeMs;
                    emitChord(qint64(llround(on)), qint64(llround(on + dur)), QString("vocab_phrase:%1").arg(id));
                }
                return true;
            }
            return false;
        }
        if (sel.startsWith("topline_phrase:")) {
            const QString id = sel.mid(QString("topline_phrase:").size()).trimmed();
            for (const auto& t : m_vocab.pianoTopLinePatterns()) {
                if (t.id.trimmed() != id) continue;
                auto degreeToMidi = [&](int deg) -> int {
                    // Simple C-major mapping for audition: 1=C,3=E,5=G,7=B,9=D,11=F,13=A.
                    const int d = ((deg % 14) + 14) % 14;
                    if (d == 1) return 72;
                    if (d == 3) return 76;
                    if (d == 5) return 79;
                    if (d == 7) return 83;
                    if (d == 9) return 74;
                    if (d == 11) return 77;
                    if (d == 13) return 81;
                    return 79;
                };
                for (const auto& h : t.hits) {
                    if (h.barOffset < 0 || h.barOffset >= 4) continue;
                    const int bar = h.barOffset;
                    const int beat = h.beatInBar;
                    const double on = double(bar) * barMs + double(beat) * beatMs + (double(h.sub) / double(qMax(1, h.count))) * beatMs;
                    const double dur = (double(qMax(1, h.dur_num)) / double(qMax(1, h.dur_den))) * wholeMs;
                    emitTop(qint64(llround(on)), qint64(llround(on + dur)), degreeToMidi(h.degree), QString("topline_phrase:%1").arg(id));
                }
                return true;
            }
            return false;
        }
        return false;
    };

    if (!m_liveBuf.isEmpty()) {
        qint64 baseMs = std::numeric_limits<qint64>::max();
        for (const auto& e : m_liveBuf) baseMs = qMin(baseMs, e.onMs);
        if (baseMs == std::numeric_limits<qint64>::max()) baseMs = 0;
        m_liveBaseMs = baseMs;

        const QString sel = m_list && m_list->currentItem() ? m_list->currentItem()->text() : QString("All");
        const bool filter = (!sel.isEmpty() && sel != "All");

        // Precompute beat/bar durations (for visualization sizing).
        const double quarterMs = 60000.0 / double(qMax(1, m_liveBpm));
        const double beatMs = quarterMs * (4.0 / double(qMax(1, m_liveTsDen)));
        const double barMs = beatMs * double(qMax(1, m_liveTsNum));
        const qint64 previewEndMsAbs = baseMs + qint64(llround(barMs * 4.0));
        // For debugging/inspection lanes (Pedal/Articulation/FX), we intentionally widen blocks so text is readable.
        // Otherwise, keyswitches and FX noises are short and will always elide.
        const qint64 minLabelMs = qMax<qint64>(350, qint64(llround(2.0 * beatMs))); // ~2 beats at current tempo
        const qint64 minFxLabelMs = qMax<qint64>(220, qint64(llround(1.0 * beatMs))); // ~1 beat

        // Sustain reconstruction: treat CC64>=32 as active (half/down), <32 as up, and draw intervals on a "Pedal" lane.
        QVector<virtuoso::ui::GrooveTimelineWidget::LaneEvent> pedalEvents;
        if (m_instrument == Instrument::Piano) {
            // Collect CC64 changes in time order.
            struct CcEv { qint64 t; int v; QString logic; };
            QVector<CcEv> ccs;
            for (const auto& e : m_liveBuf) {
                if (e.kind != "cc") continue;
                if (e.cc != 64) continue;
                if (filter && !matchesFilter(e.logic, sel)) continue;
                ccs.push_back({e.onMs, e.ccValue, e.logic});
            }
            std::sort(ccs.begin(), ccs.end(), [](const CcEv& a, const CcEv& b) { return a.t < b.t; });

            bool active = false;
            bool hardDown = false;
            qint64 downT = 0;
            QString downLogic;
            for (const auto& ce : ccs) {
                const bool isActive = (ce.v >= 32);
                const bool isHard = (ce.v >= 96);
                if (isActive && !active) {
                    active = true;
                    hardDown = isHard;
                    downT = ce.t;
                    downLogic = ce.logic;
                } else if (isActive && active) {
                    // State changes within an active region: close and reopen so labels reflect half/down.
                    if (isHard != hardDown) {
                        const qint64 upT = ce.t;
                        virtuoso::ui::GrooveTimelineWidget::LaneEvent pev;
                        pev.lane = "Pedal";
                        pev.note = hardDown ? 64 : 63;
                        pev.velocity = 127;
                        pev.onMs = qMax<qint64>(0, downT - baseMs);
                        pev.offMs = qMax<qint64>(pev.onMs + 6, upT - baseMs);
                        pev.label = downLogic.isEmpty() ? (hardDown ? QString("Down") : QString("Half")) : downLogic;
                        pedalEvents.push_back(pev);
                        downT = ce.t;
                        downLogic = ce.logic;
                        hardDown = isHard;
                    }
                } else if (!isActive && active) {
                    // Close interval.
                    const qint64 upT = ce.t;
                    virtuoso::ui::GrooveTimelineWidget::LaneEvent pev;
                    pev.lane = "Pedal";
                    pev.note = hardDown ? 64 : 63;
                    pev.velocity = 127;
                    pev.onMs = qMax<qint64>(0, downT - baseMs);
                    pev.offMs = qMax<qint64>(pev.onMs + 6, upT - baseMs);
                    pev.label = downLogic.isEmpty() ? (hardDown ? QString("Down") : QString("Half")) : downLogic;
                    pedalEvents.push_back(pev);
                    active = false;
                }
            }
            if (active) {
                virtuoso::ui::GrooveTimelineWidget::LaneEvent pev;
                pev.lane = "Pedal";
                pev.note = hardDown ? 64 : 63;
                pev.velocity = 127;
                pev.onMs = qMax<qint64>(0, downT - baseMs);
                pev.offMs = qMax<qint64>(pev.onMs + 6, previewEndMsAbs - baseMs);
                pev.label = downLogic.isEmpty() ? (hardDown ? QString("Down") : QString("Half")) : downLogic;
                pedalEvents.push_back(pev);
            }
        }

        for (const auto& e : m_liveBuf) {
            if (filter && !matchesFilter(e.logic, sel)) continue;
            if (e.kind == "cc") continue; // CC visualized separately (pedal intervals)
            if (e.kind == "keyswitch") continue; // handled below in dedicated lanes
            if (m_instrument == Instrument::Bass && e.kind == "note" && e.logic.trimmed().startsWith("Bass:fx:")) {
                // Bass FX are visualized on a dedicated FX lane (handled below).
                continue;
            }
            virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
            ev.lane = lane;
            if (m_instrument == Instrument::Piano) {
                const QString t = e.logic.trimmed();
                if (t.startsWith("piano_topline")) ev.lane = "TopLine";
                else if (t.startsWith("ballad_comp")) ev.lane = "Comp";
            }
            ev.note = e.note;
            ev.velocity = qBound(1, e.velocity, 127);
            ev.onMs = qMax<qint64>(0, e.onMs - baseMs);
            ev.offMs = qMax<qint64>(0, e.offMs - baseMs);
            ev.label = e.logic;
            m_displayEvents.push_back(ev);

            // Piano: add a readable Gesture lane label when present.
            if (m_instrument == Instrument::Piano) {
                const QString gid = extractToken(e.logic, "gesture:");
                if (!gid.isEmpty()) {
                    virtuoso::ui::GrooveTimelineWidget::LaneEvent gev;
                    gev.lane = "Gesture";
                    gev.note = 0;
                    gev.velocity = 100;
                    gev.onMs = ev.onMs;
                    gev.offMs = qMax<qint64>(ev.onMs + minLabelMs, ev.offMs);
                    gev.label = gid;
                    m_displayEvents.push_back(gev);
                }
            }
        }

        for (const auto& pev : pedalEvents) m_displayEvents.push_back(pev);

        // Bass lanes: split keyswitch, state, and FX for clarity.
        if (m_instrument == Instrument::Bass) {
            // KeySwitch + ArticulationState (from keyswitch stream)
            for (const auto& e : m_liveBuf) {
                if (e.kind != "keyswitch") continue;
                if (filter && !matchesFilter(e.logic, sel)) continue;
                virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
                ev.lane = (e.note >= 0) ? "KeySwitch" : "ArticulationState";
                ev.note = 0;
                ev.velocity = 100;
                ev.onMs = qMax<qint64>(0, e.onMs - baseMs);
                ev.offMs = qMax<qint64>(ev.onMs + minLabelMs, e.offMs - baseMs);
                ev.label = shortArtLabel(e.logic);
                m_displayEvents.push_back(ev);
            }

            // FX lane (from note stream; logic_tag prefixed with Bass:fx:)
            for (const auto& e : m_liveBuf) {
                if (e.kind != "note") continue;
                if (filter && !matchesFilter(e.logic, sel)) continue;
                const QString t = e.logic.trimmed();
                if (!t.startsWith("Bass:fx:")) continue;
                virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
                ev.lane = "FX";
                ev.note = 0;
                ev.velocity = qBound(1, e.velocity, 127);
                ev.onMs = qMax<qint64>(0, e.onMs - baseMs);
                ev.offMs = qMax<qint64>(ev.onMs + minFxLabelMs, e.offMs - baseMs);
                ev.label = t.mid(QString("Bass:fx:").size());
                m_displayEvents.push_back(ev);
            }
        }

        // Drums lanes: expose texture vs ride vs phrase gestures for debugging.
        if (m_instrument == Instrument::Drums) {
            const qint64 minLabelMs = qMax<qint64>(220, qint64(llround((60000.0 / double(qMax(1, m_liveBpm))) / 2.0)));
            for (const auto& e : m_liveBuf) {
                if (e.kind != "note") continue;
                if (filter && !matchesFilter(e.logic, sel)) continue;
                const QString t = e.logic.trimmed();
                if (t.startsWith("Drums:FeatherKick")) {
                    virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
                    ev.lane = "Kick";
                    ev.note = 0;
                    ev.velocity = qBound(1, e.velocity, 127);
                    ev.onMs = qMax<qint64>(0, e.onMs - baseMs);
                    ev.offMs = qMax<qint64>(ev.onMs + minLabelMs, e.offMs - baseMs);
                    ev.label = "Kick";
                    m_displayEvents.push_back(ev);
                    continue;
                }
                if (t.contains("BrushStirLoop")) {
                    virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
                    ev.lane = "Texture";
                    ev.note = 0;
                    ev.velocity = qBound(1, e.velocity, 127);
                    ev.onMs = qMax<qint64>(0, e.onMs - baseMs);
                    ev.offMs = qMax<qint64>(ev.onMs + qMax<qint64>(minLabelMs, 900), e.offMs - baseMs);
                    ev.label = "Stir";
                    m_displayEvents.push_back(ev);
                    continue;
                }
                if (t.contains("Ride")) {
                    virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
                    ev.lane = "Ride";
                    ev.note = 0;
                    ev.velocity = qBound(1, e.velocity, 127);
                    ev.onMs = qMax<qint64>(0, e.onMs - baseMs);
                    ev.offMs = qMax<qint64>(ev.onMs + minLabelMs, e.offMs - baseMs);
                    ev.label = "Ride";
                    m_displayEvents.push_back(ev);
                    continue;
                }
                if (t.contains("Phrase") || t.contains("Cadence")) {
                    virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
                    ev.lane = "Gesture";
                    ev.note = 0;
                    ev.velocity = qBound(1, e.velocity, 127);
                    ev.onMs = qMax<qint64>(0, e.onMs - baseMs);
                    ev.offMs = qMax<qint64>(ev.onMs + minLabelMs, e.offMs - baseMs);
                    // Short label: remove "Drums:" prefix if present.
                    ev.label = t.startsWith("Drums:") ? t.mid(QString("Drums:").size()) : t;
                    m_displayEvents.push_back(ev);
                    continue;
                }
            }
        }
    }

    // If filter is a library ID and we have no matching live events, generate a synthetic preview so it’s auditionable.
    {
        const QString sel = m_list && m_list->currentItem() ? m_list->currentItem()->text() : QString("All");
        if (m_displayEvents.isEmpty() && !sel.isEmpty() && sel != "All") {
            buildSyntheticPianoPreview(sel);
        }
    }

    m_timeline->setTempoAndSignature(m_liveBpm, m_liveTsNum, m_liveTsDen);
    m_timeline->setPreviewBars(4);
    m_timeline->setSubdivision(4);
    if (m_instrument == Instrument::Piano) {
        m_timeline->setLanes(QStringList() << "Comp" << "TopLine" << "Gesture" << "Pedal");
    } else if (m_instrument == Instrument::Bass) {
        m_timeline->setLanes(QStringList() << lane << "KeySwitch" << "ArticulationState" << "FX");
    } else if (m_instrument == Instrument::Drums) {
        m_timeline->setLanes(QStringList() << lane << "Kick" << "Texture" << "Ride" << "Gesture");
    } else {
        m_timeline->setLanes(QStringList() << lane);
    }
    m_timeline->setEvents(m_displayEvents);
    if (!m_liveMode) m_timeline->setPlayheadMs(-1);

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
        if (!m_vocabLoaded) addRow("Vocab", QString("not loaded: %1").arg(m_vocabErr.trimmed()));

        const QString phraseId = extractToken(sel, "vocab_phrase:");
        const QString beatId = extractToken(sel, "vocab:");
        const QString tlId = extractToken(sel, "topline_phrase:");
        const QString gestId = extractToken(sel, "gesture:");
        const QString pedId = extractToken(sel, "pedal:");

        auto addHitsSummary = [&](const QString& k, const QStringList& parts) {
            addRow(k, parts.isEmpty() ? QString("—") : parts.join(", "));
        };

        if (m_vocabLoaded && !phraseId.isEmpty()) {
            for (const auto& p : m_vocab.pianoPhrasePatterns()) { // PianoPhraseChoice
                if (p.id.trimmed() != phraseId.mid(QString("vocab_phrase:").size())) continue;
                addRow("Lib", "piano_phrases");
                addRow("ID", p.id);
                addRow("phraseBars", QString::number(p.phraseBars));
                addRow("notes", p.notes);
                QStringList hs;
                for (const auto& h : p.hits) {
                    hs << QString("b%1:%2  %3/%4  dur=%5/%6")
                              .arg(h.barOffset)
                              .arg(h.beatInBar)
                              .arg(h.hit.sub)
                              .arg(h.hit.count)
                              .arg(h.hit.dur_num)
                              .arg(h.hit.dur_den);
                }
                addHitsSummary("hits", hs);
                break;
            }
        }
        if (m_vocabLoaded && !beatId.isEmpty()) {
            for (const auto& b : m_vocab.pianoPatterns()) { // PianoPatternDef
                if (b.id.trimmed() != beatId.mid(QString("vocab:").size())) continue;
                addRow("Lib", "piano");
                addRow("ID", b.id);
                QStringList bs;
                for (const int x : b.beats) bs << QString::number(x);
                addRow("beats", bs.join(", "));
                addRow("flags", QString("%1%2%3")
                                    .arg(b.chordIsNewOnly ? "chordIsNewOnly " : "")
                                    .arg(b.stableOnly ? "stableOnly " : "")
                                    .arg(b.allowWhenUserSilence ? "" : "forbidWhenUserSilence"));
                addRow("chordFunctions", b.chordFunctions.join(", "));
                addRow("notes", b.notes);
                QStringList hs;
                for (const auto& h : b.hits) {
                    hs << QString("%1/%2  dur=%3/%4").arg(h.sub).arg(h.count).arg(h.dur_num).arg(h.dur_den);
                }
                addHitsSummary("hits", hs);
                break;
            }
        }
        if (m_vocabLoaded && !tlId.isEmpty()) {
            for (const auto& t : m_vocab.pianoTopLinePatterns()) {
                if (t.id.trimmed() != tlId.mid(QString("topline_phrase:").size())) continue;
                addRow("Lib", "piano_topline");
                addRow("ID", t.id);
                addRow("phraseBars", QString::number(t.phraseBars));
                addRow("allowWhenUserSilence", t.allowWhenUserSilence ? "true" : "false");
                addRow("chordFunctions", t.chordFunctions.join(", "));
                addRow("notes", t.notes);
                QStringList hs;
                for (const auto& h : t.hits) {
                    hs << QString("b%1:%2.%3/%4 deg=%5 %6")
                              .arg(h.barOffset)
                              .arg(h.beatInBar)
                              .arg(h.sub)
                              .arg(h.count)
                              .arg(h.degree)
                              .arg(h.resolve ? "res" : "");
                }
                addHitsSummary("hits", hs);
                break;
            }
        }
        if (m_vocabLoaded && !gestId.isEmpty()) {
            for (const auto& g : m_vocab.pianoGesturePatterns()) {
                if (g.id.trimmed() != gestId.mid(QString("gesture:").size())) continue;
                addRow("Lib", "piano_gestures");
                addRow("ID", g.id);
                addRow("kind", g.kind);
                addRow("style", g.style);
                addRow("spreadMs", QString::number(g.spreadMs));
                addRow("maxBpm", QString::number(g.maxBpm));
                addRow("notes", g.notes);
                break;
            }
        }
        if (m_vocabLoaded && !pedId.isEmpty()) {
            for (const auto& p : m_vocab.pianoPedalPatterns()) {
                if (p.id.trimmed() != pedId.mid(QString("pedal:").size())) continue;
                addRow("Lib", "piano_pedals");
                addRow("ID", p.id);
                addRow("defaultState", p.defaultState);
                addRow("repedalOnNewChord", p.repedalOnNewChord ? "true" : "false");
                addRow("repedalProbPct", QString::number(p.repedalProbPct));
                addRow("clearBeforeChange", p.clearBeforeChange ? "true" : "false");
                addRow("clearSub/count", QString("%1/%2").arg(p.clearSub).arg(p.clearCount));
                addRow("notes", p.notes);
                break;
            }
        }
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
        const bool laneOk = (ev.lane == instrumentName(m_instrument))
            || (m_instrument == Instrument::Piano && (ev.lane == "Comp" || ev.lane == "TopLine"));
        if (!laneOk) continue;
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
    if (!m_list->currentItem() || m_list->currentItem()->text() != id) {
        // Not found: append and select (useful for candidate_pool highlights before a plan is loaded).
        m_list->addItem(id);
        m_list->setCurrentRow(m_list->count() - 1);
    }
}

void VirtuosoVocabularyWindow::ingestTheoryEventJson(const QString& json) {
    // This is a full-plan replacement stream; drop duplicates to avoid UI churn.
    if (!m_lastPlanJson.isEmpty() && json == m_lastPlanJson) return;
    m_lastPlanJson = json;

    const QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
    if (d.isObject()) {
        const QJsonObject o = d.object();
        if (o.value("event_kind").toString().trimmed() == "candidate_pool") {
            const QJsonObject chosen = o.value("chosen").toObject();
            // If candidate_pool carries an active library ID, auto-highlight it.
            if (m_instrument == Instrument::Piano) {
                const QString g = chosen.value("gesture_id").toString().trimmed();
                const QString cp = chosen.value("comp_phrase_id").toString().trimmed();
                const QString tl = chosen.value("topline_phrase_id").toString().trimmed();
                const QString ped = chosen.value("pedal_id").toString().trimmed();
                if (!g.isEmpty()) highlightPatternId(QString("gesture:%1").arg(g));
                else if (!cp.isEmpty()) highlightPatternId(QString("vocab_phrase:%1").arg(cp));
                else if (!tl.isEmpty()) highlightPatternId(QString("topline_phrase:%1").arg(tl));
                else if (!ped.isEmpty()) highlightPatternId(QString("pedal:%1").arg(ped));
            }
        }
        return;
    }
    if (!d.isArray()) return;

    // Lookahead plan: replace buffer with the entire next-4-bars plan.
    m_liveBuf.clear();
    const auto arr = d.array();
    QStringList logLines;
    logLines.reserve(arr.size());
    qint64 bestEngineNow = 0;
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
        if (engineNowMs > bestEngineNow) bestEngineNow = engineNowMs;
        if (bpm > 0) m_liveBpm = bpm;
        if (tsNum > 0) m_liveTsNum = tsNum;
        if (tsDen > 0) m_liveTsDen = tsDen;
        const bool isCc = (kind == "cc") || (cc >= 0);
        const bool isKeyswitch = (kind == "keyswitch");
        if (onMs > 0 && (isCc || isKeyswitch || offMs > onMs)) {
            LiveEv ev;
            ev.onMs = onMs;
            ev.offMs = isCc ? onMs : (isKeyswitch ? offMs : offMs);
            ev.kind = isCc ? "cc" : (isKeyswitch ? "keyswitch" : "note");
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
        } else if (isKeyswitch) {
            logLines.push_back(QString("%1  %2  keyswitch_note=%3").arg(grid, logic, QString::number(note)));
        } else {
            logLines.push_back(QString("%1  %2  %3").arg(grid, logic, target));
        }
    }

    // Update smooth playhead anchor.
    if (bestEngineNow > 0) {
        m_lastEngineNowMs = bestEngineNow;
        m_lastEngineWallMs = QDateTime::currentMSecsSinceEpoch();
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
    if (m_livePlayheadTimer && !m_livePlayheadTimer->isActive()) m_livePlayheadTimer->start();
}

