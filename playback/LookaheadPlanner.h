#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include "chart/ChartModel.h"
#include "music/ChordSymbol.h"
#include "virtuoso/groove/GrooveGrid.h"
#include "playback/HarmonyContext.h"
#include "playback/SemanticMidiAnalyzer.h"
#include "playback/VibeStateMachine.h"

namespace playback {

class JazzBalladBassPlanner;
class JazzBalladPianoPlanner;
class BrushesBalladDrummer;

// Single source of truth for UI lookahead planning JSON.
// This replaces duplicate lookahead logic previously embedded in VirtuosoBalladMvpPlaybackEngine.
class LookaheadPlanner final {
public:
    struct Inputs {
        int bpm = 120;
        virtuoso::groove::TimeSignature ts{4, 4};
        int repeats = 1;

        const chart::ChartModel* model = nullptr;      // not owned
        const QVector<int>* sequence = nullptr;        // not owned (flattened beat steps -> cell index)

        // Harmony tracking baseline for simulation (so lookahead starts from the current chord).
        bool hasLastChord = false;
        music::ChordSymbol lastChord;

        // Harmony context (single source of truth).
        const HarmonyContext* harmonyCtx = nullptr; // not owned
        int keyWindowBars = 8;

        // Runtime agents (not owned)
        SemanticMidiAnalyzer* listener = nullptr;
        VibeStateMachine* vibe = nullptr;
        JazzBalladBassPlanner* bassPlanner = nullptr;
        JazzBalladPianoPlanner* pianoPlanner = nullptr;
        BrushesBalladDrummer* drummer = nullptr;

        // Channels
        int chDrums = 6;
        int chBass = 3;
        int chPiano = 4;

        // Style/preset state
        QString stylePresetKey;
        QHash<QString, double> agentEnergyMult; // agent -> multiplier (0..2)

        // Debug energy
        bool debugEnergyAuto = true;
        double debugEnergy = 0.25;

        // Virtuosity matrix (temporary; formalized in a later todo)
        bool virtAuto = true;
        double virtHarmonicRisk = 0.20;
        double virtRhythmicComplexity = 0.25;
        double virtInteraction = 0.50;
        double virtToneDark = 0.60;

        // Engine time domain (for TheoryEvent.engine_now_ms)
        qint64 engineNowMs = 0;

        // Lookahead snapshot time domain: caller-provided timestamp used for listener/vibe sampling.
        // This eliminates direct wall-clock reads inside planning code.
        qint64 nowMs = 0;

        // Optional precomputed interaction snapshots (preferred for background planning).
        bool hasIntentSnapshot = false;
        SemanticMidiAnalyzer::IntentState intentSnapshot{};
        bool hasVibeSnapshot = false;
        VibeStateMachine::Output vibeSnapshot{};
    };

    // Builds a compact JSON array of virtuoso::theory::TheoryEvent objects (next N bars).
    static QString buildLookaheadPlanJson(const Inputs& in, int stepNow, int horizonBars = 4);
};

} // namespace playback

