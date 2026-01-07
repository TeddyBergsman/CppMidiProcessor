#include "LibraryWindow.h"

#include <QTabWidget>
#include <QListWidget>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QTimer>
#include <QStatusBar>
#include <QSignalBlocker>
#include <QCloseEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <functional>

#include "virtuoso/ui/GuitarFretboardWidget.h"
#include "virtuoso/ui/PianoKeyboardWidget.h"

#include "midiprocessor.h"
#include "virtuoso/theory/ScaleSuggester.h"
#include "virtuoso/theory/FunctionalHarmony.h"
#include "virtuoso/groove/TimingHumanizer.h"
#include "virtuoso/drums/FluffyAudioJazzDrumsBrushesMapping.h"
using namespace virtuoso::ontology;

namespace {

static int normalizePc(int pc) {
    int v = pc % 12;
    if (v < 0) v += 12;
    return v;
}

static QVector<QString> pcNames() {
    return {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
}

static int degreeToSemitone(const ChordDef* chordCtx, int degree) {
    // Basic mapping for 1/3/5/7 from chord definition when possible.
    if (degree == 1) return 0;
    if (!chordCtx) return 0;

    // chordCtx->intervals always includes root at [0].
    auto thirdFromChord = [&]() -> int {
        // Find a 3rd-ish interval: prefer 3 or 4.
        for (int iv : chordCtx->intervals) if (iv == 3 || iv == 4) return iv;
        return 4;
    };
    auto fifthFromChord = [&]() -> int {
        for (int iv : chordCtx->intervals) if (iv == 6 || iv == 7 || iv == 8) return iv;
        return 7;
    };
    auto seventhFromChord = [&]() -> int {
        for (int iv : chordCtx->intervals) if (iv == 9 || iv == 10 || iv == 11) return iv;
        return 10;
    };

    switch (degree) {
    case 3: return thirdFromChord();
    case 5: return fifthFromChord();
    case 7: return seventhFromChord();
    case 9: return 14;
    case 11: return 17;
    case 13: return 21;
    default:
        return 0;
    }
}

static QString degreeLabelForChordInterval(int iv) {
    // Semitone-based label map (dominant-oriented). This is used for UI degree labels/tooltips.
    // For now, we keep it simple and consistent across chord/scale/voicing highlighting.
    const int pc = normalizePc(iv);
    switch (pc) {
    case 0: return "1";
    case 1: return "b9";
    case 2: return "9";
    case 3: return "#9";
    case 4: return "3";
    case 5: return "11";
    case 6: return "#11";
    case 7: return "5";
    case 8: return "b13";
    case 9: return "13";
    case 10: return "b7";
    case 11: return "7";
    default: return {};
    }
}

static int normalizeMidi(int midi) {
    if (midi < 0) return 0;
    if (midi > 127) return 127;
    return midi;
}

} // namespace

LibraryWindow::LibraryWindow(MidiProcessor* midi, QWidget* parent)
    : QMainWindow(parent),
      m_registry(OntologyRegistry::builtins()),
      m_grooveRegistry(virtuoso::groove::GrooveRegistry::builtins()),
      m_midi(midi) {
    setWindowTitle("Library");
    resize(1100, 520);
    m_harmonyHelper.setOntology(&m_registry);
    buildUi();
    populateLists();
    updateHighlights();
}

void LibraryWindow::closeEvent(QCloseEvent* e) {
    stopGrooveAuditionNow();
    QMainWindow::closeEvent(e);
}

void LibraryWindow::buildUi() {
    QWidget* root = new QWidget(this);
    setCentralWidget(root);

    QVBoxLayout* main = new QVBoxLayout(root);

    // Controls bar
    QHBoxLayout* controls = new QHBoxLayout;
    controls->setSpacing(12);

    controls->addWidget(new QLabel("Root:", this));
    m_rootCombo = new QComboBox(this);
    for (const auto& n : pcNames()) m_rootCombo->addItem(n);
    m_rootCombo->setCurrentIndex(0); // C
    controls->addWidget(m_rootCombo);

    controls->addSpacing(10);
    controls->addWidget(new QLabel("Harmony key:", this));
    m_keyCombo = new QComboBox(this);
    for (const auto& n : pcNames()) m_keyCombo->addItem(n);
    m_keyCombo->setCurrentIndex(0); // C
    controls->addWidget(m_keyCombo);

    controls->addSpacing(10);
    controls->addWidget(new QLabel("Voicing chord context:", this));
    m_chordCtxCombo = new QComboBox(this);
    m_chordCtxCombo->setCurrentIndex(0);
    controls->addWidget(m_chordCtxCombo);

    controls->addSpacing(10);
    controls->addWidget(new QLabel("Playback:", this));
    m_playInstrumentCombo = new QComboBox(this);
    m_playInstrumentCombo->addItems({"Trumpet", "Bass", "Piano", "Guitar"});
    m_playInstrumentCombo->setCurrentText("Piano");
    controls->addWidget(m_playInstrumentCombo);

    controls->addWidget(new QLabel("Position:", this));
    m_positionCombo = new QComboBox(this);
    m_positionCombo->addItems({"Low", "Mid", "High"});
    m_positionCombo->setCurrentText("Mid");
    controls->addWidget(m_positionCombo);

    controls->addWidget(new QLabel("Duration:", this));
    m_durationCombo = new QComboBox(this);
    m_durationCombo->addItems({"Short", "Medium", "Long"});
    m_durationCombo->setCurrentText("Medium"); // default, matches prior behavior
    controls->addWidget(m_durationCombo);

    m_playButton = new QPushButton("Play", this);
    controls->addWidget(m_playButton);

    controls->addSpacing(10);
    m_full88Check = new QCheckBox("Full 88", this);
    m_full88Check->setChecked(false);
    controls->addWidget(m_full88Check);

    controls->addStretch(1);
    main->addLayout(controls);

    // Main content: left lists, right visualizers
    QHBoxLayout* content = new QHBoxLayout;

    // Tabs on left
    m_tabs = new QTabWidget(this);
    m_tabs->setMinimumWidth(320);

    auto mkTab = [&](QListWidget*& outList, const QString& title) -> QWidget* {
        QWidget* w = new QWidget(this);
        QVBoxLayout* l = new QVBoxLayout(w);
        outList = new QListWidget(this);
        outList->setSelectionMode(QAbstractItemView::SingleSelection);
        l->addWidget(outList);
        w->setLayout(l);
        m_tabs->addTab(w, title);
        return w;
    };

    mkTab(m_chordsList, "Chords");
    mkTab(m_scalesList, "Scales");
    mkTab(m_voicingsList, "Voicings");

    // Polychords tab (generator UI)
    m_polyTab = new QWidget(this);
    QVBoxLayout* polyL = new QVBoxLayout(m_polyTab);
    QGridLayout* grid = new QGridLayout;

    m_polyTemplateCombo = new QComboBox(this);
    m_polyUpperRoot = new QComboBox(this);
    m_polyUpperChord = new QComboBox(this);
    m_polyLowerRoot = new QComboBox(this);
    m_polyLowerChord = new QComboBox(this);

    for (const auto& n : pcNames()) {
        m_polyUpperRoot->addItem(n);
        m_polyLowerRoot->addItem(n);
    }
    m_polyUpperRoot->setCurrentIndex(2); // D (nice default for D/C)
    m_polyLowerRoot->setCurrentIndex(0); // C

    grid->addWidget(new QLabel("Template:", this), 0, 0);
    grid->addWidget(m_polyTemplateCombo, 0, 1, 1, 3);

    grid->addWidget(new QLabel("Upper triad:", this), 1, 0);
    grid->addWidget(m_polyUpperRoot, 1, 1);
    grid->addWidget(m_polyUpperChord, 1, 2, 1, 2);

    grid->addWidget(new QLabel("Lower:", this), 2, 0);
    grid->addWidget(m_polyLowerRoot, 2, 1);
    grid->addWidget(m_polyLowerChord, 2, 2, 1, 2);

    polyL->addLayout(grid);
    polyL->addStretch(1);
    m_tabs->addTab(m_polyTab, "Polychords");

    // Grooves tab (GrooveTemplate library)
    m_grooveTab = new QWidget(this);
    QVBoxLayout* gl = new QVBoxLayout(m_grooveTab);
    {
        QHBoxLayout* gctl = new QHBoxLayout;
        gctl->setSpacing(10);
        gctl->addWidget(new QLabel("BPM:", this));
        m_grooveTempoCombo = new QComboBox(this);
        m_grooveTempoCombo->addItems({"60", "80", "100", "120", "140", "160"});
        m_grooveTempoCombo->setCurrentText("120");
        // Allow live-follow to set arbitrary song BPM values.
        m_grooveTempoCombo->setEditable(true);
        gctl->addWidget(m_grooveTempoCombo);
        gctl->addStretch(1);
        gl->addLayout(gctl);
    }
    m_groovesList = new QListWidget(this);
    m_groovesList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_grooveInfo = new QLabel("—", this);
    m_grooveInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_grooveInfo->setStyleSheet("QLabel { font-family: Menlo, monospace; font-size: 9pt; color: #ddd; }");
    gl->addWidget(m_groovesList, 1);
    gl->addWidget(m_grooveInfo, 0);
    m_tabs->addTab(m_grooveTab, "Grooves");

    content->addWidget(m_tabs, 0);

    // Visualizers on right
    QVBoxLayout* viz = new QVBoxLayout;
    m_guitar = new GuitarFretboardWidget(this);
    m_piano = new PianoKeyboardWidget(this);
    m_piano->setRange(/*A2*/45, /*C5*/72);

    viz->addWidget(m_guitar);
    viz->addWidget(m_piano);
    content->addLayout(viz, 1);

    main->addLayout(content, 1);

    // Signals
    connect(m_tabs, &QTabWidget::currentChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_rootCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_keyCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_chordCtxCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_playInstrumentCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_positionCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_durationCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_full88Check, &QCheckBox::toggled, this, &LibraryWindow::updatePianoRange);
    connect(m_playButton, &QPushButton::clicked, this, &LibraryWindow::onPlayPressed);

    connect(m_chordsList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_scalesList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_voicingsList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_groovesList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);
    if (m_grooveTempoCombo) connect(m_grooveTempoCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);

    // Polychord signals
    connect(m_polyTemplateCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_polyUpperRoot, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_polyUpperChord, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_polyLowerRoot, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_polyLowerChord, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);

    // Click-to-play from visualizers
    connect(m_guitar, &GuitarFretboardWidget::noteClicked, this, &LibraryWindow::onUserClickedMidi);
    connect(m_piano, &PianoKeyboardWidget::noteClicked, this, &LibraryWindow::onUserClickedMidi);

    // Debounced auto-play on selection changes.
    m_autoPlayTimer = new QTimer(this);
    m_autoPlayTimer->setSingleShot(true);
    connect(m_autoPlayTimer, &QTimer::timeout, this, &LibraryWindow::onPlayPressed);

    // Live-follow timeout: if no theory events arrive recently, exit live-follow mode.
    m_liveFollowTimer = new QTimer(this);
    m_liveFollowTimer->setSingleShot(true);
    connect(m_liveFollowTimer, &QTimer::timeout, this, &LibraryWindow::onLiveFollowTimeout);

    // Groove audition timer
    m_grooveAuditionTimer = new QTimer(this);
    m_grooveAuditionTimer->setInterval(5);
    connect(m_grooveAuditionTimer, &QTimer::timeout, this, &LibraryWindow::onGrooveAuditionTick);
}

void LibraryWindow::populateLists() {
    // Build stable orderings (avoid QHash iteration order).
    m_orderedChords = m_registry.allChords();
    m_orderedScales = m_registry.allScales();
    m_orderedVoicings = m_registry.allVoicings();
    m_orderedGrooves = m_grooveRegistry.allGrooveTemplates();

    std::sort(m_orderedChords.begin(), m_orderedChords.end(), [&](const ChordDef* a, const ChordDef* b) {
        if (!a || !b) return a != nullptr;
        if (a->order != b->order) return a->order < b->order;
        return a->name < b->name;
    });

    std::sort(m_orderedScales.begin(), m_orderedScales.end(), [&](const ScaleDef* a, const ScaleDef* b) {
        if (!a || !b) return a != nullptr;
        if (a->order != b->order) return a->order < b->order;
        return a->name < b->name;
    });

    std::sort(m_orderedVoicings.begin(), m_orderedVoicings.end(), [&](const VoicingDef* a, const VoicingDef* b) {
        if (!a || !b) return a != nullptr;
        if (a->order != b->order) return a->order < b->order;
        if (a->category != b->category) return a->category < b->category;
        return a->name < b->name;
    });

    // Chords
    m_chordsList->clear();
    for (const ChordDef* c : m_orderedChords) {
        QListWidgetItem* it = new QListWidgetItem(c->name, m_chordsList);
        it->setData(Qt::UserRole, c->key);
    }
    m_chordsList->setCurrentRow(0);

    // Scales
    m_scalesList->clear();
    for (const ScaleDef* s : m_orderedScales) {
        QListWidgetItem* it = new QListWidgetItem(s->name, m_scalesList);
        it->setData(Qt::UserRole, s->key);
    }
    m_scalesList->setCurrentRow(0);

    // Voicings
    m_voicingsList->clear();
    for (const VoicingDef* v : m_orderedVoicings) {
        QListWidgetItem* it = new QListWidgetItem(v->name, m_voicingsList);
        it->setData(Qt::UserRole, v->key);
    }
    m_voicingsList->setCurrentRow(0);

    // Grooves
    if (m_groovesList) {
        m_groovesList->clear();
        for (const auto* gt : m_orderedGrooves) {
            if (!gt) continue;
            QListWidgetItem* it = new QListWidgetItem(QString("%1  (%2)").arg(gt->name, gt->category), m_groovesList);
            it->setData(Qt::UserRole, gt->key);
        }
        if (m_groovesList->count() > 0) m_groovesList->setCurrentRow(0);
    }

    // Chord context combo should match chord ordering.
    m_chordCtxCombo->clear();
    for (const ChordDef* c : m_orderedChords) {
        m_chordCtxCombo->addItem(c->name);
    }
    m_chordCtxCombo->setCurrentIndex(0);

    // Polychord template combo
    if (m_polyTemplateCombo) {
        m_polyTemplateCombo->clear();
        const auto polys = m_registry.allPolychordTemplates();
        for (const auto* t : polys) {
            m_polyTemplateCombo->addItem(t->name, t->key);
        }
        m_polyTemplateCombo->setCurrentIndex(0);
    }

    // Upper triad choices: show common triads only
    if (m_polyUpperChord) {
        m_polyUpperChord->clear();
        const QVector<QString> triadKeys = {"maj","min","dim","aug","sus2","sus4","phryg"};
        for (const QString& k : triadKeys) {
            const auto* c = m_registry.chord(k);
            if (!c) continue;
            m_polyUpperChord->addItem(c->name, c->key);
        }
        if (m_polyUpperChord->count() > 0) m_polyUpperChord->setCurrentIndex(0);
    }

    // Lower chord choices: all chords (ordered)
    if (m_polyLowerChord) {
        m_polyLowerChord->clear();
        for (const auto* c : m_orderedChords) {
            m_polyLowerChord->addItem(c->name, c->key);
        }
        if (m_polyLowerChord->count() > 0) m_polyLowerChord->setCurrentIndex(0);
    }
}

void LibraryWindow::updateGrooveInfo() {
    if (!m_grooveInfo || !m_groovesList) return;
    const auto* it = m_groovesList->currentItem();
    if (!it) { m_grooveInfo->setText("—"); return; }
    const QString key = it->data(Qt::UserRole).toString();
    const auto* gt = m_grooveRegistry.grooveTemplate(key);
    if (!gt) { m_grooveInfo->setText("—"); return; }
    QStringList lines;
    lines << QString("key=%1").arg(gt->key);
    lines << QString("grid=%1 amount=%2").arg(int(gt->gridKind)).arg(gt->amount, 0, 'f', 2);
    lines << "offsets:";
    for (const auto& o : gt->offsetMap) {
        lines << QString("  at %1/%2  unit=%3  delta=%4")
                     .arg(o.withinBeat.num)
                     .arg(o.withinBeat.den)
                     .arg(int(o.unit))
                     .arg(o.value, 0, 'f', 3);
    }
    m_grooveInfo->setText(lines.join("\n"));
}

int LibraryWindow::grooveBpm() const {
    const int d = m_grooveTempoCombo ? m_grooveTempoCombo->currentText().toInt() : 120;
    return qBound(30, d > 0 ? d : 120, 300);
}

QString LibraryWindow::selectedGrooveKey() const {
    if (!m_groovesList) return {};
    const auto* it = m_groovesList->currentItem();
    if (!it) return {};
    return it->data(Qt::UserRole).toString();
}

const virtuoso::groove::GrooveTemplate* LibraryWindow::selectedGrooveTemplate() const {
    const QString key = selectedGrooveKey();
    if (key.isEmpty()) return nullptr;
    return m_grooveRegistry.grooveTemplate(key);
}

void LibraryWindow::stopGrooveAuditionNow() {
    if (m_grooveAuditionTimer) m_grooveAuditionTimer->stop();
    ++m_grooveSession; // cancel any pending groove noteOff singleShots
    m_grooveAuditionEvents.clear();
    m_grooveAuditionIndex = 0;
    m_grooveAuditionLoopLenMs = 0;
    // Stop channel 6 (drums) which is where click+drum loop live.
    stopPlaybackNow(/*channel=*/6);
}

void LibraryWindow::rebuildGrooveAuditionEvents(const virtuoso::groove::GrooveTemplate* gt, int bpm) {
    if (!gt) return;
    if (bpm <= 0) bpm = 120;

    constexpr int ch = 6;
    virtuoso::groove::TimeSignature ts{4, 4};
    const int bars = 4;

    // Humanizer (deterministic) to apply selected GrooveTemplate.
    virtuoso::groove::InstrumentGrooveProfile prof;
    prof.instrument = "GrooveAudition";
    prof.humanizeSeed = 777;
    prof.microJitterMs = 0;
    prof.attackVarianceMs = 0;
    prof.velocityJitter = 0;
    prof.pushMs = 0;
    prof.laidBackMs = 0;
    prof.driftMaxMs = 0;
    prof.driftRate = 0.0;
    prof.phraseBars = 4;
    prof.phraseTimingMaxMs = 0;
    prof.phraseVelocityMax = 0.0;
    virtuoso::groove::TimingHumanizer hz(prof);
    hz.setGrooveTemplate(*gt);

    m_grooveAuditionEvents.clear();
    m_grooveAuditionEvents.reserve(bars * 64);

    auto add = [&](int note, int vel, const virtuoso::groove::GridPos& pos, virtuoso::groove::Rational dur, bool structural) {
        const auto he = hz.humanizeNote(pos, ts, bpm, vel, dur, structural);
        GrooveAuditionEvent e;
        e.channel = ch;
        e.note = note;
        e.vel = he.velocity;
        e.onMs = qMax<qint64>(0, he.onMs);
        e.offMs = qMax<qint64>(e.onMs + 10, he.offMs);
        m_grooveAuditionEvents.push_back(e);
    };

    // Click pattern: quarter notes (snare stick) + upbeats (mapped ride) so swing/pocket is audible.
    for (int bar = 0; bar < bars; ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            const auto p0 = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(bar, beat, 0, 1, ts);
            const auto p1 = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(bar, beat, 1, 2, ts); // upbeat
            add(virtuoso::drums::fluffy_brushes::kSnareRightHand_D1, 60, p0, {1, 32}, /*structural=*/true);
            add(virtuoso::drums::fluffy_brushes::kRideHitBorder_Ds2, 26, p1, {1, 64}, /*structural=*/false);
        }
    }

    // Always include a simple drum loop: ride every beat + snare swish on 2&4 + feather kick on 1.
    for (int bar = 0; bar < bars; ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            const auto p = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(bar, beat, 0, 1, ts);
            add(virtuoso::drums::fluffy_brushes::kRideHitBorder_Ds2, 34, p, {1, 32}, /*structural=*/(beat == 0));
            if (beat == 1 || beat == 3) {
                add(virtuoso::drums::fluffy_brushes::kSnareRightHand_D1, 30, p, {1, 32}, /*structural=*/true);
            }
            if (beat == 0) {
                add(virtuoso::drums::fluffy_brushes::kKickLooseNormal_G0, 22, p, {1, 16}, /*structural=*/true);
            }
        }
    }

    std::sort(m_grooveAuditionEvents.begin(), m_grooveAuditionEvents.end(), [](const GrooveAuditionEvent& a, const GrooveAuditionEvent& b) {
        return a.onMs < b.onMs;
    });

    // Loop length must be the *musical grid length* (exact 4 bars), not "last note ended".
    // Otherwise the loop will restart early (because the last event does not land exactly at bar end).
    const virtuoso::groove::GridPos endPos{bars, virtuoso::groove::Rational(0, 1)};
    m_grooveAuditionLoopLenMs = virtuoso::groove::GrooveGrid::posToMs(endPos, ts, bpm);
    if (m_grooveAuditionLoopLenMs <= 0) m_grooveAuditionLoopLenMs = 1;
}

void LibraryWindow::startOrUpdateGrooveLoop(bool preservePhase) {
    if (!m_grooveAuditionTimer || !m_midi) return;
    const auto* gt = selectedGrooveTemplate();
    if (!gt) { stopGrooveAuditionNow(); return; }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool wasActive = m_grooveAuditionTimer->isActive();
    const qint64 oldLen = m_grooveAuditionLoopLenMs;

    double phase01 = 0.0;
    if (m_liveFollowActive && m_songStartWallMs >= 0 && oldLen > 0) {
        // Phase-lock to song transport: align groove loop phase to (now - songStart).
        const qint64 relSong = now - m_songStartWallMs;
        const qint64 relLoop = (relSong >= 0) ? (relSong % oldLen) : 0;
        phase01 = double(relLoop) / double(oldLen);
    } else if (preservePhase && wasActive && oldLen > 0) {
        const qint64 rel = now - m_grooveAuditionStartWallMs;
        const qint64 relLoop = (rel >= 0) ? (rel % oldLen) : 0;
        phase01 = double(relLoop) / double(oldLen);
    }

    const int bpm = grooveBpm();
    rebuildGrooveAuditionEvents(gt, bpm);

    if (m_grooveAuditionLoopLenMs <= 0) {
        stopGrooveAuditionNow();
        return;
    }

    if (!wasActive) {
        // First start: hard reset the drum channel, then loop.
        stopPlaybackNow(/*channel=*/6);
        phase01 = 0.0;
    }

    // Preserve phase even if loop length changes (tempo changes).
    const qint64 relNew = qint64(llround(phase01 * double(m_grooveAuditionLoopLenMs)));
    m_grooveAuditionStartWallMs = now - relNew;

    // Seek index to the first event at/after current position in the loop.
    const qint64 relLoopNew = relNew % m_grooveAuditionLoopLenMs;
    int idx = 0;
    while (idx < m_grooveAuditionEvents.size() && m_grooveAuditionEvents[idx].onMs < relLoopNew) idx++;
    m_grooveAuditionIndex = idx;

    if (!wasActive) {
        m_grooveAuditionTimer->start();
    }
}

void LibraryWindow::onGrooveAuditionTick() {
    if (!m_grooveAuditionTimer || !m_grooveAuditionTimer->isActive()) return;
    if (!m_midi) return;
    const quint64 session = m_grooveSession;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 rel = now - m_grooveAuditionStartWallMs;

    // Loop seamlessly while the Grooves tab is visible.
    if (m_grooveAuditionLoopLenMs > 0 && rel >= m_grooveAuditionLoopLenMs) {
        const qint64 k = rel / m_grooveAuditionLoopLenMs;
        m_grooveAuditionStartWallMs += k * m_grooveAuditionLoopLenMs;
        rel = now - m_grooveAuditionStartWallMs;
        m_grooveAuditionIndex = 0;
    }
    while (m_grooveAuditionIndex < m_grooveAuditionEvents.size()) {
        const auto& ev = m_grooveAuditionEvents[m_grooveAuditionIndex];
        if (ev.onMs > rel) break;
        noteOnTracked(ev.channel, ev.note, ev.vel);
        QTimer::singleShot(int(qMax<qint64>(1, ev.offMs - ev.onMs)), this, [this, session, ch=ev.channel, note=ev.note]() {
            if (session != m_grooveSession) return;
            noteOffTracked(ch, note);
        });
        m_grooveAuditionIndex++;
    }
}

void LibraryWindow::onSelectionChanged() {
    if (m_liveUpdatingUi) {
        // Avoid feedback loops while live-follow is updating selection.
        updateHighlights();
        updateGrooveInfo();
        return;
    }
    updateHighlights();
    updateGrooveInfo();
    scheduleAutoPlay();

    // Grooves tab is "always auditioning" while visible.
    const bool groovesActive = (m_tabs && m_grooveTab && m_tabs->currentWidget() == m_grooveTab);
    if (groovesActive) {
        startOrUpdateGrooveLoop(/*preservePhase=*/true); // switch groove/tempo without restarting phase
    } else {
        stopGrooveAuditionNow();
    }
}

QString LibraryWindow::jsonString(const QJsonObject& o, const char* key) {
    const auto v = o.value(QString::fromUtf8(key));
    return v.isString() ? v.toString() : QString{};
}

int LibraryWindow::jsonInt(const QJsonObject& o, const char* key, int fallback) {
    const auto v = o.value(QString::fromUtf8(key));
    if (v.isDouble()) return v.toInt();
    if (v.isString()) {
        bool ok = false;
        const int n = v.toString().toInt(&ok);
        if (ok) return n;
    }
    return fallback;
}

void LibraryWindow::ingestTheoryEventJson(const QString& json) {
    // Drive live-follow ONLY from candidate_pool (authoritative + complete).
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject obj = doc.object();

    const QString eventKind = jsonString(obj, "event_kind");
    if (eventKind != "candidate_pool") return;

    // Anchor song start wall time from engine-clock ms.
    {
        const qint64 onMs = qint64(obj.value("on_ms").toVariant().toLongLong());
        if (onMs >= 0) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            m_songStartWallMs = now - onMs;
        }
    }

    // Exact candidate pools for filtering.
    m_liveCandChordKeys.clear();
    m_liveCandScaleKeys.clear();
    m_liveCandVoicingKeys.clear();
    m_liveCandGrooveKeys.clear();

    const QJsonObject cands = obj.value("candidates").toObject();
    for (const auto& v : cands.value("scales").toArray()) {
        const QJsonObject so = v.toObject();
        const QString k = so.value("key").toString();
        if (!k.isEmpty()) m_liveCandScaleKeys.insert(k);
    }
    for (const auto& v : cands.value("piano").toArray()) {
        const QJsonObject po = v.toObject();
        const QString vk = po.value("voicing_key").toString();
        if (!vk.isEmpty()) m_liveCandVoicingKeys.insert(vk);
    }
    const QString chordDefKey = obj.value("chord_def_key").toString();
    if (!chordDefKey.isEmpty()) m_liveCandChordKeys.insert(chordDefKey);
    const QString grooveKey = obj.value("groove_template").toString();
    if (!grooveKey.isEmpty()) m_liveCandGrooveKeys.insert(grooveKey);

    const int bpm = jsonInt(obj, "tempo_bpm", 0);
    if (bpm > 0) m_liveBpm = bpm;
    m_liveFollowActive = true;
    if (m_liveFollowTimer) m_liveFollowTimer->start(1500);

    applyEnabledStatesForLiveContext();
    applyLiveChoiceToUi(obj);
}

void LibraryWindow::onLiveFollowTimeout() {
    m_liveFollowActive = false;
    applyEnabledStatesForLiveContext();
}

void LibraryWindow::applyLiveChoiceToUi(const QJsonObject& obj) {
    if (!m_tabs) return;

    const QString eventKind = jsonString(obj, "event_kind");
    if (eventKind != "candidate_pool") return;

    // Chosen fields (prefer candidate_pool because it is complete even when Piano is silent).
    QString chordDefKey;
    int chordRootPc = -1;
    QString scaleUsed;
    QString voicingKey;
    QString voicingType;
    QString grooveTpl;
    int keyTonicPc = -1;

    chordDefKey = obj.value("chord_def_key").toString();
    chordRootPc = obj.value("chord_root_pc").toInt(-1);
    keyTonicPc = obj.value("key_tonic_pc").toInt(-1);
    grooveTpl = obj.value("groove_template").toString();
    const bool chordIsNew = obj.value("chord_is_new").toBool(false);
    const QJsonObject chosen = obj.value("chosen").toObject();
    scaleUsed = chosen.value("scale_used").toString();
    voicingKey = chosen.value("voicing_key").toString();
    voicingType = chosen.value("voicing_type").toString();
    const bool hasPolyChoice = chosen.value("has_polychord").toBool(false);

    m_liveUpdatingUi = true;
    const QSignalBlocker b0(m_rootCombo);
    const QSignalBlocker b0b(m_keyCombo);
    const QSignalBlocker b1(m_chordsList);
    const QSignalBlocker b2(m_scalesList);
    const QSignalBlocker b3(m_voicingsList);
    const QSignalBlocker b4(m_groovesList);
    const QSignalBlocker b5(m_grooveTempoCombo);
    const QSignalBlocker b6(m_polyTemplateCombo);
    const QSignalBlocker b7(m_polyUpperRoot);
    const QSignalBlocker b8(m_polyUpperChord);
    const QSignalBlocker b9(m_polyLowerRoot);
    const QSignalBlocker b10(m_polyLowerChord);

    // Always update key + root deterministically.
    if (m_rootCombo && chordRootPc >= 0) m_rootCombo->setCurrentIndex(qBound(0, chordRootPc, 11));
    if (m_keyCombo && keyTonicPc >= 0) m_keyCombo->setCurrentIndex(qBound(0, keyTonicPc, 11));

    // --- Chords tab selection (always update) ---
    if (!chordDefKey.isEmpty() && m_chordsList) {
        for (int i = 0; i < m_chordsList->count(); ++i) {
            auto* it = m_chordsList->item(i);
            if (!it) continue;
            if (it->data(Qt::UserRole).toString() == chordDefKey) {
                m_chordsList->setCurrentRow(i);
                break;
            }
        }
    }

    // --- Scale tab selection (always update) ---
    if (!scaleUsed.isEmpty() && m_scalesList) {
        QString name = scaleUsed.trimmed();
        const int p = name.indexOf('(');
        if (p > 0) name = name.left(p).trimmed();
        for (int i = 0; i < m_scalesList->count(); ++i) {
            auto* it = m_scalesList->item(i);
            if (!it) continue;
            if (it->text().compare(name, Qt::CaseInsensitive) == 0) {
                m_scalesList->setCurrentRow(i);
                break;
            }
        }
    }

    // --- Voicings tab selection (prefer exact ontology key) ---
    if (!voicingKey.isEmpty() && m_voicingsList) {
        for (int i = 0; i < m_voicingsList->count(); ++i) {
            auto* it = m_voicingsList->item(i);
            if (!it) continue;
            if (it->data(Qt::UserRole).toString() == voicingKey) {
                m_voicingsList->setCurrentRow(i);
                break;
            }
        }
    } else if (!voicingType.isEmpty() && m_voicingsList) {
        // Fallback if planner didn't provide a key.
        const QString v = voicingType;
        for (int i = 0; i < m_voicingsList->count(); ++i) {
            auto* it = m_voicingsList->item(i);
            if (!it) continue;
            if (v.contains(it->text(), Qt::CaseInsensitive)) { m_voicingsList->setCurrentRow(i); break; }
        }
    }

    // --- Grooves selection (always update) ---
    if (!grooveTpl.isEmpty() && m_groovesList) {
        for (int i = 0; i < m_groovesList->count(); ++i) {
            auto* it = m_groovesList->item(i);
            if (!it) continue;
            if (it->data(Qt::UserRole).toString() == grooveTpl) {
                m_groovesList->setCurrentRow(i);
                break;
            }
        }
    }
    if (m_grooveTempoCombo && m_liveBpm > 0) m_grooveTempoCombo->setCurrentText(QString::number(m_liveBpm));

    // Polychords: disable controls unless a real polychord choice exists.
    if (m_polyTemplateCombo) m_polyTemplateCombo->setEnabled(hasPolyChoice);
    if (m_polyUpperRoot) m_polyUpperRoot->setEnabled(hasPolyChoice);
    if (m_polyUpperChord) m_polyUpperChord->setEnabled(hasPolyChoice);
    if (m_polyLowerRoot) m_polyLowerRoot->setEnabled(hasPolyChoice);
    if (m_polyLowerChord) m_polyLowerChord->setEnabled(hasPolyChoice);

    // --- Polychords tab: map UST voicing keys into a triad-over-bass view ---
    if (hasPolyChoice && m_polyTab && m_tabs && m_polyTemplateCombo && !voicingKey.isEmpty() && voicingKey.startsWith("piano_ust_", Qt::CaseInsensitive)) {
        // Choose the "triad_over_bass" template if present.
        const int tplIdx = m_polyTemplateCombo->findData("triad_over_bass");
        if (tplIdx >= 0) m_polyTemplateCombo->setCurrentIndex(tplIdx);

        // Map common UST degrees.
        auto ustOffset = [&](const QString& k) -> int {
            if (k.endsWith("_bII")) return 1;
            if (k.endsWith("_II")) return 2;
            if (k.endsWith("_bIII")) return 3;
            if (k.endsWith("_III")) return 4;
            if (k.endsWith("_IV")) return 5;
            if (k.endsWith("_bV")) return 6;
            if (k.endsWith("_V")) return 7;
            if (k.endsWith("_bVI")) return 8;
            if (k.endsWith("_VI")) return 9;
            if (k.endsWith("_bVII")) return 10;
            if (k.endsWith("_VII")) return 11;
            return 0;
        };
        const int upPc = (chordRootPc >= 0) ? ((chordRootPc + ustOffset(voicingKey)) % 12) : -1;
        if (upPc >= 0 && m_polyUpperRoot) m_polyUpperRoot->setCurrentIndex(upPc);
        if (m_polyUpperChord) {
            const int majIdx = m_polyUpperChord->findData("maj");
            if (majIdx >= 0) m_polyUpperChord->setCurrentIndex(majIdx);
        }
        if (chordRootPc >= 0 && m_polyLowerRoot) m_polyLowerRoot->setCurrentIndex(chordRootPc);
        if (!chordDefKey.isEmpty() && m_polyLowerChord) {
            const int idx = m_polyLowerChord->findData(chordDefKey);
            if (idx >= 0) m_polyLowerChord->setCurrentIndex(idx);
        }
    }

    m_liveUpdatingUi = false;

    updateHighlights();
    updateGrooveInfo();

    // --- Audition triggering ---
    // Only audition on *actual chord changes*, per your requirement.
    const bool shouldAuditionNow = chordIsNew;

    m_lastChosenChordDefKey = chordDefKey;
    m_lastChosenChordRootPc = chordRootPc;
    if (!scaleUsed.isEmpty()) m_lastChosenScaleUsed = scaleUsed;
    if (!voicingKey.isEmpty()) m_lastChosenVoicingKey = voicingKey;
    if (!grooveTpl.isEmpty()) m_lastChosenGrooveKey = grooveTpl;

    // Only play when the visible tab's thing changed (so we don't spam every beat).
    const int tab = m_tabs ? m_tabs->currentIndex() : 0;
    const bool isChordTab = (tab == 0);
    const bool isScaleTab = (tab == 1);
    const bool isVoicingTab = (tab == 2);
    const bool isPolyTab = (m_polyTab && m_tabs && tab == m_tabs->indexOf(m_polyTab));
    const bool isGrooveTab = (m_grooveTab && m_tabs && tab == m_tabs->indexOf(m_grooveTab));

    if (isGrooveTab) {
        // Keep groove loop in sync when updated programmatically (signals are blocked above).
        startOrUpdateGrooveLoop(/*preservePhase=*/true);
    } else if (shouldAuditionNow && (isChordTab || isScaleTab || isVoicingTab || (isPolyTab && hasPolyChoice))) {
        onPlayPressed();
    }
}

void LibraryWindow::applyEnabledStatesForLiveContext() {
    auto setAllVisible = [](QListWidget* w) {
        if (!w) return;
        for (int i = 0; i < w->count(); ++i) {
            auto* it = w->item(i);
            if (!it) continue;
            it->setHidden(false);
        }
    };

    if (!m_liveFollowActive) {
        // Restore normal browsing when not live-following.
        setAllVisible(m_chordsList);
        setAllVisible(m_scalesList);
        setAllVisible(m_voicingsList);
        setAllVisible(m_groovesList);
        return;
    }

    // Exact "available choices" as emitted by the planner (candidate_pool): filter out non-candidates.
    auto applyAllowedSet = [](QListWidget* w, const QSet<QString>& allowedKeys) {
        if (!w) return;
        for (int i = 0; i < w->count(); ++i) {
            auto* it = w->item(i);
            if (!it) continue;
            const QString key = it->data(Qt::UserRole).toString();
            const bool show = allowedKeys.isEmpty() ? true : allowedKeys.contains(key);
            it->setHidden(!show);
        }
    };

    applyAllowedSet(m_chordsList, m_liveCandChordKeys);
    applyAllowedSet(m_scalesList, m_liveCandScaleKeys);
    applyAllowedSet(m_voicingsList, m_liveCandVoicingKeys);
    applyAllowedSet(m_groovesList, m_liveCandGrooveKeys);
}

QSet<int> LibraryWindow::pitchClassesForPolychord() const {
    QSet<int> pcs;
    if (!m_polyTemplateCombo || !m_polyUpperChord || !m_polyLowerChord) return pcs;

    const int upperRoot = pcFromIndex(m_polyUpperRoot ? m_polyUpperRoot->currentIndex() : 0);
    const int lowerRoot = pcFromIndex(m_polyLowerRoot ? m_polyLowerRoot->currentIndex() : 0);
    const auto* upper = m_registry.chord(m_polyUpperChord->currentData().toString());
    const auto* lower = m_registry.chord(m_polyLowerChord->currentData().toString());
    if (!upper || !lower) return pcs;

    for (int iv : upper->intervals) pcs.insert(normalizePc(upperRoot + iv));

    const QString tpl = m_polyTemplateCombo->currentData().toString();
    if (tpl == "triad_over_bass") {
        pcs.insert(normalizePc(lowerRoot));
    } else {
        for (int iv : lower->intervals) pcs.insert(normalizePc(lowerRoot + iv));
    }
    return pcs;
}

QVector<int> LibraryWindow::midiNotesForPolychord() const {
    QVector<int> notes;
    if (!m_polyTemplateCombo || !m_polyUpperChord || !m_polyLowerChord) return notes;

    const int upperRootPc = pcFromIndex(m_polyUpperRoot ? m_polyUpperRoot->currentIndex() : 0);
    const int lowerRootPc = pcFromIndex(m_polyLowerRoot ? m_polyLowerRoot->currentIndex() : 0);
    const auto* upper = m_registry.chord(m_polyUpperChord->currentData().toString());
    const auto* lower = m_registry.chord(m_polyLowerChord->currentData().toString());
    if (!upper || !lower) return notes;

    const int baseLower = baseRootMidiForPosition(lowerRootPc);
    const int baseUpper = normalizeMidi(baseLower + 12 + normalizePc(upperRootPc - lowerRootPc));

    // Lower part
    const QString tpl = m_polyTemplateCombo->currentData().toString();
    if (tpl == "triad_over_bass") {
        notes.push_back(normalizeMidi(baseLower - 12)); // bass root emphasis
    } else {
        for (int iv : lower->intervals) notes.push_back(normalizeMidi(baseLower + iv));
    }

    // Upper part (triad) above
    for (int iv : upper->intervals) notes.push_back(normalizeMidi(baseUpper + iv));

    std::sort(notes.begin(), notes.end());
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
    return notes;
}

void LibraryWindow::updatePianoRange() {
    if (!m_piano) return;
    if (m_full88Check && m_full88Check->isChecked()) {
        m_piano->setRange(21, 108);
    } else {
        m_piano->setRange(/*A2*/45, /*C5*/72);
    }
    scheduleAutoPlay();
}

int LibraryWindow::pcFromIndex(int idx) {
    return normalizePc(idx);
}

QString LibraryWindow::pcName(int pc) {
    const auto names = pcNames();
    return names[normalizePc(pc)];
}

QSet<int> LibraryWindow::pitchClassesForChord(const ChordDef* chordDef, int rootPc) const {
    QSet<int> pcs;
    if (!chordDef) return pcs;
    for (int iv : chordDef->intervals) pcs.insert(normalizePc(rootPc + iv));
    return pcs;
}

QSet<int> LibraryWindow::pitchClassesForScale(const ScaleDef* scaleDef, int rootPc) const {
    QSet<int> pcs;
    if (!scaleDef) return pcs;
    for (int iv : scaleDef->intervals) pcs.insert(normalizePc(rootPc + iv));
    return pcs;
}

QSet<int> LibraryWindow::pitchClassesForVoicing(const VoicingDef* voicingDef,
                                                const ChordDef* chordContext,
                                                int rootPc) const {
    QSet<int> pcs;
    if (!voicingDef) return pcs;

    // Interval-based voicing: direct semitone offsets from root.
    if (!voicingDef->intervals.isEmpty()) {
        for (int iv : voicingDef->intervals) pcs.insert(normalizePc(rootPc + iv));
        return pcs;
    }

    // Special-case: quartal placeholder voicing currently has no degree list.
    if (voicingDef->chordDegrees.isEmpty() && voicingDef->key == "piano_quartal_stack4ths") {
        // Stage-1 approximation: use guide/extension tones so something shows and is playable.
        // (Later we can derive true quartal stacks from the chosen scale/mode.)
        const int st3 = degreeToSemitone(chordContext, 3);
        const int st7 = degreeToSemitone(chordContext, 7);
        const int st9 = degreeToSemitone(chordContext, 9);
        pcs.insert(normalizePc(rootPc + st3));
        pcs.insert(normalizePc(rootPc + st7));
        pcs.insert(normalizePc(rootPc + st9));
        return pcs;
    }

    // Stage 1: interpret voicing degrees relative to chord context with a simple extension mapping.
    for (int deg : voicingDef->chordDegrees) {
        const int st = degreeToSemitone(chordContext, deg);
        pcs.insert(normalizePc(rootPc + st));
    }
    return pcs;
}

QHash<int, QString> LibraryWindow::degreeLabelsForChord(const ChordDef* chordDef) const {
    QHash<int, QString> out;
    if (!chordDef) return out;
    for (int iv : chordDef->intervals) {
        const QString deg = degreeLabelForChordInterval(iv);
        if (!deg.isEmpty()) out.insert(normalizePc(iv), deg);
    }
    return out;
}

QHash<int, QString> LibraryWindow::degreeLabelsForScale(const ScaleDef* scaleDef) const {
    QHash<int, QString> out;
    if (!scaleDef) return out;
    for (int i = 0; i < scaleDef->intervals.size(); ++i) {
        out.insert(normalizePc(scaleDef->intervals[i]), QString::number(i + 1));
    }
    return out;
}

QHash<int, QString> LibraryWindow::degreeLabelsForVoicing(const VoicingDef* voicingDef,
                                                          const ChordDef* chordContext) const {
    QHash<int, QString> out;
    if (!voicingDef) return out;
    if (!voicingDef->intervals.isEmpty()) {
        for (int iv : voicingDef->intervals) out.insert(normalizePc(iv), degreeLabelForChordInterval(iv));
        return out;
    }
    if (voicingDef->chordDegrees.isEmpty() && voicingDef->key == "piano_quartal_stack4ths") {
        out.insert(normalizePc(degreeToSemitone(chordContext, 3)), "3");
        out.insert(normalizePc(degreeToSemitone(chordContext, 7)), "7");
        out.insert(normalizePc(degreeToSemitone(chordContext, 9)), "9");
        return out;
    }
    for (int deg : voicingDef->chordDegrees) {
        const int st = degreeToSemitone(chordContext, deg);
        out.insert(normalizePc(st), QString::number(deg));
    }
    return out;
}

int LibraryWindow::selectedPlaybackChannel() const {
    // Spec:
    // Piano -> ch4
    // Guitar -> ch5
    // Bass -> ch3
    // Trumpet -> ch1
    const QString inst = m_playInstrumentCombo ? m_playInstrumentCombo->currentText() : "Piano";
    if (inst == "Trumpet") return 1;
    if (inst == "Bass") return 3;
    if (inst == "Piano") return 4;
    if (inst == "Guitar") return 5;
    return 4;
}

int LibraryWindow::baseRootMidiForPosition(int rootPc) const {
    // Choose a base register for auditioning. This affects what octave we place the root in.
    // (This is independent of the playback instrument channel; it’s purely for a sensible register.)
    const QString pos = m_positionCombo ? m_positionCombo->currentText() : "Mid";
    int base = 60; // default: C4-ish
    if (pos == "Low") base = 48;   // C3-ish
    if (pos == "High") base = 72;  // C5-ish

    // Snap base to the selected root pitch class, at or below base, then bump up if too low.
    int baseRoot = base - normalizePc(base - rootPc);
    if (baseRoot < 24) baseRoot += 12;
    return normalizeMidi(baseRoot);
}

int LibraryWindow::perNoteDurationMs() const {
    // When live-following a playing song, sync audition timing to the song BPM.
    // (Manual library audition keeps the legacy "Short/Medium/Long" values.)
    if (m_liveFollowActive && m_liveBpm > 0) {
        const int quarterMs = qMax(40, int(llround(60000.0 / double(m_liveBpm))));
        const QString d = m_durationCombo ? m_durationCombo->currentText() : "Medium";
        if (d == "Short") return qMax(40, int(llround(double(quarterMs) * 0.50)));
        if (d == "Long") return qMax(60, int(llround(double(quarterMs) * 2.00)));
        return qMax(50, quarterMs);
    }
    const QString d = m_durationCombo ? m_durationCombo->currentText() : "Medium";
    // Restore snappier audition timing (closer to the original feel).
    if (d == "Short") return 180;
    if (d == "Long") return 900;
    return 500; // Medium
}

void LibraryWindow::setActiveMidi(int midi, bool on) {
    if (midi < 0 || midi > 127) return;
    if (on) m_activeMidis.insert(midi);
    else m_activeMidis.remove(midi);
    if (m_guitar) m_guitar->setActiveMidiNotes(m_activeMidis);
    if (m_piano) m_piano->setActiveMidiNotes(m_activeMidis);
}

void LibraryWindow::clearActiveMidis() {
    m_activeMidis.clear();
    if (m_guitar) m_guitar->setActiveMidiNotes(m_activeMidis);
    if (m_piano) m_piano->setActiveMidiNotes(m_activeMidis);
}

void LibraryWindow::scheduleAutoPlay() {
    if (!m_autoPlayTimer) return;
    // Small debounce so rapid list navigation doesn't spam MIDI.
    m_autoPlayTimer->start(80);
}

QVector<int> LibraryWindow::midiNotesForSelectionTab(int tab, int rootPc) const {
    QVector<int> notes;
    const int baseRoot = baseRootMidiForPosition(rootPc);

    if (tab == 0 && m_chordsList && m_chordsList->currentItem()) {
        const auto* chordDef = m_registry.chord(m_chordsList->currentItem()->data(Qt::UserRole).toString());
        if (!chordDef) return notes;
        for (int iv : chordDef->intervals) notes.push_back(normalizeMidi(baseRoot + iv));
        // If this chord encodes a slash-bass/inversion, add an emphasized bass note one octave below.
        if (chordDef->bassInterval >= 0) {
            notes.push_back(normalizeMidi(baseRoot - 12 + chordDef->bassInterval));
        }
    } else if (tab == 1 && m_scalesList && m_scalesList->currentItem()) {
        const auto* scaleDef = m_registry.scale(m_scalesList->currentItem()->data(Qt::UserRole).toString());
        if (!scaleDef) return notes;
        for (int iv : scaleDef->intervals) notes.push_back(normalizeMidi(baseRoot + iv));
        // add octave for a more scale-like sound
        notes.push_back(normalizeMidi(baseRoot + 12));
    } else if (tab == 2 && m_voicingsList && m_voicingsList->currentItem()) {
        const auto* voicingDef = m_registry.voicing(m_voicingsList->currentItem()->data(Qt::UserRole).toString());
        if (!voicingDef) return notes;
        const int idx = qBound(0, m_chordCtxCombo ? m_chordCtxCombo->currentIndex() : 0, m_orderedChords.size() - 1);
        const auto* chordCtx = m_orderedChords.isEmpty() ? nullptr : m_orderedChords[idx];

        if (!voicingDef->intervals.isEmpty()) {
            for (int iv : voicingDef->intervals) notes.push_back(normalizeMidi(baseRoot + iv));
        } else
        if (voicingDef->chordDegrees.isEmpty() && voicingDef->key == "piano_quartal_stack4ths") {
            // Mirror pitch-class fallback so quartal also auditions.
            notes.push_back(normalizeMidi(baseRoot + degreeToSemitone(chordCtx, 3)));
            notes.push_back(normalizeMidi(baseRoot + degreeToSemitone(chordCtx, 7)));
            notes.push_back(normalizeMidi(baseRoot + degreeToSemitone(chordCtx, 9)));
        } else {
            for (int deg : voicingDef->chordDegrees) {
                const int st = degreeToSemitone(chordCtx, deg);
                notes.push_back(normalizeMidi(baseRoot + st));
            }
        }
    }

    std::sort(notes.begin(), notes.end());
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
    return notes;
}

QVector<int> LibraryWindow::midiNotesForCurrentSelection(int rootPc) const {
    const int tab = m_tabs ? m_tabs->currentIndex() : 0;
    return midiNotesForSelectionTab(tab, rootPc);
}

void LibraryWindow::playSingleNote(int midi, int durationMs) {
    if (!m_midi) return;
    const int ch = selectedPlaybackChannel();
    const int vel = 48;
    const quint64 session = ++m_playSession;
    clearActiveMidis();
    stopPlaybackNow(ch);
    setActiveMidi(midi, true);
    noteOnTracked(ch, midi, vel);
    QTimer::singleShot(durationMs, this, [this, session, ch, midi]() {
        if (session != m_playSession) return;
        if (!m_midi) return;
        noteOffTracked(ch, midi);
        setActiveMidi(midi, false);
    });
}

void LibraryWindow::stopPlaybackNow(int channel) {
    if (!m_midi) return;
    // First, release any notes we know we turned on (works even if the host ignores CC123).
    const QSet<int> held = m_heldNotesByChannel.value(channel);
    for (int n : held) {
        m_midi->sendVirtualNoteOff(channel, n);
    }
    m_heldNotesByChannel[channel].clear();

    // Then send "panic" style messages.
    m_midi->sendVirtualCC(channel, 64, 0); // sustain off
    m_midi->sendVirtualAllNotesOff(channel);
}

void LibraryWindow::noteOnTracked(int channel, int midi, int vel) {
    if (!m_midi) return;
    m_heldNotesByChannel[channel].insert(midi);
    m_midi->sendVirtualNoteOn(channel, midi, vel);
}

void LibraryWindow::noteOffTracked(int channel, int midi) {
    if (!m_midi) return;
    m_heldNotesByChannel[channel].remove(midi);
    m_midi->sendVirtualNoteOff(channel, midi);
}

void LibraryWindow::playMidiNotes(const QVector<int>& notes, int durationMs, bool arpeggiate) {
    if (notes.isEmpty()) return;
    if (!m_midi) return;

    const int ch = selectedPlaybackChannel();
    const int vel = 48;
    const quint64 session = ++m_playSession;
    clearActiveMidis();
    // Avoid stuck notes during fast auditioning (including old canceled timers).
    stopPlaybackNow(ch);

    if (!arpeggiate) {
        for (int n : notes) {
            setActiveMidi(n, true);
            noteOnTracked(ch, n, vel);
        }
        QTimer::singleShot(durationMs, this, [this, session, ch, notes]() {
            if (session != m_playSession) return;
            if (!m_midi) return;
            for (int n : notes) {
                noteOffTracked(ch, n);
                setActiveMidi(n, false);
            }
        });
        return;
    }

    // Arpeggiate (used for scales): up then down.
    // Build sequence that always ends on the root (notes[0]) if present:
    // up (including top) then down (excluding top) ending at root.
    QVector<int> seq;
    seq.reserve(notes.size() * 2);
    for (int n : notes) seq.push_back(n);
    for (int i = notes.size() - 2; i >= 0; --i) seq.push_back(notes[i]);

    // If a groove is selected, use its timing (swing/pocket) for the scale playback.
    // Otherwise fall back to the legacy fixed-step arpeggio.
    const auto* gt = selectedGrooveTemplate();
    virtuoso::groove::TimeSignature ts{4, 4};

    QVector<qint64> onMs;
    QVector<qint64> offMs;
    if (gt) {
        virtuoso::groove::InstrumentGrooveProfile prof;
        prof.instrument = "ScaleAudition";
        prof.humanizeSeed = 4242;
        prof.microJitterMs = 0;
        prof.attackVarianceMs = 0;
        prof.velocityJitter = 0;
        prof.pushMs = 0;
        prof.laidBackMs = 0;
        prof.driftMaxMs = 0;
        prof.driftRate = 0.0;
        prof.phraseBars = 4;
        prof.phraseTimingMaxMs = 0;
        prof.phraseVelocityMax = 0.0;
        virtuoso::groove::TimingHumanizer hz(prof);
        hz.setGrooveTemplate(*gt);

        // IMPORTANT:
        // - In normal Library audition: Duration controls the absolute speed (legacy behavior).
        // - In live-follow: BPM must follow the song; Duration controls subdivision (quarter/8th/16th).
        const bool live = (m_liveFollowActive && m_liveBpm > 0);

        const bool tripletish =
            (gt->gridKind == virtuoso::groove::GrooveGridKind::Triplet8 ||
             gt->gridKind == virtuoso::groove::GrooveGridKind::Shuffle12_8);

        const QString d = m_durationCombo ? m_durationCombo->currentText() : "Medium";
        int subdivCount = tripletish ? 3 : 2; // medium default: 8ths (or 8th-triplets)
        if (d == "Short") subdivCount *= 2;  // 16ths (or 16th-triplets)
        if (d == "Long") subdivCount = 1;    // quarters

        int bpmVirtual = 120;
        if (live) {
            // Live-follow: make scale audition feel brisker.
            bpmVirtual = qBound(30, m_liveBpm * 2, 600);
        } else {
            const int stepMsBase = qMax(25, durationMs / 5);
            const int beatMsVirtual = qMax(20, stepMsBase * subdivCount);
            bpmVirtual = qBound(10, int(llround(60000.0 / double(beatMsVirtual))), 2400);
        }

        onMs.resize(seq.size());
        offMs.resize(seq.size());
        for (int i = 0; i < seq.size(); ++i) {
            const int idx = i;
            const int beatAbs = idx / subdivCount;
            const int bar = beatAbs / qMax(1, ts.num);
            const int beatInBar = beatAbs % qMax(1, ts.num);
            const int subdiv = idx % subdivCount;
            const auto pos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(bar, beatInBar, subdiv, subdivCount, ts);
            const virtuoso::groove::Rational dur{1, ts.den * subdivCount};
            const auto he = hz.humanizeNote(pos, ts, bpmVirtual, vel, dur, /*structural=*/(subdiv == 0));
            onMs[i] = qMax<qint64>(0, he.onMs);
            offMs[i] = qMax<qint64>(onMs[i] + 12, he.offMs);
        }
    }

    const bool useGroove = (gt && onMs.size() == seq.size());
    // Faster scale feel (still tied to Duration): step is a fraction of chord duration.
    const int stepMsFixed = qMax(25, durationMs / 5);
    const int gateMsFixed = qMax(18, int(double(stepMsFixed) * 0.80));

    // Use a chained timer approach (rather than N independent timers) to avoid ordering jitter.
    using StepFn = std::function<void(int idx, int prev)>;
    auto stepFn = std::make_shared<StepFn>();
    *stepFn = [this, session, ch, vel, gateMsFixed, stepMsFixed, seq, stepFn, useGroove, onMs, offMs](int idx, int prev) {
        if (session != m_playSession) return;
        if (!m_midi) return;
        if (idx >= seq.size()) {
            if (prev >= 0) {
                noteOffTracked(ch, prev);
                setActiveMidi(prev, false);
            }
            return;
        }

        const int n = seq[idx];

        // Monophonic guarantee: kill previous immediately.
        if (prev >= 0) {
            noteOffTracked(ch, prev);
            setActiveMidi(prev, false);
        }

        setActiveMidi(n, true);
        noteOnTracked(ch, n, vel);

        // Gate-off (safe even if next step already killed it).
        // In groove mode, keep notes sounding until just before the next onset.
        // This makes swing/triplet feel much more audible in a monophonic scale.
        int gateMs = gateMsFixed;
        int nextDelayMs = stepMsFixed;
        if (useGroove && idx + 1 < onMs.size()) {
            nextDelayMs = int(qMax<qint64>(1, onMs[idx + 1] - onMs[idx]));
            gateMs = qMax(18, nextDelayMs - 4);
        } else if (useGroove && idx < onMs.size()) {
            // Last note: mirror the prevailing grooved interval so it doesn't "clip" short.
            int prevDelay = stepMsFixed;
            if (idx > 0 && idx < onMs.size()) {
                prevDelay = int(qMax<qint64>(1, onMs[idx] - onMs[idx - 1]));
            }
            gateMs = qMax(18, prevDelay - 4);
        }

        QTimer::singleShot(gateMs, this, [this, session, ch, n]() {
            if (session != m_playSession) return;
            if (!m_midi) return;
            noteOffTracked(ch, n);
            setActiveMidi(n, false);
        });

        QTimer::singleShot(nextDelayMs, this, [stepFn, idx, n]() { (*stepFn)(idx + 1, n); });
    };

    if (useGroove && !onMs.isEmpty()) {
        const int firstDelay = int(onMs[0]);
        QTimer::singleShot(firstDelay, this, [stepFn]() { (*stepFn)(/*idx=*/0, /*prev=*/-1); });
    } else {
        (*stepFn)(/*idx=*/0, /*prev=*/-1);
    }
}

void LibraryWindow::onPlayPressed() {
    const int tab = m_tabs ? m_tabs->currentIndex() : 0;
    const int dur = perNoteDurationMs();

    // Polychords tab
    if (m_polyTab && tab == m_tabs->indexOf(m_polyTab)) {
        const QVector<int> notes = midiNotesForPolychord();
        playMidiNotes(notes, /*durationMs=*/dur, /*arpeggiate=*/false);
        return;
    }

    const int rootPc = pcFromIndex(m_rootCombo ? m_rootCombo->currentIndex() : 0);
    const QVector<int> notes = midiNotesForSelectionTab(tab, rootPc);
    const bool isScale = (tab == 1); // scales tab is still index 1
    playMidiNotes(notes, /*durationMs=*/dur, /*arpeggiate=*/isScale);
}

void LibraryWindow::onUserClickedMidi(int midi) {
    playSingleNote(midi, /*durationMs=*/perNoteDurationMs());
}

void LibraryWindow::updateHighlights() {
    const int rootPc = pcFromIndex(m_rootCombo ? m_rootCombo->currentIndex() : 0);
    const int keyPc = pcFromIndex(m_keyCombo ? m_keyCombo->currentIndex() : 0);

    QSet<int> pcs;
    QHash<int, QString> deg;

    const int tab = m_tabs ? m_tabs->currentIndex() : 0;

    if (m_polyTab && tab == m_tabs->indexOf(m_polyTab)) {
        const QSet<int> pcs = pitchClassesForPolychord();
        if (m_guitar) {
            m_guitar->setRootPitchClass(-1);
            m_guitar->setHighlightedPitchClasses(pcs);
            m_guitar->setDegreeLabels({});
        }
        if (m_piano) {
            m_piano->setRootPitchClass(-1);
            m_piano->setHighlightedPitchClasses(pcs);
            m_piano->setDegreeLabels({});
        }
        // Harmony + suggested scales for polychord pitch set (status bar)
        if (statusBar()) {
            QString harmonyPrefix;
            if (m_polyLowerChord && m_polyLowerRoot) {
                const int lowerRoot = pcFromIndex(m_polyLowerRoot->currentIndex());
                const auto* lower = m_registry.chord(m_polyLowerChord->currentData().toString());
                if (lower) {
                    const auto h = virtuoso::theory::analyzeChordInMajorKey(keyPc, lowerRoot, *lower);
                    harmonyPrefix = QString("Harmony: %1 — %2 (%3)").arg(h.roman, h.function, h.detail);
                }
            }
            const auto sug = virtuoso::theory::suggestScalesForPitchClasses(m_registry, pcs, 6);
            QString msg = harmonyPrefix.isEmpty() ? QString() : (harmonyPrefix + "  |  ");
            msg += "Suggested scales: ";
            for (int i = 0; i < sug.size(); ++i) {
                if (i) msg += " | ";
                msg += QString("%1 (%2)").arg(sug[i].name).arg(pcName(sug[i].bestTranspose));
            }
            statusBar()->showMessage(msg);
        }
        return;
    }
    if (tab == 0 && m_chordsList) {
        if (!m_chordsList->currentItem()) return;
        const auto* chordDef = m_registry.chord(m_chordsList->currentItem()->data(Qt::UserRole).toString());
        pcs = pitchClassesForChord(chordDef, rootPc);
        deg = degreeLabelsForChord(chordDef);
        if (statusBar() && chordDef) {
            const auto h = virtuoso::theory::analyzeChordInMajorKey(keyPc, rootPc, *chordDef);
            const auto sugg = virtuoso::theory::suggestScalesForPitchClasses(m_registry, pcs, 10);

            // Re-rank by harmony function vs the selected key.
            struct Sc { virtuoso::theory::ScaleSuggestion s; double score = 0.0; };
            QVector<Sc> ranked;
            ranked.reserve(sugg.size());
            for (const auto& s : sugg) {
                double bonus = 0.0;
                // Prefer scales rooted on the chord root for chord-scale language.
                if (normalizePc(s.bestTranspose) == normalizePc(rootPc)) bonus += 0.6;
                const QString name = s.name.toLower();
                if (h.function == "Dominant") {
                    if (name.contains("altered") || name.contains("lydian dominant") || name.contains("mixolydian") || name.contains("half-whole")) bonus += 0.35;
                } else if (h.function == "Subdominant") {
                    if (name.contains("dorian") || name.contains("lydian") || name.contains("phrygian")) bonus += 0.25;
                } else if (h.function == "Tonic") {
                    if (name.contains("ionian") || name.contains("major") || name.contains("lydian")) bonus += 0.25;
                }
                ranked.push_back({s, s.score + bonus});
            }
            std::sort(ranked.begin(), ranked.end(), [](const Sc& a, const Sc& b) {
                if (a.score != b.score) return a.score > b.score;
                return a.s.name < b.s.name;
            });

            QString msg = QString("Harmony: %1 — %2 (%3)").arg(h.roman, h.function, h.detail);
            msg += "  |  Suggested scales: ";
            const int maxShow = qMin(6, ranked.size());
            for (int i = 0; i < maxShow; ++i) {
                if (i) msg += " | ";
                msg += QString("%1 (%2)").arg(ranked[i].s.name).arg(pcName(ranked[i].s.bestTranspose));
            }
            statusBar()->showMessage(msg);
        }
    } else if (tab == 1 && m_scalesList) {
        if (!m_scalesList->currentItem()) return;
        const auto* scaleDef = m_registry.scale(m_scalesList->currentItem()->data(Qt::UserRole).toString());
        pcs = pitchClassesForScale(scaleDef, rootPc);
        deg = degreeLabelsForScale(scaleDef);
        if (statusBar()) statusBar()->clearMessage();
    } else if (tab == 2 && m_voicingsList) {
        if (!m_voicingsList->currentItem()) return;
        const auto* voicingDef = m_registry.voicing(m_voicingsList->currentItem()->data(Qt::UserRole).toString());

        // chord context by combo index into allChords() list ordering
        const auto allChords = m_registry.allChords();
        const int idx = qBound(0, m_chordCtxCombo ? m_chordCtxCombo->currentIndex() : 0, allChords.size() - 1);
        const auto* chordCtx = allChords.isEmpty() ? nullptr : allChords[idx];
        pcs = pitchClassesForVoicing(voicingDef, chordCtx, rootPc);
        deg = degreeLabelsForVoicing(voicingDef, chordCtx);

        // If this is a UST voicing, show explicit hints + ranked suggestions.
        if (statusBar() && voicingDef && voicingDef->tags.contains("ust")) {
            // union of chord context + voicing pcs
            QSet<int> unionPcs = pcs;
            if (chordCtx) {
                for (int iv : chordCtx->intervals) unionPcs.insert(normalizePc(rootPc + iv));
            }
            const QString chordKey = chordCtx ? chordCtx->key : QString();
            const auto hints = virtuoso::theory::explicitHintScalesForContext(voicingDef->key, chordKey);
            const auto ranked = virtuoso::theory::suggestScalesForPitchClasses(m_registry, unionPcs, 6);

            QString msg = "UST scale hints: ";
            if (!hints.isEmpty()) {
                for (int i = 0; i < hints.size(); ++i) {
                    msg += (i ? ", " : "");
                    // Show hinted scale name + best transpose inferred from the same union pitch set
                    QString label = hints[i];
                    int bestT = 0;
                    for (const auto& s : ranked) {
                        if (s.key == hints[i]) {
                            label = s.name;
                            bestT = s.bestTranspose;
                            break;
                        }
                    }
                    const auto* sd = m_registry.scale(hints[i]);
                    if (sd) label = sd->name;
                    msg += QString("%1 (%2)").arg(label).arg(pcName(bestT));
                }
            } else {
                msg += "(none)";
            }
            msg += "  |  Suggested scales: ";
            for (int i = 0; i < ranked.size(); ++i) {
                if (i) msg += " | ";
                msg += QString("%1 (%2)").arg(ranked[i].name).arg(pcName(ranked[i].bestTranspose));
            }
            statusBar()->showMessage(msg);
        } else if (statusBar()) {
            statusBar()->clearMessage();
        }
    }

    if (m_guitar) {
        m_guitar->setRootPitchClass(rootPc);
        m_guitar->setHighlightedPitchClasses(pcs);
        m_guitar->setDegreeLabels(deg);
    }
    if (m_piano) {
        m_piano->setRootPitchClass(rootPc);
        m_piano->setHighlightedPitchClasses(pcs);
        m_piano->setDegreeLabels(deg);
    }

    // (No generic statusBar clearing here; each tab owns its own status behavior.)
}

