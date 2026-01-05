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
#include <functional>

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
    connect(m_durationCombo, &QComboBox::currentIndexChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_full88Check, &QCheckBox::toggled, this, &LibraryWindow::updatePianoRange);
    connect(m_playButton, &QPushButton::clicked, this, &LibraryWindow::onPlayPressed);

    connect(m_chordsList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_scalesList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);
    connect(m_voicingsList, &QListWidget::currentRowChanged, this, &LibraryWindow::onSelectionChanged);

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

    auto chordRank = [](ChordId id) -> int {
        switch (id) {
        // Requested order:
        // Maj, Maj7, 7, Sus2, Sus4, Min, Min7, m7b5, dim7, aug, 5
        case ChordId::MajorTriad: return 0;
        case ChordId::Major7: return 1;
        case ChordId::Dominant7: return 2;
        case ChordId::Sus2Triad: return 3;
        case ChordId::Sus4Triad: return 4;
        case ChordId::MinorTriad: return 5;
        case ChordId::Minor7: return 6;
        case ChordId::HalfDiminished7: return 7;
        case ChordId::Diminished7: return 8;
        case ChordId::AugmentedTriad: return 9;
        case ChordId::Power5: return 10;
        // Extra items not in the requested list go after.
        case ChordId::Shell_1_3: return 50;
        case ChordId::Shell_1_7: return 51;
        case ChordId::DiminishedTriad: return 100;
        case ChordId::PhrygianTriad: return 101;
        case ChordId::MinorMajor7: return 110;
        case ChordId::Augmented7: return 111;
        case ChordId::Dominant7Sus4: return 112;
        case ChordId::SevenSharp5: return 113;
        case ChordId::SevenFlat5: return 114;
        case ChordId::Six: return 120;
        case ChordId::MinorSix: return 121;
        default: return 1000;
        }
    };

    auto scaleRank = [](ScaleId id) -> int {
        switch (id) {
        case ScaleId::Ionian: return 0;
        case ScaleId::Dorian: return 1;
        case ScaleId::Phrygian: return 2;
        case ScaleId::Lydian: return 3;
        case ScaleId::Mixolydian: return 4;
        case ScaleId::Aeolian: return 5;
        case ScaleId::Locrian: return 6;

        case ScaleId::MelodicMinor: return 20;
        case ScaleId::DorianB2: return 21;
        case ScaleId::LydianAugmented: return 22;
        case ScaleId::LydianDominant: return 23;
        case ScaleId::MixolydianB6: return 24;
        case ScaleId::LocrianNat2: return 25;
        case ScaleId::Altered: return 26;

        case ScaleId::HarmonicMinor: return 30;
        case ScaleId::LocrianSharp6: return 31;
        case ScaleId::IonianSharp5: return 32;
        case ScaleId::DorianSharp4: return 33;
        case ScaleId::PhrygianDominant: return 34;
        case ScaleId::LydianSharp2: return 35;
        case ScaleId::SuperLocrianBb7: return 36;

        case ScaleId::HarmonicMajor: return 40;
        case ScaleId::DorianB5: return 41;
        case ScaleId::PhrygianB4: return 42;
        case ScaleId::LydianB3: return 43;
        case ScaleId::MixolydianB2: return 44;
        case ScaleId::LydianAugSharp2: return 45;
        case ScaleId::LocrianBb7: return 46;

        case ScaleId::WholeTone: return 60;
        case ScaleId::DiminishedWH: return 61;
        case ScaleId::DiminishedHW: return 62;

        case ScaleId::MajorPentatonic: return 70;
        case ScaleId::MinorPentatonic: return 71;
        case ScaleId::DominantPentatonic: return 72;
        case ScaleId::Blues: return 73;
        case ScaleId::MajorBlues: return 74;

        case ScaleId::MajorBebop: return 80;
        case ScaleId::DominantBebop: return 81;
        case ScaleId::MinorBebop: return 82;
        case ScaleId::DorianBebop: return 83;
        default: return 1000;
        }
    };

    auto voicingRank = [](const VoicingDef* v) -> int {
        if (!v) return 1000;
        if (v->category == "Shell") return 0;
        if (v->category == "Rootless") return 10;
        if (v->category == "Quartal") return 20;
        return 100;
    };

    std::sort(m_orderedChords.begin(), m_orderedChords.end(), [&](const ChordDef* a, const ChordDef* b) {
        if (!a || !b) return a != nullptr;
        const int ra = chordRank(a->id);
        const int rb = chordRank(b->id);
        if (ra != rb) return ra < rb;
        return a->name < b->name;
    });

    std::sort(m_orderedScales.begin(), m_orderedScales.end(), [&](const ScaleDef* a, const ScaleDef* b) {
        if (!a || !b) return a != nullptr;
        const int ra = scaleRank(a->id);
        const int rb = scaleRank(b->id);
        if (ra != rb) return ra < rb;
        return a->name < b->name;
    });

    std::sort(m_orderedVoicings.begin(), m_orderedVoicings.end(), [&](const VoicingDef* a, const VoicingDef* b) {
        if (!a || !b) return a != nullptr;
        const int ra = voicingRank(a);
        const int rb = voicingRank(b);
        if (ra != rb) return ra < rb;
        if (a->category != b->category) return a->category < b->category;
        return a->name < b->name;
    });

    // Chords
    m_chordsList->clear();
    for (const ChordDef* c : m_orderedChords) {
        QListWidgetItem* it = new QListWidgetItem(c->name, m_chordsList);
        it->setData(Qt::UserRole, int(c->id));
    }
    m_chordsList->setCurrentRow(0);

    // Scales
    m_scalesList->clear();
    for (const ScaleDef* s : m_orderedScales) {
        QListWidgetItem* it = new QListWidgetItem(s->name, m_scalesList);
        it->setData(Qt::UserRole, int(s->id));
    }
    m_scalesList->setCurrentRow(0);

    // Voicings
    m_voicingsList->clear();
    for (const VoicingDef* v : m_orderedVoicings) {
        QListWidgetItem* it = new QListWidgetItem(v->name, m_voicingsList);
        it->setData(Qt::UserRole, int(v->id));
    }
    m_voicingsList->setCurrentRow(0);

    // Chord context combo should match chord ordering.
    m_chordCtxCombo->clear();
    for (const ChordDef* c : m_orderedChords) {
        m_chordCtxCombo->addItem(c->name);
    }
    m_chordCtxCombo->setCurrentIndex(0);
}

void LibraryWindow::onSelectionChanged() {
    updateHighlights();
    scheduleAutoPlay();
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

    // Special-case: quartal placeholder voicing currently has no degree list.
    if (voicingDef->chordDegrees.isEmpty() && voicingDef->id == VoicingId::PianoQuartal_Stack4ths) {
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
    if (voicingDef->chordDegrees.isEmpty() && voicingDef->id == VoicingId::PianoQuartal_Stack4ths) {
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
        const int idx = qBound(0, m_chordCtxCombo ? m_chordCtxCombo->currentIndex() : 0, m_orderedChords.size() - 1);
        const auto* chordCtx = m_orderedChords.isEmpty() ? nullptr : m_orderedChords[idx];

        if (voicingDef->chordDegrees.isEmpty() && voicingDef->id == VoicingId::PianoQuartal_Stack4ths) {
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

    // Use a chained timer approach (rather than N independent timers) to avoid ordering jitter.
    using StepFn = std::function<void(int idx, int prev)>;
    auto stepFn = std::make_shared<StepFn>();
    *stepFn = [this, session, ch, vel, gateMs, stepMs, seq, stepFn](int idx, int prev) {
        if (session != m_playSession) return;
        if (!m_midi) return;
        if (idx >= seq.size()) {
            if (prev >= 0) {
                m_midi->sendVirtualNoteOff(ch, prev);
                setActiveMidi(prev, false);
            }
            return;
        }

        const int n = seq[idx];

        // Monophonic guarantee: kill previous immediately.
        if (prev >= 0) {
            m_midi->sendVirtualNoteOff(ch, prev);
            setActiveMidi(prev, false);
        }

        setActiveMidi(n, true);
        noteOnTracked(ch, n, vel);

        // Gate-off (safe even if next step already killed it).
        QTimer::singleShot(gateMs, this, [this, session, ch, n]() {
            if (session != m_playSession) return;
            if (!m_midi) return;
            noteOffTracked(ch, n);
            setActiveMidi(n, false);
        });

        QTimer::singleShot(stepMs, this, [stepFn, idx, n]() {
            (*stepFn)(idx + 1, n);
        });
    };

    (*stepFn)(/*idx=*/0, /*prev=*/-1);
}

void LibraryWindow::onPlayPressed() {
    const int rootPc = pcFromIndex(m_rootCombo ? m_rootCombo->currentIndex() : 0);
    const QVector<int> notes = midiNotesForCurrentSelection(rootPc);
    const bool isScale = (m_tabs && m_tabs->currentIndex() == 1);
    const int dur = perNoteDurationMs();
    playMidiNotes(notes, /*durationMs=*/dur, /*arpeggiate=*/isScale);
}

void LibraryWindow::onUserClickedMidi(int midi) {
    playSingleNote(midi, /*durationMs=*/perNoteDurationMs());
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

