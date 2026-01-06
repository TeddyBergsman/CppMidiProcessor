#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>

#include "chart/ChartModel.h"
#include "music/ChordSymbol.h"
#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/groove/GrooveRegistry.h"
#include "virtuoso/drums/FluffyAudioJazzDrumsBrushesMapping.h"
#include "virtuoso/theory/FunctionalHarmony.h"
#include "virtuoso/ontology/OntologyRegistry.h"

#include "playback/JazzBalladBassPlanner.h"
#include "playback/JazzBalladPianoPlanner.h"
#include "playback/BrushesBalladDrummer.h"
#include "playback/SemanticMidiAnalyzer.h"
#include "playback/VibeStateMachine.h"

class MidiProcessor;

namespace playback {

struct LocalKeyEstimate {
    int tonicPc = 0;
    QString scaleKey;
    QString scaleName;
    virtuoso::theory::KeyMode mode = virtuoso::theory::KeyMode::Major;
    double score = 0.0;
    double coverage = 0.0;
};

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

    // Emit a one-shot 4-bar lookahead plan for the currently selected song/chart, even if stopped.
    // Used by instrument windows for auditioning the current song context without duplicating controls.
    void emitLookaheadPlanOnce();

    // Debug/validation knobs (glass-box controllable).
    // Global energy override (0..1). When Auto is on (default), energy follows the vibe engine.
    void setDebugEnergyAuto(bool on) { m_debugEnergyAuto = on; }
    void setDebugEnergy(double energy01) { m_debugEnergy = qBound(0.0, energy01, 1.0); }

    // Per-agent energy multipliers (0..2 recommended).
    void setAgentEnergyMultiplier(const QString& agent, double mult01to2) {
        m_agentEnergyMult.insert(agent, qBound(0.0, mult01to2, 2.0));
    }

    // Stage 3 Virtuosity Matrix (global solver weights). When Auto is on (default),
    // weights are derived from energy + song progress + listening context.
    void setVirtuosityAuto(bool on) { m_virtAuto = on; }
    void setVirtuosity(double harmonicRisk01, double rhythmicComplexity01, double interaction01, double toneDark01) {
        m_virtHarmonicRisk = qBound(0.0, harmonicRisk01, 1.0);
        m_virtRhythmicComplexity = qBound(0.0, rhythmicComplexity01, 1.0);
        m_virtInteraction = qBound(0.0, interaction01, 1.0);
        m_virtToneDark = qBound(0.0, toneDark01, 1.0);
    }

signals:
    void currentCellChanged(int cellIndex);
    void theoryEventJson(const QString& json);
    void plannedTheoryEventJson(const QString& json);
    void lookaheadPlanJson(const QString& json);
    void debugStatus(const QString& text);
    void debugEnergy(double energy01, bool isAuto);

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
    virtuoso::ontology::OntologyRegistry m_ontology = virtuoso::ontology::OntologyRegistry::builtins();
    QString m_stylePresetKey = "jazz_brushes_ballad_60_evans";

    MidiProcessor* m_midi = nullptr; // not owned

    // Harmony tracking
    music::ChordSymbol m_lastChord;
    bool m_hasLastChord = false;
    int m_keyPcGuess = 0;           // 0..11 (major-key heuristic)
    bool m_hasKeyPcGuess = false;
    QString m_keyScaleKey;          // e.g. "ionian", "dorian", "aeolian"
    QString m_keyScaleName;         // e.g. "Ionian (Major)"
    virtuoso::theory::KeyMode m_keyMode = virtuoso::theory::KeyMode::Major;
    QVector<LocalKeyEstimate> m_localKeysByBar; // flattened chart bars

    JazzBalladBassPlanner m_bassPlanner;
    JazzBalladPianoPlanner m_pianoPlanner;
    BrushesBalladDrummer m_drummer;

    // Listening MVP (semantic analysis of user input)
    SemanticMidiAnalyzer m_listener;
    VibeStateMachine m_vibe;

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

    // Stage 3 Virtuosity Matrix (defaults: Auto).
    bool m_virtAuto = true;
    double m_virtHarmonicRisk = 0.20;
    double m_virtRhythmicComplexity = 0.25;
    double m_virtInteraction = 0.50;
    double m_virtToneDark = 0.60;
};

} // namespace playback

