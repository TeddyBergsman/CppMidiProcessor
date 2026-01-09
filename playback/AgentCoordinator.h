#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include "playback/HarmonyContext.h"
#include "playback/InteractionContext.h"
#include "playback/JazzBalladBassPlanner.h"
#include "playback/JazzBalladPianoPlanner.h"
#include "playback/BrushesBalladDrummer.h"
#include "playback/StoryState.h"
#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/memory/MotivicMemory.h"
#include "virtuoso/control/PerformanceWeightsV2.h"
#include "playback/WeightNegotiator.h"

namespace playback {

// AgentCoordinator: owns the per-step agent scheduling policy (which agents act, when, and how),
// bridging HarmonyContext + InteractionContext into concrete AgentIntentNotes scheduled into VirtuosoEngine.
class AgentCoordinator final {
public:
    struct Inputs {
        // Owner (for emitting debug signals via invokeMethod; can be any QObject exposing slots/signals).
        QObject* owner = nullptr; // not owned

        // Core environment
        const chart::ChartModel* model = nullptr;   // not owned
        const QVector<int>* sequence = nullptr;     // not owned
        int repeats = 1;

        // Runtime clocks/config
        int bpm = 120;
        QString stylePresetKey;
        QHash<QString, double> agentEnergyMult;

        // Weights v2 (new global control surface).
        bool weightsV2Auto = true;
        virtuoso::control::PerformanceWeightsV2 weightsV2{};
        // Negotiated per-agent applied weights (computed by playback engine).
        playback::WeightNegotiator::Output negotiated{};

        // Debug controls
        bool debugEnergyAuto = true;
        double debugEnergy = 0.25;
        bool debugMutePianoLH = false;
        bool debugMutePianoRH = false;
        bool debugVerbose = true;

        // Channels + mapping
        int chDrums = 6;
        int chBass = 3;
        int chPiano = 4;
        int noteKick = 36;
        bool kickLocksBass = true;
        int kickLockMaxMs = 18;

        // Dependencies (not owned)
        HarmonyContext* harmony = nullptr;
        InteractionContext* interaction = nullptr;
        virtuoso::engine::VirtuosoEngine* engine = nullptr;
        const virtuoso::ontology::OntologyRegistry* ontology = nullptr;
        JazzBalladBassPlanner* bassPlanner = nullptr;
        JazzBalladPianoPlanner* pianoPlanner = nullptr;
        BrushesBalladDrummer* drummer = nullptr;
        virtuoso::memory::MotivicMemory* motivicMemory = nullptr;

        // Persistent 4–8 bar story continuity (not owned).
        StoryState* story = nullptr;
    };

    // Schedules a single beat-step worth of musical events.
    static void scheduleStep(const Inputs& in, int stepIndex);
};

} // namespace playback

