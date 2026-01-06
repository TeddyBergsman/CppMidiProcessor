#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>

#include "chart/ChartModel.h"
#include "music/ChordSymbol.h"
#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/groove/GrooveRegistry.h"
#include "virtuoso/drums/FluffyAudioJazzDrumsBrushesMapping.h"

#include "playback/JazzBalladBassPlanner.h"
#include "playback/JazzBalladPianoPlanner.h"

class MidiProcessor;

namespace playback {

// MVP: chart-driven Drums/Bass/Piano for jazz brushes ballad.
// - Uses the new virtuoso::engine::VirtuosoEngine + groove templates (no legacy generators).
// - Drums output on MIDI channel 6 (per product spec / VST routing).
class VirtuosoBalladMvpPlaybackEngine : public QObject {
    Q_OBJECT
public:
    explicit VirtuosoBalladMvpPlaybackEngine(QObject* parent = nullptr);

    void setMidiProcessor(MidiProcessor* midi);
    void setTempoBpm(int bpm);
    void setRepeats(int repeats);
    void setChartModel(const chart::ChartModel& model);

    // A key from groove::GrooveRegistry::StylePreset (e.g. "jazz_brushes_ballad_60_evans").
    void setStylePresetKey(const QString& key);
    QString stylePresetKey() const { return m_stylePresetKey; }

    bool isPlaying() const { return m_playing; }

public slots:
    void play();
    void stop();

signals:
    void currentCellChanged(int cellIndex);
    void theoryEventJson(const QString& json);

private slots:
    void onTick();

private:
    void rebuildSequence();
    void applyPresetToEngine();

    QVector<const chart::Bar*> flattenBars() const;
    QVector<int> buildPlaybackSequenceFromModel() const;
    const chart::Cell* cellForFlattenedIndex(int cellIndex) const;
    bool chordForCellIndex(int cellIndex, music::ChordSymbol& outChord, bool& isNewChord);

    void scheduleStep(int stepIndex, int seqLen);

    void scheduleDrumsBrushes(int playbackBarIndex, int beatInBar, bool structural);
    void scheduleBassTwoFeel(int playbackBarIndex, int beatInBar, const music::ChordSymbol& chord, bool chordIsNew);
    void schedulePianoComp(int playbackBarIndex, int beatInBar, const music::ChordSymbol& chord, bool chordIsNew);

    static int thirdIntervalForQuality(music::ChordQuality q);
    static int seventhIntervalFor(const music::ChordSymbol& c);
    static int chooseBassMidi(int pc);
    static int choosePianoMidi(int pc, int targetLow, int targetHigh);

    int m_bpm = 120;
    int m_repeats = 3;
    bool m_playing = false;

    chart::ChartModel m_model;
    QVector<int> m_sequence;

    // Playback state (step = 1 beat in the global playback timeline)
    int m_lastPlayheadStep = -1;
    int m_lastEmittedCell = -1;
    int m_nextScheduledStep = 0;

    QTimer m_tickTimer;

    // New engine (internal clock domain)
    virtuoso::engine::VirtuosoEngine m_engine;
    virtuoso::groove::GrooveRegistry m_registry;
    QString m_stylePresetKey = "jazz_brushes_ballad_60_evans";

    MidiProcessor* m_midi = nullptr; // not owned

    // Harmony tracking
    music::ChordSymbol m_lastChord;
    bool m_hasLastChord = false;

    JazzBalladBassPlanner m_bassPlanner;
    JazzBalladPianoPlanner m_pianoPlanner;

    // Channels (1..16)
    int m_chDrums = 6;
    int m_chBass = 3;
    int m_chPiano = 4;

    // Drum note mapping defaults (GM-ish; may be customized later per VST).
    int m_noteKick = virtuoso::drums::fluffy_brushes::kKickLooseNormal_G0;
    int m_noteSnareHit = virtuoso::drums::fluffy_brushes::kSnareRightHand_D1;
    int m_noteBrushLoop = virtuoso::drums::fluffy_brushes::kBrushCircleTwoHands_Fs3;
};

} // namespace playback

