#pragma once

#include <QMainWindow>
#include <QSet>

#include "virtuoso/ontology/OntologyRegistry.h"

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

private:
    void buildUi();
    void populateLists();
    void updateHighlights();
    void updatePianoRange();

    static QString pcName(int pc);
    static int pcFromIndex(int idx);

    QSet<int> pitchClassesForChord(const virtuoso::ontology::ChordDef* chordDef, int rootPc) const;
    QSet<int> pitchClassesForScale(const virtuoso::ontology::ScaleDef* scaleDef, int rootPc) const;
    QSet<int> pitchClassesForVoicing(const virtuoso::ontology::VoicingDef* voicingDef,
                                     const virtuoso::ontology::ChordDef* chordContext,
                                     int rootPc) const;

    QHash<int, QString> degreeLabelsForChord(const virtuoso::ontology::ChordDef* chordDef) const;
    QHash<int, QString> degreeLabelsForScale(const virtuoso::ontology::ScaleDef* scaleDef) const;
    QHash<int, QString> degreeLabelsForVoicing(const virtuoso::ontology::VoicingDef* voicingDef,
                                               const virtuoso::ontology::ChordDef* chordContext) const;

    int selectedPlaybackChannel() const;
    int baseRootMidiForPosition(int rootPc) const;
    QVector<int> midiNotesForCurrentSelection(int rootPc) const;
    void playMidiNotes(const QVector<int>& notes, int durationMs, bool arpeggiate);
    void playSingleNote(int midi, int durationMs);

    virtuoso::ontology::OntologyRegistry m_registry;
    MidiProcessor* m_midi = nullptr; // not owned

    QTabWidget* m_tabs = nullptr;

    // Global controls
    QComboBox* m_rootCombo = nullptr;      // 0..11 (C..B)
    QComboBox* m_chordCtxCombo = nullptr;  // context chord for voicing degree->interval mapping
    QComboBox* m_playInstrumentCombo = nullptr;
    QComboBox* m_positionCombo = nullptr;
    QPushButton* m_playButton = nullptr;
    QCheckBox* m_full88Check = nullptr;

    // Lists
    QListWidget* m_chordsList = nullptr;
    QListWidget* m_scalesList = nullptr;
    QListWidget* m_voicingsList = nullptr;

    // Visualizers
    GuitarFretboardWidget* m_guitar = nullptr;
    PianoKeyboardWidget* m_piano = nullptr;
};

