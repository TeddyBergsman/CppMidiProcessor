#include "VirtuosoPresetInspectorWindow.h"

#include "midiprocessor.h"
#include "playback/BalladReferenceTuning.h"
#include "playback/JazzBalladBassPlanner.h"
#include "playback/JazzBalladPianoPlanner.h"
#include "playback/BrushesBalladDrummer.h"

#include "virtuoso/drums/FluffyAudioJazzDrumsBrushesMapping.h"
#include "virtuoso/groove/GrooveRegistry.h"
#include "virtuoso/groove/TimingHumanizer.h"
#include "virtuoso/groove/GrooveGrid.h"

#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QTimer>
#include <QDateTime>

#include "virtuoso/util/StableHash.h"

using virtuoso::groove::GrooveRegistry;

VirtuosoPresetInspectorWindow::VirtuosoPresetInspectorWindow(MidiProcessor* midi, QWidget* parent)
    : QMainWindow(parent)
    , m_midi(midi) {
    setWindowTitle("Virtuoso Preset Inspector");
    resize(980, 720);

    m_regOwned = new GrooveRegistry(GrooveRegistry::builtins());

    QWidget* root = new QWidget(this);
    QVBoxLayout* v = new QVBoxLayout(root);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(8);

    // Top controls: preset + bpm
    {
        QHBoxLayout* h = new QHBoxLayout();
        m_presetCombo = new QComboBox(this);
        m_presetCombo->setMinimumWidth(420);
        m_bpm = new QSpinBox(this);
        m_bpm->setRange(30, 300);
        m_bpm->setValue(60);
        m_bpm->setSuffix(" bpm");

        QPushButton* gen = new QPushButton("Generate Preview", this);

        h->addWidget(new QLabel("Style preset:", this));
        h->addWidget(m_presetCombo, 1);
        h->addSpacing(10);
        h->addWidget(new QLabel("Tempo:", this));
        h->addWidget(m_bpm, 0);
        h->addSpacing(10);
        h->addWidget(gen, 0);
        v->addLayout(h);

        connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VirtuosoPresetInspectorWindow::onPresetChanged);
        connect(m_bpm, QOverload<int>::of(&QSpinBox::valueChanged), this, &VirtuosoPresetInspectorWindow::onBpmChanged);
        connect(gen, &QPushButton::clicked, this, &VirtuosoPresetInspectorWindow::onGeneratePreview);
    }

    m_presetSummary = new QLabel(this);
    m_presetSummary->setWordWrap(true);
    m_presetSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_presetSummary);

    QTabWidget* tabs = new QTabWidget(this);
    v->addWidget(tabs, 1);

    // Tab: Groove + profiles
    {
        QWidget* page = new QWidget(this);
        QVBoxLayout* pv = new QVBoxLayout(page);

        QGroupBox* gb1 = new QGroupBox("Groove template offsets (exact grid points)", page);
        QVBoxLayout* gb1v = new QVBoxLayout(gb1);
        m_grooveOffsets = new QTableWidget(gb1);
        m_grooveOffsets->setColumnCount(4);
        m_grooveOffsets->setHorizontalHeaderLabels({"withinBeat", "unit", "value", "ms@tempo"});
        m_grooveOffsets->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_grooveOffsets->verticalHeader()->setVisible(false);
        m_grooveOffsets->setEditTriggers(QAbstractItemView::NoEditTriggers);
        gb1v->addWidget(m_grooveOffsets);
        pv->addWidget(gb1);

        QGroupBox* gb2 = new QGroupBox("Per-instrument groove profiles (humanization)", page);
        QVBoxLayout* gb2v = new QVBoxLayout(gb2);
        m_profiles = new QTableWidget(gb2);
        m_profiles->setColumnCount(10);
        m_profiles->setHorizontalHeaderLabels({"instrument", "pushMs", "laidBackMs", "microJitterMs", "attackVarMs", "driftMaxMs", "driftRate", "velJitter", "accentDownbeat", "accentBackbeat"});
        m_profiles->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_profiles->verticalHeader()->setVisible(false);
        m_profiles->setEditTriggers(QAbstractItemView::NoEditTriggers);
        gb2v->addWidget(m_profiles);
        pv->addWidget(gb2, 1);

        tabs->addTab(page, "Groove + Profiles");
    }

    // Tab: Drum mapping
    {
        QWidget* page = new QWidget(this);
        QVBoxLayout* pv = new QVBoxLayout(page);
        QLabel* blurb = new QLabel("FluffyAudio Jazz Drums - Brushes mapping (noteName convention: C2 == MIDI 48).", page);
        blurb->setWordWrap(true);
        pv->addWidget(blurb);

        m_drumMap = new QTableWidget(page);
        m_drumMap->setColumnCount(4);
        m_drumMap->setHorizontalHeaderLabels({"MIDI", "noteName", "articulation", "holdMsForFullSample"});
        m_drumMap->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        m_drumMap->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_drumMap->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_drumMap->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_drumMap->verticalHeader()->setVisible(false);
        m_drumMap->setEditTriggers(QAbstractItemView::NoEditTriggers);
        pv->addWidget(m_drumMap, 1);

        tabs->addTab(page, "Drum Mapping");
    }

    // Tab: Brain tuning + preview
    {
        QWidget* page = new QWidget(this);
        QVBoxLayout* pv = new QVBoxLayout(page);

        QGroupBox* gb = new QGroupBox("Reference-track tuning + generated preview", page);
        QVBoxLayout* gbv = new QVBoxLayout(gb);

        m_tuningText = new QTextEdit(page);
        m_tuningText->setReadOnly(true);
        m_tuningText->setMinimumHeight(140);

        // Visual timeline (grid + per-instrument lanes)
        m_timeline = new virtuoso::ui::GrooveTimelineWidget(page);
        m_timeline->setLanes({"Drums", "Bass", "Piano"});

        QHBoxLayout* ah = new QHBoxLayout();
        m_auditionBtn = new QPushButton("Audition", page);
        ah->addWidget(m_auditionBtn);
        ah->addStretch(1);

        gbv->addWidget(m_tuningText);
        gbv->addLayout(ah);
        gbv->addWidget(m_timeline, 1);
        pv->addWidget(gb, 1);

        tabs->addTab(page, "Timeline Preview");

        m_auditionTimer = new QTimer(this);
        m_auditionTimer->setTimerType(Qt::PreciseTimer);
        m_auditionTimer->setInterval(16); // ~60fps playhead
        connect(m_auditionTimer, &QTimer::timeout, this, &VirtuosoPresetInspectorWindow::onAuditionTick);
        connect(m_auditionBtn, &QPushButton::clicked, this, &VirtuosoPresetInspectorWindow::onAuditionStartStop);
        connect(m_timeline, &virtuoso::ui::GrooveTimelineWidget::eventClicked, this, &VirtuosoPresetInspectorWindow::onTimelineEventClicked);
    }

    setCentralWidget(root);

    rebuildPresetCombo();
    rebuildDrumMapTable();
    refreshAll();
}

QString VirtuosoPresetInspectorWindow::currentPresetKey() const {
    if (!m_presetCombo) return {};
    return m_presetCombo->currentData(Qt::UserRole).toString();
}

void VirtuosoPresetInspectorWindow::rebuildPresetCombo() {
    if (!m_regOwned || !m_presetCombo) return;
    const bool prev = m_presetCombo->blockSignals(true);
    m_presetCombo->clear();
    const auto presets = m_regOwned->allStylePresets();
    int sel = -1;
    for (const auto* p : presets) {
        if (!p) continue;
        m_presetCombo->addItem(p->name, p->key);
        const int row = m_presetCombo->count() - 1;
        m_presetCombo->setItemData(row, p->key, Qt::UserRole);
        if (p->key == "jazz_brushes_ballad_60_evans") sel = row;
    }
    if (sel >= 0) m_presetCombo->setCurrentIndex(sel);
    m_presetCombo->blockSignals(prev);
}

void VirtuosoPresetInspectorWindow::rebuildDrumMapTable() {
    if (!m_drumMap) return;
    const auto notes = virtuoso::drums::fluffyAudioJazzDrumsBrushesNotes();
    m_drumMap->setRowCount(notes.size());
    for (int i = 0; i < notes.size(); ++i) {
        const auto& n = notes[i];
        m_drumMap->setItem(i, 0, new QTableWidgetItem(QString::number(n.midi)));
        m_drumMap->setItem(i, 1, new QTableWidgetItem(QString::fromUtf8(n.noteName)));
        m_drumMap->setItem(i, 2, new QTableWidgetItem(QString::fromUtf8(n.articulation)));
        m_drumMap->setItem(i, 3, new QTableWidgetItem(n.holdMsForFullSample > 0 ? QString::number(n.holdMsForFullSample) : ""));
    }
}

void VirtuosoPresetInspectorWindow::refreshAll() {
    refreshPresetSummary();
    refreshGrooveTemplateTable();
    refreshInstrumentProfilesTable();
    refreshReferenceTuningPanel();
}

void VirtuosoPresetInspectorWindow::refreshPresetSummary() {
    const QString key = currentPresetKey();
    const auto* p = (m_regOwned && !key.isEmpty()) ? m_regOwned->stylePreset(key) : nullptr;
    if (!p) {
        m_presetSummary->setText("(no preset selected)");
        return;
    }
    QString s;
    s += QString("**Key:** %1\n").arg(p->key);
    s += QString("**Name:** %1\n").arg(p->name);
    s += QString("**GrooveTemplate:** %1 (amount=%2)\n").arg(p->grooveTemplateKey).arg(p->templateAmount, 0, 'f', 2);
    s += QString("**Default:** %1 bpm, %2/%3\n").arg(p->defaultBpm).arg(p->defaultTimeSig.num).arg(p->defaultTimeSig.den);
    if (!p->articulationNotes.isEmpty()) {
        s += "\n**Articulation notes:**\n";
        const auto keys = p->articulationNotes.keys();
        for (const auto& k : keys) {
            s += QString("- %1: %2\n").arg(k, p->articulationNotes.value(k));
        }
    }
    // QLabel doesn't render markdown by default; keep it readable.
    m_presetSummary->setText(s);
}

void VirtuosoPresetInspectorWindow::refreshGrooveTemplateTable() {
    if (!m_grooveOffsets || !m_regOwned) return;
    const QString key = currentPresetKey();
    const auto* p = (!key.isEmpty()) ? m_regOwned->stylePreset(key) : nullptr;
    const auto* gt = (p && !p->grooveTemplateKey.isEmpty()) ? m_regOwned->grooveTemplate(p->grooveTemplateKey) : nullptr;
    m_grooveOffsets->setRowCount(0);
    if (!gt) return;

    const int bpm = m_bpm ? m_bpm->value() : 60;
    const double beatMs = 60000.0 / double(qMax(1, bpm)); // quarter
    const double scaledBeatMs = beatMs * (4.0 / double(qMax(1, p->defaultTimeSig.den)));

    m_grooveOffsets->setRowCount(gt->offsetMap.size());
    for (int i = 0; i < gt->offsetMap.size(); ++i) {
        const auto& op = gt->offsetMap[i];
        const QString within = QString("%1/%2").arg(op.withinBeat.num).arg(op.withinBeat.den);
        const QString unit = (op.unit == virtuoso::groove::OffsetUnit::Ms) ? "ms" : "beatFraction";
        const QString val = QString::number(op.value, 'f', 3);
        double ms = 0.0;
        if (op.unit == virtuoso::groove::OffsetUnit::Ms) ms = op.value * p->templateAmount;
        else ms = op.value * scaledBeatMs * p->templateAmount;

        m_grooveOffsets->setItem(i, 0, new QTableWidgetItem(within));
        m_grooveOffsets->setItem(i, 1, new QTableWidgetItem(unit));
        m_grooveOffsets->setItem(i, 2, new QTableWidgetItem(val));
        m_grooveOffsets->setItem(i, 3, new QTableWidgetItem(QString::number(ms, 'f', 1)));
    }
}

void VirtuosoPresetInspectorWindow::refreshInstrumentProfilesTable() {
    if (!m_profiles || !m_regOwned) return;
    const QString key = currentPresetKey();
    const auto* p = (!key.isEmpty()) ? m_regOwned->stylePreset(key) : nullptr;
    m_profiles->setRowCount(0);
    if (!p) return;

    const auto instruments = p->instrumentProfiles.keys();
    m_profiles->setRowCount(instruments.size());
    for (int r = 0; r < instruments.size(); ++r) {
        const QString inst = instruments[r];
        const auto ip = p->instrumentProfiles.value(inst);
        m_profiles->setItem(r, 0, new QTableWidgetItem(inst));
        m_profiles->setItem(r, 1, new QTableWidgetItem(QString::number(ip.pushMs)));
        m_profiles->setItem(r, 2, new QTableWidgetItem(QString::number(ip.laidBackMs)));
        m_profiles->setItem(r, 3, new QTableWidgetItem(QString::number(ip.microJitterMs)));
        m_profiles->setItem(r, 4, new QTableWidgetItem(QString::number(ip.attackVarianceMs)));
        m_profiles->setItem(r, 5, new QTableWidgetItem(QString::number(ip.driftMaxMs)));
        m_profiles->setItem(r, 6, new QTableWidgetItem(QString::number(ip.driftRate, 'f', 2)));
        m_profiles->setItem(r, 7, new QTableWidgetItem(QString::number(ip.velocityJitter)));
        m_profiles->setItem(r, 8, new QTableWidgetItem(QString::number(ip.accentDownbeat, 'f', 2)));
        m_profiles->setItem(r, 9, new QTableWidgetItem(QString::number(ip.accentBackbeat, 'f', 2)));
    }
}

void VirtuosoPresetInspectorWindow::refreshReferenceTuningPanel() {
    if (!m_tuningText) return;
    const auto t = playback::tuningForReferenceTrack(currentPresetKey());
    QString s;
    s += "Ballad Brain reference tuning (Chet Baker – My Funny Valentine)\n\n";
    s += QString("Bass: approachProbBeat3=%1, skipBeat3ProbStable=%2, allowApproachFromAbove=%3\n")
        .arg(t.bassApproachProbBeat3, 0, 'f', 2)
        .arg(t.bassSkipBeat3ProbStable, 0, 'f', 2)
        .arg(t.bassAllowApproachFromAbove ? "true" : "false");
    s += QString("Piano: skipBeat2ProbStable=%1, addSecondColorProb=%2, sparkleProbBeat4=%3, preferShells=%4\n")
        .arg(t.pianoSkipBeat2ProbStable, 0, 'f', 2)
        .arg(t.pianoAddSecondColorProb, 0, 'f', 2)
        .arg(t.pianoSparkleProbBeat4, 0, 'f', 2)
        .arg(t.pianoPreferShells ? "true" : "false");
    s += QString("Piano ranges: LH [%1..%2], RH [%3..%4], sparkle [%5..%6]\n")
        .arg(t.pianoLhLo).arg(t.pianoLhHi)
        .arg(t.pianoRhLo).arg(t.pianoRhHi)
        .arg(t.pianoSparkleLo).arg(t.pianoSparkleHi);
    m_tuningText->setPlainText(s);
}

void VirtuosoPresetInspectorWindow::onPresetChanged() {
    refreshAll();
}

void VirtuosoPresetInspectorWindow::onBpmChanged(int) {
    refreshGrooveTemplateTable();
    refreshReferenceTuningPanel();
}

void VirtuosoPresetInspectorWindow::onGeneratePreview() {
    if (!m_timeline || !m_regOwned) return;

    // Simple preview over a tiny ballad test progression (ii–V–I in C).
    // This is for *visual validation* of what planners do, not audio playback.
    const QStringList chords = {"Dm7", "G7", "Cmaj7", "Cmaj7"};

    playback::JazzBalladBassPlanner bass;
    playback::JazzBalladPianoPlanner piano;
    playback::BrushesBalladDrummer drums;
    bass.reset();
    piano.reset();

    const auto tune = playback::tuningForReferenceTrack(currentPresetKey());
    const quint32 detSeed = virtuoso::util::StableHash::fnv1a32((QString("ballad|") + currentPresetKey()).toUtf8());

    virtuoso::groove::TimeSignature ts{4, 4};
    const int bpm = m_bpm ? m_bpm->value() : 60;

    // Preview loop length in ms (4/4 assumed here; matches the preview grid).
    const qint64 totalMsPreview = qint64(llround(double(qMax(1, chords.size())) * (60000.0 / double(qMax(1, bpm))) * 4.0));

    // Resolve selected style preset -> groove template + instrument profiles for humanization.
    const QString presetKey = currentPresetKey();
    const auto* sp = (!presetKey.isEmpty()) ? m_regOwned->stylePreset(presetKey) : nullptr;
    virtuoso::groove::GrooveTemplate gtScaled;
    bool haveGt = false;
    if (sp) {
        const auto* gt = m_regOwned->grooveTemplate(sp->grooveTemplateKey);
        if (gt) {
            gtScaled = *gt;
            gtScaled.amount = qBound(0.0, sp->templateAmount, 1.0);
            haveGt = true;
        }
    }

    virtuoso::groove::InstrumentGrooveProfile drumProf;
    drumProf.instrument = "Drums";
    virtuoso::groove::InstrumentGrooveProfile bassProf;
    bassProf.instrument = "Bass";
    virtuoso::groove::InstrumentGrooveProfile pianoProf;
    pianoProf.instrument = "Piano";
    if (sp) {
        if (sp->instrumentProfiles.contains("Drums")) drumProf = sp->instrumentProfiles.value("Drums");
        if (sp->instrumentProfiles.contains("Bass")) bassProf = sp->instrumentProfiles.value("Bass");
        if (sp->instrumentProfiles.contains("Piano")) pianoProf = sp->instrumentProfiles.value("Piano");
    }

    virtuoso::groove::TimingHumanizer hDrums(drumProf);
    virtuoso::groove::TimingHumanizer hBass(bassProf);
    virtuoso::groove::TimingHumanizer hPiano(pianoProf);
    if (haveGt) {
        hDrums.setGrooveTemplate(gtScaled);
        hBass.setGrooveTemplate(gtScaled);
        hPiano.setGrooveTemplate(gtScaled);
    }
    hDrums.reset();
    hBass.reset();
    hPiano.reset();
    m_previewBars = chords.size();
    m_subdivPerBeat = 2; // default: 8ths for ballad visualization
    m_previewEvents.clear();
    m_previewEvents.reserve(256);

    for (int bar = 0; bar < chords.size(); ++bar) {
        music::ChordSymbol c;
        const bool ok = music::parseChordSymbol(chords[bar], c);
        if (!ok) continue;

        music::ChordSymbol next;
        bool haveNext = false;
        if (bar + 1 < chords.size() && music::parseChordSymbol(chords[bar + 1], next)) haveNext = true;

        for (int beat = 0; beat < 4; ++beat) {
            playback::JazzBalladBassPlanner::Context bc;
            bc.bpm = bpm;
            bc.playbackBarIndex = bar;
            bc.beatInBar = beat;
            bc.chordIsNew = (beat == 0);
            bc.chord = c;
            bc.hasNextChord = haveNext;
            bc.nextChord = next;
            bc.chordText = chords[bar];
            bc.determinismSeed = detSeed;
            bc.approachProbBeat3 = tune.bassApproachProbBeat3;
            bc.skipBeat3ProbStable = tune.bassSkipBeat3ProbStable;
            bc.allowApproachFromAbove = tune.bassAllowApproachFromAbove;

            auto bnotes = bass.planBeat(bc, /*ch*/3, ts);

            playback::JazzBalladPianoPlanner::Context pc;
            pc.bpm = bpm;
            pc.playbackBarIndex = bar;
            pc.beatInBar = beat;
            pc.chordIsNew = (beat == 0);
            pc.chord = c;
            pc.chordText = chords[bar];
            pc.determinismSeed = detSeed ^ 0xBADC0FFEu;
            pc.lhLo = tune.pianoLhLo; pc.lhHi = tune.pianoLhHi;
            pc.rhLo = tune.pianoRhLo; pc.rhHi = tune.pianoRhHi;
            pc.sparkleLo = tune.pianoSparkleLo; pc.sparkleHi = tune.pianoSparkleHi;
            pc.skipBeat2ProbStable = tune.pianoSkipBeat2ProbStable;
            pc.addSecondColorProb = tune.pianoAddSecondColorProb;
            pc.sparkleProbBeat4 = tune.pianoSparkleProbBeat4;
            pc.preferShells = tune.pianoPreferShells;

            auto pnotes = piano.planBeat(pc, /*ch*/4, ts);

            auto addHumanized = [&](const QString& lane,
                                    virtuoso::groove::TimingHumanizer& h,
                                    const virtuoso::engine::AgentIntentNote& n,
                                    const QString& label,
                                    bool structural) {
                const auto gp = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(bar, beat, 0, 1, ts);
                const auto he = h.humanizeNote(gp, ts, bpm, n.baseVelocity, n.durationWhole, structural);

                // Clamp into preview window so the loop scheduler can hit the event.
                qint64 on = qBound<qint64>(0, he.onMs, totalMsPreview);
                qint64 off = qBound<qint64>(0, he.offMs, totalMsPreview + 8000);
                if (off <= on) off = on + 60;

                virtuoso::ui::GrooveTimelineWidget::LaneEvent ev;
                ev.lane = lane;
                ev.note = n.note;
                ev.velocity = he.velocity;
                ev.onMs = on;
                ev.offMs = off;
                ev.label = label;
                m_previewEvents.push_back(ev);
            };

            for (const auto& n : bnotes) addHumanized("Bass", hBass, n, QString("n%1 %2").arg(n.note).arg(n.logic_tag), /*structural*/(beat == 0));
            for (const auto& n : pnotes) addHumanized("Piano", hPiano, n, QString("n%1 %2").arg(n.note).arg(n.voicing_type), /*structural*/(beat == 0));

            // Drums: Brushes Ballad Drummer v1 (same generator used by the real MVP runner).
            playback::BrushesBalladDrummer::Context dc;
            dc.bpm = bpm;
            dc.ts = ts;
            dc.playbackBarIndex = bar;
            dc.beatInBar = beat;
            dc.structural = (beat == 0);
            dc.determinismSeed = detSeed ^ 0xD00D'BEEFu;
            auto dnotes = drums.planBeat(dc);
            for (const auto& dn : dnotes) {
                const QString label = dn.logic_tag.isEmpty()
                    ? QString("n%1").arg(dn.note)
                    : dn.logic_tag;
                addHumanized("Drums", hDrums, dn, label, /*structural*/dn.structural);
            }
        }
    }

    m_timeline->setTempoAndSignature(bpm, 4, 4);
    m_timeline->setPreviewBars(m_previewBars);
    m_timeline->setSubdivision(m_subdivPerBeat);
    m_timeline->setEvents(m_previewEvents);
    m_timeline->setPlayheadMs(-1);
}

void VirtuosoPresetInspectorWindow::onAuditionStartStop() {
    if (!m_auditionBtn || !m_auditionTimer || !m_midi) return;
    if (m_auditionTimer->isActive()) {
        m_auditionTimer->stop();
        m_auditionBtn->setText("Audition");
        if (m_timeline) m_timeline->setPlayheadMs(-1);
        // silence
        m_midi->sendVirtualAllNotesOff(6);
        m_midi->sendVirtualAllNotesOff(3);
        m_midi->sendVirtualAllNotesOff(4);
        return;
    }
    if (m_previewEvents.isEmpty()) onGeneratePreview();
    m_auditionStartMs = QDateTime::currentMSecsSinceEpoch();
    m_auditionBtn->setText("Stop");
    m_auditionTimer->start();
}

void VirtuosoPresetInspectorWindow::onAuditionTick() {
    if (!m_auditionTimer || !m_auditionTimer->isActive() || !m_timeline) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 rel = now - m_auditionStartMs;
    // Loop playhead over preview length (ms domain)
    qint64 totalMs = 0;
    {
        // recompute total ms matching widget’s internal totalMs() is private; approximate here.
        const int bpm = m_bpm ? m_bpm->value() : 60;
        const double quarterMs = 60000.0 / double(qMax(1, bpm));
        totalMs = qint64(llround(double(qMax(1, m_previewBars)) * quarterMs * 4.0));
    }
    if (totalMs <= 0) totalMs = 1;
    const qint64 play = rel % totalMs;
    m_timeline->setPlayheadMs(play);

    // Very simple audition: fire note-ons at their onMs when crossing boundaries (no scheduler).
    // NOTE: preview events are microtimed (ms) already.
    static qint64 lastPlay = -1;
    if (lastPlay < 0) lastPlay = play;
    const bool wrapped = play < lastPlay;
    for (const auto& ev : m_previewEvents) {
        const bool hit = wrapped ? (ev.onMs >= lastPlay || ev.onMs <= play) : (ev.onMs >= lastPlay && ev.onMs <= play);
        if (!hit) continue;
        int ch = 4;
        if (ev.lane == "Drums") ch = 6;
        if (ev.lane == "Bass") ch = 3;
        if (ev.lane == "Piano") ch = 4;
        m_midi->sendVirtualNoteOn(ch, ev.note, qBound(1, ev.velocity, 127));
        const int durMs = qBound(40, int(ev.offMs - ev.onMs), 8000);
        QTimer::singleShot(durMs, this, [this, ch, n = ev.note]() {
            if (!m_midi) return;
            m_midi->sendVirtualNoteOff(ch, n);
        });
    }
    lastPlay = play;
}

void VirtuosoPresetInspectorWindow::onTimelineEventClicked(const QString& lane, int note, int velocity, const QString& label) {
    if (!m_midi) return;
    int ch = 4;
    if (lane == "Drums") ch = 6;
    if (lane == "Bass") ch = 3;
    if (lane == "Piano") ch = 4;
    Q_UNUSED(label);
    m_midi->sendVirtualNoteOn(ch, note, qBound(1, velocity > 0 ? velocity : 64, 127));
    QTimer::singleShot(180, this, [this, ch, n = note]() {
        if (!m_midi) return;
        m_midi->sendVirtualNoteOff(ch, n);
    });
}

