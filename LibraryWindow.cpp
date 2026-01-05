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
#include <functional>

#include "virtuoso/ui/GuitarFretboardWidget.h"
#include "virtuoso/ui/PianoKeyboardWidget.h"

#include "midiprocessor.h"
#include "virtuoso/theory/ScaleSuggester.h"
#include "virtuoso/theory/PatternLibrary.h"
#include "virtuoso/theory/GrooveEngine.h"
#include "virtuoso/theory/FunctionalHarmony.h"

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
      m_midi(midi) {
    m_grooveTemplates = virtuoso::theory::GrooveEngine::builtins();
    setWindowTitle("Library");
    resize(1100, 520);
    buildUi();
    populateLists();
    updateHighlights();
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

    // Patterns tab (arpeggios / scalar patterns)
    m_patternsTab = new QWidget(this);
    QVBoxLayout* patV = new QVBoxLayout(m_patternsTab);
    QHBoxLayout* patControls = new QHBoxLayout;
    patControls->addWidget(new QLabel("Target:", this));
    m_patternTargetCombo = new QComboBox(this);
    m_patternTargetCombo->addItems({"Chord", "Scale"});
    patControls->addWidget(m_patternTargetCombo);
    patControls->addStretch(1);
    patV->addLayout(patControls);

    QHBoxLayout* patLists = new QHBoxLayout;
    m_patternsList = new QListWidget(this);
    m_patternsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_patternTargetList = new QListWidget(this);
    m_patternTargetList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_patternsList->setMinimumWidth(170);
    patLists->addWidget(m_patternsList, 1);
    patLists->addWidget(m_patternTargetList, 1);
    patV->addLayout(patLists, 1);
    m_tabs->addTab(m_patternsTab, "Patterns");

    // Groove tab (micro-timing templates)
    m_grooveTab = new QWidget(this);
    QVBoxLayout* grooveV = new QVBoxLayout(m_grooveTab);
    QHBoxLayout* grooveControls = new QHBoxLayout;
    grooveControls->addWidget(new QLabel("Subdivision:", this));
    m_grooveSubdivisionCombo = new QComboBox(this);
    m_grooveSubdivisionCombo->addItem("8ths (2/beat)", 2);
    m_grooveSubdivisionCombo->addItem("16ths (4/beat)", 4);
    m_grooveSubdivisionCombo->setCurrentIndex(0);
    grooveControls->addWidget(m_grooveSubdivisionCombo);
    grooveControls->addStretch(1);
    grooveV->addLayout(grooveControls);

    m_grooveList = new QListWidget(this);
    m_grooveList->setSelectionMode(QAbstractItemView::SingleSelection);
    grooveV->addWidget(m_grooveList, 1);

    m_groovePreviewLabel = new QLabel(this);
    m_groovePreviewLabel->setWordWrap(true);
    grooveV->addWidget(m_groovePreviewLabel);
    m_tabs->addTab(m_grooveTab, "Groove");

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

    // Patterns signals
    connect(m_patternTargetCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_patternsList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_patternTargetList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);

    // Groove signals
    connect(m_grooveSubdivisionCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_grooveList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);

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
}

void LibraryWindow::populateLists() {
    // Build stable orderings (avoid QHash iteration order).
    m_orderedChords = m_registry.allChords();
    m_orderedScales = m_registry.allScales();
    m_orderedVoicings = m_registry.allVoicings();
    m_orderedPatterns = m_patternLib.all();

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

    // Patterns
    if (m_patternsList) {
        m_patternsList->clear();
        for (const auto* p : m_orderedPatterns) {
            QListWidgetItem* it = new QListWidgetItem(p->name, m_patternsList);
            it->setData(Qt::UserRole, p->key);
        }
        m_patternsList->setCurrentRow(0);
    }

    if (m_patternTargetList) {
        m_patternTargetList->clear();
        for (const ChordDef* c : m_orderedChords) {
            QListWidgetItem* it = new QListWidgetItem(c->name, m_patternTargetList);
            it->setData(Qt::UserRole, c->key);
        }
        m_patternTargetList->setCurrentRow(0);
    }

    // Groove presets
    if (m_grooveList) {
        m_grooveList->clear();
        for (const auto& g : m_grooveTemplates) {
            QListWidgetItem* it = new QListWidgetItem(g.name, m_grooveList);
            it->setData(Qt::UserRole, g.key);
        }
        if (m_grooveList->count() > 0) m_grooveList->setCurrentRow(0);
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

void LibraryWindow::onSelectionChanged() {
    updateHighlights();
    scheduleAutoPlay();
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
    const QString d = m_durationCombo ? m_durationCombo->currentText() : "Medium";
    // Medium is the previous default (650ms)
    if (d == "Short") return 250;
    if (d == "Long") return 1100;
    return 650;
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

QVector<int> LibraryWindow::midiNotesForCurrentSelection(int rootPc) const {
    QVector<int> notes;
    const int tab = m_tabs ? m_tabs->currentIndex() : 0;
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

    // Faster scale feel (still tied to Duration): step is a fraction of chord duration.
    const int stepMs = qMax(25, durationMs / 5);
    const int gateMs = qMax(18, int(double(stepMs) * 0.80));

    // Optional groove micro-timing
    virtuoso::theory::GrooveTemplate groove;
    groove.key = "straight";
    groove.name = "Straight";
    if (m_grooveList && m_grooveList->currentItem()) {
        const QString gk = m_grooveList->currentItem()->data(Qt::UserRole).toString();
        for (const auto& g : m_grooveTemplates) {
            if (g.key == gk) { groove = g; break; }
        }
    }
    const int stepsPerBeat = m_grooveSubdivisionCombo ? m_grooveSubdivisionCombo->currentData().toInt() : 2;
    const QVector<int> due = virtuoso::theory::GrooveEngine::scheduleDueMs(seq.size(),
                                                                          stepMs,
                                                                          stepsPerBeat,
                                                                          groove,
                                                                          quint32(session));

    // Use a chained timer approach (rather than N independent timers) to avoid ordering jitter.
    using StepFn = std::function<void(int idx, int prev)>;
    auto stepFn = std::make_shared<StepFn>();
    *stepFn = [this, session, ch, vel, gateMs, stepMs, seq, due, stepFn](int idx, int prev) {
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
        int gate = gateMs;
        if (idx + 1 < due.size()) {
            const int nextDelay = due[idx + 1] - due[idx];
            gate = qMin(gateMs, qMax(12, nextDelay - 2));
        }
        QTimer::singleShot(gate, this, [this, session, ch, n]() {
            if (session != m_playSession) return;
            if (!m_midi) return;
            noteOffTracked(ch, n);
            setActiveMidi(n, false);
        });

        const int nextDelay = (idx + 1 < due.size()) ? (due[idx + 1] - due[idx]) : stepMs;
        QTimer::singleShot(nextDelay, this, [stepFn, idx, n]() { (*stepFn)(idx + 1, n); });
    };

    const int firstDelay = (!due.isEmpty() ? due[0] : 0);
    QTimer::singleShot(firstDelay, this, [stepFn]() { (*stepFn)(/*idx=*/0, /*prev=*/-1); });
}

void LibraryWindow::playPatternSequence(const QVector<int>& midiSeq, int durationMs) {
    if (midiSeq.isEmpty()) return;
    if (!m_midi) return;

    const int ch = selectedPlaybackChannel();
    const int vel = 48;
    const quint64 session = ++m_playSession;
    clearActiveMidis();
    stopPlaybackNow(ch);

    const int stepMs = qMax(28, durationMs / 4);
    const int gateMs = qMax(18, int(double(stepMs) * 0.85));

    virtuoso::theory::GrooveTemplate groove;
    groove.key = "straight";
    groove.name = "Straight";
    if (m_grooveList && m_grooveList->currentItem()) {
        const QString gk = m_grooveList->currentItem()->data(Qt::UserRole).toString();
        for (const auto& g : m_grooveTemplates) {
            if (g.key == gk) { groove = g; break; }
        }
    }
    const int stepsPerBeat = m_grooveSubdivisionCombo ? m_grooveSubdivisionCombo->currentData().toInt() : 2;
    const QVector<int> due = virtuoso::theory::GrooveEngine::scheduleDueMs(midiSeq.size(),
                                                                          stepMs,
                                                                          stepsPerBeat,
                                                                          groove,
                                                                          quint32(session));

    using StepFn = std::function<void(int idx, int prev)>;
    auto stepFn = std::make_shared<StepFn>();
    *stepFn = [this, session, ch, vel, gateMs, stepMs, midiSeq, due, stepFn](int idx, int prev) {
        if (session != m_playSession) return;
        if (!m_midi) return;
        if (idx >= midiSeq.size()) {
            if (prev >= 0) {
                noteOffTracked(ch, prev);
                setActiveMidi(prev, false);
            }
            return;
        }

        const int n = midiSeq[idx];
        if (prev >= 0) {
            noteOffTracked(ch, prev);
            setActiveMidi(prev, false);
        }

        setActiveMidi(n, true);
        noteOnTracked(ch, n, vel);

        int gate = gateMs;
        if (idx + 1 < due.size()) {
            const int nextDelay = due[idx + 1] - due[idx];
            gate = qMin(gateMs, qMax(12, nextDelay - 2));
        }
        QTimer::singleShot(gate, this, [this, session, ch, n]() {
            if (session != m_playSession) return;
            if (!m_midi) return;
            noteOffTracked(ch, n);
            setActiveMidi(n, false);
        });

        const int nextDelay = (idx + 1 < due.size()) ? (due[idx + 1] - due[idx]) : stepMs;
        QTimer::singleShot(nextDelay, this, [stepFn, idx, n]() { (*stepFn)(idx + 1, n); });
    };

    const int firstDelay = (!due.isEmpty() ? due[0] : 0);
    QTimer::singleShot(firstDelay, this, [stepFn]() { (*stepFn)(/*idx=*/0, /*prev=*/-1); });
}

void LibraryWindow::playGroovePreview(int durationMs) {
    if (!m_midi) return;
    const int ch = selectedPlaybackChannel();
    const int vel = 48;
    const quint64 session = ++m_playSession;
    clearActiveMidis();
    stopPlaybackNow(ch);

    virtuoso::theory::GrooveTemplate groove;
    groove.key = "straight";
    groove.name = "Straight";
    if (m_grooveList && m_grooveList->currentItem()) {
        const QString gk = m_grooveList->currentItem()->data(Qt::UserRole).toString();
        for (const auto& g : m_grooveTemplates) {
            if (g.key == gk) { groove = g; break; }
        }
    }
    const int stepsPerBeat = m_grooveSubdivisionCombo ? m_grooveSubdivisionCombo->currentData().toInt() : 2;

    const int baseRoot = baseRootMidiForPosition(pcFromIndex(m_rootCombo ? m_rootCombo->currentIndex() : 0));
    const int stepMs = qMax(40, durationMs / 4);
    const int gateMs = qMax(18, stepMs / 2);
    const QVector<int> due = virtuoso::theory::GrooveEngine::scheduleDueMs(/*steps=*/12,
                                                                          stepMs,
                                                                          stepsPerBeat,
                                                                          groove,
                                                                          quint32(session));

    using StepFn = std::function<void(int idx)>;
    auto stepFn = std::make_shared<StepFn>();
    *stepFn = [this, session, ch, vel, gateMs, baseRoot, due, stepFn](int idx) {
        if (session != m_playSession) return;
        if (!m_midi) return;
        if (idx >= due.size()) return;

        const int n = baseRoot;
        setActiveMidi(n, true);
        noteOnTracked(ch, n, vel);
        QTimer::singleShot(gateMs, this, [this, session, ch, n]() {
            if (session != m_playSession) return;
            if (!m_midi) return;
            noteOffTracked(ch, n);
            setActiveMidi(n, false);
        });

        if (idx + 1 < due.size()) {
            const int nextDelay = due[idx + 1] - due[idx];
            QTimer::singleShot(nextDelay, this, [stepFn, idx]() { (*stepFn)(idx + 1); });
        }
    };

    const int firstDelay = (!due.isEmpty() ? due[0] : 0);
    QTimer::singleShot(firstDelay, this, [stepFn]() { (*stepFn)(0); });
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

    // Patterns tab
    if (m_patternsTab && tab == m_tabs->indexOf(m_patternsTab)) {
        const int rootPc = pcFromIndex(m_rootCombo ? m_rootCombo->currentIndex() : 0);
        const int baseRoot = baseRootMidiForPosition(rootPc);

        if (!m_patternsList || !m_patternsList->currentItem()) return;
        const auto* pat = m_patternLib.pattern(m_patternsList->currentItem()->data(Qt::UserRole).toString());
        if (!pat) return;

        const QString targetType = m_patternTargetCombo ? m_patternTargetCombo->currentText() : "Chord";
        const QString targetKey = (m_patternTargetList && m_patternTargetList->currentItem())
                                      ? m_patternTargetList->currentItem()->data(Qt::UserRole).toString()
                                      : QString();

        const ChordDef* chordDef = nullptr;
        const ScaleDef* scaleDef = nullptr;
        if (targetType == "Scale") scaleDef = m_registry.scale(targetKey);
        else chordDef = m_registry.chord(targetKey);

        const QVector<int> semis = virtuoso::theory::PatternLibrary::renderSemitoneSequence(*pat, chordDef, scaleDef);
        QVector<int> midiSeq;
        midiSeq.reserve(semis.size() + 1);
        for (int st : semis) midiSeq.push_back(normalizeMidi(baseRoot + st));
        if (midiSeq.isEmpty() || midiSeq.back() != normalizeMidi(baseRoot)) midiSeq.push_back(normalizeMidi(baseRoot));
        playPatternSequence(midiSeq, dur);
        return;
    }

    // Groove tab
    if (m_grooveTab && tab == m_tabs->indexOf(m_grooveTab)) {
        playGroovePreview(dur);
        return;
    }

    const int rootPc = pcFromIndex(m_rootCombo ? m_rootCombo->currentIndex() : 0);
    const QVector<int> notes = midiNotesForCurrentSelection(rootPc);
    const bool isScale = (tab == 1);
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

    if (m_patternsTab && tab == m_tabs->indexOf(m_patternsTab)) {
        const QString targetType = m_patternTargetCombo ? m_patternTargetCombo->currentText() : "Chord";

        if (m_patternTargetList) {
            const QString prevType = m_patternTargetList->property("targetType").toString();
            if (prevType != targetType) {
                m_patternTargetList->setProperty("targetType", targetType);
                const QSignalBlocker blocker(m_patternTargetList);
                m_patternTargetList->clear();
                if (targetType == "Scale") {
                    for (const ScaleDef* s : m_orderedScales) {
                        QListWidgetItem* it = new QListWidgetItem(s->name, m_patternTargetList);
                        it->setData(Qt::UserRole, s->key);
                    }
                } else {
                    for (const ChordDef* c : m_orderedChords) {
                        QListWidgetItem* it = new QListWidgetItem(c->name, m_patternTargetList);
                        it->setData(Qt::UserRole, c->key);
                    }
                }
                if (m_patternTargetList->count() > 0) m_patternTargetList->setCurrentRow(0);
            }
        }

        if (m_patternTargetList && m_patternTargetList->currentItem()) {
            const QString key = m_patternTargetList->currentItem()->data(Qt::UserRole).toString();
            if (targetType == "Scale") {
                const auto* scaleDef = m_registry.scale(key);
                pcs = pitchClassesForScale(scaleDef, rootPc);
                deg = degreeLabelsForScale(scaleDef);
            } else {
                const auto* chordDef = m_registry.chord(key);
                pcs = pitchClassesForChord(chordDef, rootPc);
                deg = degreeLabelsForChord(chordDef);
            }
        }

        if (statusBar()) statusBar()->clearMessage();

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
        return;
    }

    if (m_grooveTab && tab == m_tabs->indexOf(m_grooveTab)) {
        // Show a small timing preview for the selected groove.
        virtuoso::theory::GrooveTemplate groove;
        groove.key = "straight";
        groove.name = "Straight";
        if (m_grooveList && m_grooveList->currentItem()) {
            const QString gk = m_grooveList->currentItem()->data(Qt::UserRole).toString();
            for (const auto& g : m_grooveTemplates) {
                if (g.key == gk) { groove = g; break; }
            }
        }
        const int stepsPerBeat = m_grooveSubdivisionCombo ? m_grooveSubdivisionCombo->currentData().toInt() : 2;
        const int stepMs = qMax(40, perNoteDurationMs() / 4);
        const QVector<int> due = virtuoso::theory::GrooveEngine::scheduleDueMs(/*steps=*/12,
                                                                              stepMs,
                                                                              stepsPerBeat,
                                                                              groove,
                                                                              /*seed=*/123);
        if (m_groovePreviewLabel) {
            QString s = QString("Due ms (12 steps): ");
            for (int i = 0; i < due.size(); ++i) {
                if (i) s += ", ";
                s += QString::number(due[i]);
            }
            m_groovePreviewLabel->setText(s);
        }
        if (statusBar()) statusBar()->clearMessage();
        if (m_guitar) {
            m_guitar->setRootPitchClass(-1);
            m_guitar->setHighlightedPitchClasses({});
            m_guitar->setDegreeLabels({});
        }
        if (m_piano) {
            m_piano->setRootPitchClass(-1);
            m_piano->setHighlightedPitchClasses({});
            m_piano->setDegreeLabels({});
        }
        return;
    }

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
            statusBar()->showMessage(QString("Harmony: %1 — %2 (%3)").arg(h.roman, h.function, h.detail));
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

    if (statusBar() && tab != 2) {
        // Avoid stale messages outside UST/polychord contexts.
        if (!m_polyTab || tab != m_tabs->indexOf(m_polyTab)) statusBar()->clearMessage();
    }
}

