#pragma once

#include <QMainWindow>
#include <QSet>

#include "virtuoso/ontology/OntologyRegistry.h"

class QListWidget;
class QTabWidget;
class QComboBox;
class QLabel;

class GuitarFretboardWidget;
class PianoKeyboardWidget;

class LibraryWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit LibraryWindow(QWidget* parent = nullptr);

private slots:
    void onSelectionChanged();

private:
    void buildUi();
    void populateLists();
    void updateHighlights();

    static QString pcName(int pc);
    static int pcFromIndex(int idx);

    QSet<int> pitchClassesForChord(const virtuoso::ontology::ChordDef* chordDef, int rootPc) const;
    QSet<int> pitchClassesForScale(const virtuoso::ontology::ScaleDef* scaleDef, int rootPc) const;
    QSet<int> pitchClassesForVoicing(const virtuoso::ontology::VoicingDef* voicingDef,
                                     const virtuoso::ontology::ChordDef* chordContext,
                                     int rootPc) const;

    virtuoso::ontology::OntologyRegistry m_registry;

    QTabWidget* m_tabs = nullptr;

    // Global controls
    QComboBox* m_rootCombo = nullptr;      // 0..11 (C..B)
    QComboBox* m_chordCtxCombo = nullptr;  // context chord for voicing degree->interval mapping

    // Lists
    QListWidget* m_chordsList = nullptr;
    QListWidget* m_scalesList = nullptr;
    QListWidget* m_voicingsList = nullptr;

    // Visualizers
    GuitarFretboardWidget* m_guitar = nullptr;
    PianoKeyboardWidget* m_piano = nullptr;
};

