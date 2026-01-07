#pragma once

#include <QMainWindow>
#include <QSet>
#include <QtGlobal>
#include <QHash>
#include <QCloseEvent>

#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/groove/GrooveRegistry.h"

class QListWidget;
class QTabWidget;
class QComboBox;
class QLabel;
class QPushButton;
class QCheckBox;
class MidiProcessor;

class GuitarFretboardWidget;
class PianoKeyboardWidget;

class LibraryWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit LibraryWindow(MidiProcessor* midi, QWidget* parent = nullptr);

private slots:
    void onSelectionChanged();
    void onPlayPressed();
    void onUserClickedMidi(int midi);
    void onGrooveAuditionTick();

private:
    void closeEvent(QCloseEvent* e) override;

    void buildUi();
    void populateLists();
    void updateHighlights();
    void updatePianoRange();
    void updatePolychordHighlights();
    void updateGrooveInfo();

    static QString pcName(int pc);
    static int pcFromIndex(int idx);

    QSet<int> pitchClassesForChord(const virtuoso::ontology::ChordDef* chordDef, int rootPc) const;
    QSet<int> pitchClassesForScale(const virtuoso::ontology::ScaleDef* scaleDef, int rootPc) const;
    QSet<int> pitchClassesForVoicing(const virtuoso::ontology::VoicingDef* voicingDef,
                                     const virtuoso::ontology::ChordDef* chordContext,
                                     int rootPc) const;
    QSet<int> pitchClassesForPolychord() const;

    QHash<int, QString> degreeLabelsForChord(const virtuoso::ontology::ChordDef* chordDef) const;
    QHash<int, QString> degreeLabelsForScale(const virtuoso::ontology::ScaleDef* scaleDef) const;
    QHash<int, QString> degreeLabelsForVoicing(const virtuoso::ontology::VoicingDef* voicingDef,
                                               const virtuoso::ontology::ChordDef* chordContext) const;

    int selectedPlaybackChannel() const;
    int baseRootMidiForPosition(int rootPc) const;
    QVector<int> midiNotesForSelectionTab(int tab, int rootPc) const;
    QVector<int> midiNotesForCurrentSelection(int rootPc) const;
    QVector<int> midiNotesForPolychord() const;
    void playMidiNotes(const QVector<int>& notes, int durationMs, bool arpeggiate);
    void playSingleNote(int midi, int durationMs);
    void setActiveMidi(int midi, bool on);
    void clearActiveMidis();
    int perNoteDurationMs() const;
    void scheduleAutoPlay();
    void stopPlaybackNow(int channel);
    void noteOnTracked(int channel, int midi, int vel);
    void noteOffTracked(int channel, int midi);
    void stopGrooveAuditionNow();
    void startOrUpdateGrooveLoop(bool preservePhase);
    void rebuildGrooveAuditionEvents(const virtuoso::groove::GrooveTemplate* gt, int bpm);
    int grooveBpm() const;
    QString selectedGrooveKey() const;
    const virtuoso::groove::GrooveTemplate* selectedGrooveTemplate() const;

    virtuoso::ontology::OntologyRegistry m_registry;
    MidiProcessor* m_midi = nullptr; // not owned
    virtuoso::groove::GrooveRegistry m_grooveRegistry;

    // Stable, logical ordering (QHash iteration order is not deterministic)
    QVector<const virtuoso::ontology::ChordDef*> m_orderedChords;
    QVector<const virtuoso::ontology::ScaleDef*> m_orderedScales;
    QVector<const virtuoso::ontology::VoicingDef*> m_orderedVoicings;
    QVector<const virtuoso::groove::GrooveTemplate*> m_orderedGrooves;

    QTabWidget* m_tabs = nullptr;

    // Global controls
    QComboBox* m_rootCombo = nullptr;      // 0..11 (C..B)
    QComboBox* m_keyCombo = nullptr;       // harmony analysis key (0..11, C..B)
    QComboBox* m_chordCtxCombo = nullptr;  // context chord for voicing degree->interval mapping
    QComboBox* m_playInstrumentCombo = nullptr;
    QComboBox* m_positionCombo = nullptr;
    QComboBox* m_durationCombo = nullptr;
    QPushButton* m_playButton = nullptr;
    QCheckBox* m_full88Check = nullptr;

    // Lists
    QListWidget* m_chordsList = nullptr;
    QListWidget* m_scalesList = nullptr;
    QListWidget* m_voicingsList = nullptr;
    QWidget* m_polyTab = nullptr;
    QListWidget* m_groovesList = nullptr;
    QWidget* m_grooveTab = nullptr;
    QLabel* m_grooveInfo = nullptr;
    QComboBox* m_grooveTempoCombo = nullptr;
    QTimer* m_grooveAuditionTimer = nullptr;
    qint64 m_grooveAuditionStartWallMs = 0;
    int m_grooveAuditionIndex = 0;
    qint64 m_grooveAuditionLoopLenMs = 0;
    quint64 m_grooveSession = 0;
    struct GrooveAuditionEvent { qint64 onMs=0; qint64 offMs=0; int channel=6; int note=0; int vel=64; };
    QVector<GrooveAuditionEvent> m_grooveAuditionEvents;

    // Visualizers
    GuitarFretboardWidget* m_guitar = nullptr;
    PianoKeyboardWidget* m_piano = nullptr;

    // Polychord controls (procedural generator)
    QComboBox* m_polyTemplateCombo = nullptr;
    QComboBox* m_polyUpperRoot = nullptr;
    QComboBox* m_polyUpperChord = nullptr;
    QComboBox* m_polyLowerRoot = nullptr;
    QComboBox* m_polyLowerChord = nullptr;

    QSet<int> m_activeMidis;
    QTimer* m_autoPlayTimer = nullptr;
    quint64 m_playSession = 0;

    // Track which notes we turned on per channel, so we can force-release them on rapid retriggers.
    QHash<int, QSet<int>> m_heldNotesByChannel;
};

