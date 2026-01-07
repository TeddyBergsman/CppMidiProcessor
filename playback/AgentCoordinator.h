#pragma once

#include <QHash>
#include <QString>

#include "music/ChordSymbol.h"
#include "playback/HarmonyContext.h"
#include "playback/InteractionContext.h"
#include "playback/JazzBalladBassPlanner.h"
#include "playback/JazzBalladPianoPlanner.h"
#include "playback/BrushesBalladDrummer.h"
#include "playback/StoryState.h"
#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/groove/GrooveGrid.h"
#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/memory/MotivicMemory.h"

namespace playback {

class VirtuosoBalladMvpPlaybackEngine;

// AgentCoordinator: owns the per-step agent scheduling policy (which agents act, when, and how),
// bridging HarmonyContext + InteractionContext into concrete AgentIntentNotes scheduled into VirtuosoEngine.
class AgentCoordinator final {
public:
    struct Inputs {
        // Owner (for emitting debug signals)
        VirtuosoBalladMvpPlaybackEngine* owner = nullptr; // not owned

        // Core environment
        const chart::ChartModel* model = nullptr;   // not owned
        const QVector<int>* sequence = nullptr;     // not owned
        int repeats = 1;

        // Runtime clocks/config
        int bpm = 120;
        QString stylePresetKey;
        QHash<QString, double> agentEnergyMult;

        // Virtuosity controls
        bool virtAuto = true;
        double virtHarmonicRisk = 0.20;
        double virtRhythmicComplexity = 0.25;
        double virtInteraction = 0.50;
        double virtToneDark = 0.60;

        // Debug controls
        bool debugEnergyAuto = true;
        double debugEnergy = 0.25;

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

