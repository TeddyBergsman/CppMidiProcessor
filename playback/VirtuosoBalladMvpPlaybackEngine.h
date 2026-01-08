#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <atomic>

#include "chart/ChartModel.h"
#include "music/ChordSymbol.h"
#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/groove/GrooveRegistry.h"
#include "virtuoso/drums/FluffyAudioJazzDrumsBrushesMapping.h"
#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/vocab/VocabularyRegistry.h"

#include "playback/JazzBalladBassPlanner.h"
#include "playback/JazzBalladPianoPlanner.h"
#include "playback/BrushesBalladDrummer.h"
#include "playback/HarmonyContext.h"
#include "playback/TransportTimeline.h"
#include "playback/InteractionContext.h"
#include "playback/StoryState.h"
#include "virtuoso/memory/MotivicMemory.h"
#include "virtuoso/control/PerformanceWeightsV2.h"
#include "playback/WeightNegotiator.h"

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

    // A key from groove::GrooveRegistry::StylePreset (e.g. "jazz_brushes_ballad_60_evans_lush").
    void setStylePresetKey(const QString& key);
    QString stylePresetKey() const { return m_stylePresetKey; }

    bool isPlaying() const { return m_playing; }

public slots:
    void play();
    void stop();

    // Emit a one-shot 4-bar lookahead plan for the currently selected song/chart, even if stopped.
    // Used by instrument windows for auditioning the current song context without duplicating controls.
    void emitLookaheadPlanOnce();

    // Async lookahead completion (invoked on UI thread).
    Q_INVOKABLE void applyLookaheadResult(quint64 jobId, int stepNow, const QString& json, int buildMs);

    // Debug/validation knobs (glass-box controllable).
    // Global energy override (0..1). When Auto is on (default), energy follows the vibe engine.
    void setDebugEnergyAuto(bool on);
    void setDebugEnergy(double energy01);

    // Per-agent energy multipliers (0..2 recommended).
    void setAgentEnergyMultiplier(const QString& agent, double mult01to2) {
        m_agentEnergyMult.insert(agent, qBound(0.0, mult01to2, 2.0));
    }

    // New global control surface (Weights v2): primary sliders.
    void setPerformanceWeightsAuto(bool on) { m_weightsV2Auto = on; }
    void setPerformanceWeightsV2(const virtuoso::control::PerformanceWeightsV2& w) { m_weightsV2Manual = w; }

signals:
    void currentCellChanged(int cellIndex);
    void theoryEventJson(const QString& json);
    void plannedTheoryEventJson(const QString& json);
    void lookaheadPlanJson(const QString& json);
    void debugStatus(const QString& text);
    void debugEnergy(double energy01, bool isAuto);
    void debugWeightsV2(double density01,
                        double rhythm01,
                        double intensity01,
                        double dynamism01,
                        double emotion01,
                        double creativity01,
                        double tension01,
                        double interactivity01,
                        double variability01,
                        double warmth01,
                        bool isAuto);

private slots:
    void onTick();
    // Listening MVP: receive live-performance events from MidiProcessor (queued from worker thread).
    void onGuitarNoteOn(int note, int vel);
    void onGuitarNoteOff(int note);
    void onVoiceCc2Stream(int cc2);
    void onVoiceNoteOn(int note, int vel);
    void onVoiceNoteOff(int note);

private:
    void updateRealtimeEnergyGains(double energy01);
    void scheduleLookaheadAsync(int stepNow, const virtuoso::groove::TimeSignature& ts, qint64 nowWallMs, qint64 engineNowMs);

    void rebuildSequence();
    void applyPresetToEngine();

    const chart::Cell* cellForFlattenedIndex(int cellIndex) const;
    bool chordForCellIndex(int cellIndex, music::ChordSymbol& outChord, bool& isNewChord);

    void scheduleStep(int stepIndex, int seqLen);

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
    int m_lastLookaheadStepEmitted = -1;
    qint64 m_playStartWallMs = 0;
    qint64 m_engineGridBaseMs = 0;
    std::atomic<quint64> m_lookaheadJobId{0};
    int m_lastLookaheadBuildMs = -1;

    QTimer m_tickTimer;

    // New engine (internal clock domain)
    virtuoso::engine::VirtuosoEngine m_engine;
    virtuoso::groove::GrooveRegistry m_registry;
    virtuoso::ontology::OntologyRegistry m_ontology = virtuoso::ontology::OntologyRegistry::builtins();
    QString m_stylePresetKey = "jazz_brushes_ballad_60_evans";

    MidiProcessor* m_midi = nullptr; // not owned

    HarmonyContext m_harmony;

    // Transport: repeat/ending expansion + cell lookup.
    TransportTimeline m_transport;

    JazzBalladBassPlanner m_bassPlanner;
    JazzBalladPianoPlanner m_pianoPlanner;
    BrushesBalladDrummer m_drummer;

    // Data-driven rhythmic vocabulary (optional, but enabled by default for ballad MVP).
    virtuoso::vocab::VocabularyRegistry m_vocab;
    bool m_vocabLoaded = false;
    QString m_vocabError;

    // Interaction: listening + macro-dynamics
    InteractionContext m_interaction;

    // Shared memory for motivic/counterpoint logic across agents.
    virtuoso::memory::MotivicMemory m_motivicMemory;

    // Persistent long-horizon story continuity (4–8 bars).
    StoryState m_story;

    // Channels (1..16)
    int m_chDrums = 6;
    int m_chBass = 3;
    int m_chPiano = 4;

    // Drum note mapping defaults (GM-ish; may be customized later per VST).
    int m_noteKick = virtuoso::drums::fluffy_brushes::kKickLooseNormal_G0;
    int m_noteSnareHit = virtuoso::drums::fluffy_brushes::kSnareRightHand_D1;
    int m_noteBrushLoop = virtuoso::drums::fluffy_brushes::kBrushCircleTwoHands_Fs3;

    // Groove lock (prototype): align Bass downbeat attacks to Drums feather kick if present.
    bool m_kickLocksBass = true;
    int m_kickLockMaxMs = 18;

    // Debug controls (defaults: Auto energy, neutral multipliers).
    bool m_debugEnergyAuto = true;
    double m_debugEnergy = 0.25;
    QHash<QString, double> m_agentEnergyMult; // agent -> multiplier

    int m_lastCc11Piano = -1;
    int m_lastCc11Bass = -1;
    int m_lastCc11Drums = -1;
    qint64 m_lastRealtimeGainUpdateElapsedMs = -1;
    double m_realtimeEnergySmoothed = 0.25;

    // Weights v2 (defaults: Auto).
    bool m_weightsV2Auto = true;
    virtuoso::control::PerformanceWeightsV2 m_weightsV2Manual{};
    playback::WeightNegotiator::State m_weightNegState{};
    bool m_hasPrevWeightsV2ForNegotiation = false;
    virtuoso::control::PerformanceWeightsV2 m_prevWeightsV2ForNegotiation{};

    // Track whether drums were enabled last tick (for stopping loops when drums are disabled by energy layering).
    bool m_drumsEnabledLast = false;
};

} // namespace playback

