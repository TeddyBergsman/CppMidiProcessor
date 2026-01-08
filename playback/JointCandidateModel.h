#pragma once

#include <QString>
#include <QVector>

#include "playback/BrushesBalladDrummer.h"
#include "playback/JazzBalladBassPlanner.h"
#include "playback/JazzBalladPianoPlanner.h"
#include "music/ChordSymbol.h"
#include "virtuoso/control/VirtuosityMatrix.h"
#include "virtuoso/solver/BeatCostModel.h"

namespace playback {

// Shared candidate generation + scoring for joint (Bass+Piano+Drums) decisions.
// Used by both beat-level scheduling (AgentCoordinator) and phrase-level beam search (JointPhrasePlanner).
class JointCandidateModel final {
public:
    struct NoteStats {
        int count = 0;
        int minMidi = 127;
        int maxMidi = 0;
        double meanMidi = 0.0;
    };

    static NoteStats statsForNotes(const QVector<virtuoso::engine::AgentIntentNote>& notes);

    struct BassCand {
        QString id;
        JazzBalladBassPlanner::Context ctx;
        JazzBalladBassPlanner::BeatPlan plan;
        JazzBalladBassPlanner::PlannerState nextState;
        NoteStats st;
    };
    struct PianoCand {
        QString id;
        JazzBalladPianoPlanner::Context ctx;
        JazzBalladPianoPlanner::BeatPlan plan;
        JazzBalladPianoPlanner::PlannerState nextState;
        NoteStats st;
        // Additional piano-specific costs (embodiment/pedal/topline).
        double pianistFeasibilityCost = 0.0;
        double pedalClarityCost = 0.0;
        double topLineContinuityCost = 0.0;
    };
    struct DrumCand {
        QString id;
        BrushesBalladDrummer::Context ctx;
        QVector<virtuoso::engine::AgentIntentNote> plan;
        NoteStats st;
        bool hasKick = false;
    };

    struct GenerationInputs {
        JazzBalladBassPlanner* bassPlanner = nullptr;   // not owned
        JazzBalladPianoPlanner* pianoPlanner = nullptr; // not owned
        int chBass = 3;
        int chPiano = 4;
        virtuoso::groove::TimeSignature ts{4, 4};

        JazzBalladBassPlanner::Context bcSparse;
        JazzBalladBassPlanner::Context bcBase;
        JazzBalladBassPlanner::Context bcRich;

        JazzBalladPianoPlanner::Context pcSparse;
        JazzBalladPianoPlanner::Context pcBase;
        JazzBalladPianoPlanner::Context pcRich;

        // Start states to restore before generating candidates.
        JazzBalladBassPlanner::PlannerState bassStart;
        JazzBalladPianoPlanner::PlannerState pianoStart;
    };

    static void generateBassPianoCandidates(const GenerationInputs& in,
                                            QVector<BassCand>& outBass,
                                            QVector<PianoCand>& outPiano);

    struct ScoringInputs {
        virtuoso::groove::TimeSignature ts{4, 4};
        music::ChordSymbol chord;
        int beatInBar = 0;
        double cadence01 = 0.0;
        bool phraseSetupBar = false;
        bool phraseEndBar = false;
        bool userBusy = false;
        bool userSilence = false;

        int prevBassCenterMidi = 45;
        int prevPianoCenterMidi = 72;

        virtuoso::control::VirtuosityMatrix virtAvg{};
        virtuoso::solver::CostWeights weights{};

        // Transition penalties (phrase planner can set these; beat planner can leave defaults).
        QString lastBassId;
        QString lastPianoId;
        QString lastDrumsId;
        double bassSwitchPenalty = 0.20;
        double pianoSwitchPenalty = 0.15;
        double drumsSwitchPenalty = 0.10;

        // Hive-mind response bias (phrase planner).
        bool inResponse = false;
        double responseWetBonus = 0.25;
        double responsePianoRichBonus = 0.18;
        double responseBassRichBonus = 0.08;

        // Piano library continuity (session-player coherence).
        QString lastPianoCompPhraseId;
        QString lastPianoTopLinePhraseId;
        QString lastPianoPedalId;
        QString lastPianoGestureId;
        double pianoCompPhraseSwitchPenalty = 0.10;
        double pianoTopLinePhraseSwitchPenalty = 0.08;
        double pianoPedalSwitchPenalty = 0.05;
        double pianoGestureSwitchPenalty = 0.03;
    };

    struct ComboEval {
        int bi = 0;
        int pi = 0;
        int di = 0;
        QString bassId;
        QString pianoId;
        QString drumsId;
        double cost = 0.0;
        double pianoExtraCost = 0.0;
        virtuoso::solver::CostBreakdown bd{};
    };

    struct BestChoice {
        int bestBi = 0;
        int bestPi = 0;
        int bestDi = 0;
        double bestCost = 0.0;
        virtuoso::solver::CostBreakdown bestBd{};
        QVector<ComboEval> combos; // full cartesian product evaluation
    };

    // Evaluate all combinations and pick best (or follow a planned id triple).
    static BestChoice chooseBestCombo(const ScoringInputs& in,
                                      const QVector<BassCand>& bass,
                                      const QVector<PianoCand>& piano,
                                      const QVector<DrumCand>& drums,
                                      const QString& plannedBassId = {},
                                      const QString& plannedPianoId = {},
                                      const QString& plannedDrumsId = {});

private:
    static double spacingPenalty(const QVector<virtuoso::engine::AgentIntentNote>& bassNotes,
                                 const QVector<virtuoso::engine::AgentIntentNote>& pianoNotes);
};

} // namespace playback

