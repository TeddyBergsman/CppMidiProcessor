#include "LibraryWindow.h"

#include <QTabWidget>
#include <QListWidget>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

#include "virtuoso/ui/GuitarFretboardWidget.h"
#include "virtuoso/ui/PianoKeyboardWidget.h"

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

} // namespace

LibraryWindow::LibraryWindow(QWidget* parent)
    : QMainWindow(parent),
      m_registry(OntologyRegistry::builtins()) {
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
    controls->addWidget(new QLabel("Voicing chord context:", this));
    m_chordCtxCombo = new QComboBox(this);
    // Populate with chord defs (name is short; we prefix for clarity)
    for (const ChordDef* c : m_registry.allChords()) {
        m_chordCtxCombo->addItem(c->name);
    }
    m_chordCtxCombo->setCurrentIndex(0);
    controls->addWidget(m_chordCtxCombo);

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
    connect(m_chordCtxCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_chordsList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_scalesList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_voicingsList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);
}

void LibraryWindow::populateLists() {
    // Chords
    m_chordsList->clear();
    for (const ChordDef* c : m_registry.allChords()) {
        QListWidgetItem* it = new QListWidgetItem(c->name, m_chordsList);
        it->setData(Qt::UserRole, int(c->id));
    }
    m_chordsList->setCurrentRow(0);

    // Scales
    m_scalesList->clear();
    for (const ScaleDef* s : m_registry.allScales()) {
        QListWidgetItem* it = new QListWidgetItem(s->name, m_scalesList);
        it->setData(Qt::UserRole, int(s->id));
    }
    m_scalesList->setCurrentRow(0);

    // Voicings
    m_voicingsList->clear();
    for (const VoicingDef* v : m_registry.allVoicings()) {
        QListWidgetItem* it = new QListWidgetItem(v->name, m_voicingsList);
        it->setData(Qt::UserRole, int(v->id));
    }
    m_voicingsList->setCurrentRow(0);
}

void LibraryWindow::onSelectionChanged() {
    updateHighlights();
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

    // Stage 1: interpret voicing degrees relative to chord context with a simple extension mapping.
    for (int deg : voicingDef->chordDegrees) {
        const int st = degreeToSemitone(chordContext, deg);
        pcs.insert(normalizePc(rootPc + st));
    }
    return pcs;
}

void LibraryWindow::updateHighlights() {
    const int rootPc = pcFromIndex(m_rootCombo ? m_rootCombo->currentIndex() : 0);

    QSet<int> pcs;

    const int tab = m_tabs ? m_tabs->currentIndex() : 0;
    if (tab == 0 && m_chordsList) {
        if (!m_chordsList->currentItem()) return;
        const auto* chordDef = m_registry.chord(ChordId(m_chordsList->currentItem()->data(Qt::UserRole).toInt()));
        pcs = pitchClassesForChord(chordDef, rootPc);
    } else if (tab == 1 && m_scalesList) {
        if (!m_scalesList->currentItem()) return;
        const auto* scaleDef = m_registry.scale(ScaleId(m_scalesList->currentItem()->data(Qt::UserRole).toInt()));
        pcs = pitchClassesForScale(scaleDef, rootPc);
    } else if (tab == 2 && m_voicingsList) {
        if (!m_voicingsList->currentItem()) return;
        const auto* voicingDef = m_registry.voicing(VoicingId(m_voicingsList->currentItem()->data(Qt::UserRole).toInt()));

        // chord context by combo index into allChords() list ordering
        const auto allChords = m_registry.allChords();
        const int idx = qBound(0, m_chordCtxCombo ? m_chordCtxCombo->currentIndex() : 0, allChords.size() - 1);
        const auto* chordCtx = allChords.isEmpty() ? nullptr : allChords[idx];
        pcs = pitchClassesForVoicing(voicingDef, chordCtx, rootPc);
    }

    if (m_guitar) {
        m_guitar->setRootPitchClass(rootPc);
        m_guitar->setHighlightedPitchClasses(pcs);
    }
    if (m_piano) {
        m_piano->setRootPitchClass(rootPc);
        m_piano->setHighlightedPitchClasses(pcs);
    }
}

