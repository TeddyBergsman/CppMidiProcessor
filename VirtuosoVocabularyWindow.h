#pragma once

#include <QMainWindow>
#include <QHash>
#include <QVector>

#include "virtuoso/groove/GrooveRegistry.h"
#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/vocab/VocabularyRegistry.h"
#include "virtuoso/ui/GrooveTimelineWidget.h"

class QListWidget;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QTextEdit;
class QTableWidget;
class QTimer;

class MidiProcessor;

// Per-instrument vocabulary browser + audition window:
// - Shows the data-driven vocabulary patterns for one agent (Piano/Bass/Drums)
// - Can audition patterns via MidiProcessor
// - Can live-follow TheoryEvent JSON stream while a song is playing
class VirtuosoVocabularyWindow final : public QMainWindow {
    Q_OBJECT
public:
    enum class Instrument { Piano, Bass, Drums };

    explicit VirtuosoVocabularyWindow(MidiProcessor* midi,
                                      Instrument instrument,
                                      QWidget* parent = nullptr);

public slots:
    void ingestTheoryEventJson(const QString& json);

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onSelectionChanged();
    void onPresetChanged();
    void onAuditionStartStop();
    void onAuditionTick();

private:
    void buildUi();
    void loadVocab();
    void refreshPatternList();
    void refreshDetailsAndPreview();
    void rebuildPresetCombo();
    const virtuoso::groove::GrooveRegistry::StylePreset* currentPreset() const;

    void stopAuditionNow();
    void generatePreviewEvents();
    void highlightPatternId(const QString& id);
    void rebuildLiveTimeline();

    static QString instrumentName(Instrument i);
    static int defaultMidiChannel(Instrument i);

    enum class Scope { Beat, Phrase };
    Scope currentScope() const;

    // Simple chord->midi helpers for audition (rhythm is the main goal here).
    static int nearestMidiForPc(int pc, int around, int lo, int hi);
    static int bassMidiForDegree(int rootPc, int degreePc, int lo, int hi);
    static int degreeToSemitone(const virtuoso::ontology::ChordDef* chordCtx, int degree);
    static QVector<int> midiNotesForVoicing(const virtuoso::ontology::VoicingDef* v,
                                            const virtuoso::ontology::ChordDef* chordCtx,
                                            int rootPc,
                                            int rootMidi);

    MidiProcessor* m_midi = nullptr; // not owned
    const Instrument m_instrument;

    // Groove/preset config for preview humanization
    virtuoso::groove::GrooveRegistry m_grooveRegistry;
    virtuoso::ontology::OntologyRegistry m_ontology;
    QComboBox* m_presetCombo = nullptr;
    QSpinBox* m_bpm = nullptr;
    QDoubleSpinBox* m_energy = nullptr;
    QCheckBox* m_intensityPeak = nullptr; // drums only

    // Ontology-driven audition controls
    QComboBox* m_rootCombo = nullptr;      // 0..11
    QComboBox* m_chordCombo = nullptr;     // ontology chord def key
    QComboBox* m_voicingCombo = nullptr;   // piano only: ontology voicing def key
    QComboBox* m_nextRootCombo = nullptr;  // bass only
    QComboBox* m_nextChordCombo = nullptr; // bass only
    QComboBox* m_scopeCombo = nullptr;     // Beat / Phrase

    // Main panels
    QListWidget* m_list = nullptr;
    QLabel* m_liveHeader = nullptr;
    QTextEdit* m_liveLog = nullptr;
    QTableWidget* m_detailTable = nullptr;

    // Timeline preview + audition
    virtuoso::ui::GrooveTimelineWidget* m_timeline = nullptr;
    QPushButton* m_auditionBtn = nullptr;
    QTimer* m_auditionTimer = nullptr;
    qint64 m_auditionStartMs = 0;
    qint64 m_auditionLastPlayMs = -1;
    QVector<virtuoso::ui::GrooveTimelineWidget::LaneEvent> m_previewEvents;
    QVector<virtuoso::ui::GrooveTimelineWidget::LaneEvent> m_liveEvents;
    int m_previewBars = 4;
    int m_subdivPerBeat = 2;

    // Auto-mode: if we receive planned events recently, we enter Live mode and disable audition.
    bool m_liveMode = false;
    QTimer* m_liveDecayTimer = nullptr;

    // Live buffer (from Theory stream)
    struct LiveEv {
        qint64 onMs = 0;
        qint64 offMs = 0;
        int note = -1;
        int velocity = 0;
        QString logic;
        QString grid;
        qint64 engineNowMs = 0;
    };
    QVector<LiveEv> m_liveBuf;
    int m_liveBpm = 60;
    int m_liveTsNum = 4;
    int m_liveTsDen = 4;
    qint64 m_liveBaseMs = 0;

    // Vocabulary data
    virtuoso::vocab::VocabularyRegistry m_vocab;
    QHash<QString, virtuoso::vocab::VocabularyRegistry::PianoPatternDef> m_pianoById;
    QHash<QString, virtuoso::vocab::VocabularyRegistry::BassPatternDef> m_bassById;
    QHash<QString, virtuoso::vocab::VocabularyRegistry::DrumsPatternDef> m_drumsById;

    // Phrase patterns (by id)
    QHash<QString, virtuoso::vocab::VocabularyRegistry::PianoPhraseChoice> m_pianoPhraseById;
    QHash<QString, virtuoso::vocab::VocabularyRegistry::BassPhraseChoice> m_bassPhraseById;
    QHash<QString, virtuoso::vocab::VocabularyRegistry::DrumsPhraseChoice> m_drumsPhraseById;
};

