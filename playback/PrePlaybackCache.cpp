#include "playback/PrePlaybackCache.h"

#include "playback/LookaheadWindow.h"
#include "playback/BalladReferenceTuning.h"
#include "playback/JointPhrasePlanner.h"
#include "playback/ChordScaleTable.h"
#include "playback/KeyAnalyzer.h"
#include "virtuoso/util/StableHash.h"
#include "virtuoso/groove/GrooveGrid.h"

#include <QElapsedTimer>
#include <QtGlobal>
#include <QtConcurrent>
#include <QFuture>
#include <QMutex>

namespace playback {
namespace {

static virtuoso::groove::TimeSignature timeSigFromModel(const chart::ChartModel& model) {
    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (model.timeSigNum > 0) ? model.timeSigNum : 4;
    ts.den = (model.timeSigDen > 0) ? model.timeSigDen : 4;
    return ts;
}

static int adaptivePhraseBars(int bpm) {
    return (bpm <= 84) ? 8 : 4;
}

static int clampBassCenterMidi(int v) { return qBound(28, v, 67); }
static int clampPianoCenterMidi(int v) { return qBound(48, v, 96); }

} // namespace

PrePlaybackCache PrePlaybackBuilder::build(const Inputs& in, ProgressCallback progress) {
    QElapsedTimer buildTimer;
    buildTimer.start();
    
    PrePlaybackCache cache;
    
    if (!in.model || !in.sequence || in.sequence->isEmpty()) {
        qWarning() << "PrePlaybackBuilder::build - invalid inputs";
        return cache;
    }
    if (!in.harmony || !in.bassPlanner || !in.pianoPlanner || !in.drummer) {
        qWarning() << "PrePlaybackBuilder::build - missing planners";
        return cache;
    }
    
    const auto ts = timeSigFromModel(*in.model);
    cache.beatsPerBar = qMax(1, ts.num);
    cache.phraseBars = adaptivePhraseBars(in.bpm);
    cache.totalSteps = in.sequence->size() * qMax(1, in.repeats);
    cache.totalBars = cache.totalSteps / cache.beatsPerBar;
    
    qInfo().noquote() << QString("PrePlaybackBuilder: Building cache for %1 steps (%2 bars) at %3 bpm...")
        .arg(cache.totalSteps).arg(cache.totalBars).arg(in.bpm);
    
    // =========================================================================
    // OPTIMIZATION: Two-Phase Build
    // Phase 1: Compute energy-INDEPENDENT harmonic context ONCE for all steps
    // Phase 2: Build energy-DEPENDENT agent plans using shared context
    // This saves ~60% of computation time by avoiding redundant harmony analysis
    // =========================================================================
    
    QElapsedTimer contextTimer;
    contextTimer.start();
    
    qInfo().noquote() << "  Phase 1: Building harmonic context (energy-independent)...";
    auto contexts = buildContexts(in, progress);
    cache.contextBuildMs = static_cast<int>(contextTimer.elapsed());
    qInfo().noquote() << QString("    Context built in %1ms (%2 steps)")
        .arg(cache.contextBuildMs).arg(contexts.size());
    
    // Build 4 energy branches: Simmer (0.15), Build (0.40), Climax (0.70), CoolDown (0.92)
    const QVector<double> energyLevels = {0.15, 0.40, 0.70, 0.92};
    const int totalBranches = energyLevels.size();
    
    QElapsedTimer branchPhaseTimer;
    branchPhaseTimer.start();
    
    // =========================================================================
    // OPTIMIZATION: Parallel Branch Building
    // Each energy branch is independent - compute all 4 in parallel!
    // Each thread creates its own planner instances (thread-safe).
    // =========================================================================
    qInfo().noquote() << QString("  Phase 2: Building %1 energy branches in PARALLEL...").arg(totalBranches);
    
    // Mutex for progress callback (UI updates must be thread-safe)
    QMutex progressMutex;
    
    // Use QtConcurrent to build all branches in parallel
    QVector<QFuture<QVector<PreComputedBeat>>> futures;
    futures.reserve(totalBranches);
    
    for (int bi = 0; bi < totalBranches; ++bi) {
        const double energy = energyLevels[bi];
        
        // Launch async branch build
        futures.append(QtConcurrent::run([&in, &contexts, energy, bi, totalBranches, progress, &progressMutex]() {
            QElapsedTimer branchTimer;
            branchTimer.start();
            
            // Thread-safe progress wrapper
            auto threadSafeProgress = [&progressMutex, progress](int step, int total, int branch, int branches) {
                if (!progress) return;
                QMutexLocker lock(&progressMutex);
                progress(step, total, branch, branches);
            };
            
            auto branch = buildBranchFromContexts(in, contexts, energy, bi, totalBranches, threadSafeProgress);
            
            qInfo().noquote() << QString("    Branch %1 (energy=%.2f) completed in %2ms")
                .arg(bi + 1).arg(energy).arg(branchTimer.elapsed());
            
            return branch;
        }));
    }
    
    // Wait for all branches and collect results (maintaining order)
    cache.energyBranches.reserve(totalBranches);
    for (int bi = 0; bi < totalBranches; ++bi) {
        cache.energyBranches.append(futures[bi].result());
    }
    
    cache.branchBuildMs = static_cast<int>(branchPhaseTimer.elapsed());
    cache.buildTimeMs = static_cast<int>(buildTimer.elapsed());
    
    qInfo().noquote() << QString("PrePlaybackBuilder: Complete! Context=%1ms, Branches=%2ms, Total=%3ms")
        .arg(cache.contextBuildMs).arg(cache.branchBuildMs).arg(cache.buildTimeMs);
    
    return cache;
}

// =========================================================================
// Phase 1: Build energy-independent harmonic context for all steps
// This includes: chord parsing, key estimation, scale selection, functional analysis
// =========================================================================
QVector<PreComputedContext> PrePlaybackBuilder::buildContexts(const Inputs& in, ProgressCallback progress) {
    QVector<PreComputedContext> contexts;
    
    const QVector<int>& seq = *in.sequence;
    const auto ts = timeSigFromModel(*in.model);
    const int beatsPerBar = qMax(1, ts.num);
    const int totalSteps = seq.size() * qMax(1, in.repeats);
    const int phraseBars = adaptivePhraseBars(in.bpm);
    
    contexts.reserve(totalSteps);
    
    // ===========================================================================
    // KEY DETECTION: Use pattern-based analysis (ii-V-I detection) instead of
    // the 8-bar averaging approach. This gives PRECISE key boundaries.
    // ===========================================================================
    KeyAnalyzer keyAnalyzer(*in.ontology);
    const QVector<KeyRegion> keyRegions = keyAnalyzer.analyze(*in.model);
    qInfo() << "KeyAnalyzer: Detected" << keyRegions.size() << "key region(s)";
    
    // Progress: context building is "branch 0" in progress reporting
    const int progressInterval = qMax(1, beatsPerBar * 4);
    
    // Save harmony state to restore after context building
    const auto savedHarmony = in.harmony->saveRuntimeState();
    
    for (int stepIndex = 0; stepIndex < totalSteps; ++stepIndex) {
        // Report progress (phase 0 = context building)
        if (progress && (stepIndex % progressInterval == 0)) {
            progress(stepIndex, totalSteps, -1, 4);  // -1 indicates context phase
        }
        
        PreComputedContext ctx;
        ctx.stepIndex = stepIndex;
        ctx.barIndex = stepIndex / beatsPerBar;
        ctx.beatInBar = stepIndex % beatsPerBar;
        
        // Build lookahead window (still used for chord parsing, phrase structure, etc.)
        auto look = buildLookaheadWindow(*in.model, seq, in.repeats, stepIndex,
                                         /*horizonBars=*/8, phraseBars, /*keyWindowBars=*/8,
                                         *in.harmony);
        
        ctx.haveChord = look.haveCurrentChord && !look.currentChord.noChord;
        ctx.chord = look.currentChord;
        ctx.chordText = look.currentChord.originalText.trimmed();
        ctx.chordIsNew = look.chordIsNew;
        ctx.haveNextChord = look.haveNextChord;
        ctx.nextChord = look.nextChord;
        ctx.nextChanges = look.nextChanges;
        ctx.beatsUntilChange = look.beatsUntilChange;
        ctx.phraseBars = look.phraseBars;
        ctx.barInPhrase = look.barInPhrase;
        ctx.phraseEndBar = look.phraseEndBar;
        ctx.cadence01 = look.cadence01;
        
        // ===========================================================================
        // OVERRIDE KEY: Use KeyAnalyzer result instead of 8-bar averaging!
        // This provides PRECISE key boundaries based on cadence pattern detection.
        // ===========================================================================
        const int chartBarIndex = ctx.barIndex % (seq.size() / beatsPerBar);  // Handle repeats
        const KeyRegion keyRegion = KeyAnalyzer::keyAtBar(keyRegions, chartBarIndex);
        ctx.keyTonicPc = keyRegion.tonicPc;
        ctx.keyMode = keyRegion.mode;
        
        // Cache chord definition (pointer is valid for song duration)
        if (ctx.haveChord) {
            ctx.chordDef = in.harmony->chordDefForSymbol(ctx.chord);
            
            // Get CHORD-SPECIFIC scale and functional analysis
            // PERF: Use pre-computed ChordScaleTable for O(1) lookup
            if (ctx.chordDef && ctx.chord.rootPc >= 0) {
                const auto* cached = ChordScaleTable::lookup(
                    *ctx.chordDef, ctx.chord.rootPc, ctx.keyTonicPc, ctx.keyMode);
                
                if (cached) {
                    // O(1) lookup hit!
                    ctx.scaleKey = cached->scaleKey;
                    ctx.scaleName = cached->scaleName;
                    ctx.roman = cached->roman;
                    ctx.chordFunction = cached->function;
                } else {
                    // Fallback to runtime computation (should rarely happen)
                    QString roman;
                    QString func;
                    const auto scaleChoice = in.harmony->chooseScaleForChord(
                        ctx.keyTonicPc, ctx.keyMode, ctx.chord, *ctx.chordDef, &roman, &func);
                    ctx.scaleKey = scaleChoice.key;
                    ctx.scaleName = scaleChoice.name;
                    ctx.roman = roman;
                    ctx.chordFunction = func;
                }
            } else {
                // Fallback to key's scale if no chord-specific choice available
                ctx.scaleKey = look.key.scaleKey;
                ctx.scaleName = look.key.scaleName;
            }
        } else {
            // No chord - use key's default scale
            ctx.scaleKey = look.key.scaleKey;
            ctx.scaleName = look.key.scaleName;
        }
        
        contexts.append(ctx);
    }
    
    // Restore harmony state
    in.harmony->restoreRuntimeState(savedHarmony);
    
    return contexts;
}

// =========================================================================
// Phase 2: Build energy-dependent agent plans using pre-computed contexts
// This is much faster since all harmony analysis is already done
// 
// THREAD-SAFETY: This function creates LOCAL planner instances so it can
// be called from multiple threads in parallel without data races.
// =========================================================================
QVector<PreComputedBeat> PrePlaybackBuilder::buildBranchFromContexts(
    const Inputs& in, 
    const QVector<PreComputedContext>& contexts,
    double baseEnergy,
    int branchIndex, 
    int totalBranches,
    ProgressCallback progress) {
    
    QVector<PreComputedBeat> branch;
    
    const QVector<int>& seq = *in.sequence;
    const auto ts = timeSigFromModel(*in.model);
    const int beatsPerBar = qMax(1, ts.num);
    const int totalSteps = contexts.size();
    const int phraseBars = adaptivePhraseBars(in.bpm);
    
    branch.reserve(totalSteps);
    
    // Progress reporting interval
    const int progressInterval = qMax(1, beatsPerBar * 4);
    
    // =========================================================================
    // PARALLEL OPTIMIZATION: Create LOCAL planners for thread-safety
    // Each parallel branch gets its own planner instances, avoiding data races.
    // =========================================================================
    JazzBalladBassPlanner localBassPlanner;
    JazzBalladPianoPlanner localPianoPlanner;
    BrushesBalladDrummer localDrummer;
    
    localBassPlanner.reset();
    localPianoPlanner.reset();
    
    // Determinism seed
    const quint32 detSeed = virtuoso::util::StableHash::fnv1a32(
        (QString("ballad|") + in.stylePresetKey).toUtf8());
    
    // Track register centers
    int lastBassCenterMidi = 45;
    int lastPianoCenterMidi = 72;
    
    // Get reference tuning
    const BalladRefTuning tune = tuningForReferenceTrack(in.stylePresetKey);
    
    // Compute each beat using PRE-COMPUTED context (no harmony re-analysis!)
    for (int stepIndex = 0; stepIndex < totalSteps; ++stepIndex) {
        // Report progress periodically (every 4 bars)
        if (progress && (stepIndex % progressInterval == 0)) {
            progress(stepIndex, totalSteps, branchIndex, totalBranches);
        }
        
        // Get pre-computed context (replaces expensive buildLookaheadWindow call!)
        const PreComputedContext& ctx = contexts[stepIndex];
        
        PreComputedBeat beat;
        beat.stepIndex = stepIndex;
        beat.barIndex = ctx.barIndex;
        beat.beatInBar = ctx.beatInBar;
        
        if (!ctx.haveChord) {
            // No chord - emit empty beat
            branch.append(beat);
            continue;
        }
        
        beat.chordText = ctx.chordText;
        beat.phraseEndBar = ctx.phraseEndBar;
        
        // Populate theory context for LibraryWindow live-follow
        beat.chordDefKey = ctx.chordDef ? ctx.chordDef->key : QString();
        beat.chordRootPc = ctx.chord.rootPc;
        beat.keyTonicPc = ctx.keyTonicPc;
        beat.keyMode = ctx.keyMode;
        beat.chordIsNew = ctx.chordIsNew;
        beat.scaleKey = ctx.scaleKey;
        beat.grooveTemplateKey = in.stylePresetKey; // Use style preset as groove template
        
        // Extract voicing key from piano planner's first note if available (populated after planning)
        // We'll set this after the piano plan is generated
        
        // Use pre-computed values (no ontology queries needed!)
        const bool structural = (ctx.beatInBar == 0 || ctx.beatInBar == 2) || ctx.chordIsNew;
        const double cadence01 = ctx.cadence01;
        const bool phraseSetupBar = (ctx.phraseBars > 1) ? (ctx.barInPhrase == (ctx.phraseBars - 2)) : false;
        
        // Apply energy scaling (this IS energy-dependent)
        const double bassEnergy = qBound(0.0, baseEnergy * in.agentEnergyMult.value("Bass", 1.0), 1.0);
        const double pianoEnergy = qBound(0.0, baseEnergy * in.agentEnergyMult.value("Piano", 1.0), 1.0);
        const double drumsEnergy = qBound(0.0, baseEnergy * in.agentEnergyMult.value("Drums", 1.0), 1.0);
        
        // --- Build Bass Context (using pre-computed harmony) ---
        JazzBalladBassPlanner::Context bc;
        bc.bpm = in.bpm;
        bc.playbackBarIndex = ctx.barIndex;
        bc.beatInBar = ctx.beatInBar;
        bc.chordIsNew = ctx.chordIsNew;
        bc.chord = ctx.chord;
        bc.hasNextChord = ctx.haveNextChord && !ctx.nextChord.noChord;
        bc.nextChord = ctx.nextChord;
        bc.chordText = ctx.chordText;
        bc.phraseBars = ctx.phraseBars;
        bc.barInPhrase = ctx.barInPhrase;
        bc.phraseEndBar = ctx.phraseEndBar;
        bc.cadence01 = cadence01;
        bc.registerCenterMidi = clampBassCenterMidi(lastBassCenterMidi);
        bc.determinismSeed = detSeed;
        bc.approachProbBeat3 = tune.bassApproachProbBeat3;
        bc.skipBeat3ProbStable = tune.bassSkipBeat3ProbStable;
        bc.allowApproachFromAbove = tune.bassAllowApproachFromAbove;
        bc.userDensityHigh = false;
        bc.userIntensityPeak = (baseEnergy >= 0.85);
        bc.chordFunction = ctx.chordFunction;
        bc.roman = ctx.roman;
        bc.userSilence = false;
        bc.forceClimax = (baseEnergy >= 0.85);
        bc.energy = bassEnergy;
        
        // --- Build Piano Context (using pre-computed harmony) ---
        JazzBalladPianoPlanner::Context pc;
        pc.bpm = in.bpm;
        pc.playbackBarIndex = ctx.barIndex;
        pc.beatInBar = ctx.beatInBar;
        pc.chordIsNew = ctx.chordIsNew;
        pc.chord = ctx.chord;
        pc.chordText = ctx.chordText;
        pc.phraseBars = ctx.phraseBars;
        pc.barInPhrase = ctx.barInPhrase;
        pc.phraseEndBar = ctx.phraseEndBar;
        pc.cadence01 = cadence01;
        pc.hasKey = true;
        pc.keyTonicPc = ctx.keyTonicPc;
        pc.keyMode = ctx.keyMode;
        pc.hasNextChord = ctx.haveNextChord && !ctx.nextChord.noChord;
        pc.nextChord = ctx.nextChord;
        pc.nextChanges = ctx.nextChanges;
        pc.beatsUntilChordChange = ctx.beatsUntilChange;
        pc.determinismSeed = detSeed ^ 0xBADC0FFEu;
        pc.rhLo = tune.pianoRhLo; pc.rhHi = tune.pianoRhHi;
        pc.lhLo = tune.pianoLhLo; pc.lhHi = tune.pianoLhHi;
        pc.skipBeat2ProbStable = tune.pianoSkipBeat2ProbStable;
        pc.addSecondColorProb = tune.pianoAddSecondColorProb;
        pc.sparkleProbBeat4 = tune.pianoSparkleProbBeat4;
        pc.preferShells = tune.pianoPreferShells;
        pc.userDensityHigh = false;
        pc.userIntensityPeak = (baseEnergy >= 0.85);
        pc.userRegisterHigh = false;
        pc.userSilence = false;
        pc.userBusy = false;
        pc.forceClimax = (baseEnergy >= 0.85);
        pc.energy = pianoEnergy;
        
        // --- Build Drums Context (using pre-computed harmony) ---
        BrushesBalladDrummer::Context dc;
        dc.bpm = in.bpm;
        dc.ts = ts;
        dc.playbackBarIndex = ctx.barIndex;
        dc.beatInBar = ctx.beatInBar;
        dc.structural = structural;
        dc.determinismSeed = detSeed ^ 0xD00D'BEEFu;
        dc.phraseBars = ctx.phraseBars;
        dc.barInPhrase = ctx.barInPhrase;
        dc.phraseEndBar = ctx.phraseEndBar;
        dc.cadence01 = cadence01;
        dc.energy = drumsEnergy;
        dc.intensityPeak = (baseEnergy >= 0.85);
        
        // --- Generate Plans (using LOCAL thread-safe planners) ---
        // Bass plan
        beat.bassPlan = localBassPlanner.planBeatWithActions(bc, in.chBass, ts);
        beat.bassStateAfter = localBassPlanner.snapshotState();
        if (!beat.bassPlan.notes.isEmpty()) {
            // Update register center based on what was played
            int sum = 0;
            for (const auto& n : beat.bassPlan.notes) sum += n.note;
            lastBassCenterMidi = clampBassCenterMidi(sum / beat.bassPlan.notes.size());
        }
        beat.bassCenterMidi = lastBassCenterMidi;
        beat.bassId = beat.bassPlan.notes.isEmpty() ? "rest" : "base";
        
        // Piano plan
        beat.pianoPlan = localPianoPlanner.planBeatWithActions(pc, in.chPiano, ts);
        beat.pianoStateAfter = localPianoPlanner.snapshotState();
        if (!beat.pianoPlan.notes.isEmpty()) {
            int sum = 0;
            for (const auto& n : beat.pianoPlan.notes) sum += n.note;
            lastPianoCenterMidi = clampPianoCenterMidi(sum / beat.pianoPlan.notes.size());
            // Extract voicing key from the piano plan
            beat.voicingKey = beat.pianoPlan.chosenVoicingKey;
        }
        beat.pianoCenterMidi = lastPianoCenterMidi;
        beat.pianoId = beat.pianoPlan.notes.isEmpty() ? "rest" : "base";
        
        // Drums plan
        beat.drumsNotes = localDrummer.planBeat(dc);
        beat.drumsId = beat.drumsNotes.isEmpty() ? "rest" : "base";
        
        beat.costTag = QString("pre|e%1").arg(baseEnergy, 0, 'f', 2);
        
        branch.append(beat);
    }
    
    return branch;
}

} // namespace playback
