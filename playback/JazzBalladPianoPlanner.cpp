#include "playback/JazzBalladPianoPlanner.h"
#include "playback/VoicingUtils.h"

#include "virtuoso/util/StableHash.h"

#include <QtGlobal>
#include <algorithm>

// Namespace alias for cleaner code
namespace vu = playback::voicing_utils;

namespace playback {

namespace {

static int clampMidi(int m) { return qBound(0, m, 127); }
static int normalizePc(int pc) { return ((pc % 12) + 12) % 12; }

// =============================================================================
// ENERGY-DERIVED WEIGHT HELPERS
// These replace direct weights access with energy-based values
// =============================================================================
static double energyToTension(double energy) {
    return 0.3 + 0.5 * qBound(0.0, energy, 1.0);
}
static double energyToCreativity(double energy) {
    return 0.35 + 0.25 * qBound(0.0, energy, 1.0);
}
static double energyToDensity(double energy) {
    return 0.3 + 0.5 * qBound(0.0, energy, 1.0);
}
static double energyToRhythm(double energy) {
    return 0.35 + 0.35 * qBound(0.0, energy, 1.0);
}
static double energyToEmotion(double /*energy*/) {
    return 0.55;  // Always moderately expressive
}
static double energyToWarmth(double /*energy*/) {
    return 0.55;  // Always warm tone
}
static double energyToIntensity(double energy) {
    return qBound(0.0, energy, 1.0);
}
static double energyToInteractivity(double /*energy*/) {
    return 1.0;  // Always maximally interactive
}
static double energyToVariability(double energy) {
    return 0.4 + 0.3 * qBound(0.0, energy, 1.0);
}

// A voicing template defines the structure of a voicing type.
// Each voicing has degrees stacked from bottom to top.
struct VoicingTemplate {
    QString name;
    QVector<int> degrees;  // Chord degrees from bottom to top (e.g., {3,5,7,9})
    int bottomDegree;      // Which degree is at the bottom
    bool rootless;         // True if root should be omitted
};

// Build voicing templates for different chord types
// NOTE: Template names are ontology keys for direct Library window matching
QVector<VoicingTemplate> getVoicingTemplates(bool hasSeventh, bool is6thChord) {
    QVector<VoicingTemplate> templates;

    if (hasSeventh || is6thChord) {
        // Type A: 3-5-7-9 (start from 3rd, stack upward)
        templates.push_back({"piano_rootless_a", {3, 5, 7, 9}, 3, true});

        // Type B: 7-9-3-5 (start from 7th, 3 and 5 are inverted up)
        templates.push_back({"piano_rootless_b", {7, 9, 3, 5}, 7, true});

        // Shell: just 3-7 (guide tones)
        templates.push_back({"piano_guide_3_7", {3, 7}, 3, true});

        // Quartal: 3-7-9
        templates.push_back({"piano_quartal_3", {3, 7, 9}, 3, true});
    } else {
        // Triads
        templates.push_back({"piano_triad_root", {1, 3, 5}, 1, false});
        templates.push_back({"piano_triad_first_inv", {3, 5, 1}, 3, false});
    }

    return templates;
}

// =============================================================================
// SINGING MELODY LINE TARGET
// Calculate the ideal next melody note for a voice-led, expressive line
// =============================================================================
struct SingingMelodyTarget {
    int midiNote;
    int degree;                 // What chord degree this represents
    double expressiveness;      // How expressive/emotional this choice is
    bool isResolution;          // Is this a resolution to a stable tone?
    bool isLeapTarget;          // Does this require a leap (more dramatic)?
};

// Find the best melody target that creates a singing, voice-led line
SingingMelodyTarget findSingingMelodyTarget(
    int lastMelodyMidi, 
    int lastMelodyDirection,
    const music::ChordSymbol& chord,
    int rhLo, int rhHi,
    int phraseArcPhase,         // 0=building, 1=peak, 2=resolving
    double energy,
    bool isPhrasePeak,
    bool isPhraseEnd) {
    
    SingingMelodyTarget best;
    best.midiNote = lastMelodyMidi;
    best.degree = 3;
    best.expressiveness = 0.0;
    best.isResolution = false;
    best.isLeapTarget = false;
    
    // Get chord tones - inline calculation to avoid private member access
    auto pcForDegreeLocal = [&](int deg) -> int {
        if (deg == 3) {
            // 3rd: major/augmented = 4 semitones, minor/diminished = 3 semitones
            bool isMinor = (chord.quality == music::ChordQuality::Minor ||
                           chord.quality == music::ChordQuality::Diminished ||
                           chord.quality == music::ChordQuality::HalfDiminished);
            return normalizePc(chord.rootPc + (isMinor ? 3 : 4));
        }
        if (deg == 5) {
            // 5th: diminished = 6, augmented = 8, otherwise = 7
            if (chord.quality == music::ChordQuality::Diminished ||
                chord.quality == music::ChordQuality::HalfDiminished) {
                return normalizePc(chord.rootPc + 6);
            }
            if (chord.quality == music::ChordQuality::Augmented) {
                return normalizePc(chord.rootPc + 8);
            }
            return normalizePc(chord.rootPc + 7);
        }
        if (deg == 7) {
            // 7th: Only return if chord has a 7th
            // Don't add 7th to plain triads or 6th chords
            const bool has7th = (chord.seventh != music::SeventhQuality::None);
            const bool is6thChord = (chord.extension == 6 && chord.seventh == music::SeventhQuality::None);
            
            if (!has7th || is6thChord) return -1;  // No 7th on this chord
            
            if (chord.quality == music::ChordQuality::Major) {
                return normalizePc(chord.rootPc + 11);  // Major 7th
            }
            if (chord.quality == music::ChordQuality::Diminished) {
                return normalizePc(chord.rootPc + 9);   // Diminished 7th
            }
            return normalizePc(chord.rootPc + 10);  // Minor/dominant 7th
        }
        if (deg == 9) {
            // 9th is ONLY safe on specific chord types:
            // - Dominant 7ths (natural 9 or b9 on altered)
            // - Minor 7ths (dorian sound)
            // - Explicit 9th/11th/13th chords
            // AVOID on major7, 6th chords, and plain triads
            const bool is6thChord = (chord.extension == 6 && chord.seventh == music::SeventhQuality::None);
            const bool isDominant = (chord.quality == music::ChordQuality::Dominant);
            const bool isMinor7 = (chord.quality == music::ChordQuality::Minor && 
                                   chord.seventh != music::SeventhQuality::None);
            const bool hasExplicit9 = (chord.extension >= 9);
            
            if (is6thChord) return -1;  // Never on 6th chords
            if (!isDominant && !isMinor7 && !hasExplicit9) return -1;
            
            // Altered dominant uses b9
            if (chord.alt && isDominant) {
                return normalizePc(chord.rootPc + 1);  // b9
            }
            return normalizePc(chord.rootPc + 2);  // Natural 9
        }
        return -1;
    };
    
    int third = pcForDegreeLocal(3);
    int fifth = pcForDegreeLocal(5);
    int seventh = pcForDegreeLocal(7);
    int ninth = pcForDegreeLocal(9);  // May return -1 for inappropriate chords
    
    // Candidates: prefer stepwise motion (1-2 semitones)
    // Guide tones (3, 7) are most expressive
    // 9th adds color for building phrases
    // 5th is stable for resolution
    
    struct Candidate {
        int pc;
        int degree;
        double baseScore;
    };
    QVector<Candidate> candidates;
    
    // Root is always a safe option (lower score = last resort)
    int root = chord.rootPc;
    
    // Prioritize based on phrase arc
    if (phraseArcPhase == 2 || isPhraseEnd) {
        // Resolving: prefer stable tones (3rd, 5th, root)
        if (third >= 0) candidates.push_back({third, 3, 3.0});
        if (fifth >= 0) candidates.push_back({fifth, 5, 2.5});
        if (root >= 0) candidates.push_back({root, 1, 2.0});  // Root is stable
        if (seventh >= 0) candidates.push_back({seventh, 7, 1.5});
    } else if (phraseArcPhase == 1 || isPhrasePeak) {
        // Peak: prefer expressive tones (7th, 9th)
        if (seventh >= 0) candidates.push_back({seventh, 7, 3.0});
        if (ninth >= 0) candidates.push_back({ninth, 9, 2.8});
        if (third >= 0) candidates.push_back({third, 3, 2.0});
        if (fifth >= 0) candidates.push_back({fifth, 5, 1.5});
        if (root >= 0) candidates.push_back({root, 1, 1.0});  // Root as fallback
    } else {
        // Building: balanced, with slight preference for movement
        if (third >= 0) candidates.push_back({third, 3, 2.5});
        if (seventh >= 0) candidates.push_back({seventh, 7, 2.3});
        if (ninth >= 0 && energy > 0.3) candidates.push_back({ninth, 9, 2.0});
        if (fifth >= 0) candidates.push_back({fifth, 5, 1.8});
        if (root >= 0) candidates.push_back({root, 1, 1.2});  // Root as fallback
    }
    
    // SAFETY: Always have at least the root
    if (candidates.isEmpty() && root >= 0) {
        candidates.push_back({root, 1, 1.0});
    }
    
    // ULTIMATE FALLBACK: If still no candidates (broken chord), use C (0)
    if (candidates.isEmpty()) {
        candidates.push_back({0, 1, 0.5});  // C as emergency fallback
    }
    
    double bestScore = -999.0;
    
    for (const auto& cand : candidates) {
        // Find the nearest MIDI note to last melody
        for (int oct = 5; oct <= 7; ++oct) {
            int midi = cand.pc + 12 * oct;
            if (midi < rhLo || midi > rhHi) continue;
            
            int motion = midi - lastMelodyMidi;
            int absMotion = qAbs(motion);
            
            double score = cand.baseScore;
            
            // SINGING LINE: Prefer stepwise motion (1-3 semitones)
            if (absMotion == 1 || absMotion == 2) {
                score += 2.0;  // Perfect stepwise - beautiful!
            } else if (absMotion == 3 || absMotion == 4) {
                score += 1.0;  // Small interval - still good
            } else if (absMotion == 0) {
                score += 0.5;  // Holding - OK for emphasis
            } else if (absMotion <= 7) {
                score += 0.0;  // Larger interval - neutral
            } else {
                score -= 1.0;  // Large leap - use sparingly
            }
            
            // Prefer continuing in same direction (melodic momentum)
            if (lastMelodyDirection != 0) {
                bool sameDir = (lastMelodyDirection > 0 && motion > 0) ||
                               (lastMelodyDirection < 0 && motion < 0);
                if (sameDir) score += 0.5;
            }
            
            // Boundary handling: reverse at extremes
            if (midi >= rhHi - 3 && motion > 0) score -= 1.0;
            if (midi <= rhLo + 3 && motion < 0) score -= 1.0;
            
            // Sweet spot bonus (around C5-G5 for singing quality)
            if (midi >= 72 && midi <= 79) score += 0.3;
            
            if (score > bestScore) {
                bestScore = score;
                best.midiNote = midi;
                best.degree = cand.degree;
                best.expressiveness = score;
                best.isResolution = (cand.degree == 3 || cand.degree == 5) && absMotion <= 2;
                best.isLeapTarget = absMotion > 4;
            }
        }
    }
    
    return best;
}

// =============================================================================
// BROKEN TIME FEEL
// Calculate timing variations that create a fluid, breathing rhythm
// Not random - based on musical phrase position and emotional intent
// =============================================================================
struct BrokenTimeFeel {
    int timingOffsetMs;         // Milliseconds to shift (positive = late, negative = early)
    double velocityMult;        // Velocity multiplier for dynamic shaping
    double durationMult;        // Duration multiplier for articulation
    bool isBreath;              // Is this a breath moment (longer, softer)?
};

BrokenTimeFeel calculateBrokenTimeFeel(
    int beatInBar,
    int subBeat,                // 0-3 for 16th notes
    int phraseArcPhase,
    double energy,
    int bpm,
    bool isChordChange,
    bool isPhrasePeak,
    bool isPhraseEnd) {
    
    BrokenTimeFeel feel;
    feel.timingOffsetMs = 0;
    feel.velocityMult = 1.0;
    feel.durationMult = 1.0;
    feel.isBreath = false;
    
    // ==========================================================================
    // ENERGY-AWARE TIMING: 
    // - Low energy: More rubato, breathing, laid-back feel
    // - High energy: LOCKED TO GRID - driving, metronomic, forward momentum!
    // This is counterintuitive but correct for jazz piano.
    // ==========================================================================
    
    // Grid lock factor: 0.0 at low energy (more rubato), 1.0 at high (locked)
    const double gridLock = energy;  // Direct correlation
    
    // Rubato multiplier: inverse of grid lock
    const double rubatoMult = 1.0 - 0.8 * gridLock;  // 1.0 at e=0, 0.2 at e=1
    
    // Tempo factor reduced: subtle rubato even at slow tempos
    double tempoFactor = ((bpm < 70) ? 1.2 : 1.0) * rubatoMult;
    
    // PHRASE BREATHING: Only at low energy - at high energy, keep pushing!
    if (isPhraseEnd && energy < 0.6) {
        feel.timingOffsetMs = int(8 * tempoFactor);
        feel.velocityMult = 0.80;
        feel.durationMult = 1.3;
        feel.isBreath = true;
    }
    // PHRASE PEAK: Slight emphasis (more at high energy)
    else if (isPhrasePeak) {
        feel.timingOffsetMs = int(-3 * tempoFactor);  // Tiny push
        feel.velocityMult = 1.05 + 0.10 * energy;     // More punch at high energy
        feel.durationMult = 1.0 + 0.05 * energy;
    }
    // BUILDING: Forward lean (less at high energy - already driving)
    else if (phraseArcPhase == 0) {
        feel.timingOffsetMs = int(-5 * tempoFactor);
        feel.velocityMult = 0.90 + 0.15 * energy;
        feel.durationMult = 0.95;
    }
    // RESOLVING: Relaxation (less at high energy - keep the drive)
    else if (phraseArcPhase == 2 && energy < 0.7) {
        feel.timingOffsetMs = int(5 * tempoFactor);
        feel.velocityMult = 0.75 + 0.15 * energy;
        feel.durationMult = 1.15;
    }
    
    // BEAT PLACEMENT: More locked at high energy
    const int beatLockRange = int(5 * (1.0 - 0.6 * gridLock));  // 5ms at e=0, 2ms at e=1
    if (beatInBar == 0) {
        feel.timingOffsetMs = qBound(-beatLockRange, feel.timingOffsetMs, beatLockRange);
        feel.velocityMult *= 1.03 + 0.05 * energy;  // More punch at high energy
    } else if (beatInBar == 2) {
        feel.timingOffsetMs = qBound(-beatLockRange, feel.timingOffsetMs, beatLockRange);
    }
    
    // SYNCOPATION: Less at high energy for solid pulse!
    if (subBeat == 1 || subBeat == 3) {
        // At low energy: laid back (3ms), at high energy: barely any (1ms)
        feel.timingOffsetMs += int(3 * rubatoMult);
        feel.velocityMult *= (0.95 + 0.03 * energy);  // Less soft at high energy
    }
    
    // CHORD CHANGES: Always anchor!
    if (isChordChange && beatInBar == 0) {
        feel.timingOffsetMs = 0;  // Dead on time
        feel.durationMult = 1.1 + 0.1 * energy;  // Longer at high energy (power)
    }
    
    // TIGHTER bounds at high energy
    const int maxOffset = int(15 * (1.0 - 0.7 * gridLock));  // 15ms at e=0, 5ms at e=1
    feel.timingOffsetMs = qBound(-maxOffset, feel.timingOffsetMs, maxOffset);
    feel.velocityMult = qBound(0.70, feel.velocityMult, 1.20);
    feel.durationMult = qBound(0.8, feel.durationMult, 1.4);
    
    return feel;
}

// =============================================================================
// CONTEXT CONVERSION HELPERS
// Convert JazzBalladPianoPlanner::Context to generator contexts
// =============================================================================

LhVoicingGenerator::Context toLhContext(const JazzBalladPianoPlanner::Context& c) {
    LhVoicingGenerator::Context lhc;
    lhc.chord = c.chord;
    lhc.lhLo = c.lhLo;
    lhc.lhHi = c.lhHi;
    lhc.beatInBar = c.beatInBar;
    lhc.energy = c.energy;
    lhc.chordIsNew = c.chordIsNew;
    lhc.preferShells = c.preferShells;
    lhc.weights = c.weights;
    lhc.keyTonicPc = c.keyTonicPc;
    lhc.keyMode = c.keyMode;
    lhc.bassRegisterHi = c.bassRegisterHi;
    return lhc;
}

RhVoicingGenerator::Context toRhContext(const JazzBalladPianoPlanner::Context& c) {
    RhVoicingGenerator::Context rhc;
    rhc.chord = c.chord;
    rhc.rhLo = c.rhLo;
    rhc.rhHi = c.rhHi;
    rhc.sparkleLo = c.sparkleLo;
    rhc.sparkleHi = c.sparkleHi;
    rhc.beatInBar = c.beatInBar;
    rhc.energy = c.energy;
    rhc.chordIsNew = c.chordIsNew;
    rhc.weights = c.weights;
    rhc.keyTonicPc = c.keyTonicPc;
    rhc.keyMode = c.keyMode;
    rhc.barInPhrase = c.barInPhrase;
    rhc.phraseEndBar = c.phraseEndBar;
    rhc.cadence01 = c.cadence01;
    rhc.userSilence = c.userSilence;
    rhc.userBusy = c.userBusy;
    rhc.userMeanMidi = c.userMeanMidi;
    return rhc;
}

} // namespace

// =============================================================================
// Construction & State Management
// =============================================================================

JazzBalladPianoPlanner::JazzBalladPianoPlanner() {
    reset();
}

void JazzBalladPianoPlanner::setOntology(const virtuoso::ontology::OntologyRegistry* ont) {
    m_ont = ont;
    // Also set on generators (they were created without ontology initially)
    m_lhGen = LhVoicingGenerator(ont);
    m_rhGen = RhVoicingGenerator(ont);
}

void JazzBalladPianoPlanner::reset() {
    QMutexLocker locker(m_stateMutex.get());
    m_state = PlannerState{};
    m_state.perf.heldNotes.clear();
    m_state.perf.ints.insert("cc64", 0);
    m_state.lastVoicingMidi.clear();
    m_state.lastTopMidi = -1;
    m_state.lastVoicingKey.clear();
    m_state.currentPhraseId.clear();
    m_state.phraseStartBar = -1;
    
    // Reset generators
    m_lhGen.setState(LhVoicingGenerator::State{});
    m_rhGen.setState(RhVoicingGenerator::State{});
}

void JazzBalladPianoPlanner::syncGeneratorState() const {
    // Sync planner state to generators
    LhVoicingGenerator::State lhState;
    lhState.lastLhMidi = m_state.lastLhMidi;
    lhState.lastLhWasTypeA = m_state.lastLhWasTypeA;
    lhState.lastInnerVoiceIndex = m_state.lastInnerVoiceIndex;
    lhState.innerVoiceDirection = m_state.innerVoiceDirection;
    m_lhGen.setState(lhState);
    
    RhVoicingGenerator::State rhState;
    rhState.lastRhMidi = m_state.lastRhMidi;
    rhState.lastRhTopMidi = m_state.lastRhTopMidi;
    rhState.lastRhSecondMidi = m_state.lastRhSecondMidi;
    rhState.rhMelodicDirection = m_state.rhMelodicDirection;
    rhState.rhMotionsThisChord = m_state.rhMotionsThisChord;
    rhState.lastChordForRh = m_state.lastChordForRh;
    m_rhGen.setState(rhState);
}

void JazzBalladPianoPlanner::updateStateFromGenerators() {
    // Update planner state from generators
    const auto& lhState = m_lhGen.state();
    m_state.lastLhMidi = lhState.lastLhMidi;
    m_state.lastLhWasTypeA = lhState.lastLhWasTypeA;
    m_state.lastInnerVoiceIndex = lhState.lastInnerVoiceIndex;
    m_state.innerVoiceDirection = lhState.innerVoiceDirection;
    
    const auto& rhState = m_rhGen.state();
    m_state.lastRhMidi = rhState.lastRhMidi;
    m_state.lastRhTopMidi = rhState.lastRhTopMidi;
    m_state.lastRhSecondMidi = rhState.lastRhSecondMidi;
    m_state.rhMelodicDirection = rhState.rhMelodicDirection;
    m_state.rhMotionsThisChord = rhState.rhMotionsThisChord;
    m_state.lastChordForRh = rhState.lastChordForRh;
}

JazzBalladPianoPlanner::PlannerState JazzBalladPianoPlanner::snapshotState() const {
    QMutexLocker locker(m_stateMutex.get());
    syncGeneratorState();  // Ensure generators are in sync before snapshot
    return m_state;
}

void JazzBalladPianoPlanner::restoreState(const PlannerState& s) {
    QMutexLocker locker(m_stateMutex.get());
    m_state = s;
    syncGeneratorState();  // Sync generators with restored state
}

// =============================================================================
// Weight Integration
// =============================================================================

JazzBalladPianoPlanner::WeightMappings JazzBalladPianoPlanner::computeWeightMappings(const Context& c) const {
    WeightMappings m;
    
    // ==========================================================================
    // ENERGY-ONLY DERIVATION
    // All behavior is now derived from c.energy (0.0 = sparse/calm, 1.0 = dense/intense)
    // This replaces the complex weights v2 system with a simpler, more coherent approach.
    // ==========================================================================
    const double e = qBound(0.0, c.energy, 1.0);
    
    // Play probability: scales with energy (0.4 at low, 1.0 at high)
    m.playProbMod = 0.5 + 0.5 * e;
    
    // Velocity: scales with energy (0.7 at low, 1.1 at high)
    m.velocityMod = 0.7 + 0.4 * e;
    
    // Voicing fullness: more notes at higher energy
    m.voicingFullnessMod = 0.6 + 0.5 * e;
    
    // Rubato: moderate and consistent (not energy-dependent for cleaner feel)
    // Reduced from original to prevent stumbled timing
    m.rubatoPushMs = 8;  // Fixed, modest rubato
    
    // Creativity: moderate at all levels with slight energy boost
    m.creativityMod = 0.35 + 0.25 * e;
    
    // Tension: follows energy closely (harmonic color)
    m.tensionMod = 0.3 + 0.5 * e;
    
    // Interactivity: ALWAYS MAXIMUM (per user request)
    m.interactivityMod = 1.0;
    
    // Variability: moderate with energy boost
    m.variabilityMod = 0.4 + 0.3 * e;
    
    // Duration: slightly longer at low energy (more legato), shorter at high
    m.durationMod = 1.1 - 0.2 * e;
    
    // Register shift: neutral
    m.registerShiftMod = 0.0;

    return m;
}

// =============================================================================
// Microtime / Humanization
// =============================================================================

int JazzBalladPianoPlanner::computeTimingOffsetMs(const Context& c, quint32 hash) const {
    // ==========================================================================
    // MINIMAL HUMANIZATION: Prevent "stumbled" feel
    // The goal is to feel human, not drunk
    // All timing variation should be SUBTLE and CONSISTENT
    // ==========================================================================
    
    int offset = 0;
    
    // Very small random jitter for humanization (±3ms)
    offset += int(hash % 7) - 3;
    
    // Cadential push: slight forward lean at cadences
    if (c.cadence01 >= 0.7 && c.beatInBar == 3) {
        offset -= 3;  // Subtle push
    }
    
    // VERY TIGHT bounds
    return qBound(-8, offset, 8);
}

virtuoso::groove::GridPos JazzBalladPianoPlanner::applyTimingOffset(
    const virtuoso::groove::GridPos& pos, int offsetMs, int bpm,
    const virtuoso::groove::TimeSignature& ts) const {

    if (offsetMs == 0) return pos;

    const double msPerWhole = 240000.0 / double(bpm);
    const double wholeOffset = double(offsetMs) / msPerWhole;

    virtuoso::groove::GridPos result = pos;
    result.withinBarWhole = pos.withinBarWhole + 
        virtuoso::groove::Rational(qint64(wholeOffset * 1000), 1000);

    const auto barDur = virtuoso::groove::GrooveGrid::barDurationWhole(ts);

    while (result.withinBarWhole < virtuoso::groove::Rational(0, 1)) {
        result.withinBarWhole = result.withinBarWhole + barDur;
        result.barIndex--;
    }
    while (result.withinBarWhole >= barDur) {
        result.withinBarWhole = result.withinBarWhole - barDur;
        result.barIndex++;
    }

    return result;
}

// =============================================================================
// ARTICULATION & DYNAMICS
// Expressive playing through varied touch and intensity
// =============================================================================

JazzBalladPianoPlanner::ArticulationType JazzBalladPianoPlanner::determineArticulation(
    const Context& c, bool isRh, int positionInPhrase) const {
    
    // Ballads are predominantly legato
    // Exception: phrase endings, punctuation moments
    
    const bool atPhraseEnd = (positionInPhrase >= c.phraseBars * 3);
    const bool isDownbeat = (c.beatInBar == 0);
    const bool isCadence = (c.cadence01 > 0.5);
    
    // LH: mostly legato/tenuto for warmth
    if (!isRh) {
        if (isCadence && isDownbeat) {
            return ArticulationType::Accent;  // Cadential emphasis
        }
        if (energyToEmotion(c.energy) > 0.5) {
            return ArticulationType::Tenuto;  // Full, warm sustain
        }
        return ArticulationType::Legato;
    }
    
    // RH: more varied for expression
    if (atPhraseEnd) {
        return ArticulationType::Portato;  // Gentle release
    }
    if (energyToTension(c.energy) > 0.5 && isDownbeat) {
        return ArticulationType::Accent;   // Tension emphasis
    }
    if (energyToWarmth(c.energy) > 0.5) {
        return ArticulationType::Legato;   // Warm, connected
    }
    if (c.beatInBar == 2 && energyToRhythm(c.energy) > 0.4) {
        return ArticulationType::Tenuto;   // Slight emphasis on beat 3
    }
    
    return ArticulationType::Legato;  // Default for ballads
}

void JazzBalladPianoPlanner::applyArticulation(
    ArticulationType art, double& duration, int& velocity, bool isTopVoice) const {
    
    // Modify duration and velocity based on articulation
    // Duration is in whole notes
    
    switch (art) {
        case ArticulationType::Legato:
            // Full duration, slightly reduced velocity for smoothness
            velocity = int(velocity * 0.95);
            break;
            
        case ArticulationType::Tenuto:
            // Full duration, full velocity
            // No modification needed
            break;
            
        case ArticulationType::Portato:
            // 75% duration, slightly softer
            duration *= 0.75;
            velocity = int(velocity * 0.90);
            break;
            
        case ArticulationType::Staccato:
            // 40% duration (rare in ballads)
            duration *= 0.40;
            velocity = int(velocity * 0.85);
            break;
            
        case ArticulationType::Accent:
            // Full duration, boosted velocity
            if (isTopVoice) {
                velocity = qMin(127, velocity + 12);
            } else {
                velocity = qMin(127, velocity + 6);
            }
            break;
    }
}

int JazzBalladPianoPlanner::contourVelocity(
    int baseVel, int noteIndex, int noteCount, bool isRh) const {
    
    // Velocity contouring: melody voice (top) louder, inner voices softer
    // This creates natural voicing where melody sings over harmony
    
    if (noteCount <= 1) return baseVel;
    
    if (isRh) {
        // RH: top note is melody, should be loudest
        if (noteIndex == noteCount - 1) {
            // Top voice: melody boost
            return qMin(127, baseVel + 10);
        } else if (noteIndex == 0) {
            // Bottom voice: slightly softer
            return qMax(30, baseVel - 6);
        } else {
            // Middle voices: softest
            return qMax(30, baseVel - 10);
        }
    } else {
        // LH: more even, but top of voicing slightly emphasized
        if (noteIndex == noteCount - 1) {
            return qMin(127, baseVel + 4);
        } else {
            return qMax(30, baseVel - 3);
        }
    }
}

// =============================================================================
// BREATH AND SPACE
// Intentional silence for musicality - space is part of the music
// =============================================================================

bool JazzBalladPianoPlanner::shouldRest(const Context& c, quint32 hash) const {
    // Intentional rests happen:
    // 1. After phrase endings (musical breath)
    // 2. When user is actively playing (give them space)
    // 3. At low energy moments (less is more)
    // 4. To create anticipation before cadences
    
    // Musical breath after phrase endings
    const int phrasePhase = computePhraseArcPhase(c);
    if (phrasePhase == 2) {  // Resolving phase = potential rest
        return (hash % 100) < 60;
    }
    
    // Before cadences (create anticipation)
    if (c.cadence01 > 0.7 && c.beatInBar == 1) {
        return (hash % 100) < 25;  // 25% chance to rest beat before cadence
    }
    
    // At very low energy, occasional rests add space
    if (c.energy < 0.25 && energyToDensity(c.energy) < 0.4) {
        return (hash % 100) < 15;  // 15% chance at low energy
    }
    
    // When user is playing intensely, give more space
    if (c.userBusy) {
        return (hash % 100) < 20;  // 20% chance when user is active
    }
    
    return false;
}

double JazzBalladPianoPlanner::getRestDuration(const Context& c) const {
    
    // Rest duration depends on context
    // Phrase endings: longer rest (half bar to full bar)
    // Other contexts: shorter rest (1-2 beats)
    
    const double phrasePhase = computePhraseArcPhase(c);
    
    if (phrasePhase > 0.95) {
        // Phrase end: rest for remainder of phrase
        return 0.5;  // Half bar
    }
    
    if (c.cadence01 > 0.7) {
        // Before cadence: one beat
        return 0.25;  // One beat
    }
    
    // Default: short breath
    return 0.125;  // Half beat
}

// =============================================================================
// Vocabulary-Driven Rhythm
// =============================================================================

bool JazzBalladPianoPlanner::hasVocabularyLoaded() const {
    return m_vocab != nullptr;
}

QVector<JazzBalladPianoPlanner::VocabRhythmHit> JazzBalladPianoPlanner::queryVocabularyHits(
    const Context& c, QString* outPhraseId) const {
    
    QVector<VocabRhythmHit> hits;
    if (!m_vocab) return hits;

    virtuoso::vocab::VocabularyRegistry::PianoPhraseQuery pq;
    pq.ts = {4, 4};
    pq.playbackBarIndex = c.playbackBarIndex;
    pq.beatInBar = c.beatInBar;
    pq.chordText = c.chordText;
    pq.chordFunction = c.chordFunction;
    pq.chordIsNew = c.chordIsNew;
    pq.userSilence = c.userSilence;
    pq.energy = c.energy;
    pq.determinismSeed = c.determinismSeed;
    pq.phraseBars = c.phraseBars;

    QString phraseId, phraseNotes;
    const auto phraseHits = m_vocab->pianoPhraseHitsForBeat(pq, &phraseId, &phraseNotes);
    
    if (outPhraseId) *outPhraseId = phraseId;

    if (!phraseHits.isEmpty()) {
        hits.reserve(phraseHits.size());
        for (const auto& ph : phraseHits) {
            VocabRhythmHit h;
            h.sub = ph.sub;
            h.count = ph.count;
            h.durNum = ph.dur_num;
            h.durDen = ph.dur_den;
            h.velDelta = ph.vel_delta;

            if (ph.density == "sparse") h.density = VoicingDensity::Sparse;
            else if (ph.density == "guide") h.density = VoicingDensity::Guide;
            else if (ph.density == "medium") h.density = VoicingDensity::Medium;
            else if (ph.density == "lush") h.density = VoicingDensity::Lush;
            else h.density = VoicingDensity::Full;

            hits.push_back(h);
        }
        return hits;
    }

    virtuoso::vocab::VocabularyRegistry::PianoBeatQuery bq;
    bq.ts = {4, 4};
    bq.playbackBarIndex = c.playbackBarIndex;
    bq.beatInBar = c.beatInBar;
    bq.chordText = c.chordText;
    bq.chordFunction = c.chordFunction;
    bq.chordIsNew = c.chordIsNew;
    bq.userSilence = c.userSilence;
    bq.energy = c.energy;
    bq.determinismSeed = c.determinismSeed;

    const auto beatChoice = m_vocab->choosePianoBeat(bq);
    if (!beatChoice.id.isEmpty()) {
        if (outPhraseId && outPhraseId->isEmpty()) *outPhraseId = beatChoice.id;
        hits.reserve(beatChoice.hits.size());
        for (const auto& bh : beatChoice.hits) {
            VocabRhythmHit h;
            h.sub = bh.sub;
            h.count = bh.count;
            h.durNum = bh.dur_num;
            h.durDen = bh.dur_den;
            h.velDelta = bh.vel_delta;
            h.density = (bh.density == "guide") ? VoicingDensity::Guide : VoicingDensity::Full;
            hits.push_back(h);
        }
    }

    return hits;
}

bool JazzBalladPianoPlanner::shouldPlayBeatFallback(const Context& c, quint32 hash) const {
    if (c.chordIsNew) return true;

    const auto mappings = computeWeightMappings(c);
    double baseProb = 0.0;

    switch (c.beatInBar) {
        case 0: baseProb = 0.55; break;
        case 1: baseProb = 0.20 * (1.0 - c.skipBeat2ProbStable); break;
        case 2: baseProb = 0.30; break;
        case 3: baseProb = c.nextChanges ? 0.55 : 0.25; break;
        default: baseProb = 0.20;
    }

    if (c.userDensityHigh || c.userIntensityPeak || c.userBusy) {
        baseProb *= (0.3 + 0.3 * (1.0 - mappings.interactivityMod));
    }
    if (c.userSilence) {
        baseProb = qMin(1.0, baseProb + 0.30 * mappings.interactivityMod);
    }
    if (c.phraseEndBar && c.beatInBar == 3) {
        baseProb = qMin(1.0, baseProb + 0.25);
    }
    if (c.cadence01 >= 0.5) {
        baseProb = qMin(1.0, baseProb + 0.20 * c.cadence01);
    }

    baseProb *= mappings.playProbMod;
    baseProb *= (0.5 + 0.6 * qBound(0.0, c.energy, 1.0));

    const double threshold = double(hash % 1000) / 1000.0;
    return threshold < baseProb;
}

// =============================================================================
// Register Coordination
// =============================================================================

void JazzBalladPianoPlanner::adjustRegisterForBass(Context& c) const {
    const int minSpacing = 8;
    const int bassHi = c.bassRegisterHi;

    if (c.lhLo < bassHi + minSpacing) {
        const int shift = (bassHi + minSpacing) - c.lhLo;
        c.lhLo += shift;
        c.lhHi += shift;
    }

    if (c.bassActivity > 0.7) {
        c.lhLo = qMax(c.lhLo, 52);
        c.lhHi = qMax(c.lhHi, 68);
    }

    const bool hasSlashBass = (c.chord.bassPc >= 0 && c.chord.bassPc != c.chord.rootPc);
    if (hasSlashBass && c.bassPlayingThisBeat) {
        c.lhLo = qMax(c.lhLo, 54);
        c.lhHi = qMax(c.lhHi, 70);
    }
}

// =============================================================================
// PHRASE-LEVEL PLANNING
// Plans melodic arcs across multiple bars with motif development
// Creates the coherent, intentional phrasing that distinguishes great pianists
// =============================================================================

int JazzBalladPianoPlanner::computePhraseArcPhase(const Context& c) const {
    // Divide phrase into three phases:
    // 0 = Building (first ~40% of phrase) - ascending, gathering energy
    // 1 = Peak (middle ~30%) - highest activity, tension
    // 2 = Resolving (final ~30%) - descending, releasing
    
    const int bars = qMax(1, c.phraseBars);
    const int bar = c.barInPhrase;
    
    const double progress = double(bar) / double(bars);
    
    if (progress < 0.4) return 0;  // Building
    if (progress < 0.7) return 1;  // Peak
    return 2;                       // Resolving
}

int JazzBalladPianoPlanner::getArcTargetMidi(const Context& c, int arcPhase) const {
    // Target MIDI notes for each phase:
    // Building: Start mid-register, gradually ascend
    // Peak: High register (phrase climax) - BUT varies based on energy and alternation
    // Resolving: Descend back to comfortable rest
    
    const int baseRhMid = (c.rhLo + c.rhHi) / 2;  // ~76 typically
    
    // Get register variety offset to prevent staying stuck in one area
    const int varietyOffset = computeRegisterVariety(c);
    
    // Determine if this phrase peaks high or low
    const bool peakHigh = shouldPhrasePeakHigh(c);
    
    switch (arcPhase) {
        case 0: { // Building
            // Start from varied position, rise toward peak
            const double buildProgress = double(c.barInPhrase) / (0.4 * c.phraseBars);
            const int startMidi = baseRhMid - 4 + varietyOffset;
            const int peakMidi = peakHigh ? (c.rhHi - 3) : (baseRhMid + 2);
            return startMidi + int((peakMidi - startMidi) * buildProgress);
        }
        case 1: { // Peak
            if (peakHigh) {
                // High peak: upper register, more with high energy
                return c.rhHi - 3 + (c.energy > 0.6 ? 2 : 0);
            } else {
                // Low peak (introspective): mid-register, rich but not high
                return baseRhMid + 2 + varietyOffset;
            }
        }
        case 2: { // Resolving
            // Descend from peak toward rest
            const int resolveStart = c.barInPhrase - int(0.7 * c.phraseBars);
            const int resolveTotal = c.phraseBars - int(0.7 * c.phraseBars);
            const double resolveProgress = double(resolveStart) / qMax(1, resolveTotal);
            const int peakMidi = peakHigh ? (c.rhHi - 3) : (baseRhMid + 2);
            const int restMidi = baseRhMid - 4 + varietyOffset;
            return peakMidi - int((peakMidi - restMidi) * resolveProgress);
        }
        default:
            return baseRhMid + varietyOffset;
    }
}

void JazzBalladPianoPlanner::generatePhraseMotif(const Context& c) {
    // Generate a simple 2-3 note motif that will be developed through the phrase
    // Motifs are based on chord degrees rather than fixed pitches so they transpose naturally
    
    // Use determinism seed for consistency
    const quint32 seed = c.determinismSeed ^ (c.playbackBarIndex * 17);
    
    // Choose motif starting degree (prefer 3, 5, 7, 9)
    const int degreeOptions[] = {3, 5, 7, 9, 5, 3}; // Weighted toward 3 and 5
    m_state.phraseMotifStartDegree = degreeOptions[seed % 6];
    
    // Generate 2-3 note motif interval pattern (relative to start degree)
    // Common jazz motifs:
    //   Ascending 2nd: [0, +2] or [0, +1]
    //   Descending: [0, -2] or [0, -1]
    //   Turn: [0, +2, -1] or [0, -2, +1]
    //   Leap-step: [0, +4, -1]
    
    const int motifType = (seed >> 8) % 5;
    m_state.phraseMotifPcs.clear();
    
    switch (motifType) {
        case 0: // Ascending 2nd
            m_state.phraseMotifPcs = {0, 2};
            m_state.phraseMotifAscending = true;
            break;
        case 1: // Descending 2nd
            m_state.phraseMotifPcs = {0, -2};
            m_state.phraseMotifAscending = false;
            break;
        case 2: // Upper turn
            m_state.phraseMotifPcs = {0, 2, -1};
            m_state.phraseMotifAscending = true;
            break;
        case 3: // Lower turn
            m_state.phraseMotifPcs = {0, -2, 1};
            m_state.phraseMotifAscending = false;
            break;
        case 4: // Leap and step back
            m_state.phraseMotifPcs = {0, 4, -1};
            m_state.phraseMotifAscending = true;
            break;
    }
    
    m_state.phraseMotifVariation = 0;
    m_state.lastPhraseStartBar = c.playbackBarIndex;
}

int JazzBalladPianoPlanner::getMotifVariation(const Context& c) const {
    // Vary the motif through the phrase:
    // Bar 0: Original
    // Bar 1: Transposed up (start from different degree)
    // Bar 2: Inverted (flip direction)
    // Bar 3: Transposed down / Return to original
    
    const int barInPhrase = c.barInPhrase % qMax(1, c.phraseBars);
    
    // Also factor in energy - higher energy = more variation
    const bool allowInversion = (c.energy >= 0.4 || c.cadence01 >= 0.3);
    
    switch (barInPhrase % 4) {
        case 0: return 0; // Original
        case 1: return 1; // Transposed up
        case 2: return allowInversion ? 2 : 1; // Inverted or transposed
        case 3: return 3; // Transposed down / return
        default: return 0;
    }
}

QVector<int> JazzBalladPianoPlanner::applyMotifToContext(const Context& c, int variation) const {
    // Apply the stored motif with the given variation
    // Returns pitch classes that are ALWAYS consonant with current chord
    // SAFETY: All returned PCs are validated chord tones or safe extensions
    
    if (m_state.phraseMotifPcs.isEmpty()) {
        // No motif stored - return guide tones
        return {pcForDegree(c.chord, 3), pcForDegree(c.chord, 7)};
    }
    
    // Build list of safe pitch classes for this chord
    QVector<int> safePcs;
    int third = pcForDegree(c.chord, 3);
    int fifth = pcForDegree(c.chord, 5);
    int seventh = pcForDegree(c.chord, 7);
    int ninth = pcForDegree(c.chord, 9);
    
    if (third >= 0) safePcs.push_back(third);
    if (fifth >= 0) safePcs.push_back(fifth);
    if (seventh >= 0) safePcs.push_back(seventh);
    if (ninth >= 0) safePcs.push_back(ninth);
    
    if (safePcs.isEmpty()) {
        // Fallback to root
        safePcs.push_back(c.chord.rootPc);
    }
    
    QVector<int> result;
    
    // Get starting degree based on variation
    int startDegree = m_state.phraseMotifStartDegree;
    switch (variation) {
        case 1: startDegree += 2; break;  // Up a third
        case 2: startDegree = startDegree; break;  // Same start, inverted intervals
        case 3: startDegree -= 2; break;  // Down a third
    }
    // Clamp to valid degrees
    if (startDegree < 1) startDegree = 3;
    if (startDegree > 13) startDegree = 9;
    
    // Get starting PC - must be a safe chord tone
    int startPc = pcForDegree(c.chord, startDegree);
    if (startPc < 0 || !safePcs.contains(startPc)) {
        // Fall back to the first safe PC
        startPc = safePcs.first();
    }
    
    result.push_back(startPc);
    
    // Apply motif intervals - but SNAP to nearest safe PC
    for (int i = 1; i < m_state.phraseMotifPcs.size(); ++i) {
        int interval = m_state.phraseMotifPcs[i];
        
        // Inversion: flip interval direction
        if (variation == 2) {
            interval = -interval;
        }
        
        // Convert interval to semitones (roughly: 1 step = 2 semitones)
        const int semitones = interval * 2;
        const int rawPc = (startPc + semitones + 12) % 12;
        
        // SAFETY: Snap to nearest safe PC
        int bestPc = safePcs.first();
        int bestDist = 12;
        for (int safePc : safePcs) {
            int dist = qMin(qAbs(safePc - rawPc), 12 - qAbs(safePc - rawPc));
            if (dist < bestDist) {
                bestDist = dist;
                bestPc = safePc;
            }
        }
        
        // Only add if different from last (avoid repetition)
        if (result.isEmpty() || bestPc != result.last()) {
            result.push_back(bestPc);
        }
    }
    
    return result;
}

int JazzBalladPianoPlanner::getArcMelodicDirection(int arcPhase, int barInPhrase, int phraseBars) const {
    // Return melodic direction hint based on arc position:
    // +1 = ascending, 0 = neutral/hold, -1 = descending
    
    switch (arcPhase) {
        case 0: // Building - generally ascend
            return (barInPhrase == 0) ? 0 : 1;  // Start neutral, then ascend
        case 1: // Peak - can go either way, slight preference for holding
            return (barInPhrase % 2 == 0) ? 0 : 1;
        case 2: // Resolving - descend
            return -1;
        default:
            return 0;
    }
}

// =============================================================================
// QUESTION-ANSWER PHRASING
// 2-bar phrases that relate to each other musically - creates dialogue
// "Question" rises or leaves tension, "Answer" resolves or mirrors
// =============================================================================

void JazzBalladPianoPlanner::updateQuestionAnswerState(const Context& c, int melodicPeakMidi, int finalMidi) {
    // Update Q/A tracking at phrase boundaries
    // Called at the end of each 2-bar phrase
    
    m_state.barsInCurrentQA++;
    
    // Check if we're at a 2-bar phrase boundary
    if (m_state.barsInCurrentQA >= 2) {
        // Phrase complete - store data and flip
        if (m_state.lastPhraseWasQuestion) {
            // Just finished a Question - store it for the Answer to reference
            m_state.questionPeakMidi = melodicPeakMidi;
            m_state.questionEndMidi = finalMidi;
        }
        // Toggle for next phrase
        m_state.lastPhraseWasQuestion = !m_state.lastPhraseWasQuestion;
        m_state.barsInCurrentQA = 0;
    }
}

int JazzBalladPianoPlanner::getQuestionAnswerTargetMidi(const Context& c) const {
    // Determines the target register/direction based on Q/A position
    // Returns a target MIDI to aim for, or -1 if no strong preference
    
    if (m_state.lastPhraseWasQuestion) {
        // Currently playing a QUESTION phrase
        // Questions typically rise, leave an open feeling
        // Target: slightly above mid-register, end on a non-root tone
        const int rhMid = (c.rhLo + c.rhHi) / 2;
        const int questionTarget = rhMid + 4 + (m_state.barsInCurrentQA * 2);
        // SAFETY: Ensure min <= max
        return qBound(c.rhLo, questionTarget, qMax(c.rhLo, c.rhHi - 2));
    } else {
        // Currently playing an ANSWER phrase
        // Answers relate to the question: can mirror, resolve, or complement
        // Strategy: move toward a resolution note, often lower than the question peak
        
        // Start near where question ended
        if (m_state.barsInCurrentQA == 0) {
            // First bar of answer: relate to question's ending
            return qBound(c.rhLo, m_state.questionEndMidi - 2, c.rhHi);
        }
        
        // Second bar of answer: resolve lower, toward stability
        const int resolutionTarget = m_state.questionPeakMidi - 5;
        return qBound(c.rhLo, resolutionTarget, c.rhHi);
    }
}

bool JazzBalladPianoPlanner::shouldUseQuestionContour(const Context& c) const {
    // Whether to actively shape melodic line for Q/A effect
    // More likely at emotional, expressive moments; less when busy
    
    if (c.userBusy) return false;  // Let user take the melodic lead
    if (c.energy > 0.7) return false;  // At high energy, other factors dominate
    // Emotion is always moderate in energy-only mode, so always allow phrasing
    
    // Probability based on emotion and warmth
    const double prob = 0.4 + (energyToEmotion(c.energy) * 0.3) + (energyToWarmth(c.energy) * 0.2);
    const quint32 hash = c.determinismSeed ^ (c.playbackBarIndex * 13);
    return (hash % 100) < int(prob * 100);
}

// =============================================================================
// MELODIC SEQUENCES
// Repeat melodic patterns at different pitch levels for coherence
// =============================================================================

void JazzBalladPianoPlanner::updateMelodicSequenceState(const Context& c, const QVector<int>& pattern) {
    // Track patterns for sequence detection/generation
    
    if (pattern.isEmpty()) return;
    
    // Check if current pattern matches previous (transposed)
    if (!m_state.lastMelodicPattern.isEmpty() && pattern.size() == m_state.lastMelodicPattern.size()) {
        // Check if it's a transposition of the last pattern
        const int transposition = pattern[0] - m_state.lastMelodicPattern[0];
        bool isSequence = true;
        for (int i = 1; i < pattern.size(); ++i) {
            if (pattern[i] - m_state.lastMelodicPattern[i] != transposition) {
                isSequence = false;
                break;
            }
        }
        
        if (isSequence) {
            m_state.sequenceTransposition = transposition;
            m_state.sequenceRepetitions++;
        } else {
            m_state.sequenceRepetitions = 0;
        }
    } else {
        m_state.sequenceRepetitions = 0;
    }
    
    m_state.lastMelodicPattern = pattern;
}

bool JazzBalladPianoPlanner::shouldContinueSequence(const Context& c) const {
    // Should we continue an established sequence pattern?
    // Sequences sound good with 2-3 repetitions, then should break
    
    if (m_state.sequenceRepetitions == 0) return false;  // No sequence going
    if (m_state.sequenceRepetitions >= 3) return false;  // Don't overdo it
    if (c.cadence01 > 0.6) return false;  // Break sequence at cadences
    
    // 60% chance to continue if we're in a sequence
    const quint32 hash = c.determinismSeed ^ (c.playbackBarIndex * 23);
    return (hash % 100) < 60;
}

int JazzBalladPianoPlanner::getSequenceTransposition(const Context& c) const {
    // Get suggested transposition for continuing the sequence
    // Common: down a 3rd (-3 or -4 semitones), up a 2nd (+2), down a 2nd (-2)
    
    if (m_state.sequenceTransposition != 0) {
        // Continue the established transposition direction
        return m_state.sequenceTransposition;
    }
    
    // Choose new transposition based on musical context
    const quint32 hash = c.determinismSeed ^ (c.playbackBarIndex * 31);
    const int options[] = {-3, -4, 2, -2, 4};  // Common sequence intervals
    return options[hash % 5];
}

// =============================================================================
// ORNAMENTAL GESTURES
// Tasteful embellishments: grace notes, turns, mordents
// Used sparingly for expressiveness in ballads
// =============================================================================

bool JazzBalladPianoPlanner::shouldAddOrnament(const Context& c, quint32 hash) const {
    // Ornaments are used sparingly in ballads - too many become distracting
    // Best moments: downbeats, phrase starts, emotional peaks
    
    if (c.userBusy) return false;  // Don't ornament when user is playing
    if (c.energy > 0.7) return false;  // High energy = cleaner lines
    
    // Only ornament on beat 1 or beat 3 (downbeats)
    if (c.beatInBar != 0 && c.beatInBar != 2) return false;
    
    // Base probability ~8-12%
    double prob = 0.08;
    
    // Increase at emotional moments
    if (energyToEmotion(c.energy) > 0.5) prob += 0.04;
    
    // Increase at phrase starts (first bar of phrase)
    if (c.barInPhrase == 0 && c.beatInBar == 0) prob += 0.05;
    
    // Slightly more common at cadences
    if (c.cadence01 > 0.5) prob += 0.03;
    
    return (hash % 100) < int(prob * 100);
}

JazzBalladPianoPlanner::Ornament JazzBalladPianoPlanner::generateOrnament(
    const Context& c, int targetMidi, quint32 hash) const {
    
    Ornament orn;
    
    // Choose ornament type based on context
    // Grace notes: most common, subtle
    // Turns: at phrase starts, expressive moments
    // Mordents: on accented beats
    // Appoggiaturas: at cadences
    
    const int typeChoice = hash % 100;
    
    if (c.cadence01 > 0.6 && typeChoice < 30) {
        // Appoggiatura at cadence - leaning note
        orn.type = OrnamentType::Appoggiatura;
    } else if (c.barInPhrase == 0 && c.beatInBar == 0 && typeChoice < 50) {
        // Turn at phrase start
        orn.type = OrnamentType::Turn;
    } else if (typeChoice < 70) {
        // Grace note - most common
        orn.type = OrnamentType::GraceNote;
    } else {
        // Mordent
        orn.type = OrnamentType::Mordent;
    }
    
    // Get chord-safe neighbor notes for the ornament
    // Use simple whole-step neighbors if possible, snap to chord tones
    int upperNeighbor = targetMidi + 2;
    int lowerNeighbor = targetMidi - 2;
    
    // Try to snap to chord tones for safety
    const int third = pcForDegree(c.chord, 3);
    const int fifth = pcForDegree(c.chord, 5);
    const int seventh = pcForDegree(c.chord, 7);
    
    // Snap upper neighbor to nearest chord tone if close
    auto snapToNearestChordTone = [&](int midi) -> int {
        int pc = normalizePc(midi);
        if (third >= 0 && qAbs(pc - third) <= 1) return midi + (third - pc);
        if (fifth >= 0 && qAbs(pc - fifth) <= 1) return midi + (fifth - pc);
        if (seventh >= 0 && qAbs(pc - seventh) <= 1) return midi + (seventh - pc);
        return midi;
    };
    
    upperNeighbor = snapToNearestChordTone(upperNeighbor);
    lowerNeighbor = snapToNearestChordTone(lowerNeighbor);
    
    // Generate the ornament notes
    const int graceDurMs = 40;   // Very quick for grace notes
    const int turnDurMs = 60;    // Slightly longer for turns
    const int appoggDurMs = 120; // Longer for appoggiatura (expressive)
    
    // Calculate base velocity from energy
    const int baseVel = 50 + int(30.0 * c.energy);
    const int graceVel = qMax(30, int(baseVel * 0.75));  // Softer than main note
    
    switch (orn.type) {
        case OrnamentType::GraceNote:
            // Single grace note from above or below
            if ((hash >> 8) % 2 == 0) {
                orn.notes = {upperNeighbor};
            } else {
                orn.notes = {lowerNeighbor};
            }
            orn.durationsMs = {graceDurMs};
            orn.velocities = {graceVel};
            orn.mainNoteDelayMs = graceDurMs;
            break;
            
        case OrnamentType::Turn:
            // Upper-main-lower-main (inverted if hash says so)
            if ((hash >> 8) % 2 == 0) {
                orn.notes = {upperNeighbor, targetMidi, lowerNeighbor};
            } else {
                orn.notes = {lowerNeighbor, targetMidi, upperNeighbor};
            }
            orn.durationsMs = {turnDurMs, turnDurMs, turnDurMs};
            orn.velocities = {graceVel, graceVel, graceVel};
            orn.mainNoteDelayMs = turnDurMs * 3;
            break;
            
        case OrnamentType::Mordent:
            // Quick alternation: main-upper-main or main-lower-main
            if ((hash >> 8) % 2 == 0) {
                orn.notes = {targetMidi, upperNeighbor};
            } else {
                orn.notes = {targetMidi, lowerNeighbor};
            }
            orn.durationsMs = {graceDurMs, graceDurMs};
            orn.velocities = {graceVel, graceVel};
            orn.mainNoteDelayMs = graceDurMs * 2;
            break;
            
        case OrnamentType::Appoggiatura:
            // Leaning note that resolves to target
            // Usually from a step above
            orn.notes = {upperNeighbor};
            orn.durationsMs = {appoggDurMs};
            orn.velocities = {qMin(127, int(baseVel * 0.9))};  // Almost as loud as main
            orn.mainNoteDelayMs = appoggDurMs;
            break;
            
        case OrnamentType::None:
        default:
            break;
    }
    
    return orn;
}

// =============================================================================
// GROOVE LOCK (Ensemble Coordination)
// Piano timing relative to bass/drums for tight ensemble feel
// =============================================================================

int JazzBalladPianoPlanner::getGrooveLockLhOffset(const Context& c) const {
    // When bass is playing on this beat, piano can:
    // 1. Lock exactly with bass (beat 1 - tight unison)
    // 2. Play slightly after (let bass lead on beat 3)
    // 3. Play slightly before (anticipate on "and of 4")
    
    if (!c.bassPlayingThisBeat) {
        return 0;  // No coordination needed
    }
    
    // Beat 1: Lock with bass (no offset)
    if (c.beatInBar == 0) {
        return 0;
    }
    
    // Beat 3: Let bass lead slightly (piano plays 10-20ms after)
    if (c.beatInBar == 2) {
        return 12 + int(c.bassActivity * 8);
    }
    
    // Beat 4: Piano can anticipate slightly (for "and of 4" pickups)
    if (c.beatInBar == 3) {
        return -8;
    }
    
    // Beat 2: Usually no bass, but if present, slight delay
    return 8;
}

bool JazzBalladPianoPlanner::shouldComplementBass(const Context& c) const {
    // Piano should complement (not compete with) bass activity
    // When bass is very active, piano should be sparser
    // When bass is sparse, piano can fill more
    
    if (c.bassActivity > 0.7) {
        // Bass is very active - piano should lay back
        return true;
    }
    
    if (c.bassPlayingThisBeat && c.beatInBar != 0) {
        // Bass playing on non-downbeat - let it be heard
        return true;
    }
    
    return false;
}

// =============================================================================
// REGISTER VARIETY
// Ensures we don't get stuck in one register, creates natural contour
// =============================================================================

void JazzBalladPianoPlanner::updateRegisterTracking(int midiNote) {
    // Exponential moving average - recent notes matter more
    // Keep a running sum with decay
    const int WINDOW = 16;  // Approximate window of notes to consider
    
    m_state.recentRegisterSum = (m_state.recentRegisterSum * (WINDOW - 1) + midiNote) / WINDOW;
    m_state.recentRegisterCount = qMin(m_state.recentRegisterCount + 1, WINDOW);
}

int JazzBalladPianoPlanner::computeRegisterVariety(const Context& c) const {
    // Compute a register offset to encourage variety
    // If we've been high, push lower; if low, push higher
    
    if (m_state.recentRegisterCount < 4) {
        // Not enough data yet
        return 0;
    }
    
    const int avgMidi = m_state.recentRegisterSum;  // Already averaged
    const int rhMid = (c.rhLo + c.rhHi) / 2;
    
    // If average is above mid, push down; if below, push up
    int offset = 0;
    if (avgMidi > rhMid + 4) {
        // Been playing too high - encourage lower
        offset = -3 - (avgMidi - rhMid - 4) / 2;
    } else if (avgMidi < rhMid - 4) {
        // Been playing too low - encourage higher
        offset = 3 + (rhMid - 4 - avgMidi) / 2;
    }
    
    // Clamp to reasonable range
    return qBound(-6, offset, 6);
}

bool JazzBalladPianoPlanner::shouldPhrasePeakHigh(const Context& c) const {
    // Alternate phrase peaks between high and low for variety
    // Also consider energy and section
    
    // High energy = high peak
    if (c.energy >= 0.7) return true;
    
    // Low energy = low peak (introspective)
    if (c.energy <= 0.3) return false;
    
    // Otherwise alternate based on phrase number
    // Use bar index to roughly determine phrase number
    const int phraseNum = c.playbackBarIndex / qMax(1, c.phraseBars);
    return (phraseNum % 2 == 0) != m_state.lastPhraseWasHigh;
}

// =============================================================================
// RHYTHMIC VOCABULARY
// Advanced rhythmic patterns: triplets, hemiola, swing, displacement
// =============================================================================

JazzBalladPianoPlanner::RhythmicFeel JazzBalladPianoPlanner::chooseRhythmicFeel(
    const Context& c, quint32 hash) const {
    
    // Probability-based selection influenced by rhythm weight and context
    const double rhythmWeight = energyToRhythm(c.energy);
    const double creativity = energyToCreativity(c.energy);
    
    // Higher rhythm weight = more likely to use interesting patterns
    // Higher creativity = more likely to use unusual patterns
    
    int roll = hash % 100;
    int threshold = 0;
    
    // Swing feel is the baseline for jazz ballads
    // Most common at low-medium rhythm
    threshold += int(45 - 15 * rhythmWeight);  // 30-45%
    if (roll < threshold) return RhythmicFeel::Swing;
    
    // Straight feel for clarity at phrase beginnings and low energy
    threshold += int(20 + 10 * (1.0 - c.energy));  // 20-30%
    if (roll < threshold) return RhythmicFeel::Straight;
    
    // Triplet feel for jazz sophistication
    // More common with higher rhythm weight
    threshold += int(15 + 15 * rhythmWeight);  // 15-30%
    if (roll < threshold) return RhythmicFeel::Triplet;
    
    // Hemiola for tension and interest at phrase peaks
    // Only at medium-high creativity and specific phrase positions
    if (creativity >= 0.4 && (c.barInPhrase == c.phraseBars - 2 || c.cadence01 >= 0.5)) {
        threshold += int(10 + 10 * creativity);  // 10-20%
        if (roll < threshold) return RhythmicFeel::Hemiola;
    }
    
    // Metric displacement for advanced rhythmic sophistication
    // Only at high creativity and energy
    if (creativity >= 0.5 && c.energy >= 0.5) {
        threshold += int(5 + 10 * creativity);  // 5-15%
        if (roll < threshold) return RhythmicFeel::Displaced;
    }
    
    // Default to swing
    return RhythmicFeel::Swing;
}

int JazzBalladPianoPlanner::applyRhythmicFeel(
    RhythmicFeel feel, int subdivision, int beatInBar, int bpm) const {
    
    // Returns timing offset in milliseconds
    // Positive = late (laid back), Negative = early (pushed)
    // SAFETY: All offsets are CAPPED to prevent sloppiness
    
    const double beatMs = 60000.0 / bpm;  // Duration of one beat in ms
    
    // Maximum offset to prevent sloppiness (35ms is noticeable but not sloppy)
    const int maxOffset = 35;
    
    switch (feel) {
        case RhythmicFeel::Straight:
            // No modification - straight 16th note grid
            return 0;
            
        case RhythmicFeel::Swing: {
            // Jazz swing: SUBTLE delay of upbeats
            // Much smaller percentages to avoid sloppiness
            // sub 0 = beat, sub 1 = e, sub 2 = and, sub 3 = a
            int offset = 0;
            switch (subdivision) {
                case 1: offset = int(beatMs * 0.03); break;  // "e" very slightly late
                case 2: offset = int(beatMs * 0.02); break;  // "and" barely late
                case 3: offset = int(beatMs * 0.025); break; // "a" slightly late
                default: break;
            }
            return qBound(-maxOffset, offset, maxOffset);
        }
        
        case RhythmicFeel::Triplet: {
            // Triplet feel: map 4 subdivisions to triplet positions
            // REDUCED offsets to avoid sloppiness
            int offset = 0;
            switch (subdivision) {
                case 0: offset = 0; break;                   // On the beat
                case 2: offset = int(beatMs * 0.08); break;  // Triplet 2nd (reduced)
                case 3: offset = int(-beatMs * 0.04); break; // Triplet 3rd (reduced)
                default: break;
            }
            return qBound(-maxOffset, offset, maxOffset);
        }
        
        case RhythmicFeel::Hemiola: {
            // 3-against-4: create cross-rhythm tension
            // Shift certain beats to create 3-note grouping across 2 beats
            // This is applied at a higher level in pattern generation
            return 0;
        }
        
        case RhythmicFeel::Displaced: {
            // Metric displacement: shifted by a 16th note (not half a beat!)
            // Half a beat was too much - sounds sloppy, not displaced
            int offset = int(-beatMs * 0.25);  // Quarter beat = one 16th
            return qBound(-maxOffset * 2, offset, maxOffset * 2);  // Allow slightly more for displacement
        }
    }
    
    return 0;
}

QVector<std::tuple<int, int, bool>> JazzBalladPianoPlanner::generateTripletPattern(
    const Context& c, int activity) const {
    
    QVector<std::tuple<int, int, bool>> pattern;
    
    // Triplet patterns: 3 evenly spaced notes per beat
    // We use subdivisions 0, 2, 3 to approximate triplet timing
    // (applyRhythmicFeel will adjust the actual timing)
    
    switch (activity) {
        case 1:
            // Single note - on the beat
            pattern.push_back({0, 0, false});
            break;
        case 2:
            // Two notes - beat and triplet 2
            pattern.push_back({0, 0, true});
            pattern.push_back({2, -5, false});  // Will be shifted to triplet position
            break;
        case 3:
            // Full triplet
            pattern.push_back({0, 0, true});
            pattern.push_back({2, -3, false});
            pattern.push_back({3, -6, false});
            break;
        case 4:
        default:
            // Triplet with added pickup
            pattern.push_back({0, 0, true});
            pattern.push_back({2, -3, true});
            pattern.push_back({3, -5, false});
            break;
    }
    
    return pattern;
}

QVector<std::tuple<int, int, bool>> JazzBalladPianoPlanner::generateHemiolaPattern(
    const Context& c) const {
    
    QVector<std::tuple<int, int, bool>> pattern;
    
    // Hemiola: 3 notes spread across 2 beats
    // Creates rhythmic tension and forward motion
    // We only generate for the first beat of the pair
    // (The pattern continues on the next beat)
    
    // For a 2-beat hemiola, notes fall at:
    // Beat 1: sub 0 (note 1)
    // Beat 1: sub 2.67 (note 2) - between "and" and "a"
    // Beat 2: sub 1.33 (note 3) - between "e" and "and"
    
    // We use sub 0 and sub 3 on beat 1
    pattern.push_back({0, 0, true});   // Hemiola note 1
    pattern.push_back({3, -4, true});  // Hemiola note 2 (will be adjusted)
    
    return pattern;
}

// =============================================================================
// CALL-AND-RESPONSE
// Interactive playing: fills when user pauses, space when user plays
// =============================================================================

void JazzBalladPianoPlanner::updateResponseState(const Context& c) {
    // Detect transition from busy to silence (user just stopped)
    const bool justStopped = (m_state.userWasBusy && c.userSilence);
    
    if (justStopped) {
        // Enter response mode - fill the space left by user
        m_state.inResponseMode = true;
        m_state.responseWindowBeats = 8;  // Always max window (maximally interactive)
        m_state.userLastRegisterHigh = c.userHighMidi;
        m_state.userLastRegisterLow = c.userLowMidi;
    } else if (c.userBusy) {
        // User playing - exit response mode, give them space
        m_state.inResponseMode = false;
        m_state.responseWindowBeats = 0;
    } else if (m_state.responseWindowBeats > 0) {
        // Count down response window
        m_state.responseWindowBeats--;
        if (m_state.responseWindowBeats <= 0) {
            m_state.inResponseMode = false;
        }
    }
    
    // Track user state for next beat
    m_state.userWasBusy = c.userBusy || c.userDensityHigh;
}

bool JazzBalladPianoPlanner::shouldRespondToUser(const Context& c) const {
    // Should we play a fill/response?
    // Yes if: we're in response mode and have interactivity enabled
    return m_state.inResponseMode && 
           m_state.responseWindowBeats > 0;  // Always interactive
}

int JazzBalladPianoPlanner::getResponseRegister(const Context& c, bool complement) const {
    // Get a register for our response based on user's recent playing
    
    const int userMid = (m_state.userLastRegisterHigh + m_state.userLastRegisterLow) / 2;
    const int pianoMid = (c.rhLo + c.rhHi) / 2;
    
    if (complement) {
        // Complementary register: if user played high, we play low; vice versa
        if (userMid > pianoMid) {
            // User was high - we go low
            return c.rhLo + 6;
        } else {
            // User was low - we go high  
            return c.rhHi - 4;
        }
    } else {
        // Echo register: roughly match user's register
        // SAFETY: Ensure min <= max for qBound
        const int safeLo = c.rhLo + 4;
        const int safeHi = qMax(safeLo, c.rhHi - 4);
        return qBound(safeLo, userMid, safeHi);
    }
}

int JazzBalladPianoPlanner::getResponseActivityBoost(const Context& c) const {
    // How much to boost activity when responding to user silence
    // Higher interactivity = more active fills
    
    if (!shouldRespondToUser(c)) return 0;
    
    // Boost is higher early in response window, tapers off
    const double windowProgress = double(m_state.responseWindowBeats) / 8.0;
    const int boost = int(2.0 * windowProgress);  // Max interactivity
    
    return qBound(0, boost, 2);
}

// =============================================================================
// TEXTURE MODES
// Different playing modes for various musical situations
// =============================================================================

JazzBalladPianoPlanner::TextureMode JazzBalladPianoPlanner::determineTextureMode(const Context& c) const {
    // ================================================================
    // AUTOMATIC MODE SELECTION based on context
    // ================================================================
    
    // When user is busy: always sparse comp
    if (c.userBusy || c.userDensityHigh) {
        return TextureMode::Sparse;
    }
    
    // When responding to user: fill mode
    if (shouldRespondToUser(c)) {
        return TextureMode::Fill;
    }
    
    // High energy phrase peaks: lush mode
    if (c.energy >= 0.7 && computePhraseArcPhase(c) == 1) {
        return TextureMode::Lush;
    }
    
    // User silence + high creativity/variability: solo mode (rare)
    if (c.userSilence && 
        energyToCreativity(c.energy) >= 0.4 && 
        energyToVariability(c.energy) >= 0.5 &&
        c.cadence01 < 0.3) {  // Not at cadence
        return TextureMode::Solo;
    }
    
    // Low energy or phrase breathing: sparse mode
    if (c.energy <= 0.3 || 
        (computePhraseArcPhase(c) == 0 && c.barInPhrase == 0)) {
        return TextureMode::Sparse;
    }
    
    // Default: standard comping
    return TextureMode::Comp;
}

void JazzBalladPianoPlanner::applyTextureMode(
    TextureMode mode, int& lhActivity, int& rhActivity, 
    bool& preferDyads, bool& preferTriads) const {
    
    switch (mode) {
        case TextureMode::Sparse:
            // Ultra-sparse: minimal everything
            rhActivity = qMin(rhActivity, 1);
            preferDyads = false;
            preferTriads = false;
            break;
            
        case TextureMode::Comp:
            // Standard comping: moderate LH, light RH
            rhActivity = qMin(rhActivity, 2);
            preferDyads = true;
            preferTriads = false;
            break;
            
        case TextureMode::Fill:
            // Fill mode: active RH melodic fills
            rhActivity = qMax(rhActivity, 2);
            preferDyads = true;
            preferTriads = false;
            break;
            
        case TextureMode::Solo:
            // Solo mode: virtuosic RH
            rhActivity = qMax(rhActivity, 3);
            preferDyads = false;  // Single note lines for clarity
            preferTriads = false;
            break;
            
        case TextureMode::Lush:
            // Lush mode: full texture
            rhActivity = qMax(rhActivity, 3);
            preferDyads = true;
            preferTriads = true;  // Allow triads for richness
            break;
    }
}

// =============================================================================
// STYLE PRESETS
// Different pianist styles with characteristic approaches
// =============================================================================

JazzBalladPianoPlanner::StyleProfile JazzBalladPianoPlanner::getStyleProfile(PianistStyle style) {
    StyleProfile p;
    
    switch (style) {
        case PianistStyle::BillEvans:
            // Introspective, quartal voicings, sparse but rich
            // Known for: Rootless voicings, inner voice movement, rubato
            p.voicingSparseness = 0.6;
            p.rhythmicDrive = 0.3;
            p.melodicFocus = 0.7;
            p.useQuartalVoicings = 0.3;
            p.quartalPreference = 0.25;      // Bill loved quartal voicings
            p.innerVoiceMovement = 0.65;     // Signature inner voice motion (increased)
            p.useBlockChords = 0.1;
            p.bluesInfluence = 0.2;
            p.gospelTouches = 0.0;
            p.ornamentProbability = 0.08;    // Subtle, tasteful ornaments
            p.questionAnswerWeight = 0.6;    // Strong Q/A phrasing
            p.breathSpaceWeight = 0.4;       // Lots of musical space
            p.preferredRegisterLow = 52;
            p.preferredRegisterHigh = 82;
            break;
            
        case PianistStyle::RussFreeman:
            // West coast cool, melodic, bluesy touches
            // Known for: Lyrical lines, cool sound, subtle blues
            p.voicingSparseness = 0.5;
            p.rhythmicDrive = 0.4;
            p.melodicFocus = 0.8;
            p.useQuartalVoicings = 0.1;
            p.quartalPreference = 0.1;       // Less quartal, more traditional
            p.innerVoiceMovement = 0.50;     // More inner movement (increased)
            p.useBlockChords = 0.2;
            p.bluesInfluence = 0.4;
            p.gospelTouches = 0.0;
            p.ornamentProbability = 0.12;    // More grace notes (cool style)
            p.questionAnswerWeight = 0.7;    // Strong melodic conversation
            p.breathSpaceWeight = 0.35;      // Good space, not as sparse as Evans
            p.preferredRegisterLow = 50;
            p.preferredRegisterHigh = 80;
            break;
            
        case PianistStyle::OscarPeterson:
            // Driving, virtuosic, block chords
            // Known for: Power, speed, locked hands
            p.voicingSparseness = 0.2;
            p.rhythmicDrive = 0.9;
            p.melodicFocus = 0.6;
            p.useQuartalVoicings = 0.1;
            p.quartalPreference = 0.05;      // Traditional voicings mostly
            p.innerVoiceMovement = 0.40;     // More inner movement (increased)
            p.useBlockChords = 0.5;
            p.bluesInfluence = 0.5;
            p.gospelTouches = 0.3;
            p.ornamentProbability = 0.05;    // Fewer ornaments (more direct)
            p.questionAnswerWeight = 0.4;    // Less conversational
            p.breathSpaceWeight = 0.15;      // Denser, more continuous
            p.preferredRegisterLow = 48;
            p.preferredRegisterHigh = 88;
            break;
            
        case PianistStyle::KeithJarrett:
            // Gospel touches, singing lines, spontaneous
            // Known for: Right hand melody, vocalizing, exploration
            p.voicingSparseness = 0.4;
            p.rhythmicDrive = 0.5;
            p.melodicFocus = 0.9;
            p.useQuartalVoicings = 0.2;
            p.quartalPreference = 0.2;       // Some quartal
            p.innerVoiceMovement = 0.55;     // Good inner movement (increased)
            p.useBlockChords = 0.1;
            p.bluesInfluence = 0.3;
            p.gospelTouches = 0.5;
            p.ornamentProbability = 0.15;    // More ornamental (gospel influence)
            p.questionAnswerWeight = 0.5;    // Moderate Q/A
            p.breathSpaceWeight = 0.25;      // Some space but also flow
            p.preferredRegisterLow = 48;
            p.preferredRegisterHigh = 90;
            break;
            
        case PianistStyle::Default:
        default:
            // Balanced, neutral - good for cool jazz ballads
            p.voicingSparseness = 0.5;
            p.rhythmicDrive = 0.5;
            p.melodicFocus = 0.5;
            p.useQuartalVoicings = 0.15;
            p.quartalPreference = 0.15;
            p.innerVoiceMovement = 0.50;  // More inner movement (increased)
            p.useBlockChords = 0.15;
            p.bluesInfluence = 0.2;
            p.gospelTouches = 0.1;
            p.ornamentProbability = 0.1;
            p.questionAnswerWeight = 0.5;
            p.breathSpaceWeight = 0.3;
            p.preferredRegisterLow = 48;
            p.preferredRegisterHigh = 84;
            break;
    }
    
    return p;
}

void JazzBalladPianoPlanner::applyStyleProfile(const StyleProfile& profile, Context& c) const {
    // Apply style-specific register preferences
    c.rhLo = qMax(c.rhLo, profile.preferredRegisterLow + 12);  // RH is higher
    c.rhHi = qMin(c.rhHi, profile.preferredRegisterHigh);
    
    // Style influences density through its sparseness value
    // Lower sparseness = higher density weight effective
    // (The style profile just influences context; actual decisions use existing logic)
}

// =============================================================================
// MUSIC THEORY: Chord Interval Calculations
// =============================================================================

int JazzBalladPianoPlanner::thirdInterval(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::Minor:
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished:
            return 3;
        case music::ChordQuality::Sus2:
            return 2;
        case music::ChordQuality::Sus4:
            return 5;
        default:
            return 4;
    }
}

int JazzBalladPianoPlanner::fifthInterval(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished:
            return 6;
        case music::ChordQuality::Augmented:
            return 8;
        default:
            return 7;
    }
}

int JazzBalladPianoPlanner::seventhInterval(const music::ChordSymbol& c) {
    if (c.seventh == music::SeventhQuality::Major7) return 11;
    if (c.seventh == music::SeventhQuality::Dim7) return 9;
    if (c.seventh == music::SeventhQuality::Minor7) return 10;
    if (c.extension >= 7) return 10;
    return -1;
}

int JazzBalladPianoPlanner::pcForDegree(const music::ChordSymbol& c, int degree) {
    const int root = (c.rootPc >= 0) ? c.rootPc : 0;

    auto applyAlter = [&](int deg, int basePc) -> int {
        for (const auto& a : c.alterations) {
            if (a.degree == deg) {
                return normalizePc(basePc + a.delta);
            }
        }
        return normalizePc(basePc);
    };
    
    // Check if a specific alteration exists
    auto hasAlteration = [&](int deg) -> bool {
        for (const auto& a : c.alterations) {
            if (a.degree == deg) return true;
        }
        return false;
    };

    const bool isAlt = c.alt && (c.quality == music::ChordQuality::Dominant);
    const bool is6thChord = (c.extension == 6 && c.seventh == music::SeventhQuality::None);
    const bool isMajor = (c.quality == music::ChordQuality::Major);
    const bool isDominant = (c.quality == music::ChordQuality::Dominant);
    const bool isMinor = (c.quality == music::ChordQuality::Minor);

    int pc = root;
    switch (degree) {
        case 1:
            pc = root;
            break;
        case 3:
            pc = normalizePc(root + thirdInterval(c.quality));
            break;
        case 5:
            if (isAlt) {
                // Altered dominant: use b5 or #5 based on alterations
                pc = hasAlteration(5) ? applyAlter(5, normalizePc(root + 7)) : normalizePc(root + 6);
            } else {
                pc = applyAlter(5, normalizePc(root + fifthInterval(c.quality)));
            }
            break;
        case 6:
            // Only return 6th if chord is a 6th chord or has explicit 6th
            if (is6thChord || hasAlteration(6)) {
                pc = applyAlter(6, normalizePc(root + 9));
            } else {
                return -1; // No 6th on this chord
            }
            break;
        case 7:
            if (is6thChord) {
                // 6th chords use 6th as substitute for 7th
                pc = normalizePc(root + 9);
            } else {
                const int iv = seventhInterval(c);
                if (iv < 0) return -1;
                pc = normalizePc(root + iv);
            }
            break;
        case 9:
            // ================================================================
            // 9TH: Only safe to use in certain contexts
            // - Explicit 9th chord (extension >= 9)
            // - Altered dominants (use b9)
            // - Dominant 7ths (natural 9 is safe)
            // - Minor 7ths (natural 9 is safe - dorian) BUT NOT 6th chords!
            // - AVOID on plain triads and maj7 without explicit extension
            // ================================================================
            if (is6thChord) {
                // 6th chords should NOT automatically get a 9th
                // The 6th is the color - adding 9th muddies it
                return -1;
            } else if (isAlt) {
                pc = normalizePc(root + 1); // b9
            } else if (c.extension >= 9 || hasAlteration(9)) {
                pc = applyAlter(9, normalizePc(root + 2));
            } else if (isDominant) {
                // Natural 9 is safe on dom7
                pc = normalizePc(root + 2);
            } else if (isMinor && c.seventh != music::SeventhQuality::None) {
                // Natural 9 is safe on min7 (dorian) but NOT on minor triads or min6
                pc = normalizePc(root + 2);
            } else {
                // Major 7 without explicit 9, minor triads, etc - don't use
                return -1;
            }
            break;
        case 11:
            // ================================================================
            // 11TH: AVOID on major chords! The 11th (even #11) creates
            // dissonance with the 3rd. Only use when explicitly indicated.
            // ================================================================
            if (isMajor) {
                // Only use #11 if explicitly indicated in chord symbol
                if (c.extension >= 11 || hasAlteration(11)) {
                    pc = applyAlter(11, normalizePc(root + 6)); // #11
                } else {
                    return -1; // AVOID 11 on major chords!
                }
            } else if (isDominant) {
                // Dominant: use #11 only if indicated
                if (isAlt || c.extension >= 11 || hasAlteration(11)) {
                    pc = applyAlter(11, normalizePc(root + 6)); // #11
                } else {
                    return -1; // Don't add 11 to plain dominant
                }
            } else if (isMinor) {
                // Minor: natural 11 is OK (dorian/aeolian)
                pc = applyAlter(11, normalizePc(root + 5));
            } else {
                pc = applyAlter(11, normalizePc(root + 5));
            }
            break;
        case 13:
            // ================================================================
            // 13TH: Safe on dominants and when explicitly indicated
            // ================================================================
            if (isAlt) {
                pc = normalizePc(root + 8); // b13
            } else if (c.extension >= 13 || hasAlteration(13)) {
                pc = applyAlter(13, normalizePc(root + 9));
            } else if (isDominant) {
                // Natural 13 is safe on dominant 7
                pc = normalizePc(root + 9);
            } else {
                // Don't add 13 to other chord types
                return -1;
            }
            break;
        default:
            pc = root;
            break;
    }
    return normalizePc(pc);
}

int JazzBalladPianoPlanner::nearestMidiForPc(int pc, int around, int lo, int hi) {
    pc = normalizePc(pc);
    around = clampMidi(around);

    int best = -1;
    int bestDist = 9999;

    for (int m = lo; m <= hi; ++m) {
        if (normalizePc(m) != pc) continue;
        const int d = qAbs(m - around);
        if (d < bestDist) {
            bestDist = d;
            best = m;
        }
    }

    if (best >= 0) return best;

    int m = lo + ((pc - normalizePc(lo) + 12) % 12);
    while (m < lo) m += 12;
    while (m > hi) m -= 12;
    return clampMidi(m);
}

// =============================================================================
// VOICING REALIZATION - Proper Interval Stacking
// =============================================================================

QVector<int> JazzBalladPianoPlanner::realizePcsToMidi(
    const QVector<int>& pcs, int lo, int hi,
    const QVector<int>& prevVoicing, int /*targetTopMidi*/) const {

    if (pcs.isEmpty()) return {};

    QVector<int> midi;
    midi.reserve(pcs.size());

    int prevCenter = (lo + hi) / 2;
    if (!prevVoicing.isEmpty()) {
        int sum = 0;
        for (int m : prevVoicing) sum += m;
        prevCenter = sum / prevVoicing.size();
    }

    for (int pc : pcs) {
        int m = nearestMidiForPc(pc, prevCenter, lo, hi);
        midi.push_back(m);
    }

    std::sort(midi.begin(), midi.end());
    return midi;
}

// Realize a voicing template by stacking intervals properly
// This is the key function for correct Bill Evans voicings!
QVector<int> JazzBalladPianoPlanner::realizeVoicingTemplate(
    const QVector<int>& degrees,
    const music::ChordSymbol& chord,
    int bassMidi,
    int ceiling) const {

    QVector<int> midi;
    midi.reserve(degrees.size());

    // Calculate pitch classes for each degree
    QVector<int> pcs;
    for (int deg : degrees) {
        int pc = pcForDegree(chord, deg);
        if (pc < 0) continue;
        pcs.push_back(pc);
    }

    if (pcs.isEmpty()) return midi;

    // Start from bassMidi and build upward
    int cursor = bassMidi;
    
    // Find MIDI note for bottom PC closest to bassMidi
    const int bottomPc = pcs[0];
    int bottomMidi = cursor;
    while (normalizePc(bottomMidi) != bottomPc && bottomMidi <= ceiling) {
        bottomMidi++;
    }
    if (bottomMidi > ceiling) {
        bottomMidi = bassMidi;
        while (normalizePc(bottomMidi) != bottomPc && bottomMidi >= 36) {
            bottomMidi--;
        }
    }
    
    midi.push_back(bottomMidi);
    cursor = bottomMidi;

    // Stack remaining notes above
    for (int i = 1; i < pcs.size(); ++i) {
        int pc = pcs[i];
        int note = cursor + 1;
        while (normalizePc(note) != pc && note <= ceiling + 12) {
            note++;
        }
        
        if (note > ceiling) {
            note = cursor;
            while (normalizePc(note) != pc && note >= 36) {
                note--;
            }
        }
        
        midi.push_back(note);
        cursor = note;
    }

    return midi;
}

// Calculate voice-leading cost between two voicings
double JazzBalladPianoPlanner::voiceLeadingCost(const QVector<int>& prev,
                                                 const QVector<int>& next) const {
    if (prev.isEmpty()) return 0.0;
    if (next.isEmpty()) return 0.0;

    double cost = 0.0;
    int totalMotion = 0;
    int commonTones = 0;

    QVector<bool> prevUsed(prev.size(), false);
    QVector<bool> nextUsed(next.size(), false);

    // First pass: find common tones
    for (int i = 0; i < next.size(); ++i) {
        int nextPc = normalizePc(next[i]);
        for (int j = 0; j < prev.size(); ++j) {
            if (prevUsed[j]) continue;
            if (normalizePc(prev[j]) == nextPc) {
                totalMotion += qAbs(next[i] - prev[j]);
                prevUsed[j] = true;
                nextUsed[i] = true;
                commonTones++;
                break;
            }
        }
    }

    // Second pass: match remaining by nearest neighbor
    for (int i = 0; i < next.size(); ++i) {
        if (nextUsed[i]) continue;
        
        int bestJ = -1;
        int bestDist = 999;
        for (int j = 0; j < prev.size(); ++j) {
            if (prevUsed[j]) continue;
            int dist = qAbs(next[i] - prev[j]);
            if (dist < bestDist) {
                bestDist = dist;
                bestJ = j;
            }
        }
        
        if (bestJ >= 0) {
            totalMotion += bestDist;
            prevUsed[bestJ] = true;
            nextUsed[i] = true;
        } else {
            totalMotion += 12;
        }
    }

    cost = totalMotion * 0.3;
    cost -= commonTones * 2.0;

    // Soprano stability
    if (prev.size() > 0 && next.size() > 0) {
        int sopMotion = qAbs(next.last() - prev.last());
        if (sopMotion <= 2) cost -= 1.0;
        else if (sopMotion > 7) cost += 2.0;
    }

    // Bass stability
    if (prev.size() > 0 && next.size() > 0) {
        int bassMotion = qAbs(next.first() - prev.first());
        if (bassMotion > 12) cost += 1.5;
    }

    return cost;
}

bool JazzBalladPianoPlanner::isFeasible(const QVector<int>& midiNotes) const {
    if (midiNotes.isEmpty()) return false;
    if (midiNotes.size() > 10) return false;

    for (int m : midiNotes) {
        if (m < 36 || m > 96) return false;
    }

    return true;
}

QVector<int> JazzBalladPianoPlanner::repairVoicing(QVector<int> midi) const {
    if (midi.isEmpty()) return midi;

    for (int& m : midi) {
        if (m < 36) m += 12;
        if (m > 96) m -= 12;
    }

    std::sort(midi.begin(), midi.end());
    return midi;
}

// =============================================================================
// Pedal Logic - Professional Jazz Piano Sustain Technique
// =============================================================================
// KEY PRINCIPLES:
// 1. "Legato pedaling": Lift RIGHT BEFORE (not at) the new chord, then re-catch
// 2. NEVER let pedal blur two different chords together
// 3. Use half-pedal for clarity, full pedal only for effect
// 4. When in doubt, lift the pedal - dry is better than muddy
// =============================================================================

QVector<JazzBalladPianoPlanner::CcIntent> JazzBalladPianoPlanner::planPedal(
    const Context& c, const virtuoso::groove::TimeSignature& ts) const {

    QVector<CcIntent> ccs;
    
    // Calculate how quickly chords are changing
    const bool veryFrequentChanges = (c.beatsUntilChordChange <= 1);
    const bool frequentChanges = (c.beatsUntilChordChange <= 2);

    // ========================================================================
    // RULE 1: On EVERY chord change, do a clean lift-and-catch
    // The lift happens JUST BEFORE the beat, the catch happens AFTER the attack
    // ========================================================================
    if (c.chordIsNew) {
        // LIFT: Happens slightly BEFORE the chord change
        // This is achieved by a negative timing offset or by placing at previous beat's end
        // For simplicity, we lift AT the beat but the short gap clears the old sound
        CcIntent lift;
        lift.cc = 64;
        lift.value = 0;
        lift.startPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
            c.playbackBarIndex, c.beatInBar, 0, 4, ts);
        lift.structural = true;
        lift.logic_tag = "pedal_lift";
        ccs.push_back(lift);

        // CATCH: Re-engage AFTER the chord attack has sounded
        // Delay depends on how fast chords are changing
        int catchDelay = veryFrequentChanges ? 2 : 1;  // 2/16 or 1/16 of a beat
        int catchDenom = 16;
        
        // ====================================================================
        // PEDAL DEPTH: Energy-aware philosophy
        // - Low energy (Evans): Deep pedal, legato, warm connected sound
        // - Mid energy (Hancock): Moderate pedal, balanced articulation
        // - High energy (Tyner/Corea): Light/NO pedal, percussive stabs
        // ====================================================================
        int pedalDepth;
        const bool highEnergyStabMode = (c.energy >= 0.65);
        const bool midEnergyMode = (c.energy >= 0.45 && c.energy < 0.65);
        
        if (highEnergyStabMode) {
            // HIGH ENERGY: Percussive, dry attacks - McCoy Tyner style
            // Invert the energy relationship: MORE energy = LESS pedal
            if (veryFrequentChanges) {
                pedalDepth = 0;  // Completely dry for fast stabs
            } else if (frequentChanges) {
                pedalDepth = 10 + int(15.0 * (1.0 - c.energy));  // 10-20
            } else {
                pedalDepth = 20 + int(20.0 * (1.0 - c.energy));  // 20-35
            }
        } else if (midEnergyMode) {
            // MID ENERGY: Balanced articulation - Herbie style
            if (veryFrequentChanges) {
                pedalDepth = 25 + int(15.0 * c.energy);  // 30-40
            } else if (frequentChanges) {
                pedalDepth = 35 + int(20.0 * c.energy);  // 40-55
            } else {
                pedalDepth = 45 + int(20.0 * c.energy);  // 50-60
            }
        } else {
            // LOW ENERGY: Lyrical, legato - Bill Evans style
            if (veryFrequentChanges) {
                pedalDepth = 40 + int(20.0 * c.energy);  // 40-50
            } else if (frequentChanges) {
                pedalDepth = 55 + int(25.0 * c.energy);  // 55-70
            } else {
                pedalDepth = 65 + int(30.0 * c.energy);  // 65-85
            }
        }
        pedalDepth = qBound(0, pedalDepth, 90);  // Allow zero for stabs
        
        CcIntent engage;
        engage.cc = 64;
        engage.value = pedalDepth;
        engage.startPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
            c.playbackBarIndex, c.beatInBar, catchDelay, catchDenom, ts);
        engage.structural = true;
        engage.logic_tag = "pedal_catch";
        ccs.push_back(engage);
    }
    
    // ========================================================================
    // RULE 2: Pre-emptive lift when a chord change is approaching
    // Lift ~200ms before the next chord to let the sound decay cleanly
    // ========================================================================
    if (!c.chordIsNew && c.beatsUntilChordChange == 1) {
        // Lift at the "and" of the current beat (halfway through)
        CcIntent preemptiveLift;
        preemptiveLift.cc = 64;
        preemptiveLift.value = 0;
        preemptiveLift.startPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
            c.playbackBarIndex, c.beatInBar, 2, 4, ts);
        preemptiveLift.structural = false;
        preemptiveLift.logic_tag = "pedal_pre_lift";
        ccs.push_back(preemptiveLift);
    }
    
    // ========================================================================
    // RULE 3: For sustained chords (2+ beats), do a subtle refresh on beat 3
    // This prevents resonance buildup without being noticeable
    // ========================================================================
    if (!c.chordIsNew && c.beatInBar == 2 && c.beatsUntilChordChange >= 2) {
        // Quick lift-and-catch (almost imperceptible)
        CcIntent quickLift;
        quickLift.cc = 64;
        quickLift.value = 0;
        quickLift.startPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
            c.playbackBarIndex, c.beatInBar, 0, 8, ts);
        quickLift.structural = false;
        quickLift.logic_tag = "pedal_refresh_lift";
        ccs.push_back(quickLift);
        
        CcIntent quickCatch;
        quickCatch.cc = 64;
        quickCatch.value = 40 + int(30.0 * c.energy);  // Lighter on refresh
        quickCatch.startPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
            c.playbackBarIndex, c.beatInBar, 1, 8, ts);
        quickCatch.structural = false;
        quickCatch.logic_tag = "pedal_refresh_catch";
        ccs.push_back(quickCatch);
    }
    
    // ========================================================================
    // RULE 4: Full lift at end of phrases for clean separation
    // ========================================================================
    if (c.phraseEndBar && c.beatInBar == 3) {
        CcIntent phraseLift;
        phraseLift.cc = 64;
        phraseLift.value = 0;
        phraseLift.startPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
            c.playbackBarIndex, c.beatInBar, 2, 4, ts);
        phraseLift.structural = false;
        phraseLift.logic_tag = "phrase_end_lift";
        ccs.push_back(phraseLift);
    }

    return ccs;
}

// =============================================================================
// Gesture Support
// =============================================================================

void JazzBalladPianoPlanner::applyGesture(const Context& /*c*/,
                                           QVector<virtuoso::engine::AgentIntentNote>& /*notes*/,
                                           const virtuoso::groove::TimeSignature& /*ts*/) const {
    // Not implemented yet
}

// =============================================================================
// LH Voicing: Simple, Correct, Guaranteed Consonant
// =============================================================================
// Jazz LH voicings are built from chord tones stacked in close position.
// We use a straightforward approach:
// 1. Get pitch classes for 3rd, 5th, 7th (and optionally 6th for 6 chords)
// 2. Stack them in the LH register (C3-G4, MIDI 48-67)
// 3. Keep the voicing tight (within ~10 semitones span)
// 4. Voice-lead from previous chord for smooth transitions
// =============================================================================

JazzBalladPianoPlanner::LhVoicing JazzBalladPianoPlanner::generateLhRootlessVoicing(const Context& c) const {
    LhVoicing lh;
    const auto& chord = c.chord;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return lh;
    
    // ================================================================
    // STEP 1: Get the pitch classes we need
    // For jazz voicings, we use 3rd, 5th, and 7th (no root - bass plays that)
    // CRITICAL: Check for clusters (adjacent notes 1-2 semitones apart)
    // ================================================================
    const int root = chord.rootPc;
    const int third = pcForDegree(chord, 3);
    const int fifth = pcForDegree(chord, 5);
    const int seventh = pcForDegree(chord, 7);
    const int sixth = pcForDegree(chord, 6); // For 6th chords
    
    const bool is6thChord = (chord.extension == 6 && chord.seventh == music::SeventhQuality::None);
    const bool hasSeventh = (seventh >= 0);
    
    // Helper to check if two pitch classes are too close (1-2 semitones)
    auto tooClose = [](int pc1, int pc2) -> bool {
        if (pc1 < 0 || pc2 < 0) return false;
        int interval = qAbs(pc1 - pc2);
        if (interval > 6) interval = 12 - interval; // Normalize to smaller interval
        return (interval <= 2);
    };
    
    // Check for potential clusters
    const bool fifthSeventhCluster = tooClose(fifth, seventh);
    const bool thirdFifthCluster = tooClose(third, fifth);
    const bool fifthSixthCluster = tooClose(fifth, sixth);
    
    // Collect the pitch classes, AVOIDING clusters
    QVector<int> targetPcs;
    
    // 3rd is always included (it's the most important for chord quality)
    if (third >= 0) targetPcs.push_back(third);
    
    // 5th: include only if it doesn't create clusters
    // On #5 chords, the 5th often clusters with the 7th - OMIT IT
    if (fifth >= 0) {
        bool includeFifth = true;
        if (fifthSeventhCluster) includeFifth = false;  // Omit 5th if too close to 7th
        if (thirdFifthCluster) includeFifth = false;    // Omit 5th if too close to 3rd
        if (is6thChord && fifthSixthCluster) includeFifth = false;
        
        if (includeFifth) {
            targetPcs.push_back(fifth);
        }
    }
    
    // 7th or 6th: include (defines chord quality)
    if (is6thChord && sixth >= 0) {
        targetPcs.push_back(sixth);
    } else if (hasSeventh) {
        targetPcs.push_back(seventh);
    }
    
    // Must have at least 2 notes for a proper voicing
    if (targetPcs.size() < 2) {
        // Fallback: just use 3rd and 7th (guaranteed to be >2 semitones apart on any chord)
        targetPcs.clear();
        if (third >= 0) targetPcs.push_back(third);
        if (hasSeventh) {
            targetPcs.push_back(seventh);
        } else if (fifth >= 0) {
            targetPcs.push_back(fifth);
        }
    }
    
    if (targetPcs.isEmpty()) return lh;
    
    // ================================================================
    // STEP 2: Determine the starting register
    // Voice-lead from previous chord, or start around E3 (MIDI 52)
    // ================================================================
    int startMidi = 52; // E3 - good starting point for LH
    
    if (!m_state.lastLhMidi.isEmpty()) {
        // Center around the previous voicing for smooth voice-leading
        int lastCenter = 0;
        for (int m : m_state.lastLhMidi) lastCenter += m;
        lastCenter /= m_state.lastLhMidi.size();
        startMidi = qBound(50, lastCenter, 60);
    }
    
    // ================================================================
    // STEP 3: Build the voicing by stacking notes upward
    // Start with the lowest pitch class, then stack the rest above it
    // ================================================================
    
    // Find the first note: closest instance of first PC to startMidi
    int firstPc = targetPcs[0];
    int firstMidi = startMidi;
    
    // Search for the closest instance of firstPc
    int bestFirst = -1;
    int bestFirstDist = 999;
    for (int m = 48; m <= 64; ++m) {
        if (normalizePc(m) == firstPc) {
            int dist = qAbs(m - startMidi);
            if (dist < bestFirstDist) {
                bestFirstDist = dist;
                bestFirst = m;
            }
        }
    }
    
    if (bestFirst < 0) return lh; // Shouldn't happen
    
    lh.midiNotes.push_back(bestFirst);
    int cursor = bestFirst;
    
    // Stack remaining notes above the first
    for (int i = 1; i < targetPcs.size(); ++i) {
        int pc = targetPcs[i];
        
        // Find the next instance of this PC above cursor
        int nextMidi = cursor + 1;
        while (normalizePc(nextMidi) != pc && nextMidi < cursor + 12) {
            nextMidi++;
        }
        
        // If we went too high, wrap down
        if (nextMidi >= cursor + 12) {
            nextMidi = cursor + 1;
            while (normalizePc(nextMidi) != pc) {
                nextMidi++;
            }
        }
        
        // Ensure it's in range
        if (nextMidi > 67) nextMidi -= 12;
        if (nextMidi < 48) nextMidi += 12;
        
        lh.midiNotes.push_back(nextMidi);
        cursor = nextMidi;
    }
    
    // Sort the notes
    std::sort(lh.midiNotes.begin(), lh.midiNotes.end());
    
    // ================================================================
    // STEP 4: Validate - ensure notes are properly spaced
    // If voicing spans more than 12 semitones, compress it
    // ================================================================
    if (lh.midiNotes.size() >= 2) {
        int span = lh.midiNotes.last() - lh.midiNotes.first();
        if (span > 12) {
            // Too spread out - move highest note down an octave
            lh.midiNotes.last() -= 12;
            std::sort(lh.midiNotes.begin(), lh.midiNotes.end());
        }
        
        // Ensure all notes are in the LH range
        for (int& m : lh.midiNotes) {
            while (m < 48) m += 12;
            while (m > 67) m -= 12;
        }
        std::sort(lh.midiNotes.begin(), lh.midiNotes.end());
    }
    
    // ================================================================
    // STEP 5: Final validation - check for clusters (shouldn't happen with 3-5-7)
    // ================================================================
    bool hasCluster = false;
    for (int i = 0; i < lh.midiNotes.size() - 1; ++i) {
        if (lh.midiNotes[i + 1] - lh.midiNotes[i] <= 1) {
            hasCluster = true;
            break;
        }
    }
    
    if (hasCluster) {
        // This shouldn't happen with proper 3-5-7 voicings
        // Fall back to just 3rd and 7th (guaranteed 3+ semitones apart)
        lh.midiNotes.clear();
        if (third >= 0) {
            int thirdMidi = 52;
            while (normalizePc(thirdMidi) != third) thirdMidi++;
            lh.midiNotes.push_back(thirdMidi);
        }
        if (seventh >= 0 || (is6thChord && sixth >= 0)) {
            int topPc = is6thChord ? sixth : seventh;
            int topMidi = lh.midiNotes.isEmpty() ? 52 : lh.midiNotes.last() + 3;
            while (normalizePc(topMidi) != topPc && topMidi < 67) topMidi++;
            if (topMidi <= 67) lh.midiNotes.push_back(topMidi);
        }
        std::sort(lh.midiNotes.begin(), lh.midiNotes.end());
    }
    
    // Set ontology key based on voicing size
    if (lh.midiNotes.size() >= 3) {
        lh.ontologyKey = "piano_lh_voicing";
    } else if (lh.midiNotes.size() == 2) {
        lh.ontologyKey = "piano_lh_shell";
    } else {
        lh.ontologyKey = "piano_lh_single";
    }
    
    lh.isTypeA = (chord.rootPc <= 5);
    lh.cost = voiceLeadingCost(m_state.lastLhMidi, lh.midiNotes);
    
    return lh;
}

// =============================================================================
// LH INNER VOICE MOVEMENT
// Creates melodic motion within sustained voicings - makes LH feel alive
// =============================================================================

JazzBalladPianoPlanner::LhVoicing JazzBalladPianoPlanner::LhVoicing::getAlternateVoicing() const {
    LhVoicing alt = *this;
    if (alt.midiNotes.size() < 2) return alt;
    
    // Invert by moving bottom note up an octave
    if (alt.midiNotes[0] + 12 <= 67) {
        alt.midiNotes[0] += 12;
        std::sort(alt.midiNotes.begin(), alt.midiNotes.end());
        alt.ontologyKey = "piano_lh_inversion";
    }
    return alt;
}

JazzBalladPianoPlanner::LhVoicing JazzBalladPianoPlanner::LhVoicing::withInnerVoiceMovement(
    int direction, int targetPc) const {
    
    LhVoicing moved = *this;
    if (moved.midiNotes.size() < 2) return moved;
    
    // Choose the inner voice to move (not top or bottom - they anchor the voicing)
    // For 3-note voicings, move the middle note
    // For 2-note voicings, move the bottom slightly
    
    int moveIndex = (moved.midiNotes.size() >= 3) ? 1 : 0;
    int originalNote = moved.midiNotes[moveIndex];
    
    // Move by 1-2 semitones in the specified direction
    int delta = (direction > 0) ? 1 : -1;
    if (targetPc >= 0) {
        // Move toward target pitch class
        int targetMidi = originalNote;
        while (targetMidi % 12 != targetPc && qAbs(targetMidi - originalNote) < 4) {
            targetMidi += delta;
        }
        if (qAbs(targetMidi - originalNote) <= 3 && targetMidi >= 48 && targetMidi <= 67) {
            moved.midiNotes[moveIndex] = targetMidi;
        }
    } else {
        // Simple stepwise movement
        int newNote = originalNote + delta;
        if (newNote >= 48 && newNote <= 67) {
            // Verify it doesn't create a cluster with adjacent notes
            bool safe = true;
            for (int i = 0; i < moved.midiNotes.size(); ++i) {
                if (i != moveIndex && qAbs(moved.midiNotes[i] - newNote) <= 1) {
                    safe = false;
                    break;
                }
            }
            if (safe) {
                moved.midiNotes[moveIndex] = newNote;
            }
        }
    }
    
    std::sort(moved.midiNotes.begin(), moved.midiNotes.end());
    moved.ontologyKey = "piano_lh_inner_move";
    return moved;
}

JazzBalladPianoPlanner::LhVoicing JazzBalladPianoPlanner::applyInnerVoiceMovement(
    const LhVoicing& base, const Context& c, int beatInBar) const {
    
    // Inner voice movement happens on beat 3 of sustained chords
    // Creates subtle motion that makes the harmony breathe
    
    if (c.chordIsNew) {
        // New chord - no inner movement yet
        return base;
    }
    
    if (beatInBar != 2) {
        // Only move on beat 3 (creates antiphonal motion)
        return base;
    }
    
    // Determine direction based on state
    int dir = (m_state.lastInnerVoiceIndex % 2 == 0) ? 1 : -1;
    
    // Target a color tone (9th or 13th) if available
    int targetPc = -1;
    int ninth = pcForDegree(c.chord, 9);
    int thirteenth = pcForDegree(c.chord, 13);
    
    if (energyToTension(c.energy) > 0.4 && ninth >= 0) {
        targetPc = ninth;
    } else if (energyToTension(c.energy) > 0.6 && thirteenth >= 0) {
        targetPc = thirteenth;
    }
    
    return base.withInnerVoiceMovement(dir, targetPc);
}

// =============================================================================
// LH QUARTAL VOICINGS (McCoy Tyner style)
// Stacked 4ths create open, modern sound - perfect for ballads
// =============================================================================

JazzBalladPianoPlanner::LhVoicing JazzBalladPianoPlanner::generateLhQuartalVoicing(const Context& c) const {
    LhVoicing lh;
    const auto& chord = c.chord;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return lh;
    
    // Quartal voicings: stack perfect 4ths (5 semitones)
    // Start from a chord tone and stack up
    
    const int root = chord.rootPc;
    const int fifth = pcForDegree(chord, 5);
    const int ninth = pcForDegree(chord, 9);
    
    // Start from the 5th of the chord (common quartal starting point)
    int startPc = (fifth >= 0) ? fifth : root;
    
    // Find starting MIDI note
    int startMidi = 50;
    while (startMidi % 12 != startPc && startMidi < 55) startMidi++;
    if (startMidi > 55) startMidi -= 12;
    
    // Stack 4ths (5 semitones each)
    lh.midiNotes.push_back(startMidi);
    lh.midiNotes.push_back(startMidi + 5);
    
    // Add third 4th if it fits and creates nice color
    int thirdNote = startMidi + 10;
    if (thirdNote <= 65) {
        lh.midiNotes.push_back(thirdNote);
    }
    
    lh.ontologyKey = "piano_lh_quartal";
    lh.isTypeA = true;
    lh.cost = voiceLeadingCost(m_state.lastLhMidi, lh.midiNotes);
    
    return lh;
}

// NOTE: Upper Structure Triads (UST) generation has been moved to RhVoicingGenerator.
// The functions getUpperStructureTriads() and buildUstVoicing() are now in RhVoicingGenerator.

// =============================================================================
// MELODIC FRAGMENTS (Lick Library)
// =============================================================================
// Pre-composed melodic gestures that make the piano sound intentional and
// musical. These are the building blocks of jazz piano vocabulary.
//
// Key concepts:
//   - Approach notes lead into chord tones chromatically or diatonically
//   - Enclosures surround a target from above and below
//   - Scale runs create forward motion
//   - Turns ornament a sustained note
//   - Resolutions create tension-release
// =============================================================================

QVector<JazzBalladPianoPlanner::MelodicFragment> JazzBalladPianoPlanner::getMelodicFragments(
    const Context& c, int targetPc) const {
    
    QVector<MelodicFragment> fragments;
    
    const double tensionLevel = energyToTension(c.energy);
    const double creativity = energyToCreativity(c.energy);
    const bool isDominant = (c.chord.quality == music::ChordQuality::Dominant);
    
    // ========================================================================
    // APPROACH NOTES - Lead into the target
    // ========================================================================
    
    // Chromatic approach from below (very common, sounds great)
    fragments.push_back({
        FragmentType::Approach,
        {-1, 0},           // Half step below, then target
        {0.3, 0.7},        // Short approach, longer target
        {-8, 0},           // Softer approach
        0.1,               // Very safe
        "ChromApproachBelow"
    });
    
    // Chromatic approach from above
    fragments.push_back({
        FragmentType::Approach,
        {1, 0},            // Half step above, then target
        {0.3, 0.7},
        {-8, 0},
        0.15,
        "ChromApproachAbove"
    });
    
    // Diatonic approach (whole step below)
    fragments.push_back({
        FragmentType::Approach,
        {-2, 0},           // Whole step below
        {0.35, 0.65},
        {-5, 0},
        0.05,              // Very safe
        "DiatApproachBelow"
    });
    
    // ========================================================================
    // DOUBLE APPROACH - Two notes leading to target
    // ========================================================================
    
    // Chromatic double approach (classic bebop)
    fragments.push_back({
        FragmentType::DoubleApproach,
        {-2, -1, 0},       // Whole step, half step, target
        {0.25, 0.25, 0.5},
        {-10, -5, 0},
        0.2,
        "DoubleChromBelow"
    });
    
    // Scale approach from above
    fragments.push_back({
        FragmentType::DoubleApproach,
        {4, 2, 0},         // Down by steps
        {0.25, 0.25, 0.5},
        {-8, -4, 0},
        0.15,
        "ScaleApproachAbove"
    });
    
    // ========================================================================
    // ENCLOSURES - Surround the target
    // ========================================================================
    
    // Classic enclosure: above-below-target
    fragments.push_back({
        FragmentType::Enclosure,
        {1, -1, 0},        // Half above, half below, target
        {0.25, 0.25, 0.5},
        {-6, -6, 0},
        0.25,
        "EnclosureAboveBelow"
    });
    
    // Reverse enclosure: below-above-target
    fragments.push_back({
        FragmentType::Enclosure,
        {-1, 1, 0},
        {0.25, 0.25, 0.5},
        {-6, -6, 0},
        0.25,
        "EnclosureBelowAbove"
    });
    
    // Wide enclosure (more dramatic)
    if (tensionLevel > 0.4) {
        fragments.push_back({
            FragmentType::Enclosure,
            {2, -1, 0},    // Whole step above, half below
            {0.3, 0.2, 0.5},
            {-4, -8, 0},
            0.35,
            "WideEnclosure"
        });
    }
    
    // ========================================================================
    // TURNS - Ornamental figures
    // ========================================================================
    
    if (creativity > 0.3) {
        // Upper turn
        fragments.push_back({
            FragmentType::Turn,
            {0, 2, 0, -1, 0},  // Note, step up, back, step down, back
            {0.2, 0.15, 0.15, 0.15, 0.35},
            {0, -5, -3, -8, 0},
            0.3,
            "UpperTurn"
        });
        
        // Lower turn (mordent-like)
        fragments.push_back({
            FragmentType::Turn,
            {0, -1, 0},
            {0.4, 0.2, 0.4},
            {0, -10, 0},
            0.2,
            "LowerMordent"
        });
    }
    
    // ========================================================================
    // ARPEGGIOS - Broken chord figures
    // ========================================================================
    
    // Ascending arpeggio (root-3-5 or 3-5-7)
    fragments.push_back({
        FragmentType::ArpeggioUp,
        {0, 3, 7},         // Triad intervals (will be adjusted to chord)
        {0.3, 0.3, 0.4},
        {-5, -3, 0},
        0.1,
        "ArpUp_Triad"
    });
    
    // Descending arpeggio
    fragments.push_back({
        FragmentType::ArpeggioDown,
        {7, 3, 0},
        {0.3, 0.3, 0.4},
        {0, -3, -5},
        0.1,
        "ArpDown_Triad"
    });
    
    // ========================================================================
    // SCALE RUNS - Forward motion
    // ========================================================================
    
    if (c.energy > 0.4) {
        // 3-note ascending scale
        fragments.push_back({
            FragmentType::ScaleRun3,
            {-4, -2, 0},   // Scale degrees leading to target
            {0.25, 0.25, 0.5},
            {-8, -4, 0},
            0.2,
            "ScaleRun3Up"
        });
        
        // 3-note descending scale
        fragments.push_back({
            FragmentType::ScaleRun3,
            {4, 2, 0},
            {0.25, 0.25, 0.5},
            {0, -4, -8},
            0.2,
            "ScaleRun3Down"
        });
    }
    
    if (c.energy > 0.6 && creativity > 0.4) {
        // 4-note scale run (more dramatic)
        fragments.push_back({
            FragmentType::ScaleRun4,
            {-7, -5, -2, 0},
            {0.2, 0.2, 0.2, 0.4},
            {-10, -6, -3, 0},
            0.35,
            "ScaleRun4Up"
        });
    }
    
    // ========================================================================
    // RESOLUTION - Tension to resolution
    // ========================================================================
    
    if (isDominant && tensionLevel > 0.3) {
        // Tritone resolution (classic jazz)
        fragments.push_back({
            FragmentType::Resolution,
            {6, 0},        // Tritone resolving down
            {0.4, 0.6},
            {5, 0},        // Tension note slightly louder
            0.5,
            "TritoneRes"
        });
        
        // b9 to root resolution
        fragments.push_back({
            FragmentType::Resolution,
            {1, 0},        // Half step down resolution
            {0.35, 0.65},
            {3, 0},
            0.45,
            "b9Resolution"
        });
    }
    
    // ========================================================================
    // OCTAVE DISPLACEMENT - For drama
    // ========================================================================
    
    if (c.energy > 0.7 && creativity > 0.5) {
        fragments.push_back({
            FragmentType::Octave,
            {-12, 0},      // Octave below then target
            {0.4, 0.6},
            {-3, 5},       // Crescendo into target
            0.3,
            "OctaveLeap"
        });
    }
    
    // Sort by tension level (safest first for lower tension contexts)
    std::sort(fragments.begin(), fragments.end(),
              [](const MelodicFragment& a, const MelodicFragment& b) {
                  return a.tensionLevel < b.tensionLevel;
              });
    
    return fragments;
}

QVector<JazzBalladPianoPlanner::FragmentNote> JazzBalladPianoPlanner::applyMelodicFragment(
    const Context& c,
    const MelodicFragment& fragment,
    int targetMidi,
    int startSub) const {
    
    QVector<FragmentNote> notes;
    
    if (fragment.intervalPattern.isEmpty()) return notes;
    
    // ========================================================================
    // BUILD CHORD SCALE - All notes that are consonant with this chord
    // This prevents fragments from clashing with the harmony
    // ========================================================================
    QVector<int> chordScalePcs;
    
    // Core chord tones (always safe)
    int root = c.chord.rootPc;
    int third = pcForDegree(c.chord, 3);
    int fifth = pcForDegree(c.chord, 5);
    int seventh = pcForDegree(c.chord, 7);
    int ninth = pcForDegree(c.chord, 9);
    int thirteenth = pcForDegree(c.chord, 13);
    
    if (root >= 0) chordScalePcs.push_back(root);
    if (third >= 0) chordScalePcs.push_back(third);
    if (fifth >= 0) chordScalePcs.push_back(fifth);
    if (seventh >= 0) chordScalePcs.push_back(seventh);
    if (ninth >= 0) chordScalePcs.push_back(ninth);
    if (thirteenth >= 0) chordScalePcs.push_back(thirteenth);
    
    // Add scale tones based on chord quality (fill gaps for stepwise motion)
    // BE CAREFUL: avoid notes that create minor 2nds with chord tones!
    const bool isDominant = (c.chord.quality == music::ChordQuality::Dominant);
    const bool isMajor = (c.chord.quality == music::ChordQuality::Major);
    const bool isMinor = (c.chord.quality == music::ChordQuality::Minor);
    const bool isAugmented = (c.chord.quality == music::ChordQuality::Augmented);
    
    // Check for altered 5ths
    bool hasSharp5 = false;
    bool hasFlat5 = false;
    for (const auto& alt : c.chord.alterations) {
        if (alt.degree == 5) {
            if (alt.delta > 0) hasSharp5 = true;
            if (alt.delta < 0) hasFlat5 = true;
        }
    }
    
    if (isMajor) {
        // Major/Lydian: add 2 (9), #4 (lydian), 6 (13)
        if (ninth < 0) chordScalePcs.push_back(normalizePc(root + 2));
        // DON'T add natural 4 on major (it's the avoid note!)
        // Only add #4 if it's a lydian chord
        if (thirteenth < 0) chordScalePcs.push_back(normalizePc(root + 9));
    } else if (isMinor) {
        // Dorian: add 2, 4, 6
        if (ninth < 0) chordScalePcs.push_back(normalizePc(root + 2));
        chordScalePcs.push_back(normalizePc(root + 5)); // 11 (4th) - OK on minor!
        chordScalePcs.push_back(normalizePc(root + 9)); // 13 (6th) - dorian
    } else if (isDominant) {
        // Mixolydian: add 2, 6
        // DON'T add the 4th (F over C7) - it's a minor 2nd above the 3rd (E)!
        if (ninth < 0) chordScalePcs.push_back(normalizePc(root + 2));
        if (thirteenth < 0) chordScalePcs.push_back(normalizePc(root + 9));
        
        // If chord has #5, don't add natural 5
        // If chord has natural 5, add it as passing tone
        if (!hasSharp5 && !hasFlat5 && fifth >= 0) {
            // Natural 5 is already in chord tones, OK
        }
    } else if (isAugmented) {
        // Whole tone scale fragments for augmented
        if (ninth < 0) chordScalePcs.push_back(normalizePc(root + 2));
        // #4/b5 is in the whole tone scale
        chordScalePcs.push_back(normalizePc(root + 6)); // #4/b5
    }
    
    // SAFETY: Remove any notes that are a minor 2nd from chord tones
    // This prevents clashes like F against E (4th vs 3rd on C7)
    // Also check root, 9th, and 13th for b9 chords etc.
    QVector<int> allChordPcs;
    if (root >= 0) allChordPcs.push_back(root);
    if (third >= 0) allChordPcs.push_back(third);
    if (fifth >= 0) allChordPcs.push_back(fifth);
    if (seventh >= 0) allChordPcs.push_back(seventh);
    if (ninth >= 0) allChordPcs.push_back(ninth);
    if (thirteenth >= 0) allChordPcs.push_back(thirteenth);
    
    QVector<int> safeScalePcs;
    for (int scalePc : chordScalePcs) {
        bool clashes = false;
        // Check against ALL chord tones
        for (int chordPc : allChordPcs) {
            int interval = qAbs(scalePc - chordPc);
            if (interval > 6) interval = 12 - interval; // Normalize to smaller interval
            if (interval == 1) {
                clashes = true;
                break;
            }
        }
        if (!clashes) {
            safeScalePcs.push_back(scalePc);
        }
    }
    chordScalePcs = safeScalePcs;
    
    // Sort and deduplicate
    std::sort(chordScalePcs.begin(), chordScalePcs.end());
    chordScalePcs.erase(std::unique(chordScalePcs.begin(), chordScalePcs.end()), chordScalePcs.end());
    
    // Build MIDI lookup for all chord scale notes near target
    QVector<int> chordScaleMidi;
    for (int offset = -14; offset <= 14; offset++) {
        int midi = targetMidi + offset;
        if (midi < c.rhLo - 2 || midi > c.rhHi + 2) continue;
        int pc = normalizePc(midi);
        for (int scalePc : chordScalePcs) {
            if (pc == scalePc) {
                chordScaleMidi.push_back(midi);
                break;
            }
        }
    }
    std::sort(chordScaleMidi.begin(), chordScaleMidi.end());
    
    // Helper: snap a note to the nearest chord scale tone
    auto snapToChordScale = [&](int midi) -> int {
        if (chordScaleMidi.isEmpty()) return midi;
        
        int best = midi;
        int bestDist = 999;
        for (int scaleMidi : chordScaleMidi) {
            int dist = qAbs(scaleMidi - midi);
            if (dist < bestDist) {
                bestDist = dist;
                best = scaleMidi;
            }
        }
        return best;
    };
    
    // For arpeggios, use actual chord tones only
    bool useChordTones = (fragment.type == FragmentType::ArpeggioUp || 
                          fragment.type == FragmentType::ArpeggioDown);
    
    QVector<int> chordMidi;
    if (useChordTones) {
        for (int offset = -12; offset <= 12; offset++) {
            int midi = targetMidi + offset;
            if (midi < c.rhLo || midi > c.rhHi) continue;
            int pc = normalizePc(midi);
            // Only true chord tones (not scale tones)
            if (pc == root || pc == third || pc == fifth || pc == seventh) {
                chordMidi.push_back(midi);
            }
        }
        std::sort(chordMidi.begin(), chordMidi.end());
    }
    
    int currentSub = startSub;
    
    for (int i = 0; i < fragment.intervalPattern.size(); ++i) {
        FragmentNote fn;
        int rawMidi;
        
        if (useChordTones && !chordMidi.isEmpty()) {
            // For arpeggios, pick from actual chord tones
            int idx = qBound(0, i, chordMidi.size() - 1);
            if (fragment.type == FragmentType::ArpeggioDown) {
                idx = chordMidi.size() - 1 - idx;
            }
            rawMidi = chordMidi[idx];
        } else {
            // Apply interval pattern
            rawMidi = targetMidi + fragment.intervalPattern[i];
        }
        
        // ================================================================
        // CONSONANCE CHECK: Snap ALL notes to chord scale
        // STRICT: No raw intervals allowed - everything must be validated
        // This eliminates chromatic approach notes which can cause dissonance
        // ================================================================
        bool isTargetNote = (fragment.intervalPattern[i] == 0);
        
        if (isTargetNote) {
            // Target stays as-is (should already be a chord tone)
            fn.midiNote = rawMidi;
        } else {
            // ALL non-target notes: snap to chord scale for consonance
            // This is stricter than before but eliminates dissonance
            fn.midiNote = snapToChordScale(rawMidi);
        }
        
        // Verify the snapped note is within an octave of the target
        // If too far, snap to a closer chord tone
        if (qAbs(fn.midiNote - targetMidi) > 7) {
            // Try snapping the raw note from the other direction
            int alternate = snapToChordScale(rawMidi + (rawMidi < targetMidi ? 12 : -12));
            if (qAbs(alternate - targetMidi) < qAbs(fn.midiNote - targetMidi)) {
                fn.midiNote = alternate;
            }
        }
        
        // Ensure within range
        fn.midiNote = qBound(c.rhLo, fn.midiNote, c.rhHi);
        
        // Calculate timing
        fn.subBeatOffset = currentSub;
        
        // Duration from pattern
        fn.durationMult = (i < fragment.rhythmPattern.size()) ? fragment.rhythmPattern[i] : 0.5;
        
        // Velocity from pattern
        fn.velocityDelta = (i < fragment.velocityPattern.size()) ? fragment.velocityPattern[i] : 0;
        
        notes.push_back(fn);
        
        // Advance sub-beat position (simplified - assumes 4 subs per beat)
        if (i < fragment.rhythmPattern.size() - 1) {
            double nextDur = fragment.rhythmPattern[i];
            currentSub += qMax(1, int(nextDur * 4)); // Convert to 16th note position
            if (currentSub >= 4) currentSub = 3; // Cap at end of beat
        }
    }
    
    return notes;
}

// =============================================================================
// PHRASE COMPING PATTERNS - The Core Innovation for Beautiful Phrasing
// =============================================================================
// 
// These patterns define WHERE to play across a 2-4 bar phrase.
// The key insight: real jazz pianists think in PHRASES, not beats.
// They plan: "catch beat 1, lay out, hit 'and of 3', land beat 1 next bar"
//
// Benefits over beat-by-beat decisions:
// 1. Default is REST - only play when pattern says so
// 2. Consistent voicing style throughout phrase
// 3. Melodic contour planned in advance
// 4. Creates musical SPACE - the hallmark of great ballad playing
// =============================================================================

QVector<JazzBalladPianoPlanner::PhraseCompPattern> JazzBalladPianoPlanner::getAvailablePhrasePatterns(
    const Context& c) const {
    
    QVector<PhraseCompPattern> patterns;
    
    // ========================================================================
    // PATTERN 1: "Sparse Ballad" - The Bill Evans signature
    // Just 2-3 voicings across 4 bars. Maximum space, maximum beauty.
    // ========================================================================
    {
        PhraseCompPattern p;
        p.name = "sparse_ballad";
        p.bars = 4;
        p.densityRating = 0.15;
        p.preferHighRegister = false;
        p.melodicContour = "arch";
        
        // Bar 1, beat 1: Statement voicing
        p.hits.push_back({0, 0, 0, 0, 0, 0, true, false, "statement"});
        
        // Bar 2, beat 3 and-of: Soft response
        p.hits.push_back({1, 2, 2, 1, -8, 15, false, false, "response"});
        
        // Bar 3, beat 1: Resolution/restatement
        p.hits.push_back({2, 0, 0, 0, -3, -10, true, false, "resolution"});
        
        patterns.push_back(p);
    }
    
    // ========================================================================
    // PATTERN 2: "Charleston Feel" - Classic jazz rhythm
    // Beat 1, then "and of 2" - creates forward motion
    // ========================================================================
    {
        PhraseCompPattern p;
        p.name = "charleston";
        p.bars = 2;
        p.densityRating = 0.25;
        p.preferHighRegister = true;
        p.melodicContour = "rise";
        
        // Bar 1, beat 1: On the beat
        p.hits.push_back({0, 0, 0, 0, 0, -5, true, false, "statement"});
        
        // Bar 1, and-of-2: The "Charleston" hit
        p.hits.push_back({0, 1, 2, 1, -5, 0, false, false, "syncopation"});
        
        // Bar 2, beat 1: Resolution
        p.hits.push_back({1, 0, 0, 0, -3, 5, false, false, "resolution"});
        
        patterns.push_back(p);
    }
    
    // ========================================================================
    // PATTERN 3: "Breath" - Ultra sparse, just one chord statement
    // For moments when less is more
    // ========================================================================
    {
        PhraseCompPattern p;
        p.name = "breath";
        p.bars = 4;
        p.densityRating = 0.08;
        p.preferHighRegister = false;
        p.melodicContour = "level";
        
        // Just one voicing at the start
        p.hits.push_back({0, 0, 0, 0, 0, 0, true, false, "statement"});
        
        // Maybe a soft touch on bar 3
        p.hits.push_back({2, 2, 0, 2, -12, 20, false, false, "breath"});
        
        patterns.push_back(p);
    }
    
    // ========================================================================
    // PATTERN 4: "Anticipation" - Pickup to next phrase
    // Builds toward the next chord change
    // ========================================================================
    {
        PhraseCompPattern p;
        p.name = "anticipation";
        p.bars = 2;
        p.densityRating = 0.20;
        p.preferHighRegister = true;
        p.melodicContour = "rise";
        
        // Bar 1, beat 1: Grounding
        p.hits.push_back({0, 0, 0, 0, 0, 0, true, false, "statement"});
        
        // Bar 2, and-of-4: Pickup (anticipates next bar)
        p.hits.push_back({1, 3, 2, 1, -5, -20, false, true, "pickup"});
        
        patterns.push_back(p);
    }
    
    // ========================================================================
    // PATTERN 5: "Dialogue" - Question and answer within phrase
    // Two statements that relate to each other
    // ========================================================================
    {
        PhraseCompPattern p;
        p.name = "dialogue";
        p.bars = 4;
        p.densityRating = 0.22;
        p.preferHighRegister = true;
        p.melodicContour = "arch";
        
        // Bar 1, beat 1: Question
        p.hits.push_back({0, 0, 0, 0, 0, 0, true, false, "question"});
        
        // Bar 2, beat 3: Let question breathe, then soft touch
        p.hits.push_back({1, 2, 0, 2, -10, 10, false, false, "breath"});
        
        // Bar 3, beat 1: Answer (lower register)
        p.hits.push_back({2, 0, 0, 1, 0, 0, true, false, "answer"});
        
        // Bar 4, beat 2: Resolution
        p.hits.push_back({3, 1, 2, 2, -8, 15, false, false, "resolution"});
        
        patterns.push_back(p);
    }
    
    // ========================================================================
    // PATTERN 6: "Rubato Phrase" - Free timing feel
    // Hits are intentionally laid back or pushed
    // ========================================================================
    {
        PhraseCompPattern p;
        p.name = "rubato";
        p.bars = 2;
        p.densityRating = 0.20;
        p.preferHighRegister = false;
        p.melodicContour = "fall";
        
        // Beat 1 laid back
        p.hits.push_back({0, 0, 0, 0, 0, 35, true, false, "statement"});
        
        // Beat 3 early (anticipating)
        p.hits.push_back({0, 2, 2, 1, -5, -25, false, false, "anticipation"});
        
        // Next bar beat 1 on time
        p.hits.push_back({1, 0, 0, 0, -3, 0, false, false, "resolution"});
        
        patterns.push_back(p);
    }
    
    // ========================================================================
    // PATTERN 7: "Active" - More hits for high energy moments
    // Still sparse compared to old code, but more motion
    // ========================================================================
    if (c.energy >= 0.5) {
        PhraseCompPattern p;
        p.name = "active";
        p.bars = 2;
        p.densityRating = 0.40;
        p.preferHighRegister = true;
        p.melodicContour = "rise";
        
        // Bar 1: Statement and syncopation
        p.hits.push_back({0, 0, 0, 0, 0, 0, true, false, "statement"});
        p.hits.push_back({0, 2, 2, 1, -3, 0, false, false, "syncopation"});
        
        // Bar 2: More motion
        p.hits.push_back({1, 0, 0, 1, 0, 0, false, false, "continuation"});
        p.hits.push_back({1, 2, 0, 2, -5, 10, false, false, "breath"});
        
        patterns.push_back(p);
    }
    
    // ========================================================================
    // PATTERN 8: "Punctuation" - Short interjections
    // Like a session player adding tasteful accents
    // ========================================================================
    {
        PhraseCompPattern p;
        p.name = "punctuation";
        p.bars = 4;
        p.densityRating = 0.12;
        p.preferHighRegister = true;
        p.melodicContour = "level";
        
        // Just two strategic hits, widely spaced
        p.hits.push_back({0, 2, 0, 2, 0, 0, false, false, "accent"});
        p.hits.push_back({2, 0, 2, 1, -5, -15, false, false, "echo"});
        
        patterns.push_back(p);
    }
    
    // ========================================================================
    // PATTERN 9: "Call Back" - Echo/response to a previous phrase
    // Creates a sense of musical conversation
    // ========================================================================
    {
        PhraseCompPattern p;
        p.name = "callback";
        p.bars = 2;
        p.densityRating = 0.18;
        p.preferHighRegister = false;
        p.melodicContour = "fall";
        
        // Bar 2 only - like responding to something
        p.hits.push_back({1, 0, 0, 0, 0, 20, true, false, "response"});
        p.hits.push_back({1, 2, 2, 2, -6, 0, false, false, "tail"});
        
        patterns.push_back(p);
    }
    
    // ========================================================================
    // PATTERN 10: "Walking" - Gentle movement through phrase
    // For when you want gentle forward motion without being busy
    // ========================================================================
    {
        PhraseCompPattern p;
        p.name = "walking";
        p.bars = 2;
        p.densityRating = 0.28;
        p.preferHighRegister = true;
        p.melodicContour = "rise";
        
        // Hits on 1 and 3 of each bar (like soft quarter note hits)
        p.hits.push_back({0, 0, 0, 0, 0, 0, true, false, "step1"});
        p.hits.push_back({0, 2, 0, 2, -3, 5, false, false, "step2"});
        p.hits.push_back({1, 0, 0, 1, 0, 0, false, false, "step3"});
        
        patterns.push_back(p);
    }
    
    // ========================================================================
    // PATTERN 11: "Spacious" - Ultra-minimal with long silences
    // For the most introspective moments
    // ========================================================================
    {
        PhraseCompPattern p;
        p.name = "spacious";
        p.bars = 8;  // Entire 8-bar phrase with just one or two touches
        p.densityRating = 0.05;
        p.preferHighRegister = false;
        p.melodicContour = "level";
        
        // Just one hit in 8 bars
        p.hits.push_back({0, 0, 0, 0, 0, 0, true, false, "statement"});
        p.hits.push_back({4, 2, 0, 3, -10, 25, false, false, "distant"});
        
        patterns.push_back(p);
    }
    
    return patterns;
}

int JazzBalladPianoPlanner::selectPhrasePattern(const Context& c, quint32 hash) const {
    const auto patterns = getAvailablePhrasePatterns(c);
    if (patterns.isEmpty()) return -1;
    
    // ========================================================================
    // SESSION PLAYER VARIETY: Real musicians don't repeat the same pattern!
    // Use weighted random selection with penalties for recently used patterns
    // ========================================================================
    
    // Target density based on context
    double targetDensity = 0.15;
    targetDensity += c.energy * 0.15;
    if (c.cadence01 > 0.5) targetDensity += 0.08;
    if (c.userBusy || c.userDensityHigh) targetDensity = 0.10;
    
    // Build weighted candidate list
    QVector<QPair<int, double>> candidates;  // (index, weight)
    
    for (int i = 0; i < patterns.size(); ++i) {
        double weight = 1.0;
        
        // Density match (closer = higher weight)
        double densityDiff = qAbs(patterns[i].densityRating - targetDensity);
        weight *= (1.0 - qMin(densityDiff * 2.0, 0.8));  // Max 80% penalty
        
        // VARIETY BONUS: Heavily penalize recently used patterns
        if (i == m_state.phrasePatternIndex) {
            weight *= 0.15;  // 85% penalty for the CURRENT pattern
        }
        if (i == m_state.lastPhrasePatternIndex) {
            weight *= 0.30;  // 70% penalty for the PREVIOUS pattern  
        }
        
        // Register variety: prefer patterns that alternate register
        bool patternPrefersHigh = patterns[i].preferHighRegister;
        if (patternPrefersHigh != m_state.lastPhraseWasHigh) {
            weight *= 1.3;  // 30% bonus for register change
        }
        
        // Random variation (using hash to keep it deterministic for the same position)
        quint32 patternHash = virtuoso::util::StableHash::mix(hash, quint32(i * 7919));
        double randomFactor = 0.7 + 0.6 * ((patternHash % 1000) / 1000.0);
        weight *= randomFactor;
        
        // Section-aware variety: different sections should feel different
        // Use phrase position to influence pattern selection
        quint32 sectionHash = virtuoso::util::StableHash::mix(
            quint32(c.barInPhrase), quint32(i * 3571));
        weight *= 0.8 + 0.4 * ((sectionHash % 100) / 100.0);
        
        candidates.push_back({i, weight});
    }
    
    // Select pattern with weighted probability
    // (Higher weight = more likely, but not deterministic)
    double totalWeight = 0.0;
    for (const auto& cand : candidates) totalWeight += cand.second;
    
    if (totalWeight <= 0.0) return 0;
    
    double randomPoint = (hash % 10000) / 10000.0 * totalWeight;
    double cumulative = 0.0;
    
    for (const auto& cand : candidates) {
        cumulative += cand.second;
        if (randomPoint <= cumulative) {
            return cand.first;
        }
    }
    
    return candidates.last().first;
}

bool JazzBalladPianoPlanner::shouldPlayAtPhrasePosition(
    const Context& c, 
    const PhraseCompPattern& pattern,
    int barInPattern, 
    int beatInBar) const {
    
    // Check if any hit matches this position
    for (const auto& hit : pattern.hits) {
        if (hit.barOffset == barInPattern && hit.beatInBar == beatInBar) {
            return true;
        }
    }
    return false;
}

const JazzBalladPianoPlanner::PhraseCompHit* JazzBalladPianoPlanner::getPhraseHitAt(
    const PhraseCompPattern& pattern,
    int barInPattern, 
    int beatInBar) const {
    
    for (const auto& hit : pattern.hits) {
        if (hit.barOffset == barInPattern && hit.beatInBar == beatInBar) {
            return &hit;
        }
    }
    return nullptr;
}

QVector<int> JazzBalladPianoPlanner::planPhraseContour(
    const Context& c, 
    const PhraseCompPattern& pattern) const {
    
    QVector<int> contour;
    if (pattern.hits.isEmpty()) return contour;
    
    // Determine register based on phrase characteristics
    int baseMidi = 72; // Middle C area
    if (pattern.preferHighRegister) baseMidi = 76;
    if (m_state.lastPhraseWasHigh) baseMidi -= 5; // Alternate register for variety
    
    // Generate contour based on melodic shape
    const int numHits = pattern.hits.size();
    
    if (pattern.melodicContour == "rise") {
        // Start low, end high
        for (int i = 0; i < numHits; ++i) {
            int midi = baseMidi + (i * 3);
            contour.push_back(qBound(c.rhLo, midi, c.rhHi));
        }
    } else if (pattern.melodicContour == "fall") {
        // Start high, end low
        for (int i = 0; i < numHits; ++i) {
            int midi = baseMidi + 8 - (i * 3);
            contour.push_back(qBound(c.rhLo, midi, c.rhHi));
        }
    } else if (pattern.melodicContour == "arch") {
        // Rise to peak, then fall
        for (int i = 0; i < numHits; ++i) {
            int peakPos = numHits / 2;
            int distFromPeak = qAbs(i - peakPos);
            int midi = baseMidi + 6 - (distFromPeak * 3);
            contour.push_back(qBound(c.rhLo, midi, c.rhHi));
        }
    } else {
        // Level: stay in same register
        for (int i = 0; i < numHits; ++i) {
            contour.push_back(qBound(c.rhLo, baseMidi, c.rhHi));
        }
    }
    
    return contour;
}

// LH: Provides harmonic foundation. ALWAYS plays regardless of user activity.
// The LH is the anchor - it doesn't back off, only the RH does.
// 
// Jazz ballad comping style:
// - ALWAYS play on chord changes (defines the harmony)
// - Often add 1-2 additional touches on same chord (tasteful reinforcement)
// - Sometimes delay first hit for jazz feel (anticipation/syncopation)
// - More active at higher energy, sparser at low energy
bool JazzBalladPianoPlanner::shouldLhPlayBeat(const Context& c, quint32 hash) const {
    // ================================================================
    // LH NEVER backs off for user activity - it's the foundation
    // (Only RH becomes sparse when user is playing)
    // ================================================================
    
    // ================================================================
    // GROOVE LOCK: When bass is very active, let it breathe
    // Piano can be slightly sparser to give bass space
    // ================================================================
    const bool complementBass = shouldComplementBass(c);
    
    // Chord changes: always play (groove lock doesn't override this)
    if (c.chordIsNew) {
        return true;
    }
    
    // ================================================================
    // WITHIN A SUSTAINED CHORD: Add tasteful reinforcement hits
    // Jazz pianists don't just hit once and wait - they add subtle touches
    // ================================================================
    
    // Beat 1 (without chord change): strong probability to reinforce
    if (c.beatInBar == 0) {
        double prob = 0.70 + 0.20 * energyToDensity(c.energy);
        // Higher at phrase boundaries (need to be present)
        if (c.barInPhrase == 0 || c.phraseEndBar) prob = 0.85;
        // Groove lock: if bass very active, be slightly sparser
        if (complementBass) prob -= 0.15;
        return (hash % 100) < int(prob * 100);
    }
    
    // Beat 3: secondary strong beat - good for comping
    if (c.beatInBar == 2) {
        double prob = 0.45 + 0.30 * energyToDensity(c.energy);
        // More likely at cadences
        if (c.cadence01 >= 0.4) prob += 0.20;
        // More likely at phrase ends (closing gesture)
        if (c.phraseEndBar) prob += 0.25;
        // At high energy, almost always play
        if (c.energy >= 0.6) prob += 0.20;
        // Groove lock: let bass lead on beat 3
        if (complementBass) prob -= 0.20;
        return (hash % 100) < int(prob * 100);
    }
    
    // Beat 2: syncopated anticipation - INCREASED for more jazz feel
    if (c.beatInBar == 1) {
        // This is the "and of 1" feel - creates forward motion
        // Jazz pianists LOVE this beat for creating momentum
        double prob = 0.35 + 0.25 * c.energy + 0.15 * energyToRhythm(c.energy);
        // More likely approaching cadences
        if (c.cadence01 >= 0.3) prob += 0.20;
        // At phrase peaks, add anticipation
        if (computePhraseArcPhase(c) == 1) prob += 0.15;
        return (hash % 100) < int(prob * 100);
    }
    
    // Beat 4: pickup/anticipation - THE key jazz comping beat!
    if (c.beatInBar == 3) {
        // Jazz pianists often hit the "and of 4" to push into the next bar
        double prob = 0.30 + 0.25 * c.energy;
        // ALWAYS more likely if next beat is a chord change (anticipation)
        if (c.beatsUntilChordChange <= 1) prob += 0.35;
        // Also more likely approaching phrase boundaries
        if (c.barInPhrase >= c.phraseBars - 1) prob += 0.15;
        return (hash % 100) < int(prob * 100);
    }
    
    return false;
}

// RH activity: Melodic color and movement. 
// REVISED: Much more conservative - great pianists leave SPACE!
// Activity 0-1 is the NORM, 2-3 only at climaxes, 4 is exceptional
int JazzBalladPianoPlanner::rhActivityLevel(const Context& c, quint32 hash) const {
    // ================================================================
    // WHEN USER IS PLAYING: RH becomes VERY sparse
    // Piano should SUPPORT, not compete with the soloist
    // ================================================================
    if (c.userBusy || c.userDensityHigh || c.userIntensityPeak) {
        if (c.chordIsNew) {
            return (hash % 100) < 20 ? 1 : 0; // 20% single note on chord changes
        }
        return 0; // Almost never play when user is active
    }
    
    // ================================================================
    // MUSICAL PHRASING: RH plays in phrases, not constantly
    // Great pianists don't play on every beat - they leave space!
    // ================================================================
    
    const int arcPhase = computePhraseArcPhase(c);
    
    // ================================================================
    // ENERGY BOOST: At high energy, RH is MORE active across all phases
    // This creates the driving, exciting feel of an energized performance
    // ================================================================
    const int energyBoost = (c.energy > 0.6) ? 1 : 0;  // +1 at high energy
    
    // ================================================================
    // RESOLVING PHASE (after phrase peak): Can breathe at low energy
    // At high energy: maintain momentum!
    // ================================================================
    if (arcPhase == 2) {
        if (c.chordIsNew) return 2 + energyBoost;
        // At high energy: keep 2-3 notes even during resolution
        if (c.energy > 0.6) {
            return (hash % 100) < 70 ? 2 : 3;
        }
        return (hash % 100) < 60 ? 1 : 2;
    }
    
    // ================================================================
    // WEAK BEATS: More active at high energy for driving rhythm
    // At low energy: lighter for breathing room
    // ================================================================
    const bool isWeakBeat = (c.beatInBar == 1 || c.beatInBar == 3);
    if (isWeakBeat && !c.chordIsNew) {
        if (c.energy > 0.7) {
            // At high energy: weak beats are STRONG! (drives the rhythm)
            return (hash % 100) < 50 ? 2 : 3;
        }
        return (hash % 100) < 65 ? 1 : 2;
    }
    
    // ================================================================
    // BUILDING PHASE: Scales with energy
    // ================================================================
    if (arcPhase == 0) {
        const double phraseProg = double(c.barInPhrase) / qMax(1, c.phraseBars);
        
        // Early in phrase: 1-2 notes (2-3 at high energy)
        if (phraseProg < 0.3) {
            if (c.chordIsNew) return 2 + energyBoost;
            return (hash % 100) < 60 ? 1 + energyBoost : 2 + energyBoost;
        }
        // Mid-phrase building: 1-2 notes (2-3 at high energy)
        if (phraseProg < 0.7) {
            if (c.chordIsNew) return (c.energy > 0.4) ? 3 : 2;
            return (hash % 100) < 50 ? 2 : 1 + energyBoost;
        }
        // Approaching peak: 2-3 notes (3-4 at high energy)
        if (c.chordIsNew) return qMin(4, int(2 + c.energy * 2));
        return (hash % 100) < 60 ? 2 + energyBoost : 1 + energyBoost;
    }
    
    // ================================================================
    // PEAK PHASE: Maximum activity - scales strongly with energy
    // At high energy: FULL DRIVE (3-4 notes)
    // ================================================================
    if (arcPhase == 1) {
        if (c.chordIsNew) {
            // Chord changes at peak: 3-5 based on energy
            int peakActivity = 3;
            if (c.energy > 0.5) peakActivity = 4;
            if (c.energy > 0.8) peakActivity = 5;
            return peakActivity;
        }
        // Non-chord-change beats at peak: 2-4 based on energy
        if (c.energy > 0.7) return 4;
        if (c.energy > 0.4) return 3;
        return 2;
    }
    
    // ================================================================
    // CADENCE: Punctuate clearly
    // ================================================================
    if (c.cadence01 > 0.6) {
        if (c.beatInBar == 0) {
            // Cadence resolution beat: definite statement
            return 3;
        }
        // After cadence beat: lighter
        return 1;
    }
    
    // ================================================================
    // DEFAULT: 1-2 notes, not silence
    // ================================================================
    if (c.chordIsNew) {
        return 2; // Dyad on chord changes
    }
    
    // Non-chord-change, non-special context: still play!
    return (hash % 100) < 50 ? 1 : 2;
}

// Select next melodic target for RH top voice (stepwise preferred)
// CONSONANCE-FIRST: Prioritize guide tones, extensions only when tension warrants
// PHRASE-AWARE: Uses arc position to guide melodic direction and register
int JazzBalladPianoPlanner::selectNextRhMelodicTarget(const Context& c) const {
    int lastTop = (m_state.lastRhTopMidi > 0) ? m_state.lastRhTopMidi : 74;
    
    // ================================================================
    // PHRASE ARC: Get the melodic direction and target from phrase position
    // ================================================================
    const int arcPhase = computePhraseArcPhase(c);
    int arcTarget = getArcTargetMidi(c, arcPhase);
    const int arcDirection = getArcMelodicDirection(arcPhase, c.barInPhrase, c.phraseBars);
    
    // ================================================================
    // CALL-AND-RESPONSE: Blend response register when filling
    // Creates conversational interplay with user
    // SAFETY: Keep target within reasonable bounds, don't over-influence
    // ================================================================
    if (shouldRespondToUser(c)) {
        // Alternate between complement and echo every 2 beats
        const bool complement = (c.beatInBar <= 1);
        const int responseTarget = getResponseRegister(c, complement);
        // Blend arc target with response target - REDUCED influence (40% not 60%)
        // to prevent pulling too far from chord-appropriate notes
        arcTarget = int(arcTarget * 0.6 + responseTarget * 0.4);
        // Clamp to safe RH range - SAFETY: ensure min <= max
        const int arcLo = c.rhLo + 4;
        const int arcHi = qMax(arcLo, c.rhHi - 4);
        arcTarget = qBound(arcLo, arcTarget, arcHi);
    }
    
    // Determine tension level for extension usage
    const double tensionLevel = energyToTension(c.energy);
    
    // ================================================================
    // MOTIF INTEGRATION: If we have a phrase motif, prefer its notes
    // ================================================================
    QVector<int> motifPcs = applyMotifToContext(c, getMotifVariation(c));
    
    // Collect scale tones for melodic motion - CONSONANCE FIRST
    // pcForDegree now returns -1 for inappropriate extensions
    QVector<int> scalePcs;
    int third = pcForDegree(c.chord, 3);
    int fifth = pcForDegree(c.chord, 5);
    int seventh = pcForDegree(c.chord, 7);
    int ninth = pcForDegree(c.chord, 9);
    int thirteenth = pcForDegree(c.chord, 13);
    
    // PRIORITY 0: Motif notes (if available and on phrase-relevant beats)
    const bool useMotif = !motifPcs.isEmpty() && (c.beatInBar == 0 || c.chordIsNew);
    if (useMotif) {
        for (int pc : motifPcs) {
            if (pc >= 0) scalePcs.push_back(pc);
        }
    }
    
    // PRIORITY 1: Guide tones (define the chord)
    if (third >= 0) scalePcs.push_back(third);
    if (seventh >= 0) scalePcs.push_back(seventh);
    
    // PRIORITY 2: Fifth
    if (fifth >= 0) scalePcs.push_back(fifth);
    
    // PRIORITY 3: Extensions (pcForDegree already filters appropriately)
    if (tensionLevel > 0.3) {
        if (ninth >= 0) scalePcs.push_back(ninth);
        if (thirteenth >= 0 && tensionLevel > 0.5) scalePcs.push_back(thirteenth);
    }
    
    if (scalePcs.isEmpty()) return lastTop;
    
    // ================================================================
    // DIRECTION: Combine phrase arc direction with local motion
    // Arc direction provides the overall contour
    // Local direction provides step-by-step guidance
    // ================================================================
    int dir = m_state.rhMelodicDirection;
    
    // Weight arc direction more heavily than local state
    // Arc direction: +1 ascending, 0 neutral, -1 descending
    if (arcDirection != 0) {
        // Blend: arc direction is 60% of influence
        if (arcDirection > 0 && dir <= 0) dir = 1;
        else if (arcDirection < 0 && dir >= 0) dir = -1;
    }
    
    // Strong tendency to move toward arc target
    if (lastTop < arcTarget - 4) dir = 1;
    else if (lastTop > arcTarget + 4) dir = -1;
    
    // Tendency to reverse near boundaries
    if (lastTop >= 80) dir = -1;
    else if (lastTop <= 70) dir = 1;
    else if (m_state.rhMotionsThisChord >= 3) {
        // After a few motions, tend to reverse
        dir = -dir;
    }
    
    // ================================================================
    // HARMONIC ANTICIPATION: When chord change is approaching,
    // prefer notes that will become chord tones in the next chord.
    // This creates forward motion and smooth voice-leading into changes.
    // ================================================================
    QVector<int> nextChordTones;
    const bool approachingChange = c.hasNextChord && c.beatsUntilChordChange <= 2;
    
    if (approachingChange) {
        // Collect the next chord's primary tones
        int nextThird = pcForDegree(c.nextChord, 3);
        int nextFifth = pcForDegree(c.nextChord, 5);
        int nextSeventh = pcForDegree(c.nextChord, 7);
        int nextRoot = c.nextChord.rootPc;
        
        if (nextThird >= 0) nextChordTones.push_back(nextThird);
        if (nextFifth >= 0) nextChordTones.push_back(nextFifth);
        if (nextSeventh >= 0) nextChordTones.push_back(nextSeventh);
        nextChordTones.push_back(nextRoot);
    }
    
    // Find nearest scale tone in preferred direction, preferring proximity to arc target
    int bestTarget = lastTop;
    int bestScore = -999;  // Higher is better
    
    for (int pc : scalePcs) {
        for (int oct = 5; oct <= 7; ++oct) {
            int midi = pc + 12 * oct;
            if (midi < c.rhLo || midi > c.rhHi) continue;
            
            int motion = midi - lastTop;
            bool rightDirection = (dir == 0) || (dir > 0 && motion > 0) || (dir < 0 && motion < 0);
            
            if (qAbs(motion) >= 1 && qAbs(motion) <= 5) {
                // Score: prefer right direction, small steps, and proximity to arc target
                int score = 0;
                if (rightDirection) score += 20;
                score -= qAbs(motion) * 2;  // Prefer small steps
                score -= qAbs(midi - arcTarget) / 2;  // Prefer proximity to arc target
                
                // Bonus for motif notes
                if (useMotif && motifPcs.contains(pc)) score += 10;
                
                // ============================================================
                // HARMONIC ANTICIPATION BONUS: 
                // Notes that are chord tones in the next chord get a big boost
                // This creates smooth voice-leading into chord changes
                // ============================================================
                if (approachingChange && nextChordTones.contains(pc)) {
                    // Bigger bonus when closer to the change
                    int anticipationBonus = (c.beatsUntilChordChange == 1) ? 25 : 15;
                    score += anticipationBonus;
                }
                
                if (score > bestScore) {
                    bestScore = score;
                    bestTarget = midi;
                }
            }
        }
    }
    
    // If no good target, allow any motion (but still consider anticipation)
    if (bestScore == -999) {
        for (int pc : scalePcs) {
            for (int oct = 5; oct <= 7; ++oct) {
                int midi = pc + 12 * oct;
                if (midi < c.rhLo || midi > c.rhHi) continue;
                int motion = qAbs(midi - lastTop);
                if (motion >= 1 && motion <= 6) {
                    int score = -motion - qAbs(midi - arcTarget) / 2;
                    
                    // Still apply anticipation bonus
                    if (approachingChange && nextChordTones.contains(pc)) {
                        score += 15;
                    }
                    
                    if (score > bestScore) {
                        bestScore = score;
                        bestTarget = midi;
                    }
                }
            }
        }
    }
    
    // ================================================================
    // FINAL FALLBACK: If approaching a chord change and we still have
    // no good target, consider notes that resolve BY STEP to next chord tones.
    // E.g., play D if E (next chord 3rd) is coming = approach from below
    // ================================================================
    if (bestScore < 0 && approachingChange && !nextChordTones.isEmpty()) {
        for (int nextPc : nextChordTones) {
            // Try notes a step below and above the next chord tone
            for (int delta : {-2, -1, 1, 2}) {
                int approachPc = (nextPc + delta + 12) % 12;
                // Check if this approach note is at least somewhat consonant with current chord
                bool currentConsonant = scalePcs.contains(approachPc);
                if (!currentConsonant) continue;
                
                for (int oct = 5; oct <= 7; ++oct) {
                    int midi = approachPc + 12 * oct;
                    if (midi < c.rhLo || midi > c.rhHi) continue;
                    int motion = qAbs(midi - lastTop);
                    if (motion <= 5) {
                        int score = 5 - motion;  // Prefer small motion
                        if (score > bestScore) {
                            bestScore = score;
                            bestTarget = midi;
                        }
                    }
                }
            }
        }
    }
    
    return bestTarget;
}

// =============================================================================
// Main Planning Function
// =============================================================================

QVector<virtuoso::engine::AgentIntentNote> JazzBalladPianoPlanner::planBeat(
    const Context& c, int midiChannel, const virtuoso::groove::TimeSignature& ts) {

    auto plan = planBeatWithActions(c, midiChannel, ts);
    return plan.notes;
}

JazzBalladPianoPlanner::BeatPlan JazzBalladPianoPlanner::planBeatWithActions(
    const Context& c, int midiChannel, const virtuoso::groove::TimeSignature& ts) {

    // THREAD SAFETY: Protect all access to m_state
    // Multiple threads can call this concurrently (lookahead, phrase planner, main scheduler)
    QMutexLocker locker(m_stateMutex.get());

    BeatPlan plan;

    Context adjusted = c;
    adjustRegisterForBass(adjusted);
    
    // ================================================================
    // STYLE PRESET: Apply current pianist style characteristics
    // ================================================================
    const StyleProfile styleProfile = getStyleProfile(m_currentStyle);
    applyStyleProfile(styleProfile, adjusted);
    
    // Check if chord changed - reset RH melodic motion counter
    const bool chordChanged = c.chordIsNew || 
        (c.chord.rootPc != m_state.lastChordForRh.rootPc) ||
        (c.chord.quality != m_state.lastChordForRh.quality);
    
    // ================================================================
    // PHRASE-LEVEL PLANNING: Generate motif at phrase start
    // The motif will be developed throughout the phrase for coherence
    // ================================================================
    const bool newPhrase = (adjusted.barInPhrase == 0 && adjusted.beatInBar == 0);
    if (newPhrase || m_state.lastPhraseStartBar < 0) {
        // Generate a new motif for this phrase
        generatePhraseMotif(adjusted);
    }
    
    // Get current phrase arc phase for decisions below
    const int arcPhase = computePhraseArcPhase(adjusted);
    
    // ================================================================
    // CALL-AND-RESPONSE: Update interactive state
    // Detects when user stops playing and enables fill mode
    // ================================================================
    updateResponseState(adjusted);
    const bool responding = shouldRespondToUser(adjusted);
    const int responseBoost = getResponseActivityBoost(adjusted);
    
    // Determinism hashes
    const quint32 lhHash = virtuoso::util::StableHash::mix(
        adjusted.determinismSeed, adjusted.playbackBarIndex * 17 + adjusted.beatInBar);
    const quint32 rhHash = virtuoso::util::StableHash::mix(
        adjusted.determinismSeed, adjusted.playbackBarIndex * 23 + adjusted.beatInBar * 3);
    const quint32 timingHash = virtuoso::util::StableHash::mix(
        adjusted.determinismSeed, adjusted.playbackBarIndex * 31 + adjusted.beatInBar * 7);
    
    const auto mappings = computeWeightMappings(adjusted);
    
    // ================================================================
    // VELOCITY: STRONGLY scales with energy!
    // At high energy, piano should DRIVE the band with stronger touch
    // When user is playing/singing, piano BACKS OFF significantly
    // ================================================================
    int baseVel;
    const double e = adjusted.energy;
    
    if (adjusted.userBusy || adjusted.userDensityHigh || adjusted.userIntensityPeak) {
        // USER IS ACTIVE: Play SOFT to support, not overpower
        // But still scale somewhat with energy (40-65 range)
        baseVel = 40 + int(25.0 * e);
    } else if (adjusted.userSilence) {
        // USER IS SILENT: Full presence! (52-97 range - raised floor for audibility)
        // At high energy, we're DRIVING the music!
        baseVel = 52 + int(45.0 * e);
    } else {
        // NORMAL: Moderate but responsive (48-85 range)
        baseVel = 48 + int(37.0 * e);
    }
    
    // At very low energy, slightly softer but still audible
    if (e < 0.2) {
        baseVel = int(baseVel * 0.92);  // Less reduction than before
    }
    
    // ================================================================
    // PHRASE ARC DYNAMICS: Shape velocity across the phrase
    // Building: crescendo toward peak
    // Peak: boost (bigger at high energy)
    // Resolving: diminuendo
    // ================================================================
    switch (arcPhase) {
        case 0: { // Building - start at base, grow by up to 10%
            const double buildProgress = qBound(0.0, double(adjusted.barInPhrase) / (0.4 * adjusted.phraseBars), 1.0);
            // At start of phrase: 100% of base; at end of building: 110%
            baseVel = int(baseVel * (1.0 + 0.10 * buildProgress));
            break;
        }
        case 1: // Peak - full dynamics (bigger boost at high energy)
            // At peak: 105-115% boost depending on energy
            baseVel = int(baseVel * (1.05 + 0.10 * e));
            break;
        case 2: { // Resolving - diminuendo from base down to 85%
            const int resolveStart = adjusted.barInPhrase - int(0.7 * adjusted.phraseBars);
            const int resolveTotal = adjusted.phraseBars - int(0.7 * adjusted.phraseBars);
            const double resolveProgress = qBound(0.0, double(resolveStart) / qMax(1, resolveTotal), 1.0);
            // Fade from 100% to 85%
            baseVel = int(baseVel * (1.0 - 0.15 * resolveProgress));
            break;
        }
    }
    
    QString pedalId;
    
    // Get pedal from vocabulary if available
    if (hasVocabularyLoaded() && m_vocab != nullptr) {
        virtuoso::vocab::VocabularyRegistry::PianoPedalQuery pedalQ;
        pedalQ.ts = {4, 4};
        pedalQ.playbackBarIndex = adjusted.playbackBarIndex;
        pedalQ.beatInBar = adjusted.beatInBar;
        pedalQ.chordText = adjusted.chordText;
        pedalQ.chordFunction = adjusted.chordFunction;
        pedalQ.chordIsNew = adjusted.chordIsNew;
        pedalQ.userBusy = adjusted.userBusy;
        pedalQ.userSilence = adjusted.userSilence;
        pedalQ.nextChanges = adjusted.nextChanges;
        pedalQ.beatsUntilChordChange = adjusted.beatsUntilChordChange;
        pedalQ.energy = adjusted.energy;
        pedalQ.determinismSeed = adjusted.determinismSeed;
        const auto pedalChoice = m_vocab->choosePianoPedal(pedalQ);
        pedalId = pedalChoice.id;
    }
    
    // ==========================================================================
    // LEFT HAND: Bill Evans-Inspired Voicings
    // ==========================================================================
    // 
    // STUDIED FROM BILL EVANS:
    // - Always full rootless voicings (3-4 notes)
    // - Higher register at higher energy (brighter, more present)
    // - When progressions repeat, shift register to create beautiful lines
    // - Works both at section level AND within sections for local patterns
    //
    // KEY PRINCIPLES:
    // 1. Always full rootless voicings (never sparse)
    // 2. Energy influences register TENDENCY (high energy → higher register)
    // 3. Detect repeating chord patterns and create ascending lines
    // 4. Voice-leading creates smooth connections between voicings
    // ==========================================================================
    
    // ==========================================================================
    // BLOCK CHORD PRE-CHECK (Stage 4)
    // ==========================================================================
    // At very high energy, we may use block chord technique - this replaces
    // normal LH+RH with a unified powerful voicing. Detect early so we can
    // skip regular LH emission when block chord will be used.
    // ==========================================================================
    
    bool isBlockChordMoment = false;
    if (m_enableRightHand && adjusted.chordIsNew && adjusted.energy >= 0.72) {
        const bool userActive = adjusted.userBusy || adjusted.userDensityHigh || adjusted.userIntensityPeak;
        if (!userActive && !m_state.lastLhMidi.isEmpty()) {
            const int blockHash = (adjusted.playbackBarIndex * 31 + adjusted.chord.rootPc * 13) % 100;
            const int blockThreshold = 15 + int((adjusted.energy - 0.7) * 65);
            isBlockChordMoment = (blockHash < blockThreshold);
        }
    }
    
    // Sync generator state for voice-leading continuity
    syncGeneratorState();
    
    // Skip normal LH if block chord will be used
    if (adjusted.chordIsNew && !isBlockChordMoment) {
        auto lhGenContext = toLhContext(adjusted);
        const double energy = adjusted.energy;
        
        // ======================================================================
        // CHORD VOICING MEMORY: Track what we've played for each chord type
        // ======================================================================
        // When a chord appears again, we want DIFFERENT voicing treatment:
        // 1. Different voicing type (Type A vs Type B)
        // 2. Different register (ascending line)
        // 3. Combined, this creates real variety
        // ======================================================================
        
        // Chord signature: combines root + quality into a single key (0-143)
        // 12 roots * 12 quality types = 144 possible chord types
        const int currentRoot = adjusted.chord.rootPc;
        const int currentQuality = static_cast<int>(adjusted.chord.quality);
        const int chordKey = (currentRoot * 12 + currentQuality) % 144;
        
        // Static memory: for each chord type, track appearances and voicings used
        struct ChordMemory {
            int appearanceCount = 0;      // How many times we've seen this chord
            int lastRegisterCenter = 54;  // Where we played it last
            bool lastWasTypeA = true;     // Which voicing type we used
        };
        static ChordMemory chordMemory[144];
        
        // Get memory for this chord
        ChordMemory& mem = chordMemory[chordKey];
        const bool isRepeat = (mem.appearanceCount > 0);
        
        // Full usable range
        lhGenContext.lhLo = 42;
        lhGenContext.lhHi = 70;
        
        // ======================================================================
        // REGISTER CENTER: Energy + Ascending lines on repeats
        // ======================================================================
        
        // Energy-based center (MIDI 52 to 62)
        int energyCenter = 52 + int(energy * 10);
        
        // If this chord has appeared before, create ascending line
        int registerCenter;
        if (isRepeat) {
            // Shift up from last time (ascending line)
            // Each repeat shifts up by 3-4 semitones
            int ascent = 3 + (mem.appearanceCount % 2);  // Alternates 3, 4, 3, 4...
            registerCenter = mem.lastRegisterCenter + ascent;
            
            // Wrap around if too high
            if (registerCenter > 66) {
                registerCenter = 50 + ((registerCenter - 50) % 16);
            }
        } else {
            // First appearance: use energy-based center with section variety
            const int sectionLength = 8;
            const int sectionIndex = adjusted.playbackBarIndex / sectionLength;
            const int barInSection = adjusted.playbackBarIndex % sectionLength;
            
            int sectionOffset = (sectionIndex % 4) * 2;
            int barVariety = ((barInSection * 7) % 5) - 2;
            
            registerCenter = energyCenter + sectionOffset + barVariety;
        }
        
        // Clamp to safe range
        registerCenter = qBound(50, registerCenter, 68);
        
        // ======================================================================
        // VOICING TYPE: Alternate between Type A and Type B on repeats
        // ======================================================================
        // This is the key to variety! Same chord = different voicing structure
        // ======================================================================
        
        bool forceTypeA = false;
        bool forceTypeB = false;
        
        if (isRepeat) {
            // Alternate from last time
            if (mem.lastWasTypeA) {
                forceTypeB = true;
            } else {
                forceTypeA = true;
            }
        }
        
        // ======================================================================
        // VOICE-LEADING MANAGEMENT
        // ======================================================================
        
        static int lastSectionIndex = -1;
        static double lastResetEnergy = 0.5;
        static int lastRegisterCenter = 54;
        
        const int sectionLength = 8;
        const int sectionIndex = adjusted.playbackBarIndex / sectionLength;
        const int barInSection = adjusted.playbackBarIndex % sectionLength;
        
        const bool newSection = (sectionIndex != lastSectionIndex && barInSection == 0);
        const bool energyShift = qAbs(energy - lastResetEnergy) > 0.3;
        const bool registerJump = qAbs(registerCenter - lastRegisterCenter) > 4;
        
        if (newSection || energyShift || registerJump) {
            m_lhGen.resetVoiceLeadingState();
            m_lhGen.state().lastLhMidi = {registerCenter};
            
            lastSectionIndex = sectionIndex;
            lastResetEnergy = energy;
        }
        lastRegisterCenter = registerCenter;
        
        // ======================================================================
        // GENERATE VOICING (with type forcing for variety)
        // ======================================================================
        
        LhVoicingGenerator::LhVoicing voicing;
        
        if (forceTypeA) {
            // Force Type A (starts from 3rd)
            voicing = m_lhGen.generateRootlessFromDegree(lhGenContext, 3);
        } else if (forceTypeB) {
            // Force Type B (starts from 7th)
            voicing = m_lhGen.generateRootlessFromDegree(lhGenContext, 7);
        } else {
            // First appearance: use optimal voice-leading
            voicing = m_lhGen.generateRootlessOptimal(lhGenContext);
        }
        
        // Safety: ensure at least 2 notes
        if (voicing.midiNotes.size() < 2) {
            m_lhGen.resetVoiceLeadingState();
            m_lhGen.state().lastLhMidi = {registerCenter};
            voicing = m_lhGen.generateRootlessOptimal(lhGenContext);
        }
        
        // ======================================================================
        // UPDATE CHORD MEMORY
        // ======================================================================
        mem.appearanceCount++;
        mem.lastRegisterCenter = registerCenter;
        mem.lastWasTypeA = voicing.isTypeA;
        
        // ======================================================================
        // EMIT NOTES
        // ======================================================================
        
        if (!voicing.midiNotes.isEmpty()) {
            
            // ==============================================================
            // PHASE 4A: ANTICIPATIONS & DELAYED ENTRIES
            // ==============================================================
            // Two complementary techniques for timing variety:
            // 1. ANTICIPATION: Play on "& of 4" of previous bar (early, forward)
            // 2. DELAYED ENTRY: Play on "& of 1" (late, relaxed, let bass lead)
            //
            // Anticipation: Creates forward motion, urgency
            // Delayed entry: Creates space, relaxation, let harmony breathe
            // ==============================================================
            
            bool useAnticipation = false;
            bool useDelayedEntry = false;
            
            if (adjusted.beatInBar == 0 && adjusted.playbackBarIndex > 0) {
                
                // ============================================================
                // SAFETY CHECKS: When NOT to use timing variations
                // ============================================================
                
                bool safeToVary = true;
                
                // 1. Don't vary at phrase endings (cadence points)
                //    The resolution needs to land ON the beat
                const bool isPhraseCadence = (adjusted.cadence01 >= 0.5);
                if (isPhraseCadence) {
                    safeToVary = false;
                }
                
                // 2. Don't vary at section starts (first bar of 8-bar section)
                const int barInSection = adjusted.playbackBarIndex % 8;
                if (barInSection == 0) {
                    safeToVary = false;
                }
                
                // 3. Don't vary first few bars of song
                if (adjusted.playbackBarIndex < 2) {
                    safeToVary = false;
                }
                
                // 4. Check harmonic compatibility with previous chord
                const int prevRoot = (currentRoot + 12 - 5) % 12;
                const int rootMotion = qAbs(currentRoot - prevRoot);
                const int normalizedMotion = (rootMotion > 6) ? (12 - rootMotion) : rootMotion;
                
                // Chromatic motion (1 semitone) or tritone (6 semitones) = don't vary
                if (normalizedMotion == 1 || normalizedMotion == 6) {
                    safeToVary = false;
                }
                
                // ============================================================
                // TIMING SELECTION: Anticipation vs Delayed Entry
                // ============================================================
                
                if (safeToVary) {
                    const int timingHash = 
                        (adjusted.playbackBarIndex * 17 + currentRoot * 7) % 100;
                    
                    // Energy influences which technique to use:
                    // - High energy (≥0.6): Prefer anticipation (forward, driving)
                    // - Low energy (<0.4): Prefer delayed entry (relaxed, breathing)
                    // - Mid energy: Both possible, delayed more common
                    //
                    // Delayed entries are MORE COMMON overall - creates relaxed,
                    // breathing feel that lets bass lead. Very Bill Evans.
                    
                    if (energy >= 0.6) {
                        // High energy: ~18% anticipation, ~8% delayed
                        if (timingHash < 18) {
                            useAnticipation = true;
                        } else if (timingHash >= 80 && timingHash < 88) {
                            useDelayedEntry = true;
                        }
                    } else if (energy < 0.4) {
                        // Low energy: ~25% delayed entry (very relaxed feel)
                        // Works on most chords at low energy
                        if (timingHash < 25) {
                            useDelayedEntry = true;
                        }
                    } else {
                        // Mid energy: ~10% anticipation, ~18% delayed
                        if (timingHash < 10) {
                            useAnticipation = true;
                        } else if (timingHash >= 75 && timingHash < 93) {
                            useDelayedEntry = true;
                        }
                    }
                }
            }
            
            // Calculate grid position
            virtuoso::groove::GridPos lhPos;
            
            if (useAnticipation) {
                // ANTICIPATION: Play on "& of 4" of previous bar (early)
                // Creates forward motion, urgency
                lhPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                    adjusted.playbackBarIndex - 1,  // Previous bar
                    3,                               // Beat 4 (0-indexed = 3)
                    2,                               // "And" subdivision (0=beat, 2=and)
                    4, ts);
            } else if (useDelayedEntry) {
                // DELAYED ENTRY: Play on "& of 1" (late)
                // Let bass establish harmony first, relaxed breathing feel
                lhPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                    adjusted.playbackBarIndex,       // Current bar
                    0,                               // Beat 1 (0-indexed = 0)
                    2,                               // "And" subdivision
                    4, ts);
            } else {
                // Normal: on beat 1
                lhPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                    adjusted.playbackBarIndex, adjusted.beatInBar, 0, 4, ts);
            }
            
            // ================================================================
            // PHASE 4D: BPM-AWARE HUMANIZED TIMING + LAY BACK
            // ================================================================
            // Bill Evans' signature: playing slightly BEHIND the beat
            // - Low energy, slow tempo: ~15-20ms behind (very relaxed)
            // - High energy, fast tempo: ~3-5ms (on top of the beat, driving)
            // 
            // Formula: layBackMs = baseLay × (1 - energy×0.7) × (90/bpm)
            // Also add small humanization jitter for natural feel
            // ================================================================
            
            const int bpm = adjusted.bpm > 0 ? adjusted.bpm : 90;  // Default 90 BPM
            const double tempoScale = 90.0 / qBound(50, bpm, 180);  // Normalize to 90 BPM
            
            // Base lay back: 12ms at reference tempo (90 BPM)
            const double baseLay = 12.0;
            
            // Energy reduces lay back (high energy = more on top of beat)
            const double energyFactor = 1.0 - (energy * 0.7);  // 1.0 at e=0, 0.3 at e=1
            
            // Calculate lay back in ms
            int layBackMs = int(baseLay * energyFactor * tempoScale);
            
            // At very high energy (stab mode), minimal lay back or even slight push
            if (energy >= 0.75) {
                layBackMs = qMax(0, layBackMs - 5);  // Reduce lay back
            }
            
            // Small humanization jitter (±3ms)
            const int humanHash = (adjusted.playbackBarIndex * 41 + currentRoot * 13) % 7;
            const int humanizeMs = humanHash - 3;  // Range: -3 to +3
            
            // Total timing offset (positive = late/behind, negative = early/ahead)
            int lhTimingOffsetMs = layBackMs + humanizeMs;
            
            // Clamp to reasonable range based on tempo
            const int maxOffset = (bpm < 70) ? 25 : 18;
            lhTimingOffsetMs = qBound(-5, lhTimingOffsetMs, maxOffset);
            
            // Apply timing offset to position
            if (lhTimingOffsetMs != 0) {
                lhPos = applyTimingOffset(lhPos, lhTimingOffsetMs, bpm, ts);
            }
            
            // ================================================================
            // VELOCITY & ARTICULATION: Energy-driven style adaptation
            // ================================================================
            // Low energy (Evans): Warm, sustained, legato
            // Mid energy (Hancock): Balanced, articulated
            // High energy (Tyner/Corea): Percussive stabs, short, punchy
            // ================================================================
            
            const bool stabMode = (energy >= 0.65);
            const bool midMode = (energy >= 0.45 && energy < 0.65);
            
            int lhVel = 48 + int(energy * 40);
            
            // At high energy, add extra "punch" to the attack
            if (stabMode) {
                lhVel += 8;  // More assertive
            }
            
            // Back off when user is active
            if (adjusted.userBusy || adjusted.userDensityHigh) {
                lhVel = qMin(lhVel, 62);
            }
            lhVel = qBound(42, lhVel, 95);
            
            // Duration: varies with energy style
            // Low energy: 1.5 beats (legato, sustained)
            // Mid energy: 1.2 beats (balanced)
            // High energy: 0.8 beats (staccato stabs)
            double durBeats;
            if (stabMode) {
                durBeats = 0.8;  // Short, percussive
            } else if (midMode) {
                durBeats = 1.2;  // Moderate
            } else {
                durBeats = 1.5;  // Sustained legato
            }
            const virtuoso::groove::Rational lhDurWhole(int(durBeats * 1000), 4000);
            
            // ================================================================
            // PHASE 4E: TASTEFUL GENTLE ROLLS (arpeggiated chords)
            // ================================================================
            // Bill Evans' signature: occasionally roll the chord from bottom
            // to top, creating a warm, harp-like quality.
            //
            // When to roll:
            // - Low to mid energy (warm, expressive moments)
            // - NOT at high energy (stabs need to be tight)
            // - NOT with anticipations (would blur the timing)
            // - Phrase starts, emotional moments
            //
            // Roll speed: ~15-50ms total spread depending on voicing size
            // ================================================================
            
            bool useRoll = false;
            int rollSpreadMs = 0;
            
            // Only consider rolls at low-mid energy, not stabs
            if (!stabMode && !useAnticipation && voicing.midiNotes.size() >= 3) {
                const int rollHash = (adjusted.playbackBarIndex * 19 + currentRoot * 11) % 100;
                
                // Probability: ~20% at low energy, ~12% at mid energy
                const int rollThreshold = (energy < 0.4) ? 20 : 12;
                
                // Prefer rolls at phrase starts or section starts
                const int barInSection = adjusted.playbackBarIndex % 8;
                const bool isStructuralMoment = (barInSection == 0 || barInSection == 4);
                
                if (rollHash < rollThreshold || (isStructuralMoment && rollHash < rollThreshold + 10)) {
                    useRoll = true;
                    
                    // Roll speed: slower at low energy (more expressive)
                    // ~40ms at low energy, ~25ms at mid energy
                    // Scaled by BPM
                    const int bpmForRoll = adjusted.bpm > 0 ? adjusted.bpm : 90;
                    const double tempoScale = 90.0 / qBound(50, bpmForRoll, 160);
                    
                    if (energy < 0.35) {
                        rollSpreadMs = int(45.0 * tempoScale);  // Slow, expressive
                    } else if (energy < 0.55) {
                        rollSpreadMs = int(32.0 * tempoScale);  // Moderate
                    } else {
                        rollSpreadMs = int(22.0 * tempoScale);  // Quick, subtle
                    }
                    
                    // Clamp to reasonable range
                    rollSpreadMs = qBound(15, rollSpreadMs, 60);
                }
            }
            
            // ================================================================
            // STAGE 6A: GRACE NOTES (chromatic approach)
            // ================================================================
            // A quick note a semitone below one chord tone, played just
            // before the main chord lands. Creates a "lean-in" effect.
            //
            // When to use:
            // - Low-mid energy (expressive moments)
            // - NOT with rolls (don't stack ornaments)
            // - NOT with anticipations
            // - NOT at high energy (stabs need clean attack)
            // ================================================================
            
            bool useGraceNote = false;
            int graceNoteMidi = -1;
            int graceNoteTargetIdx = -1;
            
            // Only consider grace notes when not using other ornaments
            if (!stabMode && !useAnticipation && !useRoll && voicing.midiNotes.size() >= 2) {
                const int graceHash = (adjusted.playbackBarIndex * 13 + currentRoot * 17) % 100;
                
                // Probability: ~15% at low energy, ~8% at mid energy
                const int graceThreshold = (energy < 0.4) ? 15 : 8;
                
                if (graceHash < graceThreshold) {
                    useGraceNote = true;
                    
                    // Choose which note to approach (prefer bass or top note)
                    graceNoteTargetIdx = (graceHash % 2 == 0) ? 0 : (voicing.midiNotes.size() - 1);
                    
                    // Grace note is a semitone below the target
                    graceNoteMidi = voicing.midiNotes[graceNoteTargetIdx] - 1;
                    
                    // Safety: don't go below reasonable range
                    if (graceNoteMidi < 40) {
                        useGraceNote = false;
                    }
                }
            }
            
            // ================================================================
            // STAGE 6B: OCTAVE BASS DOUBLING
            // ================================================================
            // Occasionally double the lowest note an octave lower for
            // extra bass emphasis. Creates weight and grounding.
            //
            // When to use:
            // - Section starts, strong structural moments
            // - Low-mid energy (adds warmth)
            // - NOT with grace notes or rolls (don't stack)
            // ================================================================
            
            bool useOctaveDouble = false;
            int octaveDoubleMidi = -1;
            
            // Only consider at low-mid energy, on structural moments
            if (!stabMode && !useRoll && !useGraceNote && voicing.midiNotes.size() >= 2) {
                const int octaveHash = (adjusted.playbackBarIndex * 11 + currentRoot * 23) % 100;
                
                // Strong structural moments: bar 0 or 4 of section, beat 1
                const int barInSection = adjusted.playbackBarIndex % 8;
                const bool isStrongMoment = (barInSection == 0 || barInSection == 4) && adjusted.beatInBar == 0;
                
                // Probability: ~20% at structural moments, ~5% otherwise
                const int octaveThreshold = isStrongMoment ? 20 : 5;
                
                if (energy < 0.55 && octaveHash < octaveThreshold) {
                    // Double the lowest note an octave below
                    int lowestNote = voicing.midiNotes.first();
                    int octaveNote = lowestNote - 12;
                    
                    // Safety: don't go below piano range
                    if (octaveNote >= 36) {  // Low C on piano
                        useOctaveDouble = true;
                        octaveDoubleMidi = octaveNote;
                    }
                }
            }
            
            // Emit octave doubling if applicable
            if (useOctaveDouble && octaveDoubleMidi >= 0) {
                virtuoso::engine::AgentIntentNote octaveNote;
                octaveNote.agent = "Piano";
                octaveNote.channel = midiChannel;
                octaveNote.note = octaveDoubleMidi;
                octaveNote.baseVelocity = lhVel - 5;  // Slightly softer
                octaveNote.startPos = lhPos;  // Same timing as main chord
                octaveNote.durationWhole = lhDurWhole;
                octaveNote.structural = true;
                octaveNote.chord_context = adjusted.chordText;
                octaveNote.voicing_type = "LH_octave";
                octaveNote.logic_tag = "LH";
                
                plan.notes.push_back(octaveNote);
            }
            
            // Emit grace note if applicable
            if (useGraceNote && graceNoteMidi >= 0) {
                const int bpmForGrace = adjusted.bpm > 0 ? adjusted.bpm : 90;
                const double tempoScale = 90.0 / qBound(50, bpmForGrace, 160);
                
                // Grace note timing: 35-50ms before main chord
                const int graceOffsetMs = -int(40.0 * tempoScale);
                
                virtuoso::engine::AgentIntentNote graceNote;
                graceNote.agent = "Piano";
                graceNote.channel = midiChannel;
                graceNote.note = graceNoteMidi;
                graceNote.baseVelocity = lhVel - 15;  // Softer than main
                graceNote.startPos = applyTimingOffset(lhPos, graceOffsetMs, bpmForGrace, ts);
                graceNote.durationWhole = virtuoso::groove::Rational(80, 4000);  // Very short (~0.08 beats)
                graceNote.structural = false;
                graceNote.chord_context = adjusted.chordText;
                graceNote.voicing_type = "LH_grace";
                graceNote.logic_tag = "LH";
                
                plan.notes.push_back(graceNote);
            }
            
            // Emit notes (with optional roll timing)
            const int numNotes = voicing.midiNotes.size();
            const int bpmForOffset = adjusted.bpm > 0 ? adjusted.bpm : 90;
            
            for (int i = 0; i < numNotes; ++i) {
                const int midi = voicing.midiNotes[i];
                
                virtuoso::engine::AgentIntentNote note;
                note.agent = "Piano";
                note.channel = midiChannel;
                note.note = midi;
                note.baseVelocity = lhVel;
                
                // Apply roll offset: ascending from bottom to top
                if (useRoll && numNotes > 1) {
                    // Spread the notes evenly across rollSpreadMs
                    const int noteOffsetMs = (i * rollSpreadMs) / (numNotes - 1);
                    note.startPos = applyTimingOffset(lhPos, noteOffsetMs, bpmForOffset, ts);
                } else {
                    note.startPos = lhPos;
                }
                
                note.durationWhole = lhDurWhole;
                note.structural = true;
                note.chord_context = adjusted.chordText;
                note.voicing_type = useRoll ? "LH_roll" : (stabMode ? "LH_stab" : voicing.ontologyKey);
                note.logic_tag = "LH";
                
                plan.notes.push_back(note);
            }
            
            // Update state for voice-leading continuity
            m_state.lastLhMidi = voicing.midiNotes;
            m_state.lastLhWasTypeA = voicing.isTypeA;
            m_lhGen.state().lastLhMidi = voicing.midiNotes;
            m_lhGen.state().lastLhWasTypeA = voicing.isTypeA;
        }
    }
    
    // ==========================================================================
    // PHASE 4B: ENERGY-SCALED COMPING DENSITY
    // ==========================================================================
    // Base rates (same at all energies) + additional hits at higher energy:
    //   All energies: Base beat 3 (~35%), base "& of 2" (~20%)
    //   High (≥0.65): Add "& of 3", "& of 4", beat 2
    //   Peak (≥0.80): Even more density, McCoy Tyner intensity
    // ==========================================================================
    
    if (!adjusted.chordIsNew && !m_state.lastLhMidi.isEmpty()) {
        const double energy = adjusted.energy;
        
        // Energy bands for ADDITIONAL density
        const bool isPeakEnergy = (energy >= 0.80);
        const bool isHighEnergy = (energy >= 0.65);
        
        // Use deterministic hash for this beat
        const int compHash = (adjusted.playbackBarIndex * 31 + adjusted.beatInBar * 13) % 100;
        
        // Collect comping hits for this beat
        struct CompHit {
            int subdivision;  // 0=on beat, 2=on "and"
            int velOffset;    // Velocity adjustment from base
            int variation;    // 0=full, 1=shell, 2=drop, 3=shift, 4=inner voice movement
        };
        QVector<CompHit> compHits;
        const int beat = adjusted.beatInBar;
        
        // Variation selector based on beat and bar
        // Beat 3 gets higher chance of inner voice movement (variation 4)
        const int varHash = (adjusted.playbackBarIndex * 23 + beat * 11) % 5;
        const bool preferInnerVoice = (beat == 2 && energy < 0.6 && varHash < 2);  // ~40% on beat 3
        
        // ==================================================================
        // STAGE 6C: GHOST TOUCHES (very soft, textural repetitions)
        // ==================================================================
        // At very low energy, add whisper-soft touches that create subtle
        // pulse without being prominent. Bill Evans signature texture.
        // ==================================================================
        
        bool addGhostTouch = false;
        if (energy < 0.35 && !isHighEnergy) {
            const int ghostHash = (adjusted.playbackBarIndex * 41 + beat * 19) % 100;
            
            // Ghost touches on beat 2 or 4 at very low energy
            if ((beat == 1 || beat == 3) && ghostHash < 12) {
                addGhostTouch = true;
                // Ghost touches are very soft shell voicings
                compHits.append({0, -25, 1});  // On beat, very soft, shell voicing
            }
        }
        
        // ==================================================================
        // BASE RATES (all energies) - same as original Bill Evans style
        // ==================================================================
        
        // Beat 3: 30-55% depending on energy (original formula)
        if (beat == 2) {
            const int beat3Threshold = 30 + int(energy * 25);
            if (compHash < beat3Threshold) {
                // Beat 3 comping: prefer inner voice movement at low-mid energy
                // This creates melodic motion within sustained chords (Bill Evans signature)
                int var;
                if (preferInnerVoice) {
                    var = 4;  // Inner voice movement
                } else if (varHash == 0) {
                    var = 1;  // Shell
                } else {
                    var = 2;  // Drop middle
                }
                compHits.append({0, 0, var});  // Beat 3
            }
        }
        
        // "& of 2": 15-35% depending on energy (original formula)
        if (beat == 1) {
            const int andOf2Threshold = 15 + int(energy * 20);
            if (compHash < andOf2Threshold) {
                compHits.append({2, -3, (varHash == 1) ? 1 : 0});  // "& of 2"
            }
        }
        
        // ==================================================================
        // HIGH ENERGY ADDITIONS (≥0.65): Extra hits on top of base
        // ==================================================================
        if (isHighEnergy) {
            // "& of 3": syncopated push after beat 3
            if (beat == 2 && (compHash % 3 == 0)) {
                compHits.append({2, -8, 2});  // "& of 3", drop middle
            }
            
            // "& of 4": pushes into next bar
            if (beat == 3 && compHash < 25) {
                compHits.append({2, -5, 1});  // "& of 4", shell
            }
        }
        
        // ==================================================================
        // PEAK ENERGY ADDITIONS (≥0.80): McCoy Tyner intensity
        // At peak, use full voicings for power (variation 0)
        // ==================================================================
        if (isPeakEnergy) {
            // Beat 2: extra hit at the top of beat 2
            if (beat == 1 && compHash < 50) {
                compHits.append({0, -2, 0});  // Beat 2, full voicing for power
            }
            
            // Beat 4: extra hit
            if (beat == 3 && compHash < 35) {
                compHits.append({0, -3, 0});  // Beat 4, full voicing
            }
            
            // "& of 3" with higher probability at peak
            if (beat == 2 && compHash < 45) {
                // Check if we already added "& of 3" from high energy section
                bool hasAndOf3 = false;
                for (const auto& h : compHits) {
                    if (h.subdivision == 2) hasAndOf3 = true;
                }
                if (!hasAndOf3) {
                    compHits.append({2, -5, 0});  // "& of 3", full voicing
                }
            }
        }
        
        // Don't add comping hits when user is very active
        if (adjusted.userBusy && adjusted.userDensityHigh) {
            compHits.clear();
        }
        
        // ==================================================================
        // EMIT COMPING HITS
        // ==================================================================
        const bool stabMode = (energy >= 0.65);
        
        for (const auto& hit : compHits) {
            virtuoso::groove::GridPos compPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                adjusted.playbackBarIndex, adjusted.beatInBar, hit.subdivision, 4, ts);
            
            // ==============================================================
            // PHASE 4D: BPM-AWARE TIMING FOR COMPING HITS
            // ==============================================================
            // Comping hits get slightly different timing treatment:
            // - Syncopated hits ("&" subdivisions): slight push (ahead of beat)
            // - On-beat hits: slight lay back (behind beat)
            // - High energy: tighter timing, less swing
            // ==============================================================
            const int bpm = adjusted.bpm > 0 ? adjusted.bpm : 90;
            const double tempoScale = 90.0 / qBound(50, bpm, 180);
            
            int compTimingMs;
            if (hit.subdivision == 2) {
                // Syncopated "and" hits: slight push forward for swing feel
                compTimingMs = int(-4.0 * tempoScale * (1.0 - energy * 0.5));  // -2 to -4ms
            } else {
                // On-beat comping: slight lay back, less than main hit
                compTimingMs = int(6.0 * tempoScale * (1.0 - energy * 0.6));   // 2-6ms
            }
            
            // Humanization jitter
            const int compHumanHash = (adjusted.playbackBarIndex * 29 + beat * 7 + hit.subdivision) % 5;
            compTimingMs += compHumanHash - 2;  // ±2ms
            
            // Clamp and apply
            compTimingMs = qBound(-8, compTimingMs, 12);
            if (compTimingMs != 0) {
                compPos = applyTimingOffset(compPos, compTimingMs, bpm, ts);
            }
            
            // ==============================================================
            // HIGH-ENERGY STABS: Pedal lift before attack for dry, percussive sound
            // McCoy Tyner / Chick Corea style - clean rhythmic articulation
            // ==============================================================
            if (stabMode) {
                // Lift pedal 1/16 beat BEFORE the stab for clean attack
                int liftSubdiv = (hit.subdivision == 0) ? 0 : hit.subdivision - 1;
                int liftDenom = (hit.subdivision == 0) ? 8 : 4;  // Earlier for on-beat
                
                CcIntent stabLift;
                stabLift.cc = 64;
                stabLift.value = 0;
                stabLift.startPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                    adjusted.playbackBarIndex, adjusted.beatInBar, 
                    liftSubdiv, liftDenom, ts);
                stabLift.structural = false;
                stabLift.logic_tag = "stab_pedal_lift";
                plan.ccs.push_back(stabLift);
                
                // Very light catch after the attack (just enough sustain, not muddy)
                // Only if energy < 0.85; at peak energy, stay completely dry
                if (energy < 0.85) {
                    CcIntent lightCatch;
                    lightCatch.cc = 64;
                    lightCatch.value = 15 + int(10.0 * (0.85 - energy));  // 15-25
                    int catchSubdiv = hit.subdivision + 1;
                    lightCatch.startPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                        adjusted.playbackBarIndex, adjusted.beatInBar,
                        catchSubdiv, 4, ts);
                    lightCatch.structural = false;
                    lightCatch.logic_tag = "stab_pedal_catch";
                    plan.ccs.push_back(lightCatch);
                }
            }
            
            // Velocity: softer than main hit, with per-hit adjustment
            int compVel = 40 + int(energy * 28) + hit.velOffset;
            
            if (adjusted.userBusy) {
                compVel = qMin(compVel, 52);
            }
            
            // Ghost touches (velOffset <= -20) are allowed to be very soft
            const bool isGhostTouch = (hit.velOffset <= -20);
            if (isGhostTouch) {
                compVel = qBound(22, compVel, 38);  // Ghost range: 22-38
            } else {
                compVel = qBound(35, compVel, 72);  // Normal range: 35-72
            }
            
            // Duration: shorter for syncopated hits; even shorter for stabs
            double durBeats = (hit.subdivision == 0) ? 0.9 : 0.6;
            if (stabMode) {
                durBeats *= 0.7;  // Even tighter for percussive feel
            }
            const virtuoso::groove::Rational compDur(int(durBeats * 1000), 4000);
            
            // ==============================================================
            // PHASE 4C: VOICING VARIATION ON REPEAT HITS
            // ==============================================================
            // variation: 0=full, 1=shell (outer 2), 2=drop middle, 3=shift
            // This prevents repetitive sound when same chord is hit multiple times
            // ==============================================================
            QVector<int> compVoicing;
            const QVector<int>& fullVoicing = m_state.lastLhMidi;
            
            if (fullVoicing.size() >= 3) {
                switch (hit.variation) {
                    case 1: // Shell: just lowest and highest notes (3rd and 7th essence)
                        compVoicing.append(fullVoicing.first());
                        compVoicing.append(fullVoicing.last());
                        break;
                        
                    case 2: // Drop middle: remove one middle note
                        for (int i = 0; i < fullVoicing.size(); ++i) {
                            if (i != fullVoicing.size() / 2) {  // Skip middle note
                                compVoicing.append(fullVoicing[i]);
                            }
                        }
                        break;
                        
                    case 3: // Shift: move all notes up a minor 3rd (stays in chord)
                        for (int midi : fullVoicing) {
                            int shifted = midi + 3;
                            if (shifted <= 72) {  // Keep in reasonable range
                                compVoicing.append(shifted);
                            } else {
                                compVoicing.append(midi);  // Don't shift if too high
                            }
                        }
                        break;
                        
                    case 4: {
                        // ==========================================================
                        // STAGE 5: INNER VOICE MOVEMENT (Bill Evans signature)
                        // ==========================================================
                        // Move one inner voice to a CHORD TONE OR AVAILABLE TENSION.
                        // NEVER move to a note outside the chord - that creates dissonance.
                        // ==========================================================
                        
                        compVoicing = fullVoicing;  // Start with full voicing
                        
                        // Build set of valid target pitch classes (chord tones + tensions)
                        QVector<int> validPcs;
                        const int root = adjusted.chord.rootPc;
                        
                        // Add chord tones based on quality
                        validPcs.append(root);                    // Root
                        validPcs.append((root + 7) % 12);         // 5th (always safe)
                        
                        // 3rd: minor or major depending on quality
                        if (adjusted.chord.quality == music::ChordQuality::Minor ||
                            adjusted.chord.quality == music::ChordQuality::HalfDiminished ||
                            adjusted.chord.quality == music::ChordQuality::Diminished) {
                            validPcs.append((root + 3) % 12);     // Minor 3rd
                        } else {
                            validPcs.append((root + 4) % 12);     // Major 3rd
                        }
                        
                        // 7th: major or minor depending on quality and seventh type
                        if (adjusted.chord.quality == music::ChordQuality::Diminished) {
                            validPcs.append((root + 9) % 12);     // Diminished 7th
                        } else if (adjusted.chord.quality == music::ChordQuality::Major) {
                            // Major quality can have major 7th
                            validPcs.append((root + 11) % 12);    // Major 7th
                        } else {
                            validPcs.append((root + 10) % 12);    // Minor 7th (default)
                        }
                        
                        // Safe tensions: 9th and 13th (almost always available)
                        validPcs.append((root + 2) % 12);         // 9th
                        validPcs.append((root + 9) % 12);         // 13th (6th)
                        
                        // Choose which inner voice to move (not first or last)
                        const int moveIndex = fullVoicing.size() / 2;  // Middle voice
                        int originalNote = compVoicing[moveIndex];
                        int originalPc = originalNote % 12;
                        
                        // Find the nearest valid pitch class in either direction
                        int bestNewNote = originalNote;  // Default: no change
                        int bestDistance = 99;
                        
                        for (int delta = -3; delta <= 3; ++delta) {
                            if (delta == 0) continue;  // Skip no-change
                            
                            int candidateNote = originalNote + delta;
                            int candidatePc = ((candidateNote % 12) + 12) % 12;
                            
                            // Check if this pitch class is valid
                            bool isValid = validPcs.contains(candidatePc);
                            if (!isValid) continue;
                            
                            // Check range
                            if (candidateNote < 48 || candidateNote > 70) continue;
                            
                            // Check for clusters with other notes
                            bool hasCluster = false;
                            for (int i = 0; i < compVoicing.size(); ++i) {
                                if (i != moveIndex && qAbs(compVoicing[i] - candidateNote) <= 1) {
                                    hasCluster = true;
                                    break;
                                }
                            }
                            if (hasCluster) continue;
                            
                            // Prefer smaller movements
                            if (qAbs(delta) < bestDistance) {
                                bestDistance = qAbs(delta);
                                bestNewNote = candidateNote;
                            }
                        }
                        
                        // Apply the movement if we found a valid target
                        if (bestNewNote != originalNote) {
                            compVoicing[moveIndex] = bestNewNote;
                            std::sort(compVoicing.begin(), compVoicing.end());
                        }
                        break;
                    }
                        
                    default: // 0 = full voicing unchanged
                        compVoicing = fullVoicing;
                        break;
                }
            } else {
                // If voicing is too small, just use it as-is
                compVoicing = fullVoicing;
            }
            
            // Emit the (possibly varied) voicing
            QString variationType;
            if (isGhostTouch) {
                variationType = "LH_ghost";
            } else {
                switch (hit.variation) {
                    case 1: variationType = "LH_shell"; break;
                    case 2: variationType = "LH_drop"; break;
                    case 3: variationType = "LH_shift"; break;
                    case 4: variationType = "LH_inner"; break;  // Inner voice movement
                    default: variationType = stabMode ? "LH_stab" : "LH_comp"; break;
                }
            }
            
            for (int midi : compVoicing) {
                virtuoso::engine::AgentIntentNote note;
                note.agent = "Piano";
                note.channel = midiChannel;
                note.note = midi;
                note.baseVelocity = compVel;
                note.startPos = compPos;
                note.durationWhole = compDur;
                note.structural = false;
                note.chord_context = adjusted.chordText;
                note.voicing_type = variationType;
                note.logic_tag = "LH";
                
                plan.notes.push_back(note);
            }
        }
    }
    
    // ==========================================================================
    // RIGHT HAND: UPPER STRUCTURE VOICINGS (Stage 1 - Minimal Foundation)
    // ==========================================================================
    // Bill Evans approach: RH plays 2-3 note voicings that add harmonic
    // richness above the LH. These are upper structure triads/voicings.
    //
    // Stage 1 goals:
    // - Only on chord changes (sparse)
    // - 2-3 note voicings (not single notes)
    // - Register: above LH (C5-C6, MIDI 72-84)
    // - Chord tones only for now (3rd, 5th, 7th, 9th)
    // ==========================================================================
    
    if (!m_enableRightHand) {
        goto rh_done;
    }
    
    {
    // RH processing scope
    const bool userActive = adjusted.userBusy || adjusted.userDensityHigh || adjusted.userIntensityPeak;
    const double energy = adjusted.energy;
    
    // ==========================================================================
    // STAGE 4: BLOCK CHORD EMISSION (if pre-detected above LH section)
    // ==========================================================================
    // Block chord moment was detected before LH emission (isBlockChordMoment flag).
    // Now we emit the unified voicing. LH was skipped, so we compute both LH+RH here.
    // ==========================================================================
    
    if (isBlockChordMoment && adjusted.chordIsNew) {
        const int root = adjusted.chord.rootPc;
        
        // === COMPUTE LH PORTION (rootless voicing for this chord) ===
        // Same intervals as normal LH would use
        int lhThird = (adjusted.chord.quality == music::ChordQuality::Minor ||
                       adjusted.chord.quality == music::ChordQuality::HalfDiminished ||
                       adjusted.chord.quality == music::ChordQuality::Diminished) ? 3 : 4;
        int lhSeventh = (adjusted.chord.quality == music::ChordQuality::Major) ? 11 :
                        (adjusted.chord.quality == music::ChordQuality::Diminished) ? 9 : 10;
        int lhFifth = (adjusted.chord.quality == music::ChordQuality::HalfDiminished ||
                       adjusted.chord.quality == music::ChordQuality::Diminished) ? 6 :
                      (adjusted.chord.quality == music::ChordQuality::Augmented) ? 8 : 7;
        int lhNinth = 2;  // Major 9th (octave reduced)
        
        // LH in middle register (C3-C4 area, MIDI 48-60)
        int lhBaseMidi = 48;
        int lh3 = lhBaseMidi + ((root + lhThird) % 12);
        int lh5 = lhBaseMidi + ((root + lhFifth) % 12);
        int lh7 = lhBaseMidi + ((root + lhSeventh) % 12);
        int lh9 = lhBaseMidi + ((root + lhNinth) % 12);
        
        // Ensure ascending order
        if (lh5 < lh3) lh5 += 12;
        if (lh7 < lh5) lh7 += 12;
        if (lh9 < lh7) lh9 += 12;
        
        // === COMPUTE RH PORTION (upper structure) ===
        int rhThird = lhThird;  // Same quality as LH
        int rhSeventh = lhSeventh;
        int rhNinth = 14;  // Major 9th (full)
        
        int rhBaseMidi = 72;  // C5
        int rh3 = rhBaseMidi + ((root + rhThird) % 12);
        int rh7 = rhBaseMidi + ((root + rhSeventh) % 12);
        int rh9 = rhBaseMidi + ((root + rhNinth) % 12);
        
        if (rh3 < rhBaseMidi) rh3 += 12;
        if (rh7 < rh3) rh7 += 12;
        if (rh9 < rh7) rh9 += 12;
        
        // === BUILD UNIFIED BLOCK VOICING ===
        QVector<int> blockVoicing;
        
        // LH foundation (3rd, 7th - shell, or fuller)
        blockVoicing.append(lh3);
        blockVoicing.append(lh7);
        if (energy >= 0.8) {
            // At very high energy, add 5th for thicker LH
            if (!blockVoicing.contains(lh5)) blockVoicing.append(lh5);
        }
        
        // Doubled melody (RH top note dropped an octave)
        const int rhMelody = (rh9 <= 88) ? rh9 : rh7;
        const int doubledMelody = rhMelody - 12;
        
        // Add doubled melody if it fits in the gap
        if (doubledMelody > lh7 + 2 && !blockVoicing.contains(doubledMelody)) {
            blockVoicing.append(doubledMelody);
        }
        
        // RH upper notes
        if (!blockVoicing.contains(rh3)) blockVoicing.append(rh3);
        if (!blockVoicing.contains(rh7)) blockVoicing.append(rh7);
        if (rh9 <= 88 && !blockVoicing.contains(rh9)) blockVoicing.append(rh9);
        
        std::sort(blockVoicing.begin(), blockVoicing.end());
        
        // === EMIT BLOCK CHORD ===
        virtuoso::groove::GridPos blockPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
            adjusted.playbackBarIndex, adjusted.beatInBar, 0, 4, ts);
        
        // Slight timing: tight on the beat for power (minimal lay-back)
        const int bpmForOffset = adjusted.bpm > 0 ? adjusted.bpm : 90;
        const double tempoScale = 90.0 / qBound(50, bpmForOffset, 160);
        blockPos = applyTimingOffset(blockPos, int(3.0 * tempoScale), bpmForOffset, ts);
        
        // Velocity: POWERFUL
        const int blockVel = 78 + int(energy * 17);  // 78-95
        
        // Duration: punchy and defined
        const virtuoso::groove::Rational blockDur(750, 4000);  // ~0.75 beats
        
        for (int midi : blockVoicing) {
            virtuoso::engine::AgentIntentNote note;
            note.agent = "Piano";
            note.channel = midiChannel;
            note.note = midi;
            note.baseVelocity = blockVel;
            note.startPos = blockPos;
            note.durationWhole = blockDur;
            note.structural = true;
            note.chord_context = adjusted.chordText;
            note.voicing_type = "Block_chord";
            note.logic_tag = "Piano_block";
            plan.notes.push_back(note);
        }
        
        // Update state: LH uses the lower notes, RH uses the upper
        m_state.lastLhMidi = {lh3, lh7};
        m_state.lastRhMidi = {rh3, rh7};
        if (rh9 <= 88) m_state.lastRhMidi.append(rh9);
        
        // Block chord complete - skip normal RH
        goto rh_done;
    }
    
    // ==========================================================================
    // STAGE 3: RHYTHMIC DIALOGUE - Decide WHEN RH plays
    // ==========================================================================
    // RH and LH have a conversational relationship:
    // - Sometimes together (chord changes)
    // - Sometimes RH responds (beat 2 after LH)
    // - Sometimes RH fills (beat 3 when LH sustains)
    // - Sometimes RH stays silent (let LH breathe)
    // ==========================================================================
    
    enum class RhTiming { Silent, WithLh, Respond, Fill };
    RhTiming rhTiming = RhTiming::Silent;
    
    const int rhDialogueHash = (adjusted.playbackBarIndex * 17 + adjusted.beatInBar * 11 + adjusted.chord.rootPc) % 100;
    // Note: 'energy' already defined at top of RH scope
    
    if (adjusted.chordIsNew && !userActive) {
        // Chord change: decide if RH plays WITH LH or stays silent
        // Higher energy = much more likely to play together (Evans drove hard at high energy)
        const int playWithLhThreshold = 55 + int(energy * 40);  // 55-95%
        if (rhDialogueHash < playWithLhThreshold) {
            rhTiming = RhTiming::WithLh;
        }
    } else if (!adjusted.chordIsNew && !userActive && !m_state.lastRhMidi.isEmpty()) {
        // Non-chord-change beat: decide if RH responds or fills
        // At high energy, RH is much more active (driving rhythm)
        
        if (adjusted.beatInBar == 1) {
            // Beat 2: RH can "respond" to LH that hit on beat 1
            const int respondThreshold = 18 + int(energy * 35);  // 18-53%
            if (rhDialogueHash < respondThreshold) {
                rhTiming = RhTiming::Respond;
            }
        } else if (adjusted.beatInBar == 2) {
            // Beat 3: RH can "fill" the space
            const int fillThreshold = 12 + int(energy * 28);  // 12-40%
            if (rhDialogueHash < fillThreshold) {
                rhTiming = RhTiming::Fill;
            }
        } else if (adjusted.beatInBar == 3 && energy >= 0.7) {
            // Beat 4: At high energy, RH can push into next bar
            const int pushThreshold = int((energy - 0.5) * 40);  // 8-20% at high energy
            if (rhDialogueHash < pushThreshold) {
                rhTiming = RhTiming::Fill;  // Reuse Fill mode for beat 4
            }
        }
    }
    
    // ==========================================================================
    // STAGE 1: UPPER STRUCTURE VOICINGS (when timing says to play)
    // ==========================================================================
    
    if (rhTiming != RhTiming::Silent) {
        const int root = adjusted.chord.rootPc;
        const double energy = adjusted.energy;
        
        // Determine chord intervals based on quality
        int third, fifth, seventh, ninth;
        
        // 3rd: minor or major
        if (adjusted.chord.quality == music::ChordQuality::Minor ||
            adjusted.chord.quality == music::ChordQuality::HalfDiminished ||
            adjusted.chord.quality == music::ChordQuality::Diminished) {
            third = 3;   // Minor 3rd
        } else {
            third = 4;   // Major 3rd
        }
        
        // 5th: perfect, diminished, or augmented
        if (adjusted.chord.quality == music::ChordQuality::HalfDiminished ||
            adjusted.chord.quality == music::ChordQuality::Diminished) {
            fifth = 6;   // Diminished 5th
        } else if (adjusted.chord.quality == music::ChordQuality::Augmented) {
            fifth = 8;   // Augmented 5th
        } else {
            fifth = 7;   // Perfect 5th
        }
        
        // 7th: major, minor, or diminished
        if (adjusted.chord.quality == music::ChordQuality::Major) {
            seventh = 11;  // Major 7th
        } else if (adjusted.chord.quality == music::ChordQuality::Diminished) {
            seventh = 9;   // Diminished 7th
        } else {
            seventh = 10;  // Minor 7th (dominant, minor, half-dim)
        }
        
        // 9th: always major 9th for now (safe tension)
        ninth = 14;  // Major 9th (2 + 12)
        
        // ==========================================================================
        // BUILD UPPER STRUCTURE VOICING
        // ==========================================================================
        // Choose 2-3 notes from: 3rd, 5th, 7th, 9th
        // Register: C5-C6 (MIDI 72-84)
        // ==========================================================================
        
        QVector<int> rhNotes;
        const int rhBaseMidi = 72;  // C5
        
        // Calculate MIDI notes for each degree
        int thirdMidi = rhBaseMidi + ((root + third) % 12);
        int fifthMidi = rhBaseMidi + ((root + fifth) % 12);
        int seventhMidi = rhBaseMidi + ((root + seventh) % 12);
        int ninthMidi = rhBaseMidi + ((root + ninth) % 12);
        
        // Ensure notes are in ascending order and in range
        if (thirdMidi < rhBaseMidi) thirdMidi += 12;
        if (fifthMidi < thirdMidi) fifthMidi += 12;
        if (seventhMidi < fifthMidi) seventhMidi += 12;
        if (ninthMidi < seventhMidi) ninthMidi += 12;
        
        // Voicing selection based on energy
        // Low energy: 2 notes (3rd + 7th - the essence)
        // Mid energy: 3 notes (3rd + 5th + 7th or 3rd + 7th + 9th)
        // High energy: 3 notes with 9th (more color)
        
        const int voicingHash = (adjusted.playbackBarIndex * 13 + root * 7) % 100;
        
        if (energy < 0.4) {
            // Low energy: sparse dyad (3rd + 7th)
            rhNotes.append(thirdMidi);
            rhNotes.append(seventhMidi);
        } else if (energy < 0.65) {
            // Mid energy: triad (3rd + 5th + 7th) or (3rd + 7th + 9th)
            rhNotes.append(thirdMidi);
            if (voicingHash < 50) {
                rhNotes.append(fifthMidi);
                rhNotes.append(seventhMidi);
            } else {
                rhNotes.append(seventhMidi);
                if (ninthMidi <= 86) {  // Don't go too high
                    rhNotes.append(ninthMidi);
                }
            }
        } else {
            // High energy: full color (3rd + 7th + 9th)
            rhNotes.append(thirdMidi);
            rhNotes.append(seventhMidi);
            if (ninthMidi <= 86) {
                rhNotes.append(ninthMidi);
            }
        }
        
        // ==========================================================================
        // STAGE 2: REGISTER SEPARATION (voice-leading in future iteration)
        // ==========================================================================
        // Ensure RH bottom is above LH top - simple and safe approach
        // ==========================================================================
        
        // Get LH top note for register separation
        const int lhTopMidi = m_state.lastLhMidi.isEmpty() ? 60 : m_state.lastLhMidi.last();
        const int rhFloor = qMax(72, lhTopMidi + 5);  // At least C5, or 5 above LH top
        
        // If any RH note is below the floor, shift the entire voicing up an octave
        bool needsShift = false;
        for (int midi : rhNotes) {
            if (midi < rhFloor) {
                needsShift = true;
                break;
            }
        }
        
        if (needsShift) {
            for (int& midi : rhNotes) {
                midi += 12;
            }
        }
        
        // Final clamp: don't go too high
        for (int& midi : rhNotes) {
            if (midi > 90) midi -= 12;
        }
        
        // Sort ascending
        std::sort(rhNotes.begin(), rhNotes.end());
        
        // ==========================================================================
        // EMIT RH VOICING
        // ==========================================================================
        
        if (!rhNotes.isEmpty()) {
            // Position: depends on dialogue timing mode
            virtuoso::groove::GridPos rhPos;
            int rhBeat = adjusted.beatInBar;
            int rhSubdivision = 0;  // Default: on the beat
            
            // ==========================================================
            // RH TIMING: Lay back + humanization (like LH but slightly different)
            // ==========================================================
            // RH should have a slightly different feel from LH:
            // - When WITH LH: match LH timing closely
            // - Respond/Fill: can be on "and" for more conversational feel
            // - Add lay back and humanization for human feel
            // ==========================================================
            
            switch (rhTiming) {
                case RhTiming::WithLh:
                    // Play with LH on chord change beat
                    rhBeat = adjusted.beatInBar;
                    rhSubdivision = 0;  // On the beat with LH
                    break;
                case RhTiming::Respond: {
                    // Respond: sometimes on beat 2, sometimes on "& of 2"
                    rhBeat = 1;
                    const int respondSubHash = (adjusted.playbackBarIndex * 29 + adjusted.chord.rootPc * 11) % 100;
                    if (energy >= 0.5 && respondSubHash < 40) {
                        rhSubdivision = 2;  // "& of 2" - more syncopated feel
                    } else {
                        rhSubdivision = 0;  // On beat 2
                    }
                    break;
                }
                case RhTiming::Fill: {
                    // Fill: sometimes on beat 3, sometimes on "& of 3"
                    rhBeat = 2;
                    const int fillSubHash = (adjusted.playbackBarIndex * 31 + adjusted.chord.rootPc * 7) % 100;
                    if (energy >= 0.55 && fillSubHash < 35) {
                        rhSubdivision = 2;  // "& of 3" - anticipating beat 4
                    } else {
                        rhSubdivision = 0;  // On beat 3
                    }
                    break;
                }
                default:
                    rhBeat = adjusted.beatInBar;
                    rhSubdivision = 0;
                    break;
            }
            
            rhPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                adjusted.playbackBarIndex, rhBeat, rhSubdivision, 4, ts);
            
            // ==========================================================
            // RH LAY BACK + HUMANIZATION (BPM-aware, energy-scaled)
            // ==========================================================
            // RH timing feel:
            // - Low energy: slightly behind LH (supportive, relaxed)
            // - High energy: tighter, closer to LH (driving together)
            // - Add small humanization jitter
            // ==========================================================
            
            const int bpm = adjusted.bpm > 0 ? adjusted.bpm : 90;
            const double tempoScale = 90.0 / qBound(50, bpm, 180);
            
            // RH lay back: 6-10ms (slightly less than LH's 8-12ms)
            // This makes RH feel like it's responding to LH, not leading
            const double baseRhLay = 8.0;
            int rhLayBackMs = int(baseRhLay * tempoScale * (1.0 - energy * 0.5));  // 4-8ms range
            
            // When playing WITH LH, match LH timing more closely
            if (rhTiming == RhTiming::WithLh) {
                rhLayBackMs = int(10.0 * tempoScale * (1.0 - energy * 0.7));  // Same as LH
            }
            
            // Humanization jitter (±2ms for RH - slightly tighter than LH's ±3ms)
            const int rhHumanHash = (adjusted.playbackBarIndex * 47 + rhBeat * 13 + adjusted.chord.rootPc * 5) % 5;
            const int rhHumanizeMs = rhHumanHash - 2;  // Range: -2 to +2
            
            // Apply timing offset
            const int rhTimingOffsetMs = rhLayBackMs + rhHumanizeMs;
            rhPos = applyTimingOffset(rhPos, rhTimingOffsetMs, bpm, ts);
            
            // Velocity: Evans approach
            // Low energy: RH softer than LH (supportive color)
            // High energy: RH approaches LH level (block chord power)
            int rhVel;
            if (rhTiming == RhTiming::WithLh) {
                // With LH: at high energy, approach LH velocity
                if (energy >= 0.7) {
                    rhVel = 58 + int(energy * 28);  // 78-86 at high energy
                } else {
                    rhVel = 42 + int(energy * 30);  // 42-63 at low-mid
                }
            } else {
                // Respond/Fill: softer, supportive
                rhVel = 38 + int(energy * 25);  // 38-63
            }
            
            // ==========================================================
            // VOICING VARIATION FOR RESPOND/FILL (like LH comping)
            // ==========================================================
            // When RH plays Respond or Fill, use a varied voicing
            // to avoid repetition and create interest
            // ==========================================================
            
            QVector<int> finalRhNotes = rhNotes;
            
            if (rhTiming == RhTiming::Respond || rhTiming == RhTiming::Fill) {
                const int varHash = (adjusted.playbackBarIndex * 23 + adjusted.beatInBar * 17) % 3;
                
                if (finalRhNotes.size() >= 3) {
                    switch (varHash) {
                        case 0:
                            // Shell: just top 2 notes
                            finalRhNotes.remove(0);  // Remove lowest
                            break;
                        case 1:
                            // Drop bottom: just top 2
                            if (finalRhNotes.size() > 2) {
                                finalRhNotes.remove(0);
                            }
                            break;
                        case 2:
                            // Keep full voicing
                            break;
                    }
                } else if (finalRhNotes.size() == 2 && varHash == 0) {
                    // For dyads, sometimes just play top note (melodic)
                    finalRhNotes.remove(0);
                }
            }
            if (userActive) {
                rhVel = qMin(rhVel, 50);
            }
            rhVel = qBound(38, rhVel, 75);
            
            // Duration: similar to LH
            double durBeats = 1.2;
            if (energy >= 0.65) {
                durBeats = 0.9;  // Shorter at high energy
            }
            const virtuoso::groove::Rational rhDur(int(durBeats * 1000), 4000);
            
            for (int midi : finalRhNotes) {
                virtuoso::engine::AgentIntentNote note;
                note.agent = "Piano";
                note.channel = midiChannel;
                note.note = midi;
                note.baseVelocity = rhVel;
                note.startPos = rhPos;
                note.durationWhole = rhDur;
                note.structural = (rhTiming == RhTiming::WithLh);  // Only structural when with LH
                note.chord_context = adjusted.chordText;
                // Voicing type reflects dialogue mode
                switch (rhTiming) {
                    case RhTiming::WithLh: note.voicing_type = "RH_upper"; break;
                    case RhTiming::Respond: note.voicing_type = "RH_respond"; break;
                    case RhTiming::Fill: note.voicing_type = "RH_fill"; break;
                    default: note.voicing_type = "RH_upper"; break;
                }
                note.logic_tag = "RH";
                
                plan.notes.push_back(note);
            }
            
            // Store full voicing for voice-leading (not the varied one)
            m_state.lastRhMidi = rhNotes;
        }
    }
    
    // ==========================================================================
    // STAGE 5: MELODIC SINGING LINES (Simplified, Grid-Based)
    // ==========================================================================
    // Evans' melodic RH was intentional, not random. Key principles:
    // 1. Notes on REAL grid positions (8ths, triplets) - not random timing
    // 2. Simple gestures: 2-3 notes max, clearly placed
    // 3. Phrase-level feel: whole gesture has unified character
    // 4. Specific rhythmic cells that work musically
    // ==========================================================================
    
    // Only on specific beats when RH isn't already playing chords
    // Beat 3 or Beat 4: space for a melodic gesture before next bar
    
    if (m_enableRightHand && !isBlockChordMoment && !userActive && !adjusted.chordIsNew) {
        const int melodyHash = (adjusted.playbackBarIndex * 37 + adjusted.beatInBar * 19 + adjusted.chord.rootPc * 7) % 100;
        
        // Only trigger on specific beats with appropriate energy
        const bool isGoodBeat = (adjusted.beatInBar == 2 || adjusted.beatInBar == 3);
        const bool isLowMidEnergy = (energy < 0.55);
        const int barInSection = adjusted.playbackBarIndex % 8;
        const bool isPhraseEnding = (barInSection == 3 || barInSection == 7);
        
        // Conservative probability
        int melodicThreshold = 0;
        if (isGoodBeat && isLowMidEnergy && isPhraseEnding) melodicThreshold = 30;
        else if (isGoodBeat && isLowMidEnergy) melodicThreshold = 12;
        
        if (melodyHash < melodicThreshold) {
            const int root = adjusted.chord.rootPc;
            
            // Chord intervals
            int third = (adjusted.chord.quality == music::ChordQuality::Minor ||
                         adjusted.chord.quality == music::ChordQuality::HalfDiminished ||
                         adjusted.chord.quality == music::ChordQuality::Diminished) ? 3 : 4;
            int seventh = (adjusted.chord.quality == music::ChordQuality::Major) ? 11 :
                          (adjusted.chord.quality == music::ChordQuality::Diminished) ? 9 : 10;
            int ninth = 14;  // Major 9th
            
            // === SIMPLE MELODIC CELLS ===
            // Pre-defined 2-note gestures that sound musical
            // Each cell: {interval1, interval2, rhythm_type}
            // Rhythm types: 0 = two 8ths, 1 = dotted-8th + 16th, 2 = quarter + 8th
            
            struct MelodicCell {
                int note1;    // Interval from root
                int note2;    // Interval from root  
                int rhythm;   // Rhythm pattern
            };
            
            // Safe, musical cells based on chord tones
            const MelodicCell cells[] = {
                {seventh, third, 0},    // 7 → 3 (resolution feel)
                {ninth, seventh, 0},    // 9 → 7 (descending step)
                {third, seventh, 1},    // 3 → 7 (ascending, dotted)
                {seventh, ninth, 2},    // 7 → 9 (upward reach)
            };
            
            const int cellIndex = melodyHash % 4;
            const MelodicCell& cell = cells[cellIndex];
            
            // Calculate MIDI notes (upper register: C5-C6)
            int baseMidi = 72;  // C5
            int note1Midi = baseMidi + ((root + cell.note1) % 12);
            int note2Midi = baseMidi + ((root + cell.note2) % 12);
            
            // Ensure proper octave placement
            if (note1Midi < baseMidi) note1Midi += 12;
            if (note2Midi < note1Midi - 6) note2Midi += 12;  // Keep within octave
            if (note2Midi > note1Midi + 6) note2Midi -= 12;
            
            // Clamp to range
            note1Midi = qBound(72, note1Midi, 88);
            note2Midi = qBound(72, note2Midi, 88);
            
            // === GRID-BASED TIMING ===
            // Notes land on actual subdivisions, no random jitter
            
            virtuoso::groove::GridPos pos1, pos2;
            virtuoso::groove::Rational dur1, dur2;
            
            switch (cell.rhythm) {
                case 0:  // Two straight 8ths: current beat, then "and"
                    pos1 = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                        adjusted.playbackBarIndex, adjusted.beatInBar, 0, 4, ts);
                    pos2 = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                        adjusted.playbackBarIndex, adjusted.beatInBar, 2, 4, ts);  // "and"
                    dur1 = virtuoso::groove::Rational(500, 4000);  // 0.5 beats
                    dur2 = virtuoso::groove::Rational(500, 4000);
                    break;
                    
                case 1:  // Dotted 8th + 16th: longer first, quick second
                    pos1 = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                        adjusted.playbackBarIndex, adjusted.beatInBar, 0, 4, ts);
                    pos2 = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                        adjusted.playbackBarIndex, adjusted.beatInBar, 3, 4, ts);  // 3/4 through beat
                    dur1 = virtuoso::groove::Rational(750, 4000);  // 0.75 beats
                    dur2 = virtuoso::groove::Rational(250, 4000);  // 0.25 beats
                    break;
                    
                case 2:  // Quarter + 8th: on beat, then next beat's "and"
                default:
                    pos1 = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                        adjusted.playbackBarIndex, adjusted.beatInBar, 0, 4, ts);
                    // Second note on next beat's "and" (if room)
                    if (adjusted.beatInBar < 3) {
                        pos2 = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                            adjusted.playbackBarIndex, adjusted.beatInBar + 1, 0, 4, ts);
                    } else {
                        pos2 = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                            adjusted.playbackBarIndex, adjusted.beatInBar, 2, 4, ts);
                    }
                    dur1 = virtuoso::groove::Rational(900, 4000);  // 0.9 beats
                    dur2 = virtuoso::groove::Rational(400, 4000);  // 0.4 beats
                    break;
            }
            
            // === PHRASE-LEVEL LAY BACK ===
            // Apply consistent timing feel to both notes (not random per-note)
            const int bpmForMelody = adjusted.bpm > 0 ? adjusted.bpm : 90;
            const double tempoScale = 90.0 / qBound(50, bpmForMelody, 160);
            const int layBackMs = int(8.0 * tempoScale);  // Subtle, consistent lay back
            
            pos1 = applyTimingOffset(pos1, layBackMs, bpmForMelody, ts);
            pos2 = applyTimingOffset(pos2, layBackMs, bpmForMelody, ts);
            
            // Velocity: soft, expressive
            const int melodyVel = 40 + int(energy * 15);  // 40-55
            
            // Emit note 1
            {
                virtuoso::engine::AgentIntentNote note;
                note.agent = "Piano";
                note.channel = midiChannel;
                note.note = note1Midi;
                note.baseVelocity = melodyVel;
                note.startPos = pos1;
                note.durationWhole = dur1;
                note.structural = false;
                note.chord_context = adjusted.chordText;
                note.voicing_type = "RH_melody";
                note.logic_tag = "RH";
                plan.notes.push_back(note);
            }
            
            // Emit note 2
            {
                virtuoso::engine::AgentIntentNote note;
                note.agent = "Piano";
                note.channel = midiChannel;
                note.note = note2Midi;
                note.baseVelocity = melodyVel - 3;  // Slightly softer
                note.startPos = pos2;
                note.durationWhole = dur2;
                note.structural = false;
                note.chord_context = adjusted.chordText;
                note.voicing_type = "RH_melody";
                note.logic_tag = "RH";
                plan.notes.push_back(note);
            }
            
            // Update state
            m_state.lastRhMidi = {note2Midi};
        }
    }
    
    // ==========================================================================
    // RH STAGES COMPLETE - Stage 6 (Dynamics & expression) to be added
    // ==========================================================================
    
    // OLD COMPLEX RH CODE REMOVED - Building up from minimal foundation
    // (Previous ~800 lines of phrase patterns, melodic fragments, etc. removed)
    
    #if 0  // Disabled old RH code
    if (newPhrase || m_state.phrasePatternIndex < 0) {
        // Track previous pattern for variety
        m_state.lastPhrasePatternIndex = m_state.phrasePatternIndex;
        
        // Select a new phrase pattern for this phrase
        m_state.phrasePatternIndex = selectPhrasePattern(adjusted, rhHash);
        m_state.phrasePatternBar = 0;
        m_state.phrasePatternBeat = 0;
        m_state.phrasePatternHitIndex = 0;
        
        // Alternate high/low register for variety
        m_state.lastPhraseWasHigh = !m_state.lastPhraseWasHigh;
    }
    
    // Get current pattern
    const auto patterns = getAvailablePhrasePatterns(adjusted);
    const bool hasPattern = m_state.phrasePatternIndex >= 0 && 
                            m_state.phrasePatternIndex < patterns.size();
    
    // ========================================================================
    // PHRASE POSITION CHECK: Should we play at this position?
    // This is the KEY INNOVATION: default is REST, only play when pattern says so
    // ========================================================================
    
    bool shouldPlayRh = false;
    const PhraseCompHit* currentHit = nullptr;
    // Note: 'e' already defined earlier as adjusted.energy
    
    // ========================================================================
    // HIGH ENERGY MODE: At high energy, RH should be DRIVING and RHYTHMIC
    // Override the sparse pattern system with aggressive energy-based playing
    // ========================================================================
    if (e >= 0.6 && !userActive) {
        // HIGH ENERGY: Play on almost every beat, driving rhythm!
        double playProb = 0.0;
        
        if (adjusted.chordIsNew) {
            playProb = 0.98;  // Almost always on chord changes
        } else if (adjusted.beatInBar == 0) {
            // Beat 1: driving anchor (85-95%)
            playProb = 0.85 + 0.10 * e;
        } else if (adjusted.beatInBar == 2) {
            // Beat 3: strong backbeat (75-90%)
            playProb = 0.75 + 0.15 * e;
        } else if (adjusted.beatInBar == 1) {
            // Beat 2: push the groove (60-80%)
            playProb = 0.60 + 0.20 * e;
        } else {
            // Beat 4: pickup energy (65-85%)
            playProb = 0.65 + 0.20 * e;
        }
        
        shouldPlayRh = ((rhHash % 100) < int(playProb * 100));
        
        // At very high energy (>0.8), even add offbeat 8th notes occasionally
        if (e > 0.8 && !shouldPlayRh) {
            const bool addOffbeat = ((rhHash % 100) < 30);  // 30% chance of offbeat
            shouldPlayRh = addOffbeat;
        }
    }
    // ========================================================================
    // NORMAL/LOW ENERGY: Use pattern-based sparse playing
    // ========================================================================
    else if (hasPattern && !userActive) {
        const auto& pattern = patterns[m_state.phrasePatternIndex];
        
        // Calculate our position within the pattern
        const int barInPattern = adjusted.barInPhrase % pattern.bars;
        
        // Check if this position has a hit
        currentHit = getPhraseHitAt(pattern, barInPattern, adjusted.beatInBar);
        shouldPlayRh = (currentHit != nullptr);
    }
    // ========================================================================
    // FALLBACK: No pattern and low/medium energy
    // ========================================================================
    else if (!userActive) {
        double playProb = 0.0;
        
        if (adjusted.chordIsNew) {
            playProb = 0.85;
        } else if (adjusted.beatInBar == 0) {
            playProb = 0.40 + 0.30 * e;
        } else if (adjusted.beatInBar == 2) {
            playProb = 0.25 + 0.25 * e;
        } else {
            playProb = 0.10 + 0.20 * e;
        }
        
        shouldPlayRh = ((rhHash % 100) < int(playProb * 100));
    }
    
    // ========================================================================
    // USER ACTIVE: Be sparse to support, not compete
    // ========================================================================
    if (userActive) {
        shouldPlayRh = adjusted.chordIsNew && ((rhHash % 100) < 25);
    }
    
    // ========================================================================
    // GENERATE RH NOTES ONLY IF PATTERN SAYS TO PLAY
    // ========================================================================
    
    if (shouldPlayRh) {
        // Get hit parameters (or defaults if no pattern)
        const int hitVoicingType = currentHit ? currentHit->voicingType : 0;
        const int hitVelDelta = currentHit ? currentHit->velocityDelta : 0;
        const int hitTimingMs = currentHit ? currentHit->timingMs : 0;
        const int hitSubdivision = currentHit ? currentHit->subdivision : 0;
        const bool hitIsAccent = currentHit ? currentHit->isAccent : false;
        const QString hitIntent = currentHit ? currentHit->intentTag : "statement";
        
        // Voicing type from pattern (0=Drop2, 1=Triad, 2=Dyad, 3=Single)
        enum class RhVoicingType { Drop2, Triad, Dyad, Single };
        RhVoicingType voicingType = RhVoicingType::Drop2;
        
        // Get phrase context early for energy-based decisions
        const int curArcPhase = computePhraseArcPhase(adjusted);
        const bool isCadence = (adjusted.cadence01 >= 0.4);
        const double e = c.energy;
        
        if (userActive) {
            voicingType = RhVoicingType::Dyad;  // Simple when user is playing
        } else if (e >= 0.7) {
            // ================================================================
            // HIGH ENERGY: ALWAYS use full Drop2 voicings for driving sound!
            // This creates the dense, rhythmic, powerful jazz piano sound
            // ================================================================
            voicingType = RhVoicingType::Drop2;
        } else if (e >= 0.5) {
            // MEDIUM-HIGH ENERGY: Triads or Drop2
            voicingType = ((rhHash % 100) < 60) ? RhVoicingType::Drop2 : RhVoicingType::Triad;
        } else {
            // LOWER ENERGY: Use pattern-based voicing with upgrades
            switch (hitVoicingType) {
                case 0: voicingType = RhVoicingType::Drop2; break;
                case 1: voicingType = RhVoicingType::Triad; break;
                case 2: voicingType = RhVoicingType::Triad; break;  // Upgrade dyad to triad
                case 3: voicingType = RhVoicingType::Dyad; break;   // Upgrade single to dyad
            }
        }
        
        // ================================================================
        // PHRASE-CONTEXT OVERRIDES (only at lower energy)
        // At high energy, we DRIVE - no breathing back
        // ================================================================
        if (e < 0.6) {
            if (adjusted.phraseEndBar && isCadence) {
                voicingType = RhVoicingType::Drop2;  // Full voicing for resolution
            }
            if (curArcPhase == 2 && !isCadence) {
                voicingType = RhVoicingType::Dyad;   // Breathing, lighter
            }
        }
        // At any energy, peak phase gets full voicing
        if (curArcPhase == 1) {
            voicingType = RhVoicingType::Drop2;  // Full at peak
        }
        
        // ================================================================
        // PHRASE-LEVEL RUBATO: Use the hit's timing offset
        // This is REAL rubato - planned at phrase level, not random!
        // ================================================================
        int rhTimingOffset = hitTimingMs;
        
        // Add subtle broken time feel
        const BrokenTimeFeel baseBrokenFeel = calculateBrokenTimeFeel(
            adjusted.beatInBar,
            hitSubdivision,
            curArcPhase,
            c.energy,
            adjusted.bpm,
            chordChanged,
            (curArcPhase == 1),      // isPhrasePeak
            adjusted.phraseEndBar    // isPhraseEnd
        );
        
        rhTimingOffset += baseBrokenFeel.timingOffsetMs;
        
        // Cap but allow real expressive timing (not micro-offsets)
        const int maxOffset = (adjusted.bpm < 70) ? 60 : 45;
        rhTimingOffset = qBound(-maxOffset, rhTimingOffset, maxOffset);
        
        // ================================================================
        // MELODIC TARGET SELECTION: Use singing, voice-led approach
        // ================================================================
        
        int currentTopMidi = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 74;
        int currentDirection = m_state.rhMelodicDirection;
        
        // Use phrase contour for melodic direction
        if (hasPattern) {
            const auto& pattern = patterns[m_state.phrasePatternIndex];
            const auto contour = planPhraseContour(adjusted, pattern);
            
            // If we have a contour, aim for the appropriate target
            if (m_state.phrasePatternHitIndex < contour.size()) {
                m_state.phraseMelodicTargetMidi = contour[m_state.phrasePatternHitIndex];
            }
        }
        
        // Find melody note using the singing approach
        // This prioritizes chord tones and smooth voice leading
        const SingingMelodyTarget melodyTarget = findSingingMelodyTarget(
            currentTopMidi,
            currentDirection,
            adjusted.chord,
            adjusted.rhLo, adjusted.rhHi,
            curArcPhase,
            c.energy,
            (curArcPhase == 1),       // isPhrasePeak
            adjusted.phraseEndBar     // isPhraseEnd
        );
        
        int bestTarget = melodyTarget.midiNote;
        
        // ================================================================
        // CRITICAL: RH melody MUST be a chord tone - not just a scale tone!
        // Scale tones (like the 4th or 2nd) are very dissonant when held.
        // ================================================================
        if (!vu::isChordTone(normalizePc(bestTarget), adjusted.chord)) {
            // Force to nearest chord tone - not just any scale tone!
            QVector<int> chordTones = vu::getChordTonePcs(adjusted.chord);
            if (!chordTones.isEmpty()) {
                int bestMidi = bestTarget;
                int bestDist = 999;
                for (int chordPc : chordTones) {
                    for (int oct = 5; oct <= 7; ++oct) {
                        int midi = chordPc + 12 * oct;
                        if (midi < adjusted.rhLo || midi > adjusted.rhHi) continue;
                        int dist = qAbs(midi - bestTarget);
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestMidi = midi;
                        }
                    }
                }
                bestTarget = bestMidi;
            }
        }
        
        // ================================================================
        // MELODIC FRAGMENTS: Approach notes, enclosures, arpeggios
        // These add jazz vocabulary - notes that lead INTO the target
        // ================================================================
        QVector<FragmentNote> fragmentNotes;
        bool usedFragment = false;
        
        // HIGH ENERGY: No melodic fragments! They're too ornate for driving rhythmic playing.
        // Fragments are for intimate, expressive low-energy moments.
        const bool highEnergyMode = (adjusted.energy >= 0.6);
        
        if (m_enableMelodicFragments && !userActive && !highEnergyMode) {
            // Probability of using a fragment depends on:
            // - Energy level (LOWER energy = MORE fragments)
            // - Phrase position (more at phrase peaks only at lower energy)
            // - NOT on chord changes (keep those clean)
            const double fragmentProb = 0.10 + (0.5 - adjusted.energy) * 0.30 + 
                                        ((curArcPhase == 1 && adjusted.energy < 0.5) ? 0.10 : 0.0);
            const bool wantsFragment = !adjusted.chordIsNew && 
                                       ((rhHash % 100) < int(fragmentProb * 100));
            
            if (wantsFragment) {
                // Get available fragments for this context
                const int targetPc = normalizePc(bestTarget);
                const auto fragments = getMelodicFragments(adjusted, targetPc);
                
                if (!fragments.isEmpty()) {
                    // Select a fragment based on tension level
                    // Lower tension = simpler fragments (approaches)
                    // Higher tension = more complex (enclosures, runs)
                    int fragIndex = 0;
                    const double tensionLevel = energyToTension(adjusted.energy);
                    
                    // Find fragments matching our tension level
                    QVector<int> matchingIndices;
                    for (int i = 0; i < fragments.size(); ++i) {
                        if (fragments[i].tensionLevel <= tensionLevel + 0.2) {
                            matchingIndices.push_back(i);
                        }
                    }
                    
                    if (!matchingIndices.isEmpty()) {
                        fragIndex = matchingIndices[(rhHash / 7) % matchingIndices.size()];
                        
                        // Apply the fragment
                        fragmentNotes = applyMelodicFragment(adjusted, fragments[fragIndex], bestTarget, 0);
                        usedFragment = !fragmentNotes.isEmpty();
                    }
                }
            }
        }
        
        // Helper: Check if RH note would clash with LH voicing
        auto wouldClashWithLh = [&](int rhMidi) -> bool {
            int rhPc = normalizePc(rhMidi);
            for (int lhNote : m_state.lastLhMidi) {
                int lhPc = normalizePc(lhNote);
                int pcInterval = qAbs(rhPc - lhPc);
                if (pcInterval > 6) pcInterval = 12 - pcInterval;
                if (pcInterval == 1 && qAbs(rhMidi - lhNote) <= 24) return true;
                int midiDist = qAbs(rhMidi - lhNote);
                if (midiDist > 0 && midiDist <= 2) return true;
            }
            return false;
        };
        
        // ================================================================
        // CHORD TONES FOR VOICING
        // ================================================================
        int third = pcForDegree(adjusted.chord, 3);
        int fifth = pcForDegree(adjusted.chord, 5);
        int seventh = pcForDegree(adjusted.chord, 7);
        int root = adjusted.chord.rootPc;
        int ninth = pcForDegree(adjusted.chord, 9);
        
        const double hitTensionLevel = energyToTension(c.energy);
        const bool allowExtensions = hitTensionLevel > 0.3;
        
        // ================================================================
        // UPPER STRUCTURE TRIADS (Bill Evans Signature!)
        // On dominant chords with sufficient tension, use USTs for color
        // ================================================================
        const bool isDominant = (adjusted.chord.quality == music::ChordQuality::Dominant);
        const bool isMajor7 = (adjusted.chord.quality == music::ChordQuality::Major && 
                               adjusted.chord.seventh == music::SeventhQuality::Major7);
        const bool isMinor7 = (adjusted.chord.quality == music::ChordQuality::Minor);
        // UST on dominant, major7, AND minor7 chords (all sound beautiful!)
        // At high energy: more USTs even during resolution (driving colorful harmony)
        const bool allowInResolve = (c.energy > 0.6);  // At high energy, USTs anywhere!
        const bool wantsUST = (isDominant || isMajor7 || isMinor7) && 
                              hitTensionLevel > 0.15 &&   // Lower threshold for more color
                              !userActive &&
                              (curArcPhase != 2 || allowInResolve);
        
        // Probability of using UST: scales with ENERGY now, not just tension
        // Low energy: 30-50%, High energy: 60-90%!
        const int ustProb = int(30 + c.energy * 40 + hitTensionLevel * 20);
        const bool useUST = wantsUST && ((rhHash % 100) < ustProb);
        
        QVector<int> rhMidiNotes;
        QString voicingName;
        bool usedUST = false;
        
        if (useUST) {
            // Get UST candidates via RH generator
            const auto rhGenContext = toRhContext(adjusted);
            const auto ustCandidates = m_rhGen.getUpperStructureTriads(adjusted.chord);
            
            if (!ustCandidates.isEmpty()) {
                // Select UST based on tension level
                // Lower tension = safer USTs (lower index), higher tension = more colorful
                int ustIndex = 0;
                if (hitTensionLevel > 0.6 && ustCandidates.size() > 1) {
                    ustIndex = qMin(1, ustCandidates.size() - 1);
                }
                if (hitTensionLevel > 0.75 && ustCandidates.size() > 2) {
                    ustIndex = qMin(2, ustCandidates.size() - 1);
                }
                
                // Build the UST voicing via generator
                const auto ustVoicing = m_rhGen.buildUstVoicing(rhGenContext, ustCandidates[ustIndex]);
                
                if (!ustVoicing.midiNotes.isEmpty()) {
                    rhMidiNotes = ustVoicing.midiNotes;
                    voicingName = ustVoicing.ontologyKey;
                    usedUST = true;
                    
                    // Update melodic state from UST
                    if (ustVoicing.topNoteMidi > 0) {
                        bestTarget = ustVoicing.topNoteMidi;
                    }
                }
            }
        }
        
        // ================================================================
        // BUILD SINGLE VOICING FOR THIS PHRASE HIT
        // (Only if UST wasn't used)
        // ================================================================
        
        int topPc = normalizePc(bestTarget);
        if (!usedUST) voicingName = "piano_rh_drop2";
        
        // Get all available chord tones for voicing (only if UST not used)
        QVector<int> voicingPcs;
        if (!usedUST) {
            if (third >= 0) voicingPcs.push_back(third);
            if (fifth >= 0) voicingPcs.push_back(fifth);
            if (seventh >= 0) voicingPcs.push_back(seventh);
            if (ninth >= 0 && allowExtensions) voicingPcs.push_back(ninth);
        }
        
        // ================================================================
        // DROP-2 VOICING (Default for ballads!)
        // ================================================================
        if (!usedUST && voicingType == RhVoicingType::Drop2 && voicingPcs.size() >= 3) {
            rhMidiNotes.push_back(bestTarget);
            
            QVector<int> closePositionPcs;
            for (int pc : voicingPcs) {
                if (pc != topPc) closePositionPcs.push_back(pc);
            }
            
            std::sort(closePositionPcs.begin(), closePositionPcs.end(), [topPc](int a, int b) {
                int distA = (topPc - a + 12) % 12;
                int distB = (topPc - b + 12) % 12;
                return distA < distB;
            });
            
            int cursor = bestTarget;
            QVector<int> closePositionMidi;
            for (int i = 0; i < qMin(3, closePositionPcs.size()); ++i) {
                int pc = closePositionPcs[i];
                int midi = cursor - 1;
                while (normalizePc(midi) != pc && midi > adjusted.rhLo - 12) midi--;
                if (midi >= adjusted.rhLo - 12) {
                    closePositionMidi.push_back(midi);
                    cursor = midi;
                }
            }
            
            for (int i = 0; i < closePositionMidi.size(); ++i) {
                int midi = closePositionMidi[i];
                if (i == 0 && closePositionMidi.size() >= 2) midi -= 12;
                const int drop2Floor = adjusted.lhHi - 8;
                if (midi >= drop2Floor && !wouldClashWithLh(midi)) {
                    rhMidiNotes.push_back(midi);
                }
            }
            voicingName = "piano_drop2";
        }
        // ================================================================
        // TRIAD VOICING - Build from ACTUAL chord tones, not fixed intervals!
        // ================================================================
        else if (!usedUST && (voicingType == RhVoicingType::Triad || voicingPcs.size() < 3)) {
            rhMidiNotes.push_back(bestTarget);
            
            // Get actual chord tones to build voicing
            QVector<int> chordTonePcs = vu::getChordTonePcs(adjusted.chord);
            int topPcLocal = normalizePc(bestTarget);
            
            // Find chord tones below the melody, sorted by distance
            QVector<QPair<int, int>> candidates;  // (midi, distance from top)
            for (int chordPc : chordTonePcs) {
                if (chordPc == topPcLocal) continue;
                for (int oct = 6; oct >= 4; --oct) {
                    int midi = chordPc + 12 * oct;
                    if (midi < bestTarget && midi >= adjusted.lhHi - 8 && !wouldClashWithLh(midi)) {
                        candidates.push_back({midi, bestTarget - midi});
                        break;
                    }
                }
            }
            
            // Sort by distance (prefer close voicing)
            std::sort(candidates.begin(), candidates.end(), 
                [](const QPair<int,int>& a, const QPair<int,int>& b) {
                    return a.second < b.second;
                });
            
            // Add up to 2 support notes
            for (int i = 0; i < qMin(2, candidates.size()); ++i) {
                rhMidiNotes.push_back(candidates[i].first);
            }
            voicingName = "piano_triad";
        }
        // ================================================================
        // DYAD VOICING - Use actual chord tones!
        // ================================================================
        else if (!usedUST && voicingType == RhVoicingType::Dyad) {
            rhMidiNotes.push_back(bestTarget);
            
            // Find the nearest chord tone below the melody
            QVector<int> chordTonePcs = vu::getChordTonePcs(adjusted.chord);
            int topPcDyad = normalizePc(bestTarget);
            
            int bestDyadMidi = -1;
            int bestDist = 999;
            for (int chordPc : chordTonePcs) {
                if (chordPc == topPcDyad) continue;
                for (int oct = 6; oct >= 4; --oct) {
                    int midi = chordPc + 12 * oct;
                    if (midi < bestTarget && midi >= adjusted.lhHi - 5 && !wouldClashWithLh(midi)) {
                        int dist = bestTarget - midi;
                        // Prefer thirds/sixths for dyads (3-4 or 8-9 semitones)
                        bool isThird = (dist >= 3 && dist <= 4);
                        bool isSixth = (dist >= 8 && dist <= 9);
                        int score = (isThird || isSixth) ? (100 - dist) : (50 - dist);
                        if (score > (100 - bestDist)) {
                            bestDist = dist;
                            bestDyadMidi = midi;
                        }
                        break;
                    }
                }
            }
            
            if (bestDyadMidi > 0) {
                rhMidiNotes.push_back(bestDyadMidi);
            } else {
                // Fallback: use the 3rd of the chord
                int third = vu::pcForDegree(adjusted.chord, 3);
                if (third >= 0) {
                    int fallback = vu::nearestMidiForPc(third, bestTarget - 4, adjusted.lhHi - 8, adjusted.rhHi);
                    if (fallback > 0 && !wouldClashWithLh(fallback)) {
                        rhMidiNotes.push_back(fallback);
                    }
                }
            }
            voicingName = "piano_rh_dyad";
        }
        // ================================================================
        // FALLBACK VOICING - Always aim for at least a dyad when user is not active
        // ================================================================
        else if (!usedUST) {
            rhMidiNotes.push_back(bestTarget);
            
            // ALWAYS try to add harmony when user is not active
            if (!userActive) {
                // Find an actual chord tone below the melody (NOT scale tones!)
                bool addedSupport = false;
                QVector<int> chordTonePcs = vu::getChordTonePcs(adjusted.chord);
                int topPcFallback = normalizePc(bestTarget);
                
                // Find nearest chord tone below the melody
                int bestSupportMidi = -1;
                int bestSupportDist = 999;
                for (int chordPc : chordTonePcs) {
                    if (chordPc == topPcFallback) continue;  // Skip same PC as top
                    for (int oct = 6; oct >= 4; --oct) {
                        int midi = chordPc + 12 * oct;
                        if (midi < bestTarget && midi >= adjusted.lhHi - 8 && !wouldClashWithLh(midi)) {
                            int dist = bestTarget - midi;
                            if (dist >= 3 && dist <= 12 && dist < bestSupportDist) {
                                bestSupportDist = dist;
                                bestSupportMidi = midi;
                            }
                            break;
                        }
                    }
                }
                
                if (bestSupportMidi > 0) {
                    rhMidiNotes.push_back(bestSupportMidi);
                    addedSupport = true;
                }
                
                // If still no support, add the actual 3rd of the chord
                if (!addedSupport) {
                    int thirdPc = vu::pcForDegree(adjusted.chord, 3);
                    if (thirdPc >= 0) {
                        int thirdMidi = vu::nearestMidiForPc(thirdPc, bestTarget - 4, adjusted.lhHi - 10, adjusted.rhHi);
                        if (thirdMidi > 0 && thirdMidi < bestTarget && !wouldClashWithLh(thirdMidi)) {
                            rhMidiNotes.push_back(thirdMidi);
                        }
                    }
                }
            }
            voicingName = (rhMidiNotes.size() >= 2) ? "piano_rh_dyad_guide" : "piano_rh_single_guide";
        }
        
        std::sort(rhMidiNotes.begin(), rhMidiNotes.end());
        
        // ================================================================
        // CONSONANCE VALIDATION: Ensure all RH notes are chord/scale tones
        // This prevents dissonant notes from slipping through
        // ================================================================
        if (!rhMidiNotes.isEmpty()) {
            rhMidiNotes = vu::validateVoicing(rhMidiNotes, adjusted.chord, 
                                              adjusted.rhLo, adjusted.rhHi);
        }
        
        if (rhMidiNotes.isEmpty()) {
            // If no notes, skip but still update state
            m_state.phrasePatternHitIndex++;
        } else {
            // ================================================================
            // CREATE NOTES FOR THIS VOICING
            // ================================================================
            
            virtuoso::groove::GridPos rhPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                adjusted.playbackBarIndex, adjusted.beatInBar, hitSubdivision, 4, ts);
            rhPos = applyTimingOffset(rhPos, rhTimingOffset, adjusted.bpm, ts);
            
            // Velocity based on phrase hit accent and user activity
            int rhVel = int(baseVel * mappings.velocityMod * baseBrokenFeel.velocityMult + hitVelDelta);
            if (hitIsAccent) rhVel += 5;
            int maxRhVel = userActive ? 50 : 75;
            rhVel = qBound(32, rhVel, maxRhVel);
            
            // Duration: longer for accented hits
            double rhDurBeats = hitIsAccent ? 0.80 : 0.60;
            rhDurBeats *= baseBrokenFeel.durationMult * mappings.durationMod;
            const virtuoso::groove::Rational rhDurWhole(qint64(rhDurBeats * 1000), 4000);
            
            // ================================================================
            // MELODIC FRAGMENTS: Lead-in notes before the main voicing
            // These add jazz vocabulary (approach notes, enclosures, runs)
            // ================================================================
            if (usedFragment && !fragmentNotes.isEmpty()) {
                // Calculate fragment timing (notes lead into the beat)
                int fragOffsetMs = 0;
                
                // Total duration of all fragment notes (to start early enough)
                double totalFragDurBeats = 0.0;
                for (const auto& fn : fragmentNotes) {
                    totalFragDurBeats += fn.durationMult * 0.25;  // Base is 1/16 note
                }
                const int totalFragMs = int((totalFragDurBeats * 60000.0) / adjusted.bpm);
                fragOffsetMs = -totalFragMs;
                
                for (int fi = 0; fi < fragmentNotes.size(); ++fi) {
                    const auto& fn = fragmentNotes[fi];
                    
                    // Validate fragment note to chord/scale tones
                    const int validatedMidi = vu::validateToConsonant(
                        fn.midiNote, adjusted.chord, adjusted.rhLo, adjusted.rhHi);
                    
                    virtuoso::groove::GridPos fragPos = rhPos;
                    fragPos = applyTimingOffset(fragPos, fragOffsetMs, adjusted.bpm, ts);
                    
                    virtuoso::engine::AgentIntentNote fragNote;
                    fragNote.agent = "Piano";
                    fragNote.channel = midiChannel;
                    fragNote.note = validatedMidi;
                    fragNote.baseVelocity = qBound(30, rhVel + fn.velocityDelta, 90);
                    fragNote.startPos = fragPos;
                    
                    // Duration from fragment pattern
                    const double fragDurBeats = fn.durationMult * 0.25;  // Base 1/16 note
                    fragNote.durationWhole = virtuoso::groove::Rational(qint64(fragDurBeats * 1000), 4000);
                    fragNote.structural = false;
                    fragNote.chord_context = adjusted.chordText;
                    fragNote.voicing_type = "piano_melodic_fragment";
                    fragNote.logic_tag = "RH_fragment";
                    
                    plan.notes.push_back(fragNote);
                    
                    // Advance timing for next fragment note
                    fragOffsetMs += int((fragDurBeats * 60000.0) / adjusted.bpm);
                }
            }
            
            // ================================================================
            // ORNAMENTS: Grace notes, turns, appoggiaturas (~12% probability)
            // Add expressive ornaments before the main voicing on special moments
            // ================================================================
            const quint32 ornHash = virtuoso::util::StableHash::mix(rhHash, adjusted.playbackBarIndex * 41);
            const auto rhGenContext = toRhContext(adjusted);
            if (m_rhGen.shouldAddOrnament(rhGenContext, ornHash) && !rhMidiNotes.isEmpty()) {
                const int topNote = rhMidiNotes.last();  // Ornament the top (melodic) note
                const auto orn = m_rhGen.generateOrnament(rhGenContext, topNote, ornHash);
                
                if (orn.type != RhVoicingGenerator::OrnamentType::None && !orn.notes.isEmpty()) {
                    // Calculate ornament start position (before main note)
                    int totalOrnDurMs = 0;
                    for (int d : orn.durationsMs) totalOrnDurMs += d;
                    
                    // Create ornament notes
                    int ornOffsetMs = -totalOrnDurMs;  // Start before main note
                    for (int i = 0; i < orn.notes.size(); ++i) {
                        virtuoso::groove::GridPos ornPos = rhPos;
                        ornPos = applyTimingOffset(ornPos, ornOffsetMs, adjusted.bpm, ts);
                        
                        virtuoso::engine::AgentIntentNote ornNote;
                        ornNote.agent = "Piano";
                        ornNote.channel = midiChannel;
                        ornNote.note = orn.notes[i];
                        ornNote.baseVelocity = orn.velocities[i];
                        ornNote.startPos = ornPos;
                        // Short duration for ornament notes
                        const double ornDurBeats = double(orn.durationsMs[i]) / (60000.0 / adjusted.bpm);
                        ornNote.durationWhole = virtuoso::groove::Rational(qint64(ornDurBeats * 1000), 4000);
                        ornNote.structural = false;
                        ornNote.chord_context = adjusted.chordText;
                        ornNote.voicing_type = "piano_ornament";
                        ornNote.logic_tag = "RH_grace";
                        
                        plan.notes.push_back(ornNote);
                        ornOffsetMs += orn.durationsMs[i];
                    }
                    
                    // Delay main note if ornament requires it
                    if (orn.mainNoteDelayMs > 0) {
                        rhPos = applyTimingOffset(rhPos, orn.mainNoteDelayMs, adjusted.bpm, ts);
                    }
                }
            }
            
            // ================================================================
            // TRIPLET PATTERNS: Rhythmic variety within a single hit
            // Occasionally play the chord as a triplet for rhythmic interest
            // ================================================================
            const bool considerTriplet = m_enableTripletPatterns && 
                                         !userActive && 
                                         !usedFragment &&  // Don't combine with fragments
                                         !adjusted.chordIsNew &&  // Keep chord changes clean
                                         c.energy > 0.35;
            
            // Probability based on creativity and beat position
            const double tripletProb = 0.08 + (energyToCreativity(adjusted.energy) * 0.12);
            const quint32 tripHash = virtuoso::util::StableHash::mix(rhHash, 0xBEAD);
            const bool useTriplet = considerTriplet && ((tripHash % 100) < int(tripletProb * 100));
            
            if (useTriplet && rhMidiNotes.size() > 1) {
                // Generate triplet: play the chord 3 times with triplet rhythm
                const auto tripletPattern = generateTripletPattern(adjusted, 3);  // Full triplet
                
                const double beatDurationMs = 60000.0 / adjusted.bpm;
                
                for (int tripIdx = 0; tripIdx < tripletPattern.size(); ++tripIdx) {
                    const auto& [subdivision, velDelta, isAccent] = tripletPattern[tripIdx];
                    
                    // Calculate triplet position within the beat
                    const double tripletOffsetMs = (subdivision * beatDurationMs) / 4.0;
                    virtuoso::groove::GridPos tripPos = rhPos;
                    tripPos = applyTimingOffset(tripPos, int(tripletOffsetMs), adjusted.bpm, ts);
                    
                    // Shorter duration for triplet notes
                    const double tripDurBeats = rhDurBeats * 0.4;
                    const virtuoso::groove::Rational tripDur(qint64(tripDurBeats * 1000), 4000);
                    
                    for (int noteIdx = 0; noteIdx < rhMidiNotes.size(); ++noteIdx) {
                        int midi = rhMidiNotes[noteIdx];
                        int tripVel = qBound(30, rhVel + velDelta + (isAccent ? 5 : -3), 90);
                        tripVel = contourVelocity(tripVel, noteIdx, rhMidiNotes.size(), true);
                        
                        virtuoso::engine::AgentIntentNote note;
                        note.agent = "Piano";
                        note.channel = midiChannel;
                        note.note = midi;
                        note.baseVelocity = tripVel;
                        note.startPos = tripPos;
                        note.durationWhole = tripDur;
                        note.structural = (tripIdx == 0 && adjusted.chordIsNew);
                        note.chord_context = adjusted.chordText;
                        note.voicing_type = voicingName;
                        note.logic_tag = QString("RH_triplet_%1").arg(tripIdx + 1);
                        
                        plan.notes.push_back(note);
                    }
                }
            } else {
                // Standard: Add all notes of voicing
                for (int noteIdx = 0; noteIdx < rhMidiNotes.size(); ++noteIdx) {
                    int midi = rhMidiNotes[noteIdx];
                    int contouredVel = contourVelocity(rhVel, noteIdx, rhMidiNotes.size(), true);
                    
                    virtuoso::engine::AgentIntentNote note;
                    note.agent = "Piano";
                    note.channel = midiChannel;
                    note.note = midi;
                    note.baseVelocity = contouredVel;
                    note.startPos = rhPos;
                    note.durationWhole = rhDurWhole;
                    note.structural = adjusted.chordIsNew;
                    note.chord_context = adjusted.chordText;
                    note.voicing_type = voicingName;
                    note.logic_tag = QString("RH_%1").arg(hitIntent);
                    
                    plan.notes.push_back(note);
                }
            }
            
            // Update state (both planner and generator)
            m_state.lastRhTopMidi = bestTarget;
            m_state.lastRhMidi = rhMidiNotes;
            if (bestTarget > currentTopMidi) m_state.rhMelodicDirection = 1;
            else if (bestTarget < currentTopMidi) m_state.rhMelodicDirection = -1;
            m_state.rhMotionsThisChord++;
            m_state.phrasePatternHitIndex++;
            
            // Sync to generator state
            m_rhGen.state().lastRhTopMidi = bestTarget;
            m_rhGen.state().lastRhMidi = rhMidiNotes;
            m_rhGen.state().rhMelodicDirection = m_state.rhMelodicDirection;
            m_rhGen.state().rhMotionsThisChord = m_state.rhMotionsThisChord;
        }
        
        // Update register tracking for variety calculation - SAFETY: bounds check
        if (currentTopMidi >= 0 && currentTopMidi <= 127) {
            updateRegisterTracking(currentTopMidi);
        }
    }
    
    // Track phrase peak alternation at phrase boundaries
    // SAFETY: Validate MIDI values are in reasonable range before state updates
    if (adjusted.phraseEndBar && adjusted.beatInBar == 3) {
        const int safeMidi = qBound(0, m_state.lastRhTopMidi, 127);
        const bool wasHigh = (safeMidi > (adjusted.rhLo + adjusted.rhHi) / 2 + 3);
        m_state.lastPhraseWasHigh = wasHigh;
        
        // Update Q/A state for next phrase - validate inputs
        const int safePeak = qBound(0, m_state.currentPhrasePeakMidi, 127);
        const int safeLast = qBound(0, m_state.currentPhraseLastMidi, 127);
        updateQuestionAnswerState(adjusted, safePeak, safeLast);
    }
    
    // Track melodic peaks for Q/A phrasing - SAFETY: bounds check
    const int safeLastRhTop = qBound(0, m_state.lastRhTopMidi, 127);
    if (safeLastRhTop > 0 && safeLastRhTop > m_state.currentPhrasePeakMidi) {
        m_state.currentPhrasePeakMidi = safeLastRhTop;
    }
    if (safeLastRhTop > 0) {
        m_state.currentPhraseLastMidi = safeLastRhTop;
    }
    #endif  // End of disabled old RH code
    
    } // End of RH scope
    
rh_done:
    // Reset phrase tracking on new phrase
    if (newPhrase) {
        m_state.currentPhrasePeakMidi = 60;
    }
    
    // Return early if no notes generated
    if (plan.notes.isEmpty()) {
        return plan;
    }
    
    // Combine for legacy state tracking
    QVector<int> combinedMidi;
    for (const auto& n : plan.notes) {
        if (!combinedMidi.contains(n.note)) {
            combinedMidi.push_back(n.note);
        }
    }
    std::sort(combinedMidi.begin(), combinedMidi.end());
    m_state.lastVoicingMidi = combinedMidi;
    m_state.lastTopMidi = combinedMidi.isEmpty() ? -1 : combinedMidi.last();
    // Get voicing key from the notes we just scheduled (LH notes have the voicing_type)
    QString voicingKeyFromNotes = "piano_lh_voicing";
    for (const auto& n : plan.notes) {
        if (n.logic_tag == "LH" && !n.voicing_type.isEmpty()) {
            voicingKeyFromNotes = n.voicing_type;
            break;
        }
    }
    m_state.lastVoicingKey = voicingKeyFromNotes;

    plan.chosenVoicingKey = m_state.lastVoicingKey;
    plan.ccs = planPedal(adjusted, ts);

    virtuoso::piano::PianoPerformancePlan perf;
    perf.compPhraseId = m_state.currentPhraseId;
    perf.pedalId = pedalId;
    perf.gestureProfile = m_state.lastVoicingKey;
    plan.performance = perf;

    return plan;
}

} // namespace playback
