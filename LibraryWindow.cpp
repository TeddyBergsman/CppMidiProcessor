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

#include "virtuoso/ui/GuitarFretboardWidget.h"
#include "virtuoso/ui/PianoKeyboardWidget.h"

#include "midiprocessor.h"

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
    // Very small Stage-1 mapping for visual feedback.
    // (Later: quality-aware b3/#5/b7, etc.)
    if (iv == 0) return "1";
    if (iv == 3 || iv == 4) return "3";
    if (iv == 6 || iv == 7 || iv == 8) return "5";
    if (iv == 9 || iv == 10 || iv == 11) return "7";
    if (iv == 14) return "9";
    if (iv == 17) return "11";
    if (iv == 21) return "13";
    return {};
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
    connect(m_playInstrumentCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_positionCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_full88Check, &QCheckBox::toggled, this, &LibraryWindow::updatePianoRange);
    connect(m_playButton, &QPushButton::clicked, this, &LibraryWindow::onPlayPressed);

    connect(m_chordsList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_scalesList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_voicingsList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);

    // Click-to-play from visualizers
    connect(m_guitar, &GuitarFretboardWidget::noteClicked, this, &LibraryWindow::onUserClickedMidi);
    connect(m_piano, &PianoKeyboardWidget::noteClicked, this, &LibraryWindow::onUserClickedMidi);
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

void LibraryWindow::updatePianoRange() {
    if (!m_piano) return;
    if (m_full88Check && m_full88Check->isChecked()) {
        m_piano->setRange(21, 108);
    } else {
        m_piano->setRange(/*A2*/45, /*C5*/72);
    }
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

QVector<int> LibraryWindow::midiNotesForCurrentSelection(int rootPc) const {
    QVector<int> notes;
    const int tab = m_tabs ? m_tabs->currentIndex() : 0;
    const int baseRoot = baseRootMidiForPosition(rootPc);

    if (tab == 0 && m_chordsList && m_chordsList->currentItem()) {
        const auto* chordDef = m_registry.chord(ChordId(m_chordsList->currentItem()->data(Qt::UserRole).toInt()));
        if (!chordDef) return notes;
        for (int iv : chordDef->intervals) notes.push_back(normalizeMidi(baseRoot + iv));
    } else if (tab == 1 && m_scalesList && m_scalesList->currentItem()) {
        const auto* scaleDef = m_registry.scale(ScaleId(m_scalesList->currentItem()->data(Qt::UserRole).toInt()));
        if (!scaleDef) return notes;
        for (int iv : scaleDef->intervals) notes.push_back(normalizeMidi(baseRoot + iv));
        // add octave for a more scale-like sound
        notes.push_back(normalizeMidi(baseRoot + 12));
    } else if (tab == 2 && m_voicingsList && m_voicingsList->currentItem()) {
        const auto* voicingDef = m_registry.voicing(VoicingId(m_voicingsList->currentItem()->data(Qt::UserRole).toInt()));
        if (!voicingDef) return notes;
        const auto allChords = m_registry.allChords();
        const int idx = qBound(0, m_chordCtxCombo ? m_chordCtxCombo->currentIndex() : 0, allChords.size() - 1);
        const auto* chordCtx = allChords.isEmpty() ? nullptr : allChords[idx];
        for (int deg : voicingDef->chordDegrees) {
            const int st = degreeToSemitone(chordCtx, deg);
            notes.push_back(normalizeMidi(baseRoot + st));
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
    m_midi->sendVirtualNoteOn(ch, midi, vel);
    QTimer::singleShot(durationMs, this, [this, ch, midi]() {
        if (!m_midi) return;
        m_midi->sendVirtualNoteOff(ch, midi);
    });
}

void LibraryWindow::playMidiNotes(const QVector<int>& notes, int durationMs, bool arpeggiate) {
    if (notes.isEmpty()) return;
    if (!m_midi) return;

    const int ch = selectedPlaybackChannel();
    const int vel = 48;

    if (!arpeggiate) {
        for (int n : notes) m_midi->sendVirtualNoteOn(ch, n, vel);
        QTimer::singleShot(durationMs, this, [this, ch, notes]() {
            if (!m_midi) return;
            for (int n : notes) m_midi->sendVirtualNoteOff(ch, n);
        });
        return;
    }

    // Arpeggiate (used for scales)
    int stepMs = qMax(40, durationMs / qMax(1, notes.size()));
    for (int i = 0; i < notes.size(); ++i) {
        const int n = notes[i];
        QTimer::singleShot(i * stepMs, this, [this, ch, n, vel, stepMs]() {
            if (!m_midi) return;
            m_midi->sendVirtualNoteOn(ch, n, vel);
            QTimer::singleShot(stepMs - 5, this, [this, ch, n]() {
                if (!m_midi) return;
                m_midi->sendVirtualNoteOff(ch, n);
            });
        });
    }
}

void LibraryWindow::onPlayPressed() {
    const int rootPc = pcFromIndex(m_rootCombo ? m_rootCombo->currentIndex() : 0);
    const QVector<int> notes = midiNotesForCurrentSelection(rootPc);
    const bool isScale = (m_tabs && m_tabs->currentIndex() == 1);
    playMidiNotes(notes, /*durationMs=*/650, /*arpeggiate=*/isScale);
}

void LibraryWindow::onUserClickedMidi(int midi) {
    playSingleNote(midi, /*durationMs=*/200);
}

void LibraryWindow::updateHighlights() {
    const int rootPc = pcFromIndex(m_rootCombo ? m_rootCombo->currentIndex() : 0);

    QSet<int> pcs;
    QHash<int, QString> deg;

    const int tab = m_tabs ? m_tabs->currentIndex() : 0;
    if (tab == 0 && m_chordsList) {
        if (!m_chordsList->currentItem()) return;
        const auto* chordDef = m_registry.chord(ChordId(m_chordsList->currentItem()->data(Qt::UserRole).toInt()));
        pcs = pitchClassesForChord(chordDef, rootPc);
        deg = degreeLabelsForChord(chordDef);
    } else if (tab == 1 && m_scalesList) {
        if (!m_scalesList->currentItem()) return;
        const auto* scaleDef = m_registry.scale(ScaleId(m_scalesList->currentItem()->data(Qt::UserRole).toInt()));
        pcs = pitchClassesForScale(scaleDef, rootPc);
        deg = degreeLabelsForScale(scaleDef);
    } else if (tab == 2 && m_voicingsList) {
        if (!m_voicingsList->currentItem()) return;
        const auto* voicingDef = m_registry.voicing(VoicingId(m_voicingsList->currentItem()->data(Qt::UserRole).toInt()));

        // chord context by combo index into allChords() list ordering
        const auto allChords = m_registry.allChords();
        const int idx = qBound(0, m_chordCtxCombo ? m_chordCtxCombo->currentIndex() : 0, allChords.size() - 1);
        const auto* chordCtx = allChords.isEmpty() ? nullptr : allChords[idx];
        pcs = pitchClassesForVoicing(voicingDef, chordCtx, rootPc);
        deg = degreeLabelsForVoicing(voicingDef, chordCtx);
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
}

