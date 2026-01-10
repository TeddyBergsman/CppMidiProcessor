#pragma once

#include <QVector>
#include <QString>
#include <QHash>
#include <QElapsedTimer>
#include <functional>

#include "chart/ChartModel.h"
#include "playback/StoryState.h"
#include "playback/JazzBalladBassPlanner.h"
#include "playback/JazzBalladPianoPlanner.h"
#include "playback/BrushesBalladDrummer.h"
#include "playback/HarmonyContext.h"
#include "playback/InteractionContext.h"
#include "playback/WeightNegotiator.h"
#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/theory/FunctionalHarmony.h"

namespace playback {

/**
 * PrePlaybackCache: Pre-computed musical decisions for zero-latency playback.
 * 
 * ARCHITECTURE PHILOSOPHY (from Product Spec):
 * "If that means we need to pre-plan even for an entire second or more before 
 * a song starts, that's fine - but lag can never happen while the actual music 
 * has started playing."
 * 
 * This cache is computed BEFORE playback starts and contains:
 * 1. All phrase plans for the entire song
 * 2. Multiple energy-level variants for real-time interaction
 * 3. Pre-computed bass/piano/drum decisions for every beat
 * 
 * During playback, the engine only does O(1) lookups - no computation.
 */

// Energy band for branch selection (Module 4.2: Macro-Dynamics)
enum class EnergyBand {
    Simmer = 0,   // 0.0 - 0.25: Very sparse, minimal activity
    Build = 1,    // 0.25 - 0.55: Building energy, more motion
    Climax = 2,   // 0.55 - 0.85: Peak energy, full texture
    CoolDown = 3  // 0.85 - 1.0: Resolving, winding down
};

// A single pre-computed beat decision
struct PreComputedBeat {
    int stepIndex = -1;
    
    // Chosen IDs for each agent
    QString bassId;
    QString pianoId;
    QString drumsId;
    QString costTag;
    
    // Pre-computed plans (ready to schedule directly)
    JazzBalladBassPlanner::BeatPlan bassPlan;
    JazzBalladPianoPlanner::BeatPlan pianoPlan;
    QVector<virtuoso::engine::AgentIntentNote> drumsNotes;
    
    // State snapshots for continuity
    JazzBalladBassPlanner::PlannerState bassStateAfter;
    JazzBalladPianoPlanner::PlannerState pianoStateAfter;
    
    // Register tracking
    int bassCenterMidi = 45;
    int pianoCenterMidi = 72;
    
    // Contextual info for debugging
    QString chordText;
    int barIndex = 0;
    int beatInBar = 0;
    bool phraseEndBar = false;
    
    // Theory context for LibraryWindow live-follow (from PreComputedContext)
    QString chordDefKey;      // e.g., "min7", "dom7" - for chord list selection
    int chordRootPc = 0;      // 0-11 pitch class for root dropdown
    int keyTonicPc = 0;       // 0-11 pitch class for key dropdown
    virtuoso::theory::KeyMode keyMode = virtuoso::theory::KeyMode::Major;  // Key mode (Major/Minor)
    bool chordIsNew = false;  // True on first beat of a new chord
    QString scaleKey;         // e.g., "dorian" - for scale list selection
    QString voicingKey;       // e.g., "piano_shell_37" - for voicing selection
    QString grooveTemplateKey; // Groove template used
};

// A complete song cache with multiple energy branches
struct PrePlaybackCache {
    // Song metadata
    int totalSteps = 0;
    int beatsPerBar = 4;
    int totalBars = 0;
    int phraseBars = 4;
    
    // Pre-computed beats indexed by [stepIndex]
    // Each step has multiple energy variants: [EnergyBand][stepIndex]
    QVector<QVector<PreComputedBeat>> energyBranches;
    
    // Quick access helpers
    const PreComputedBeat* getBeat(int stepIndex, EnergyBand energy) const {
        const int e = static_cast<int>(energy);
        if (e < 0 || e >= energyBranches.size()) return nullptr;
        if (stepIndex < 0 || stepIndex >= energyBranches[e].size()) return nullptr;
        return &energyBranches[e][stepIndex];
    }
    
    // Map energy01 value to EnergyBand (simple, no hysteresis - for cache building)
    static EnergyBand energyToBand(double energy01) {
        if (energy01 < 0.25) return EnergyBand::Simmer;
        if (energy01 < 0.55) return EnergyBand::Build;
        if (energy01 < 0.85) return EnergyBand::Climax;
        return EnergyBand::CoolDown;
    }
    
    // Map energy01 to EnergyBand WITH HYSTERESIS (for runtime playback)
    // This prevents oscillation at boundaries and makes transitions feel smoother
    static EnergyBand energyToBandWithHysteresis(double energy01, EnergyBand currentBand) {
        // Hysteresis margin: must cross threshold by this amount to switch
        constexpr double margin = 0.08;
        
        // Thresholds with hysteresis
        const double simmerToBuild = (currentBand == EnergyBand::Simmer) ? 0.25 + margin : 0.25 - margin;
        const double buildToClimax = (currentBand == EnergyBand::Build) ? 0.55 + margin : 0.55 - margin;
        const double climaxToCoolDown = (currentBand == EnergyBand::Climax) ? 0.85 + margin : 0.85 - margin;
        
        // Determine new band based on hysteresis thresholds
        if (energy01 < simmerToBuild) return EnergyBand::Simmer;
        if (energy01 < buildToClimax) return EnergyBand::Build;
        if (energy01 < climaxToCoolDown) return EnergyBand::Climax;
        return EnergyBand::CoolDown;
    }
    
    bool isValid() const { return totalSteps > 0 && !energyBranches.isEmpty(); }
    void clear() { 
        totalSteps = 0; 
        energyBranches.clear(); 
    }
    
    // Build statistics
    int buildTimeMs = 0;
    int contextBuildMs = 0;  // Time spent on energy-independent context
    int branchBuildMs = 0;   // Time spent on energy-dependent planning
};

/**
 * Pre-computed harmonic context for a single step.
 * This is ENERGY-INDEPENDENT and computed only once, then shared across all branches.
 */
struct PreComputedContext {
    int stepIndex = -1;
    int barIndex = 0;
    int beatInBar = 0;
    
    // Lookahead window (expensive to compute, identical across energy levels)
    bool haveChord = false;
    music::ChordSymbol chord;
    QString chordText;
    bool chordIsNew = false;
    
    // Next chord lookahead
    bool haveNextChord = false;
    music::ChordSymbol nextChord;
    bool nextChanges = false;
    int beatsUntilChange = 0;
    
    // Key/scale analysis (very expensive - involves ontology queries)
    int keyTonicPc = 0;
    virtuoso::theory::KeyMode keyMode = virtuoso::theory::KeyMode::Major;
    QString scaleKey;
    QString scaleName;
    QString roman;
    QString chordFunction;
    
    // Phrase context
    int phraseBars = 4;
    int barInPhrase = 0;
    bool phraseEndBar = false;
    double cadence01 = 0.0;
    
    // Chord definition (cached pointer - valid for song duration)
    const virtuoso::ontology::ChordDef* chordDef = nullptr;
};

/**
 * PrePlaybackBuilder: Builds the complete cache before playback.
 * 
 * This runs synchronously when play() is called (or async when song loads).
 * It may take 500ms-2000ms depending on song length, but that's acceptable
 * because it happens BEFORE any audio starts.
 */
class PrePlaybackBuilder {
public:
    // Progress callback: (currentStep, totalSteps, currentBranch, totalBranches)
    using ProgressCallback = std::function<void(int, int, int, int)>;
    
    struct Inputs {
        const chart::ChartModel* model = nullptr;
        const QVector<int>* sequence = nullptr;
        int repeats = 1;
        int bpm = 120;
        QString stylePresetKey;
        
        // Planners (shared instances for state continuity)
        JazzBalladBassPlanner* bassPlanner = nullptr;
        JazzBalladPianoPlanner* pianoPlanner = nullptr;
        BrushesBalladDrummer* drummer = nullptr;
        
        // Context
        HarmonyContext* harmony = nullptr;
        virtuoso::engine::VirtuosoEngine* engine = nullptr;
        virtuoso::ontology::OntologyRegistry* ontology = nullptr;
        InteractionContext* interaction = nullptr;
        StoryState* story = nullptr;
        
        // Channels
        int chBass = 4;
        int chPiano = 3;
        int chDrums = 6;
        
        // Energy multipliers per agent
        QHash<QString, double> agentEnergyMult;
        
        // Note: Negotiated weights are not used in pre-cache since we don't have 
        // real-time interaction context. Energy levels are pre-computed per branch instead.
    };
    
    // Build the complete cache for all energy levels
    // Optional progress callback receives (currentStep, totalSteps, currentBranch, totalBranches)
    static PrePlaybackCache build(const Inputs& in, ProgressCallback progress = nullptr);
    
private:
    // Phase 1: Build energy-independent harmonic context for all steps (ONCE)
    static QVector<PreComputedContext> buildContexts(const Inputs& in, ProgressCallback progress);
    
    // Phase 2: Build one energy branch using pre-computed contexts
    static QVector<PreComputedBeat> buildBranchFromContexts(
        const Inputs& in, 
        const QVector<PreComputedContext>& contexts,
        double baseEnergy, 
        int branchIndex, 
        int totalBranches,
        ProgressCallback progress);
};

} // namespace playback
