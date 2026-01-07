#pragma once

#include <QMainWindow>
#include <QVector>

#include "virtuoso/ui/GrooveTimelineWidget.h"

class QListWidget;
class QLabel;
class QPushButton;
class QCheckBox;
class QTextEdit;
class QTableWidget;
class QTimer;
class QSlider;

class MidiProcessor;

// Per-instrument procedural player window:
// - Shows current-song 4-bar lookahead plan for this instrument
// - Lets user audition what the player will do (in current song context)
// - Exposes per-instrument multipliers (e.g., Energy) that affect the live engine
class VirtuosoVocabularyWindow final : public QMainWindow {
    Q_OBJECT
public:
    enum class Instrument { Piano, Bass, Drums };

    explicit VirtuosoVocabularyWindow(MidiProcessor* midi,
                                      Instrument instrument,
                                      QWidget* parent = nullptr);

signals:
    // Request a one-shot 4-bar lookahead plan for the currently selected song (even if stopped).
    void requestSongPreview();
    // Per-instrument multiplier changes (0..2 recommended).
    void agentEnergyMultiplierChanged(const QString& agent, double mult01to2);

public slots:
    void ingestTheoryEventJson(const QString& json);

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onSelectionChanged();
    void onAuditionStartStop();
    void onAuditionTick();

private:
    void buildUi();
    void refreshTagList();
    void rebuildTimelineFromLivePlan();

    void stopAuditionNow();
    void highlightPatternId(const QString& id);

    static QString instrumentName(Instrument i);
    static int defaultMidiChannel(Instrument i);

    MidiProcessor* m_midi = nullptr; // not owned
    const Instrument m_instrument;

    // Per-instrument multipliers
    QSlider* m_energyMultSlider = nullptr; // 0..200 (%)
    QLabel* m_energyMultLabel = nullptr;

    // Main panels
    QListWidget* m_list = nullptr;
    QLabel* m_liveHeader = nullptr;
    QTextEdit* m_liveLog = nullptr;
    QTableWidget* m_detailTable = nullptr;

    // Timeline (single widget) + audition
    virtuoso::ui::GrooveTimelineWidget* m_timeline = nullptr;
    QPushButton* m_auditionBtn = nullptr;
    QTimer* m_auditionTimer = nullptr;
    qint64 m_auditionStartMs = 0;
    qint64 m_auditionLastPlayMs = -1;

    // Auto-mode: if we receive planned events recently, we enter Live mode and disable audition.
    bool m_liveMode = false;
    QTimer* m_liveDecayTimer = nullptr;
    QTimer* m_livePlayheadTimer = nullptr;

    // Live playhead tracking (engine clock domain -> wall clock smoothing)
    qint64 m_lastEngineNowMs = 0;
    qint64 m_lastEngineWallMs = 0;
    qint64 m_liveBaseMs = 0;

    // Live buffer (from Theory stream)
    struct LiveEv {
        qint64 onMs = 0;
        qint64 offMs = 0;
        QString kind; // "note" (default) or "cc"
        int note = -1;
        int velocity = 0;
        int cc = -1;
        int ccValue = -1;
        QString logic;
        QString grid;
        qint64 engineNowMs = 0;
    };
    QVector<LiveEv> m_liveBuf;
    int m_liveBpm = 60;
    int m_liveTsNum = 4;
    int m_liveTsDen = 4;
    QString m_lastPlanJson;

    // Current plan rendered on timeline (filtered by selected logic_tag).
    QVector<virtuoso::ui::GrooveTimelineWidget::LaneEvent> m_displayEvents;
};

