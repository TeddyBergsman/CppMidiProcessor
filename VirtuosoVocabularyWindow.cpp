#include "VirtuosoVocabularyWindow.h"

#include <QListWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
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

#include "playback/BrushesBalladDrummer.h"

#include <algorithm>

#include "midiprocessor.h"
#include "music/ChordSymbol.h"
void VirtuosoVocabularyWindow::closeEvent(QCloseEvent* e) {
    stopAuditionNow();
    QMainWindow::closeEvent(e);
}


namespace {
static int clampMidi(int m) { return (m < 0) ? 0 : (m > 127 ? 127 : m); }

static QString midiName(int midi) {
    static const char* names[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    const int m = clampMidi(midi);
    const int pc = m % 12;
    const int oct = (m / 12) - 1;
    return QString("%1%2").arg(names[pc]).arg(oct);
}

static QString joinInts(const QVector<int>& v) {
    QStringList s;
    for (int x : v) s.push_back(QString::number(x));
    return s.join(",");
}

static QString beatsString(const QVector<int>& beats) {
    QStringList s;
    for (int b : beats) s.push_back(QString::number(b + 1));
    return s.join(",");
}
} // namespace

VirtuosoVocabularyWindow::VirtuosoVocabularyWindow(MidiProcessor* midi,
                                                   Instrument instrument,
                                                   QWidget* parent)
    : QMainWindow(parent)
    , m_midi(midi)
    , m_instrument(instrument)
    , m_grooveRegistry(virtuoso::groove::GrooveRegistry::builtins()) {
    m_ontology = virtuoso::ontology::OntologyRegistry::builtins();
    setWindowTitle(QString("Virtuoso Vocabulary — %1").arg(instrumentName(m_instrument)));
    resize(1180, 680);
    buildUi();
    loadVocab();
    rebuildPresetCombo();
    refreshPatternList();
    refreshDetailsAndPreview();
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

    controls->addWidget(new QLabel("Preset:", this));
    m_presetCombo = new QComboBox(this);
    m_presetCombo->setMinimumWidth(260);
    controls->addWidget(m_presetCombo);

    controls->addSpacing(10);
    controls->addWidget(new QLabel("BPM:", this));
    m_bpm = new QSpinBox(this);
    m_bpm->setRange(30, 260);
    m_bpm->setValue(60);
    controls->addWidget(m_bpm);

    controls->addSpacing(10);
    controls->addWidget(new QLabel("Energy:", this));
    m_energy = new QDoubleSpinBox(this);
    m_energy->setRange(0.0, 1.0);
    m_energy->setSingleStep(0.05);
    m_energy->setValue(0.45);
    controls->addWidget(m_energy);

    if (m_instrument == Instrument::Drums) {
        m_intensityPeak = new QCheckBox("Intensity peak", this);
        controls->addWidget(m_intensityPeak);
    }

    // --- Ontology-driven chord/voicing selectors (no manual typing) ---
    auto addPcCombo = [&](QComboBox*& combo) {
        combo = new QComboBox(this);
        combo->addItems({"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"});
        combo->setCurrentIndex(0);
    };

    controls->addSpacing(10);
    controls->addWidget(new QLabel("Root:", this));
    addPcCombo(m_rootCombo);
    controls->addWidget(m_rootCombo);

    controls->addWidget(new QLabel("Chord:", this));
    m_chordCombo = new QComboBox(this);
    m_chordCombo->setMinimumWidth(150);
    controls->addWidget(m_chordCombo);

    if (m_instrument == Instrument::Piano) {
        controls->addWidget(new QLabel("Voicing:", this));
        m_voicingCombo = new QComboBox(this);
        m_voicingCombo->setMinimumWidth(220);
        controls->addWidget(m_voicingCombo);
    }

    if (m_instrument == Instrument::Bass) {
        controls->addWidget(new QLabel("Next root:", this));
        addPcCombo(m_nextRootCombo);
        controls->addWidget(m_nextRootCombo);

        controls->addWidget(new QLabel("Next chord:", this));
        m_nextChordCombo = new QComboBox(this);
        m_nextChordCombo->setMinimumWidth(150);
        controls->addWidget(m_nextChordCombo);
    }

    if (m_instrument != Instrument::Drums) {
        controls->addSpacing(10);
        controls->addWidget(new QLabel("Scope:", this));
        m_scopeCombo = new QComboBox(this);
        m_scopeCombo->addItem("Beat", int(Scope::Beat));
        m_scopeCombo->addItem("Phrase (4 bars)", int(Scope::Phrase));
        m_scopeCombo->setCurrentIndex(0);
        controls->addWidget(m_scopeCombo);
    }

    controls->addStretch(1);

    // Live-follow is automatic when the song is playing; no checkbox.

    m_auditionBtn = new QPushButton("Audition", this);
    controls->addWidget(m_auditionBtn);

    main->addLayout(controls);

    // --- Main split: list | details/live/timeline ---
    QHBoxLayout* split = new QHBoxLayout;
    split->setSpacing(10);

    m_list = new QListWidget(this);
    m_list->setMinimumWidth(320);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    split->addWidget(m_list, 0);

    QVBoxLayout* right = new QVBoxLayout;
    right->setSpacing(10);

    // Details table
    m_detailTable = new QTableWidget(this);
    m_detailTable->setColumnCount(6);
    m_detailTable->setHorizontalHeaderLabels({"Field", "Beats", "Energy", "Weight", "Hit/Action", "Notes"});
    m_detailTable->horizontalHeader()->setStretchLastSection(true);
    m_detailTable->verticalHeader()->setVisible(false);
    m_detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_detailTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_detailTable->setMinimumHeight(140);
    right->addWidget(m_detailTable, 0);

    // Timeline (single widget): shows preview in Audition mode, and planned lookahead in Live mode.
    m_timeline = new virtuoso::ui::GrooveTimelineWidget(this);
    m_timeline->setMinimumHeight(220);
    right->addWidget(m_timeline, 1);

    // Live panel
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

    // Live decay timer: if planned events stop, we return to Audition mode.
    m_liveDecayTimer = new QTimer(this);
    m_liveDecayTimer->setSingleShot(true);
    connect(m_liveDecayTimer, &QTimer::timeout, this, [this]() {
        m_liveMode = false;
        if (m_auditionBtn) m_auditionBtn->setEnabled(true);
        refreshDetailsAndPreview();
    });

    // Connections
    connect(m_list, &QListWidget::currentRowChanged, this, &VirtuosoVocabularyWindow::onSelectionChanged);
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VirtuosoVocabularyWindow::onPresetChanged);
    connect(m_bpm, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { refreshDetailsAndPreview(); });
    connect(m_energy, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { refreshDetailsAndPreview(); });
    if (m_intensityPeak) connect(m_intensityPeak, &QCheckBox::toggled, this, [this](bool) { refreshDetailsAndPreview(); });
    if (m_rootCombo) connect(m_rootCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { refreshDetailsAndPreview(); });
    if (m_chordCombo) connect(m_chordCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { refreshDetailsAndPreview(); });
    if (m_voicingCombo) connect(m_voicingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { refreshDetailsAndPreview(); });
    if (m_nextRootCombo) connect(m_nextRootCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { refreshDetailsAndPreview(); });
    if (m_nextChordCombo) connect(m_nextChordCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { refreshDetailsAndPreview(); });
    if (m_scopeCombo) connect(m_scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshPatternList();
        refreshDetailsAndPreview();
    });
    connect(m_auditionBtn, &QPushButton::clicked, this, &VirtuosoVocabularyWindow::onAuditionStartStop);
    connect(m_auditionTimer, &QTimer::timeout, this, &VirtuosoVocabularyWindow::onAuditionTick);
}

VirtuosoVocabularyWindow::Scope VirtuosoVocabularyWindow::currentScope() const {
    if (!m_scopeCombo) return Scope::Beat;
    return Scope(m_scopeCombo->currentData().toInt());
}

void VirtuosoVocabularyWindow::loadVocab() {
    QString err;
    m_vocab.loadFromResourcePath(":/virtuoso/vocab/cool_jazz_vocabulary.json", &err);

    m_pianoById.clear();
    m_bassById.clear();
    m_drumsById.clear();
    for (const auto& p : m_vocab.pianoPatterns()) m_pianoById.insert(p.id, p);
    for (const auto& p : m_vocab.bassPatterns()) m_bassById.insert(p.id, p);
    for (const auto& p : m_vocab.drumsPatterns()) m_drumsById.insert(p.id, p);

    m_pianoPhraseById.clear();
    m_bassPhraseById.clear();
    m_drumsPhraseById.clear();
    for (const auto& p : m_vocab.pianoPhrasePatterns()) m_pianoPhraseById.insert(p.id, p);
    for (const auto& p : m_vocab.bassPhrasePatterns()) m_bassPhraseById.insert(p.id, p);
    for (const auto& p : m_vocab.drumsPhrasePatterns()) m_drumsPhraseById.insert(p.id, p);
}

void VirtuosoVocabularyWindow::rebuildPresetCombo() {
    const bool prev = m_presetCombo->blockSignals(true);
    m_presetCombo->clear();
    for (const auto* p : m_grooveRegistry.allStylePresets()) {
        m_presetCombo->addItem(p->name, p->key);
    }
    // Default to the MVP ballad preset if present.
    const QString want = "jazz_brushes_ballad_60_evans";
    int idx = -1;
    for (int i = 0; i < m_presetCombo->count(); ++i) {
        if (m_presetCombo->itemData(i).toString() == want) { idx = i; break; }
    }
    if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
    m_presetCombo->blockSignals(prev);
}

const virtuoso::groove::GrooveRegistry::StylePreset* VirtuosoVocabularyWindow::currentPreset() const {
    if (!m_presetCombo) return nullptr;
    const QString key = m_presetCombo->currentData().toString();
    return m_grooveRegistry.stylePreset(key);
}

static QVector<const virtuoso::ontology::ChordDef*> orderedChordsForUi(const virtuoso::ontology::OntologyRegistry& o) {
    auto all = o.allChords();
    std::sort(all.begin(), all.end(), [](auto* a, auto* b) {
        if (a->order != b->order) return a->order < b->order;
        return a->name < b->name;
    });
    return all;
}

static QVector<const virtuoso::ontology::VoicingDef*> orderedVoicingsForUi(const virtuoso::ontology::OntologyRegistry& o,
                                                                          virtuoso::ontology::InstrumentKind k) {
    auto all = o.voicingsFor(k);
    std::sort(all.begin(), all.end(), [](auto* a, auto* b) {
        if (a->order != b->order) return a->order < b->order;
        if (a->category != b->category) return a->category < b->category;
        return a->name < b->name;
    });
    return all;
}

void VirtuosoVocabularyWindow::refreshPatternList() {
    m_list->clear();
    QVector<QString> ids;
    if (m_instrument == Instrument::Drums) {
        // Drums single source of truth is BrushesBalladDrummer -> show tags that drummer actually uses.
        ids = {"Drums:BrushStirLoop","Drums:SnareSwish","Drums:BrushSwishShort","Drums:FeatherKick",
               "Drums:RideBackbeat","Drums:RidePulse","Drums:RidePulseUpbeat",
               "Drums:IntensitySupportRide","Drums:PhraseEndSwish"};
    } else if (currentScope() == Scope::Phrase) {
        if (m_instrument == Instrument::Piano) ids = m_pianoPhraseById.keys().toVector();
        if (m_instrument == Instrument::Bass) ids = m_bassPhraseById.keys().toVector();
    } else {
        if (m_instrument == Instrument::Piano) ids = m_pianoById.keys().toVector();
        if (m_instrument == Instrument::Bass) ids = m_bassById.keys().toVector();
    }
    std::sort(ids.begin(), ids.end());
    for (const auto& id : ids) m_list->addItem(id);
    if (m_list->count() > 0) m_list->setCurrentRow(0);
}

void VirtuosoVocabularyWindow::onSelectionChanged() {
    refreshDetailsAndPreview();
}

void VirtuosoVocabularyWindow::onPresetChanged() {
    refreshDetailsAndPreview();
}

int VirtuosoVocabularyWindow::nearestMidiForPc(int pc, int around, int lo, int hi) {
    around = clampMidi(around);
    if (pc < 0) pc = 0;
    int best = -1;
    int bestDist = 999999;
    for (int m = lo; m <= hi; ++m) {
        if (((m % 12) + 12) % 12 != ((pc % 12) + 12) % 12) continue;
        const int d = qAbs(m - around);
        if (d < bestDist) { bestDist = d; best = m; }
    }
    if (best >= 0) return best;
    int m = lo + ((pc - (lo % 12) + 1200) % 12);
    while (m < lo) m += 12;
    while (m > hi) m -= 12;
    return clampMidi(m);
}

int VirtuosoVocabularyWindow::bassMidiForDegree(int rootPc, int degreePc, int lo, int hi) {
    int pc = ((degreePc % 12) + 12) % 12;
    int midi = lo + ((pc - (lo % 12) + 1200) % 12);
    while (midi < lo) midi += 12;
    while (midi > hi) midi -= 12;
    return clampMidi(midi);
}

int VirtuosoVocabularyWindow::degreeToSemitone(const virtuoso::ontology::ChordDef* chordCtx, int degree) {
    // Mirrors LibraryWindow mapping; uses chordCtx intervals when possible.
    if (degree == 1) return 0;
    if (!chordCtx) return 0;

    auto thirdFromChord = [&]() -> int {
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
        default: return 0;
    }
}

QVector<int> VirtuosoVocabularyWindow::midiNotesForVoicing(const virtuoso::ontology::VoicingDef* v,
                                                          const virtuoso::ontology::ChordDef* chordCtx,
                                                          int rootPc,
                                                          int rootMidi) {
    QVector<int> out;
    if (!v) return out;
    const int base = clampMidi(rootMidi);
    if (!v->intervals.isEmpty()) {
        for (int iv : v->intervals) out.push_back(clampMidi(base + iv));
        return out;
    }
    for (int deg : v->chordDegrees) {
        const int semis = degreeToSemitone(chordCtx, deg);
        out.push_back(clampMidi(base + semis));
    }
    // Light de-dup
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void VirtuosoVocabularyWindow::refreshDetailsAndPreview() {
    if (!m_detailTable || !m_list) return;
    m_detailTable->setRowCount(0);
    m_previewEvents.clear();

    const auto* preset = currentPreset();
    const int bpm = m_bpm ? m_bpm->value() : 60;
    const double energy = m_energy ? m_energy->value() : 0.45;
    const virtuoso::groove::TimeSignature ts{4, 4};

    const auto addRow = [&](const QString& field, const QString& beats, const QString& enr,
                            const QString& w, const QString& hit, const QString& notes) {
        const int r = m_detailTable->rowCount();
        m_detailTable->insertRow(r);
        m_detailTable->setItem(r, 0, new QTableWidgetItem(field));
        m_detailTable->setItem(r, 1, new QTableWidgetItem(beats));
        m_detailTable->setItem(r, 2, new QTableWidgetItem(enr));
        m_detailTable->setItem(r, 3, new QTableWidgetItem(w));
        m_detailTable->setItem(r, 4, new QTableWidgetItem(hit));
        m_detailTable->setItem(r, 5, new QTableWidgetItem(notes));
    };

    const auto* item = m_list->currentItem();
    const QString id = item ? item->text() : QString();
    if (id.isEmpty()) return;

    // Render a preview: beat patterns repeat; phrase patterns span their phraseBars.
    int bars = m_previewBars;
    if (m_instrument != Instrument::Drums && currentScope() == Scope::Phrase) {
        if (m_instrument == Instrument::Piano && m_pianoPhraseById.contains(id)) bars = qMax(1, m_pianoPhraseById.value(id).phraseBars);
        if (m_instrument == Instrument::Bass && m_bassPhraseById.contains(id)) bars = qMax(1, m_bassPhraseById.value(id).phraseBars);
    }
    const qint64 totalMsPreview = qint64(llround(double(bars) * (60000.0 / double(qMax(1, bpm))) * 4.0));

    auto makeHumanizer = [&](const QString& agent) -> virtuoso::groove::TimingHumanizer {
        virtuoso::groove::InstrumentGrooveProfile p;
        p.instrument = agent;
        if (preset && preset->instrumentProfiles.contains(agent)) p = preset->instrumentProfiles.value(agent);
        virtuoso::groove::TimingHumanizer h(p);
        if (preset) {
            if (const auto* gt = m_grooveRegistry.grooveTemplate(preset->grooveTemplateKey)) h.setGrooveTemplate(*gt);
        }
        return h;
    };

    const QString lane = instrumentName(m_instrument);
    auto h = makeHumanizer(lane);

    // Populate ontology dropdowns on-demand (safe to call repeatedly)
    {
        const auto chords = orderedChordsForUi(m_ontology);
        if (m_chordCombo && m_chordCombo->count() == 0) {
            const bool prev = m_chordCombo->blockSignals(true);
            for (const auto* cdef : chords) m_chordCombo->addItem(cdef->name, cdef->key);
            // Default to maj7 if present
            int idx = m_chordCombo->findData("maj7");
            if (idx >= 0) m_chordCombo->setCurrentIndex(idx);
            m_chordCombo->blockSignals(prev);
        }
        if (m_nextChordCombo && m_nextChordCombo->count() == 0) {
            const bool prev = m_nextChordCombo->blockSignals(true);
            for (const auto* cdef : chords) m_nextChordCombo->addItem(cdef->name, cdef->key);
            int idx = m_nextChordCombo->findData("min7");
            if (idx >= 0) m_nextChordCombo->setCurrentIndex(idx);
            m_nextChordCombo->blockSignals(prev);
        }
        if (m_voicingCombo && m_voicingCombo->count() == 0) {
            const bool prev = m_voicingCombo->blockSignals(true);
            const auto voicings = orderedVoicingsForUi(m_ontology, virtuoso::ontology::InstrumentKind::Piano);
            for (const auto* vdef : voicings) {
                const QString label = vdef->category.trimmed().isEmpty()
                    ? vdef->name
                    : QString("%1 — %2").arg(vdef->category, vdef->name);
                m_voicingCombo->addItem(label, vdef->key);
            }
            // Prefer a shell-like voicing if present
            int idx = m_voicingCombo->findData("piano_shell_3_7");
            if (idx < 0) idx = m_voicingCombo->findData("piano_shell_1_7");
            if (idx >= 0) m_voicingCombo->setCurrentIndex(idx);
            m_voicingCombo->blockSignals(prev);
        }
    }

    const int rootPc = m_rootCombo ? m_rootCombo->currentIndex() : 0;
    const QString chordKey = m_chordCombo ? m_chordCombo->currentData().toString() : QString("maj7");
    const auto* chordDef = m_ontology.chord(chordKey);

    const int nextRootPc = m_nextRootCombo ? m_nextRootCombo->currentIndex() : rootPc;
    const QString nextChordKey = m_nextChordCombo ? m_nextChordCombo->currentData().toString() : chordKey;
    const auto* nextChordDef = m_ontology.chord(nextChordKey);

    const auto addEvent = [&](int bar, int beat, int sub, int count,
                              int midi, int baseVel, const virtuoso::groove::Rational& dur,
                              const QString& label, bool structural) {
        const auto gp = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(bar, beat, sub, count, ts);
        const auto he = h.humanizeNote(gp, ts, bpm, baseVel, dur, structural);
        qint64 on = qBound<qint64>(0, he.onMs, totalMsPreview);
        qint64 off = qBound<qint64>(0, he.offMs, totalMsPreview + 8000);
        if (off <= on) off = on + 60;
        virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
        ev.lane = lane;
        ev.note = midi;
        ev.velocity = he.velocity;
        ev.onMs = on;
        ev.offMs = off;
        ev.label = label;
        m_previewEvents.push_back(ev);
    };

    // NOTE: we intentionally do not draw a separate "Rhythm" lane; rhythm is visible in the notes themselves.

    if (m_instrument == Instrument::Drums) {
        // Drums preview is sourced from the actual procedural drummer.
        addRow("Drummer", "procedural", "-", "-", id, "BrushesBalladDrummer");

        playback::BrushesBalladDrummer drummer;
        // Keep determinism stable for audition previews.
        const quint32 seed = 0xD00DCAFEu;
        const bool peak = m_intensityPeak ? m_intensityPeak->isChecked() : false;
        for (int bar = 0; bar < bars; ++bar) {
            for (int beat = 0; beat < 4; ++beat) {
                playback::BrushesBalladDrummer::Context ctx;
                ctx.bpm = bpm;
                ctx.ts = ts;
                ctx.playbackBarIndex = bar;
                ctx.beatInBar = beat;
                ctx.structural = (beat == 0);
                ctx.determinismSeed = seed;
                ctx.energy = energy;
                ctx.intensityPeak = peak;
                const auto notes = drummer.planBeat(ctx);
                for (const auto& n : notes) {
                    if (!id.isEmpty() && n.logic_tag != id) continue;
                    const auto he = h.humanizeNote(n.startPos, ts, bpm, n.baseVelocity, n.durationWhole, n.structural);
                    qint64 on = qBound<qint64>(0, he.onMs, totalMsPreview);
                    qint64 off = qBound<qint64>(0, he.offMs, totalMsPreview + 8000);
                    if (off <= on) off = on + 60;
                    virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
                    ev.lane = lane;
                    ev.note = n.note;
                    ev.velocity = he.velocity;
                    ev.onMs = on;
                    ev.offMs = off;
                    ev.label = n.logic_tag;
                    m_previewEvents.push_back(ev);
                }
            }
        }
    } else if (m_instrument == Instrument::Piano) {
        const bool phrase = (currentScope() == Scope::Phrase);
        QVector<virtuoso::vocab::VocabularyRegistry::PianoPhraseHit> phraseHits;
        QVector<virtuoso::vocab::VocabularyRegistry::PianoHit> beatHits;
        QString notesText;
        if (phrase && m_pianoPhraseById.contains(id)) {
            const auto p = m_pianoPhraseById.value(id);
            phraseHits = p.hits;
            notesText = p.notes;
            addRow("Piano PHRASE", QString("bars=%1").arg(p.phraseBars),
                   "-", "-", QString("hits=%1").arg(p.hits.size()), p.notes);
        } else {
            const auto p = m_pianoById.value(id);
            beatHits = p.hits;
            notesText = p.notes;
            addRow("Piano pattern", beatsString(p.beats),
                   QString("%1..%2").arg(p.minEnergy, 0, 'f', 2).arg(p.maxEnergy, 0, 'f', 2),
                   QString::number(p.weight, 'f', 2),
                   QString("hits=%1").arg(p.hits.size()),
                   p.notes);
        }

        const QString vkey = m_voicingCombo ? m_voicingCombo->currentData().toString() : QString();
        const auto* vdef = m_ontology.voicing(vkey);
        const int rootMidi = 60; // middle C as an audition anchor; voicing shape matters more than register here
        // Use ontology voicing to pick actual chord tones; "guide" means choose a shell voicing if available.
        QVector<int> fullNotes = midiNotesForVoicing(vdef, chordDef, rootPc, rootMidi);
        if (fullNotes.isEmpty()) {
            // fallback: basic chord intervals
            if (chordDef) for (int iv : chordDef->intervals) fullNotes.push_back(clampMidi(rootMidi + iv));
        }

        auto renderHit = [&](int bar, int beat, const virtuoso::vocab::VocabularyRegistry::PianoHit& hit) {
            QVector<int> chordNotes = fullNotes;
            if (hit.density.trimmed().toLower() == "guide") {
                const auto* shell37 = m_ontology.voicing("piano_shell_3_7");
                const auto* shell17 = m_ontology.voicing("piano_shell_1_7");
                const auto* shell = shell37 ? shell37 : shell17;
                chordNotes = midiNotesForVoicing(shell, chordDef, rootPc, rootMidi);
                if (chordNotes.isEmpty()) chordNotes = fullNotes;
            }
            for (int m : chordNotes) {
                addEvent(bar, beat, hit.sub, hit.count, clampMidi(m),
                         qBound(1, 52 + hit.vel_delta + int(llround(10.0 * energy)), 127),
                         virtuoso::groove::Rational(qMax(1, hit.dur_num), qMax(1, hit.dur_den)),
                         QString("%1").arg(id),
                         /*structural*/(beat == 0));
            }
        };
        if (phrase) {
            for (const auto& ph : phraseHits) {
                if (ph.barOffset < 0 || ph.barOffset >= bars) continue;
                renderHit(ph.barOffset, ph.beatInBar, ph.hit);
            }
        } else {
            const auto p = m_pianoById.value(id);
            for (int bar = 0; bar < bars; ++bar) {
                for (int beat = 0; beat < 4; ++beat) {
                    if (!p.beats.contains(beat)) continue;
                    for (const auto& hit : p.hits) renderHit(bar, beat, hit);
                }
            }
        }
    } else if (m_instrument == Instrument::Bass) {
        const bool phrase = (currentScope() == Scope::Phrase);
        if (phrase && m_bassPhraseById.contains(id)) {
            const auto p = m_bassPhraseById.value(id);
            addRow("Bass PHRASE", QString("bars=%1").arg(p.phraseBars),
                   "-", "-", QString("hits=%1").arg(p.hits.size()), p.notes);
            for (const auto& ph : p.hits) {
                if (ph.barOffset < 0 || ph.barOffset >= bars) continue;
                if (ph.action == virtuoso::vocab::VocabularyRegistry::BassAction::Rest) continue;
                int midi = -1;
                if (ph.action == virtuoso::vocab::VocabularyRegistry::BassAction::Root) midi = bassMidiForDegree(rootPc, rootPc, 40, 57);
                else if (ph.action == virtuoso::vocab::VocabularyRegistry::BassAction::Fifth) midi = bassMidiForDegree(rootPc, (rootPc + 7) % 12, 40, 57);
                else if (ph.action == virtuoso::vocab::VocabularyRegistry::BassAction::Third) {
                    const int thirdIv = degreeToSemitone(chordDef, 3);
                    midi = bassMidiForDegree(rootPc, (rootPc + thirdIv) % 12, 40, 57);
                } else if (ph.action == virtuoso::vocab::VocabularyRegistry::BassAction::PickupToNext ||
                           ph.action == virtuoso::vocab::VocabularyRegistry::BassAction::ApproachToNext) {
                    const int nextRootMidi = bassMidiForDegree(nextRootPc, nextRootPc, 40, 57);
                    midi = nextRootMidi - 1;
                    while (midi < 40) midi += 12;
                    while (midi > 57) midi -= 12;
                }
                if (midi < 0) continue;
                addEvent(ph.barOffset, ph.beatInBar, ph.sub, ph.count, clampMidi(midi),
                         qBound(1, 52 + ph.vel_delta + int(llround(8.0 * energy)), 127),
                         virtuoso::groove::Rational(qMax(1, ph.dur_num), qMax(1, ph.dur_den)),
                         QString("%1").arg(id),
                         /*structural*/(ph.beatInBar == 0));
            }
        } else {
            const auto p = m_bassById.value(id);
            addRow("Bass pattern", beatsString(p.beats),
                   QString("%1..%2").arg(p.minEnergy, 0, 'f', 2).arg(p.maxEnergy, 0, 'f', 2),
                   QString::number(p.weight, 'f', 2),
                   QString("action=%1").arg(int(p.action)),
                   p.notes);

            for (int bar = 0; bar < bars; ++bar) {
                for (int beat = 0; beat < 4; ++beat) {
                    if (!p.beats.contains(beat)) continue;
                    int midi = -1;
                    if (p.action == virtuoso::vocab::VocabularyRegistry::BassAction::Rest) continue;
                    if (p.action == virtuoso::vocab::VocabularyRegistry::BassAction::Root) {
                        midi = bassMidiForDegree(rootPc, rootPc, 40, 57);
                    } else if (p.action == virtuoso::vocab::VocabularyRegistry::BassAction::Fifth) {
                        midi = bassMidiForDegree(rootPc, (rootPc + 7) % 12, 40, 57);
                    } else if (p.action == virtuoso::vocab::VocabularyRegistry::BassAction::Third) {
                        const int thirdIv = degreeToSemitone(chordDef, 3);
                        midi = bassMidiForDegree(rootPc, (rootPc + thirdIv) % 12, 40, 57);
                    } else if (p.action == virtuoso::vocab::VocabularyRegistry::BassAction::ApproachToNext ||
                               p.action == virtuoso::vocab::VocabularyRegistry::BassAction::PickupToNext) {
                        const int nextRootMidi = bassMidiForDegree(nextRootPc, nextRootPc, 40, 57);
                        midi = nextRootMidi - 1;
                        while (midi < 40) midi += 12;
                        while (midi > 57) midi -= 12;
                    }
                    if (midi < 0) continue;
                    addEvent(bar, beat, p.sub, p.count, clampMidi(midi),
                             qBound(1, 52 + p.vel_delta + int(llround(8.0 * energy)), 127),
                             virtuoso::groove::Rational(qMax(1, p.dur_num), qMax(1, p.dur_den)),
                             QString("%1").arg(id),
                             /*structural*/(beat == 0));
                }
            }
        }
    }

    // Update timeline widget
    if (m_timeline) {
        m_timeline->setTempoAndSignature(bpm, 4, 4);
        m_timeline->setPreviewBars(bars);
        // Make rhythm patterns visually obvious: default to 16th-grid view.
        m_timeline->setSubdivision(4);
        m_timeline->setLanes(QStringList() << lane);
        m_timeline->setEvents(m_previewEvents);
        m_timeline->setPlayheadMs(-1);
    }
}

void VirtuosoVocabularyWindow::onAuditionStartStop() {
    if (!m_auditionBtn || !m_auditionTimer || !m_midi) return;
    if (m_liveMode) return; // disabled during live playback
    if (m_auditionTimer->isActive()) {
        stopAuditionNow();
        return;
    }
    if (m_previewEvents.isEmpty()) refreshDetailsAndPreview();
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
    const int bpm = m_bpm ? m_bpm->value() : 60;
    qint64 totalMs = qint64(llround(double(qMax(1, m_previewBars)) * (60000.0 / double(qMax(1, bpm))) * 4.0));
    if (totalMs <= 0) totalMs = 1;
    const qint64 play = rel % totalMs;
    m_timeline->setPlayheadMs(play);

    if (m_auditionLastPlayMs < 0) m_auditionLastPlayMs = play;
    const bool wrapped = play < m_auditionLastPlayMs;

    const int ch = defaultMidiChannel(m_instrument);
    for (const auto& ev : m_previewEvents) {
        // Never play rhythm markers (note=0 lane) — they’re purely visual.
        if (ev.lane != instrumentName(m_instrument)) continue;
        const bool hit = wrapped ? (ev.onMs >= m_auditionLastPlayMs || ev.onMs <= play) : (ev.onMs >= m_auditionLastPlayMs && ev.onMs <= play);
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
    const QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
    if (d.isArray()) {
        // Lookahead plan: replace buffer with the entire next-4-bars plan.
        m_liveBuf.clear();
        const auto arr = d.array();
        for (const auto& v : arr) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            const QString agent = o.value("agent").toString();
            if (agent.trimmed() != instrumentName(m_instrument)) continue;
            const QString grid = o.value("grid_pos").toString(o.value("timestamp").toString());
            const QString logic = o.value("logic_tag").toString();
            const int note = o.value("note").toInt(-1);
            const int vel = o.value("dynamic_marking").toString().toInt();
            const qint64 onMs = qint64(o.value("on_ms").toDouble(0.0));
            const qint64 offMs = qint64(o.value("off_ms").toDouble(0.0));
            const int bpm = o.value("tempo_bpm").toInt(0);
            const int tsNum = o.value("ts_num").toInt(0);
            const int tsDen = o.value("ts_den").toInt(0);
            const qint64 engineNowMs = qint64(o.value("engine_now_ms").toDouble(0.0));
            if (bpm > 0) m_liveBpm = bpm;
            if (tsNum > 0) m_liveTsNum = tsNum;
            if (tsDen > 0) m_liveTsDen = tsDen;
            if (onMs > 0 && offMs > onMs) {
                LiveEv ev;
                ev.onMs = onMs;
                ev.offMs = offMs;
                ev.note = note;
                ev.velocity = vel;
                ev.logic = logic;
                ev.grid = grid;
                ev.engineNowMs = engineNowMs;
                m_liveBuf.push_back(ev);
            }
        }
        // Enter live mode whenever we receive a lookahead plan.
        m_liveMode = true;
        if (m_auditionTimer && m_auditionTimer->isActive()) stopAuditionNow();
        if (m_auditionBtn) m_auditionBtn->setEnabled(false);
        if (m_liveDecayTimer) m_liveDecayTimer->start(1600);
        rebuildLiveTimeline();
        return;
    }
    if (!d.isObject()) return;
    const QJsonObject o = d.object();
    const QString agent = o.value("agent").toString();
    if (agent.trimmed() != instrumentName(m_instrument)) return;

    const QString grid = o.value("grid_pos").toString(o.value("timestamp").toString());
    const QString chord = o.value("chord_context").toString();
    const QString voicing = o.value("voicing_type").toString();
    const QString logic = o.value("logic_tag").toString();
    const QString target = o.value("target_note").toString();
    const int note = o.value("note").toInt(-1);
    const int vel = o.value("dynamic_marking").toString().toInt();
    const qint64 onMs = qint64(o.value("on_ms").toDouble(0.0));
    const qint64 offMs = qint64(o.value("off_ms").toDouble(0.0));
    const int bpm = o.value("tempo_bpm").toInt(0);
    const int tsNum = o.value("ts_num").toInt(0);
    const int tsDen = o.value("ts_den").toInt(0);
    const qint64 engineNowMs = qint64(o.value("engine_now_ms").toDouble(0.0));

    if (m_liveHeader) {
        m_liveHeader->setText(QString("%1  chord=%2  voicing=%3  logic=%4")
                                  .arg(grid, chord, voicing, logic));
    }
    if (m_liveLog) {
        const QString line = QString("%1  %2  %3").arg(grid, logic, target);
        m_liveLog->append(line);
        // Trim occasionally
        if (m_liveLog->document()->blockCount() > 200) {
            QTextCursor c(m_liveLog->document());
            c.movePosition(QTextCursor::Start);
            c.select(QTextCursor::BlockUnderCursor);
            c.removeSelectedText();
            c.deleteChar();
        }
    }

    // Always follow during live playback; highlight whichever vocab entry is active.
    {
        const QString prefix = QString("Vocab:%1:").arg(instrumentName(m_instrument));
        if (logic.startsWith(prefix)) {
            const QString id = logic.mid(prefix.size());
            highlightPatternId(id);
        }
        const QString pfx2 = QString("VocabPhrase:%1:").arg(instrumentName(m_instrument));
        if (logic.startsWith(pfx2)) {
            const QString id = logic.mid(pfx2.size());
            highlightPatternId(id);
        }
    }

    // Accumulate for live timeline
    if (onMs > 0 && offMs > onMs) {
        if (bpm > 0) m_liveBpm = bpm;
        if (tsNum > 0) m_liveTsNum = tsNum;
        if (tsDen > 0) m_liveTsDen = tsDen;
        LiveEv ev;
        ev.onMs = onMs;
        ev.offMs = offMs;
        ev.note = note;
        ev.velocity = vel;
        ev.logic = logic;
        ev.grid = grid;
        ev.engineNowMs = engineNowMs;
        m_liveBuf.push_back(ev);
        // prune
        if (m_liveBuf.size() > 600) m_liveBuf.remove(0, m_liveBuf.size() - 600);

        // Switch into Live mode automatically while the engine is producing planned events.
        m_liveMode = true;
        if (m_auditionTimer && m_auditionTimer->isActive()) stopAuditionNow();
        if (m_auditionBtn) m_auditionBtn->setEnabled(false);
        if (m_liveDecayTimer) m_liveDecayTimer->start(1600);

        rebuildLiveTimeline();
    }
}

void VirtuosoVocabularyWindow::rebuildLiveTimeline() {
    if (!m_timeline) return;
    if (m_liveBuf.isEmpty()) return;

    const double quarterMs = 60000.0 / double(qMax(1, m_liveBpm));
    const double beatMs = quarterMs * (4.0 / double(qMax(1, m_liveTsDen)));
    const qint64 barMs = qMax<qint64>(1, qint64(llround(beatMs * double(qMax(1, m_liveTsNum)))));
    const int bars = 4;

    // Lookahead window is anchored to engine_now_ms (not "last note time"), so you see the upcoming 4 bars.
    qint64 nowMs = m_liveBuf.last().engineNowMs;
    if (nowMs <= 0) nowMs = m_liveBuf.last().onMs;
    const qint64 base = qMax<qint64>(0, (nowMs / barMs) * barMs);
    m_liveBaseMs = base;
    const qint64 end = base + barMs * qint64(bars);

    QVector<virtuoso::ui::GrooveTimelineWidget::LaneEvent> evs;
    evs.reserve(128);

    const QString lane = instrumentName(m_instrument);

    for (const auto& e : m_liveBuf) {
        // Show events that start inside our 4-bar lookahead.
        if (e.onMs < base || e.onMs > end) continue;
        const qint64 onRel = e.onMs - base;
        const qint64 offRel = e.offMs - base;

        // Instrument note event
        virtuoso::ui::GrooveTimelineWidget::LaneEvent n;
        n.lane = lane;
        n.note = e.note;
        n.velocity = e.velocity;
        n.onMs = onRel;
        n.offMs = offRel;
        // Label as "noteName  patternId" so you can see what choice generated it.
        n.label = (e.note >= 0) ? (midiName(e.note) + " " + e.logic) : e.logic;
        evs.push_back(n);
    }

    m_timeline->setTempoAndSignature(m_liveBpm, m_liveTsNum, m_liveTsDen);
    m_timeline->setPreviewBars(bars);
    m_timeline->setSubdivision(4);
    m_timeline->setLanes(QStringList() << lane);
    m_timeline->setEvents(evs);
    // Playhead shows "now" inside the 4-bar window.
    const qint64 play = qBound<qint64>(0, nowMs - base, end - base);
    m_timeline->setPlayheadMs(play);
}

