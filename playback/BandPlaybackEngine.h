#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTimer>
#include <QVector>
#include <QHash>

#include "chart/ChartModel.h"
#include "music/WalkingBassGenerator.h"
#include "music/BassProfile.h"

namespace playback {

// Playback engine that drives the chart playhead AND emits virtual musician MIDI events.
// v1: one cell per beat (quarter note), 4 cells per bar.
class BandPlaybackEngine : public QObject {
    Q_OBJECT
public:
    explicit BandPlaybackEngine(QObject* parent = nullptr);

    void setTempoBpm(int bpm);
    void setRepeats(int repeats);
    void setChartModel(const chart::ChartModel& model);
    void setBassProfile(const music::BassProfile& p);
    const music::BassProfile& bassProfile() const { return m_bassProfile; }

    bool isPlaying() const { return m_playing; }

public slots:
    void play();
    void stop();

signals:
    void currentCellChanged(int cellIndex);

    // Virtual bass MIDI events (1-based channel)
    void bassNoteOn(int channel, int note, int velocity);
    void bassNoteOff(int channel, int note);
    void bassAllNotesOff(int channel);

    // Human-readable log line explaining why an event was played.
    // Emitted only when BassProfile::reasoningLogEnabled is true.
    void bassLogLine(const QString& line);

private slots:
    void onTick();
    void onDispatch();

private:
    QVector<const chart::Bar*> flattenBars() const;
    QVector<int> buildPlaybackSequence() const;
    QVector<QString> buildBarSections() const;

    const chart::Cell* cellForFlattenedIndex(int cellIndex) const;
    bool chordForCellIndex(int cellIndex, music::ChordSymbol& outChord, bool& isNewChord);
    bool chordForNextCellIndex(int cellIndex, music::ChordSymbol& outChord);

    int m_bpm = 120;
    int m_repeats = 3;
    bool m_playing = false;

    chart::ChartModel m_model;
    QVector<int> m_sequence;
    QVector<QString> m_barSections; // barIndex -> section label ("" if unknown)

    music::WalkingBassGenerator m_bass;
    music::BassProfile m_bassProfile;
    music::ChordSymbol m_lastChord; // last non-empty/non-placeholder chord encountered
    bool m_hasLastChord = false;
    int m_lastBassMidi = -1;
    int m_lastStep = -1;
    int m_lastEmittedCell = -1;
    int m_lastPlayheadStep = -1;
    int m_nextScheduledStep = 0;

    // ---- Scheduling (single timer + min-heap) ----
    enum class PendingKind { NoteOn, NoteOff, AllNotesOff };
    struct PendingEvent {
        qint64 dueMs = 0; // absolute in m_clock.elapsed() ms
        PendingKind kind = PendingKind::NoteOn;
        int channel = 1;
        int note = 0;
        int velocity = 0;
        bool emitLog = false;
        QString logLine;
    };
    QVector<PendingEvent> m_eventHeap; // min-heap by dueMs (front = earliest)
    QTimer m_dispatchTimer;

    QRandomGenerator m_timingRng;
    int m_driftMs = 0; // slow random-walk timing drift

    QElapsedTimer m_clock;
    QTimer m_tickTimer;

    // Safety/validation
    int m_lastBarIndex = -1;
    QHash<int, int> m_scheduledNoteOnsInBar; // barIndex -> note-ons scheduled (for sanity)
};

} // namespace playback

