#pragma once

#include <QMainWindow>
#include <QSet>
#include <QtGlobal>
#include <QHash>
#include <QCloseEvent>
#include <QJsonObject>

#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/groove/GrooveRegistry.h"
#include "playback/HarmonyContext.h"

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

public slots:
    // Live-follow hook (connected from NoteMonitorWidget).
    void ingestTheoryEventJson(const QString& json);

private slots:
    void onSelectionChanged();
    void onPlayPressed();
    void onUserClickedMidi(int midi);
    void onGrooveAuditionTick();
    void onLiveFollowTimeout();

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
    
    // Compatibility helpers for smart filtering
    bool isChordCompatible(const virtuoso::ontology::ChordDef* candidate, 
                           const virtuoso::ontology::ChordDef* current) const;
    bool isScaleCompatible(const virtuoso::ontology::ScaleDef* candidate,
                           const virtuoso::ontology::ChordDef* currentChord) const;
    bool isVoicingCompatible(const virtuoso::ontology::VoicingDef* candidate,
                             const virtuoso::ontology::ChordDef* currentChord) const;
    QVector<QPair<QString, QString>> suggestUpperStructures(const virtuoso::ontology::ChordDef* chord,
                                                            int rootPc) const;

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
    void applyLiveChoiceToUi(const QJsonObject& obj);
    void applyEnabledStatesForLiveContext();
    static QString jsonString(const QJsonObject& o, const char* key);
    static int jsonInt(const QJsonObject& o, const char* key, int fallback = 0);

    virtuoso::ontology::OntologyRegistry m_registry;
    playback::HarmonyContext m_harmonyHelper;
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
    QComboBox* m_keyModeCombo = nullptr;   // key mode (Major/Minor)
    QComboBox* m_chordCtxCombo = nullptr;  // context chord for voicing degree->interval mapping (auto-synced during live follow)
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
    QListWidget* m_polySuggestionsList = nullptr;  // Upper structure suggestions based on current chord

    QSet<int> m_activeMidis;
    QTimer* m_autoPlayTimer = nullptr;
    quint64 m_playSession = 0;

    // Track which notes we turned on per channel, so we can force-release them on rapid retriggers.
    QHash<int, QSet<int>> m_heldNotesByChannel;

    // --- Live follow state (driven by Virtuoso TheoryEvent JSON) ---
    QTimer* m_liveFollowTimer = nullptr;
    bool m_liveFollowActive = false;
    bool m_liveUpdatingUi = false;
    int m_liveBpm = 120;
    QString m_lastLiveGridPos;
    QString m_liveChordText;
    QString m_liveScaleUsed;
    QString m_liveVoicingType;
    QString m_liveGrooveTemplateKey;

    // Exact candidate pools (from event_kind="candidate_pool").
    QSet<QString> m_liveCandChordKeys;
    QSet<QString> m_liveCandScaleKeys;
    QSet<QString> m_liveCandVoicingKeys;
    QSet<QString> m_liveCandGrooveKeys;

    // De-dupe/triggering: only audition when the *choice* changes (not every beat).
    QString m_lastChosenChordDefKey;
    int m_lastChosenChordRootPc = -1;
    QString m_lastChosenScaleUsed;
    QString m_lastChosenVoicingKey;
    QString m_lastChosenGrooveKey;

    // Song transport anchoring (wall clock <-> engine clock).
    qint64 m_songStartWallMs = -1;
};

