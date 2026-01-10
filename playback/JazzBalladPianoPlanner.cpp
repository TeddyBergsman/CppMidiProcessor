#include "playback/JazzBalladPianoPlanner.h"

#include "virtuoso/util/StableHash.h"

#include <QtGlobal>
#include <algorithm>

namespace playback {

namespace {

static int clampMidi(int m) { return qBound(0, m, 127); }
static int normalizePc(int pc) { return ((pc % 12) + 12) % 12; }

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
// DROP-2 VOICINGS
// A Drop-2 voicing takes a close-position chord and drops the 2nd voice from
// the top down an octave. This creates a more open, pianistic sound.
// The TOP note becomes the melody - crucial for voice-led playing!
// =============================================================================
struct Drop2Voicing {
    QVector<int> pcs;           // Pitch classes from bottom to top
    int melodyPc;               // The top note (melody)
    int melodyDegree;           // What chord degree is the melody (3, 5, 7, 9, etc.)
    QString name;               // For debugging
    double tension;             // How tense is this voicing (0.0 = consonant)
};

// =============================================================================
// DIATONIC TRIADS
// Triads built from each scale degree that harmonize with the current chord.
// These create rich harmonic color while remaining diatonic and beautiful.
// =============================================================================
struct DiatonicTriad {
    int rootPc;                 // Root of the triad
    bool isMajor;               // Major or minor quality
    int scaleDegree;            // Which scale degree this triad is built on
    QVector<int> pcs;           // The 3 pitch classes
    double harmonyScore;        // How well this harmonizes (higher = better)
    QString name;
};

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
            // 7th depends on quality
            if (chord.quality == music::ChordQuality::Major) {
                return normalizePc(chord.rootPc + 11);  // Major 7th
            }
            if (chord.quality == music::ChordQuality::Diminished) {
                return normalizePc(chord.rootPc + 9);   // Diminished 7th
            }
            return normalizePc(chord.rootPc + 10);  // Minor/dominant 7th
        }
        if (deg == 9) {
            return normalizePc(chord.rootPc + 2);  // 9th = 2 semitones
        }
        return -1;
    };
    
    int third = pcForDegreeLocal(3);
    int fifth = pcForDegreeLocal(5);
    int seventh = pcForDegreeLocal(7);
    int ninth = pcForDegreeLocal(9);
    
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
    
    // Prioritize based on phrase arc
    if (phraseArcPhase == 2 || isPhraseEnd) {
        // Resolving: prefer stable tones (3rd, 5th)
        if (third >= 0) candidates.push_back({third, 3, 3.0});
        if (fifth >= 0) candidates.push_back({fifth, 5, 2.5});
        if (seventh >= 0) candidates.push_back({seventh, 7, 1.5});
    } else if (phraseArcPhase == 1 || isPhrasePeak) {
        // Peak: prefer expressive tones (7th, 9th)
        if (seventh >= 0) candidates.push_back({seventh, 7, 3.0});
        if (ninth >= 0) candidates.push_back({ninth, 9, 2.8});
        if (third >= 0) candidates.push_back({third, 3, 2.0});
        if (fifth >= 0) candidates.push_back({fifth, 5, 1.5});
    } else {
        // Building: balanced, with slight preference for movement
        if (third >= 0) candidates.push_back({third, 3, 2.5});
        if (seventh >= 0) candidates.push_back({seventh, 7, 2.3});
        if (ninth >= 0 && energy > 0.3) candidates.push_back({ninth, 9, 2.0});
        if (fifth >= 0) candidates.push_back({fifth, 5, 1.8});
    }
    
    if (candidates.isEmpty()) return best;
    
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
    
    // Slower tempos allow MORE rubato - make it really noticeable!
    double tempoFactor = (bpm < 70) ? 2.5 : ((bpm < 90) ? 1.8 : 1.2);
    
    // PHRASE BREATHING: Significant stretching at phrase endings
    if (isPhraseEnd) {
        feel.timingOffsetMs = int(25 * tempoFactor);  // Quite late - lingering, breathing
        feel.velocityMult = 0.75;                      // Much softer for resolution
        feel.durationMult = 1.6;                       // Longer - let it breathe and ring
        feel.isBreath = true;
    }
    // PHRASE PEAK: Emphasis, slightly ahead for urgency
    else if (isPhrasePeak) {
        feel.timingOffsetMs = int(-8 * tempoFactor);  // Slightly early - urgent, passionate
        feel.velocityMult = 1.15;                      // Louder at climax
        feel.durationMult = 1.1;                       // Full, present
    }
    // BUILDING: Forward momentum - eager, anticipating
    else if (phraseArcPhase == 0) {
        feel.timingOffsetMs = int(-12 * tempoFactor); // Early - pushing forward eagerly
        feel.velocityMult = 0.90 + 0.15 * energy;     // Build dynamically
        feel.durationMult = 0.85;                      // Shorter - articulate, rhythmic
    }
    // RESOLVING: Relaxing, slowing, breathing
    else if (phraseArcPhase == 2) {
        feel.timingOffsetMs = int(18 * tempoFactor);  // Late - relaxed, unwinding
        feel.velocityMult = 0.70;                      // Softer - intimate
        feel.durationMult = 1.4;                       // Longer - legato, sustained
        feel.isBreath = true;
    }
    
    // BEAT PLACEMENT: Strong metric contrast
    if (beatInBar == 0) {
        // Beat 1: anchor point - slightly early for strength
        feel.timingOffsetMs -= 5;
        feel.velocityMult *= 1.05;
    } else if (beatInBar == 2) {
        // Beat 3: secondary strength
        feel.timingOffsetMs -= 3;
    } else {
        // Beats 2 & 4: weak - laid back and softer
        feel.timingOffsetMs += int(10 * tempoFactor);
        feel.velocityMult *= 0.85;
    }
    
    // SYNCOPATION: Off-beat 16ths swing and breathe
    if (subBeat == 1 || subBeat == 3) {
        feel.timingOffsetMs += int(15 * tempoFactor);  // Laid back, swinging
        feel.velocityMult *= 0.9;                       // Lighter
    }
    
    // CHORD CHANGES: Ground the harmony but still breathe
    if (isChordChange && beatInBar == 0) {
        feel.timingOffsetMs = qBound(-20, feel.timingOffsetMs, 15);  // Controlled but expressive
        feel.durationMult = 1.3;                                     // Let harmony ring
    }
    
    // Cap timing offset - allow more rubato than before!
    feel.timingOffsetMs = qBound(-50, feel.timingOffsetMs, 60);
    feel.velocityMult = qBound(0.55, feel.velocityMult, 1.25);
    feel.durationMult = qBound(0.6, feel.durationMult, 1.8);
    
    return feel;
}

} // namespace

// =============================================================================
// Construction & State Management
// =============================================================================

JazzBalladPianoPlanner::JazzBalladPianoPlanner() {
    reset();
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
}

JazzBalladPianoPlanner::PlannerState JazzBalladPianoPlanner::snapshotState() const {
    QMutexLocker locker(m_stateMutex.get());
    return m_state;
}

void JazzBalladPianoPlanner::restoreState(const PlannerState& s) {
    QMutexLocker locker(m_stateMutex.get());
    m_state = s;
}

// =============================================================================
// Weight Integration
// =============================================================================

JazzBalladPianoPlanner::WeightMappings JazzBalladPianoPlanner::computeWeightMappings(const Context& c) const {
    WeightMappings m;
    const auto& w = c.weights;

    m.playProbMod = 0.4 + 0.8 * qBound(0.0, w.density, 1.0);
    m.playProbMod *= (0.8 + 0.4 * qBound(0.0, w.rhythm, 1.0));
    m.velocityMod = 0.7 + 0.5 * qBound(0.0, w.intensity, 1.0);
    m.voicingFullnessMod = 0.5 + 0.6 * qBound(0.0, w.dynamism, 1.0);
    m.rubatoPushMs = int(25.0 * qBound(0.0, w.emotion, 1.0));
    m.creativityMod = qBound(0.0, w.creativity, 1.0);
    m.tensionMod = qBound(0.0, w.tension, 1.0);
    m.interactivityMod = qBound(0.0, w.interactivity, 1.0);
    m.variabilityMod = qBound(0.0, w.variability, 1.0);
    const double warmthVal = qBound(0.0, w.warmth, 1.0);
    m.durationMod = 0.8 + 0.5 * warmthVal;
    m.registerShiftMod = -3.0 * warmthVal;

    return m;
}

// =============================================================================
// Microtime / Humanization
// =============================================================================

int JazzBalladPianoPlanner::computeTimingOffsetMs(const Context& c, quint32 hash) const {
    const auto mappings = computeWeightMappings(c);
    int offset = 0;

    // REDUCED rubato influence to prevent sloppiness
    const int rubato = int(mappings.rubatoPushMs * 0.5);  // Halved
    if (rubato > 0) {
        const int jitter = int(hash % (2 * rubato + 1)) - rubato;
        offset += jitter;
    }

    // REDUCED offbeat offset
    if (c.beatInBar == 1 || c.beatInBar == 3) {
        offset += 3 + int(mappings.rubatoPushMs * 0.15);  // Much smaller
    }

    // Slight push at cadences
    if (c.cadence01 >= 0.7 && c.beatInBar == 3) {
        offset -= 5;  // Reduced from 8
    }

    // TIGHTER bounds to prevent sloppiness
    return qBound(-25, offset, 25);
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
        if (c.weights.emotion > 0.7) {
            return ArticulationType::Tenuto;  // Full, warm sustain
        }
        return ArticulationType::Legato;
    }
    
    // RH: more varied for expression
    if (atPhraseEnd) {
        return ArticulationType::Portato;  // Gentle release
    }
    if (c.weights.tension > 0.6 && isDownbeat) {
        return ArticulationType::Accent;   // Tension emphasis
    }
    if (c.weights.warmth > 0.7) {
        return ArticulationType::Legato;   // Warm, connected
    }
    if (c.beatInBar == 2 && c.weights.rhythm > 0.4) {
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
    if (c.energy < 0.25 && c.weights.density < 0.3) {
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
    if (c.weights.emotion < 0.3) return false;  // Low emotion = less phrasing
    
    // Probability based on emotion and warmth
    const double prob = 0.4 + (c.weights.emotion * 0.3) + (c.weights.warmth * 0.2);
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
    if (c.weights.emotion > 0.6) prob += 0.04;
    
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
    const double rhythmWeight = c.weights.rhythm;
    const double creativity = c.weights.creativity;
    
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
        m_state.responseWindowBeats = 4 + int(4 * c.weights.interactivity);  // 4-8 beats to respond
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
           m_state.responseWindowBeats > 0 && 
           c.weights.interactivity >= 0.3;
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
    const int boost = int(2.0 * windowProgress * c.weights.interactivity);
    
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
        c.weights.creativity >= 0.7 && 
        c.weights.variability >= 0.6 &&
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
            p.innerVoiceMovement = 0.4;      // Signature inner voice motion
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
            p.innerVoiceMovement = 0.25;     // Some inner movement
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
            p.innerVoiceMovement = 0.15;     // Less inner movement
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
            p.innerVoiceMovement = 0.35;     // Good inner movement
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
            p.innerVoiceMovement = 0.3;
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
// Voicing Generation
// =============================================================================

QVector<JazzBalladPianoPlanner::Voicing> JazzBalladPianoPlanner::generateVoicingCandidates(
    const Context& c, VoicingDensity density) const {

    QVector<Voicing> candidates;
    candidates.reserve(6);

    const auto& chord = c.chord;
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) {
        return candidates;
    }

    const bool hasSeventh = (seventhInterval(chord) >= 0);
    const bool is6thChord = (chord.extension == 6 && chord.seventh == music::SeventhQuality::None);
    const bool hasColorTone = hasSeventh || is6thChord;
    const bool hasSlashBass = (chord.bassPc >= 0 && chord.bassPc != chord.rootPc);
    const int slashBassPc = hasSlashBass ? normalizePc(chord.bassPc) : -1;

    int voicingFloor = c.lhLo;
    int voicingCeiling = c.rhHi;

    auto templates = getVoicingTemplates(hasColorTone, is6thChord);

    for (const auto& tmpl : templates) {
        if (density == VoicingDensity::Sparse && tmpl.degrees.size() > 2) continue;
        if (density == VoicingDensity::Guide && tmpl.degrees.size() > 3) continue;

        Voicing v;
        v.ontologyKey = tmpl.name;

        // Determine voicing type from ontology key
        if (tmpl.name.contains("rootless_a")) v.type = VoicingType::RootlessA;
        else if (tmpl.name.contains("rootless_b")) v.type = VoicingType::RootlessB;
        else if (tmpl.name.contains("guide") || tmpl.name.contains("shell")) v.type = VoicingType::Shell;
        else if (tmpl.name.contains("quartal")) v.type = VoicingType::Quartal;
        else v.type = VoicingType::Shell;

        v.density = density;

        // Build pitch classes
        for (int deg : tmpl.degrees) {
            int pc = pcForDegree(chord, deg);
            if (pc >= 0 && (!hasSlashBass || pc != slashBassPc)) {
                v.pcs.push_back(pc);
            }
        }

        if (v.pcs.isEmpty()) continue;

        // Determine base position for voicing
        int baseMidi = voicingFloor;
        if (!m_state.lastVoicingMidi.isEmpty()) {
            int sum = 0;
            for (int m : m_state.lastVoicingMidi) sum += m;
            baseMidi = sum / m_state.lastVoicingMidi.size();
            // SAFETY: Ensure min <= max for qBound
            const int voicingHi = qMax(voicingFloor, voicingCeiling - 12);
            baseMidi = qBound(voicingFloor, baseMidi - 6, voicingHi);
        }

        // For Type B, start lower (it begins on the 7th which is lower than the 3rd)
        if (tmpl.name == "RootlessB") {
            baseMidi = qMax(voicingFloor, baseMidi - 5);
        }

        v.midiNotes = realizeVoicingTemplate(tmpl.degrees, chord, baseMidi, voicingCeiling);

        // Filter out slash bass notes
        if (hasSlashBass) {
            QVector<int> filtered;
            for (int m : v.midiNotes) {
                if (normalizePc(m) != slashBassPc) {
                    filtered.push_back(m);
                }
            }
            v.midiNotes = filtered;
            v.avoidsSlashBass = true;
        }

        if (v.midiNotes.size() < 2) continue;

        v.midiNotes = repairVoicing(v.midiNotes);
        v.cost = voiceLeadingCost(m_state.lastVoicingMidi, v.midiNotes);

        if (!v.midiNotes.isEmpty()) {
            v.topNoteMidi = v.midiNotes.last();
            v.topNotePc = normalizePc(v.topNoteMidi);
        }

        candidates.push_back(v);
    }

    return candidates;
}

// =============================================================================
// Context-Aware Voicing Density
// =============================================================================

JazzBalladPianoPlanner::VoicingDensity JazzBalladPianoPlanner::computeContextDensity(const Context& c) const {
    const auto mappings = computeWeightMappings(c);

    double densityScore = 0.5;
    densityScore += 0.3 * (c.energy - 0.5);

    const double phraseProgress = (c.phraseBars > 0)
        ? double(c.barInPhrase) / double(c.phraseBars)
        : 0.5;
    densityScore += 0.15 * (phraseProgress - 0.5);

    if (c.cadence01 >= 0.5) {
        densityScore += 0.1 * c.cadence01;
    }

    if (c.userBusy || c.userDensityHigh) {
        densityScore -= 0.25;
    }

    densityScore += 0.15 * (mappings.voicingFullnessMod - 0.8);

    if (c.bpm < 70) {
        densityScore -= 0.1;
    }

    densityScore = qBound(0.25, densityScore, 0.95);

    if (densityScore < 0.35) return VoicingDensity::Guide;
    if (densityScore < 0.50) return VoicingDensity::Medium;
    if (densityScore < 0.70) return VoicingDensity::Full;
    return VoicingDensity::Lush;
}

// =============================================================================
// Melodic Top Note Selection
// =============================================================================

int JazzBalladPianoPlanner::selectMelodicTopNote(const QVector<int>& candidatePcs,
                                                  int rhLo, int rhHi,
                                                  int lastTopMidi,
                                                  const Context& /*c*/) const {
    if (candidatePcs.isEmpty()) return -1;

    if (lastTopMidi < 0) {
        const int targetMidi = (rhLo + rhHi) / 2 + 4;
        int bestPc = candidatePcs.last();
        return nearestMidiForPc(bestPc, targetMidi, rhLo, rhHi);
    }

    QVector<std::pair<int, double>> candidates;
    candidates.reserve(candidatePcs.size() * 3);

    for (int pc : candidatePcs) {
        for (int octave = 4; octave <= 6; ++octave) {
            int midi = pc + 12 * octave;
            if (midi < rhLo || midi > rhHi) continue;

            double cost = 0.0;
            const int absMotion = qAbs(midi - lastTopMidi);

            if (absMotion <= 2) cost += 0.0;
            else if (absMotion <= 4) cost += 1.0;
            else if (absMotion <= 7) cost += 2.0;
            else cost += 4.0;

            const int sweetCenter = (rhLo + rhHi) / 2 + 4;
            cost += qAbs(midi - sweetCenter) * 0.1;

            candidates.push_back({midi, cost});
        }
    }

    if (candidates.isEmpty()) {
        return nearestMidiForPc(candidatePcs.last(), lastTopMidi, rhLo, rhHi);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    return candidates.first().first;
}

int JazzBalladPianoPlanner::getDegreeForPc(int pc, const music::ChordSymbol& chord) const {
    const int root = (chord.rootPc >= 0) ? chord.rootPc : 0;
    const int interval = normalizePc(pc - root);

    switch (interval) {
        case 0: return 1;
        case 3: case 4: return 3;
        case 6: case 7: case 8: return 5;
        case 9: case 10: case 11: return 7;
        case 1: case 2: return 9;
        case 5: return 11;
        default: return 0;
    }
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
        
        // Pedal depth: shallower for fast changes, deeper for slow passages
        int pedalDepth;
        if (veryFrequentChanges) {
            pedalDepth = 30 + int(25.0 * c.energy);  // Light: 30-55
        } else if (frequentChanges) {
            pedalDepth = 45 + int(30.0 * c.energy);  // Medium: 45-75
        } else {
            pedalDepth = 55 + int(40.0 * c.energy);  // Fuller: 55-95
        }
        pedalDepth = qBound(30, pedalDepth, 95);  // Never too light or too heavy
        
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
    
    if (c.weights.tension > 0.4 && ninth >= 0) {
        targetPc = ninth;
    } else if (c.weights.tension > 0.6 && thirteenth >= 0) {
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

// RH Melodic: Create dyads/triads that move melodically
// Top note follows stepwise motion, inner voice provides harmony
// CONSONANCE-FIRST: Prioritize guide tones, use extensions based on tension
JazzBalladPianoPlanner::RhMelodic JazzBalladPianoPlanner::generateRhMelodicVoicing(
    const Context& c, int targetTopMidi) const {
    
    RhMelodic rh;
    const auto& chord = c.chord;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return rh;
    
    // ================================================================
    // CONSONANCE-FIRST APPROACH
    // Guide tones (3, 7) are always safe and define the chord
    // Extensions (9, 13) add color but only when appropriate
    // ================================================================
    QVector<int> colorPcs;
    
    // Core chord tones
    int third = pcForDegree(chord, 3);
    int fifth = pcForDegree(chord, 5);
    int seventh = pcForDegree(chord, 7);
    int root = chord.rootPc;
    
    // Extensions
    int ninth = pcForDegree(chord, 9);
    int thirteenth = pcForDegree(chord, 13);
    
    // Determine tension level for extension usage
    const double tensionLevel = c.weights.tension * 0.6 + c.energy * 0.4;
    const bool isDominant = (chord.quality == music::ChordQuality::Dominant);
    
    // PRIORITY 1: Guide tones (always beautiful)
    if (third >= 0) colorPcs.push_back(third);
    if (seventh >= 0) colorPcs.push_back(seventh);
    
    // PRIORITY 2: Fifth (safe, consonant)
    if (fifth >= 0) colorPcs.push_back(fifth);
    
    // PRIORITY 3: Extensions (pcForDegree now filters appropriately)
    if (tensionLevel > 0.3) {
        if (ninth >= 0) colorPcs.push_back(ninth);
        if (thirteenth >= 0 && tensionLevel > 0.5) colorPcs.push_back(thirteenth);
    }
    
    if (colorPcs.isEmpty()) return rh;
    
    // Select top note: prefer stepwise motion from previous
    int lastTop = (m_state.lastRhTopMidi > 0) ? m_state.lastRhTopMidi : 74;
    if (targetTopMidi > 0) lastTop = targetTopMidi;
    
    // Find best top note candidate (within 2-4 semitones of last)
    QVector<std::pair<int, double>> candidates;
    for (int pc : colorPcs) {
        for (int oct = 5; oct <= 7; ++oct) {
            int midi = pc + 12 * oct;
            if (midi < c.rhLo || midi > c.rhHi) continue;
            
            double cost = 0.0;
            int motion = qAbs(midi - lastTop);
            
            // Prefer stepwise (1-2 semitones)
            if (motion <= 2) cost = 0.0;
            else if (motion <= 4) cost = 1.0;
            else if (motion <= 7) cost = 3.0;
            else cost = 6.0;
            
            // PREFERENCE for guide tones (they sound most "right")
            if (pc == third || pc == seventh) cost -= 0.8;
            // Slight preference for extensions only at higher tension
            else if ((pc == ninth || pc == thirteenth) && tensionLevel > 0.5) cost -= 0.3;
            
            // Prefer staying in sweet spot (72-82)
            if (midi >= 72 && midi <= 82) cost -= 0.3;
            
            candidates.push_back({midi, cost});
        }
    }
    
    if (candidates.isEmpty()) return rh;
    
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    rh.topNoteMidi = candidates.first().first;
    int topPc = normalizePc(rh.topNoteMidi);
    
    // Determine melodic direction
    if (rh.topNoteMidi > lastTop + 1) rh.melodicDirection = 1;
    else if (rh.topNoteMidi < lastTop - 1) rh.melodicDirection = -1;
    else rh.melodicDirection = 0;
    
    // ================================================================
    // CONSONANT SECOND VOICE SELECTION
    // Prefer 3rds (3-4 semitones) and 6ths (8-9 semitones)
    // Avoid 2nds, tritones, and 7ths unless tension is high
    // ================================================================
    int secondPc = -1;
    int secondMidi = -1;
    int bestConsonance = 99;
    
    // Find the most consonant second voice with proper scoring
    for (int pc : colorPcs) {
        if (pc == topPc) continue;
        int interval = (topPc - pc + 12) % 12;
        
        // Score by consonance (lower is better)
        int score = 99;
        if (interval == 3 || interval == 4) score = 0;  // Minor/major 3rd - sweetest
        else if (interval == 8 || interval == 9) score = 1;  // Minor/major 6th - beautiful
        else if (interval == 5) score = 2;  // Perfect 4th - stable
        else if (interval == 7) score = 3;  // Perfect 5th - open
        else if ((interval == 10 || interval == 11) && tensionLevel > 0.5) score = 5; // 7ths OK with tension
        // Avoid 2nds (1-2) and tritones (6) unless very high tension
        else if ((interval == 1 || interval == 2) && tensionLevel > 0.7) score = 7;
        else if (interval == 6 && isDominant && tensionLevel > 0.6) score = 6;
        else score = 99; // Skip dissonant intervals at low tension
        
        if (score < bestConsonance) {
            bestConsonance = score;
            secondPc = pc;
        }
    }
    
    // Last resort: just use the 7th or 3rd (guaranteed consonant with chord)
    if (secondPc < 0 || bestConsonance > 5) {
        secondPc = (seventh >= 0 && seventh != topPc) ? seventh : third;
    }
    
    if (secondPc >= 0) {
        // Place second voice 3-9 semitones below top (sweet spot for dyads)
        secondMidi = rh.topNoteMidi - 3;
        while (normalizePc(secondMidi) != secondPc && secondMidi > rh.topNoteMidi - 10) {
            secondMidi--;
        }
        
        // Verify actual interval is consonant before adding
        int actualInterval = rh.topNoteMidi - secondMidi;
        bool intervalOk = (actualInterval >= 3 && actualInterval <= 9) ||
                          (actualInterval == 10 && tensionLevel > 0.5);
        
        if (intervalOk && secondMidi >= c.rhLo) {
            rh.midiNotes.push_back(secondMidi);
        }
    }
    
    rh.midiNotes.push_back(rh.topNoteMidi);
    std::sort(rh.midiNotes.begin(), rh.midiNotes.end());
    
    // Determine ontology key
    if (topPc == ninth || topPc == thirteenth) {
        rh.isColorTone = true;
        rh.ontologyKey = (rh.midiNotes.size() == 2) ? "piano_rh_dyad_color" : "piano_rh_single_color";
    } else {
        rh.isColorTone = false;
        rh.ontologyKey = (rh.midiNotes.size() == 2) ? "piano_rh_dyad_guide" : "piano_rh_single_guide";
    }
    
    return rh;
}

// =============================================================================
// UPPER STRUCTURE TRIADS (UST) - Bill Evans Signature Sound
// =============================================================================
// A UST is a simple major or minor triad played in the RH that creates
// sophisticated extensions over the LH chord. The magic is that simple
// triads produce complex harmonic colors.
//
// Key relationships:
//   Dominant 7:  D/C7 → 9-#11-13 (lydian dominant color)
//                Eb/C7 → b9-11-b13 (altered dominant)
//                F#/C7 → #11-7-b9 (tritone sub color)
//   Minor 7:     F/Dm7 → b3-5-b7 (reinforces minor quality)
//                Eb/Dm7 → b9-11-b13 (phrygian color)
//   Major 7:     D/Cmaj7 → 9-#11-13 (lydian color)
//                E/Cmaj7 → 3-#5-7 (augmented color)
// =============================================================================

QVector<JazzBalladPianoPlanner::UpperStructureTriad> JazzBalladPianoPlanner::getUpperStructureTriads(
    const music::ChordSymbol& chord) const {
    
    QVector<UpperStructureTriad> triads;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return triads;
    
    const int root = chord.rootPc;
    const bool isDominant = (chord.quality == music::ChordQuality::Dominant);
    const bool isMajor = (chord.quality == music::ChordQuality::Major);
    const bool isMinor = (chord.quality == music::ChordQuality::Minor);
    const bool isAlt = chord.alt && isDominant;
    
    // ==========================================================================
    // DOMINANT 7TH CHORDS - Most UST options (the jazz workhorse)
    // ==========================================================================
    if (isDominant) {
        if (isAlt) {
            // Altered dominant: prefer tense USTs
            // bII major (half step up) → b9, 3, b13
            triads.push_back({normalizePc(root + 1), true, 0.7, "b9-3-b13"});
            // bVI major (minor 6th up) → b9, #11, b13
            triads.push_back({normalizePc(root + 8), true, 0.8, "b9-#11-b13"});
            // #IV major (tritone) → #11, 7, b9
            triads.push_back({normalizePc(root + 6), true, 0.6, "#11-7-b9"});
        } else {
            // Standard dominant - range of colors from safe to tense
            
            // II major (whole step up) → 9-#11-13 (lydian dominant - BEAUTIFUL)
            triads.push_back({normalizePc(root + 2), true, 0.3, "9-#11-13"});
            
            // bVII major (whole step down) → 7-9-11 (mixolydian - safe)
            triads.push_back({normalizePc(root + 10), true, 0.2, "b7-9-11"});
            
            // VI major (major 6th up) → 13-#9-#11 (bright tension)
            triads.push_back({normalizePc(root + 9), true, 0.5, "13-#9-#11"});
            
            // bIII major (minor 3rd up) → #9-#11-13 (more tension)
            triads.push_back({normalizePc(root + 3), true, 0.6, "#9-#11-13"});
            
            // #IV major (tritone) → #11-7-b9 (tritone sub hint)
            triads.push_back({normalizePc(root + 6), true, 0.7, "#11-7-b9"});
        }
    }
    
    // ==========================================================================
    // MINOR 7TH CHORDS
    // ==========================================================================
    else if (isMinor) {
        // bIII major (minor 3rd up) → b3-5-b7 (reinforces minor - SAFE)
        triads.push_back({normalizePc(root + 3), true, 0.1, "b3-5-b7"});
        
        // IV major (perfect 4th up) → 11-13-9 (dorian color - beautiful)
        triads.push_back({normalizePc(root + 5), true, 0.3, "11-13-9"});
        
        // bVII major (minor 7th up) → b7-9-11 (safe extension)
        triads.push_back({normalizePc(root + 10), true, 0.2, "b7-9-11"});
        
        // II minor (whole step up) → 9-11-13 (dorian 9-11-13)
        triads.push_back({normalizePc(root + 2), false, 0.4, "9-11-13"});
    }
    
    // ==========================================================================
    // MAJOR 7TH CHORDS
    // ==========================================================================
    else if (isMajor) {
        // II major (whole step up) → 9-#11-13 (lydian color - CLASSIC)
        triads.push_back({normalizePc(root + 2), true, 0.3, "9-#11-13"});
        
        // V major (perfect 5th up) → 5-7-9 (simple, safe extension)
        triads.push_back({normalizePc(root + 7), true, 0.1, "5-7-9"});
        
        // III minor (major 3rd up) → 3-5-7 (reinforces maj7 - SAFE)
        triads.push_back({normalizePc(root + 4), false, 0.1, "3-5-7"});
        
        // VII minor (major 7th up) → 7-9-#11 (lydian hint)
        triads.push_back({normalizePc(root + 11), false, 0.4, "7-9-#11"});
    }
    
    // ==========================================================================
    // HALF-DIMINISHED / DIMINISHED
    // ==========================================================================
    else if (chord.quality == music::ChordQuality::HalfDiminished) {
        // bIII major → b3-5-b7 (locrian natural 9)
        triads.push_back({normalizePc(root + 3), true, 0.2, "b3-5-b7"});
        
        // bVI major → b9-11-b13 (phrygian color)
        triads.push_back({normalizePc(root + 8), true, 0.5, "b9-11-b13"});
    }
    
    // Sort by tension level (safest first)
    std::sort(triads.begin(), triads.end(), 
              [](const UpperStructureTriad& a, const UpperStructureTriad& b) {
                  return a.tensionLevel < b.tensionLevel;
              });
    
    return triads;
}

JazzBalladPianoPlanner::RhMelodic JazzBalladPianoPlanner::buildUstVoicing(
    const Context& c, const UpperStructureTriad& ust) const {
    
    RhMelodic rh;
    
    // Build the triad: root, 3rd, 5th of the UST
    int ustRoot = ust.rootPc;
    int ustThird = normalizePc(ustRoot + (ust.isMajor ? 4 : 3)); // major 3rd or minor 3rd
    int ustFifth = normalizePc(ustRoot + 7); // perfect 5th
    
    // Target the top voice for melodic continuity
    int lastTop = (m_state.lastRhTopMidi > 0) ? m_state.lastRhTopMidi : 76;
    
    // Find best voicing of the triad in the RH register
    // Prefer inversion that puts a note near the last top note
    QVector<QVector<int>> inversions = {
        {ustRoot, ustThird, ustFifth},  // Root position
        {ustThird, ustFifth, ustRoot},  // 1st inversion
        {ustFifth, ustRoot, ustThird}   // 2nd inversion
    };
    
    int bestInversion = 0;
    int bestDist = 999;
    int bestTopMidi = -1;
    
    for (int inv = 0; inv < inversions.size(); ++inv) {
        int topPc = inversions[inv].last();
        
        // Find MIDI note for top voice
        for (int oct = 5; oct <= 7; ++oct) {
            int topMidi = topPc + 12 * oct;
            if (topMidi < c.rhLo || topMidi > c.rhHi) continue;
            
            int dist = qAbs(topMidi - lastTop);
            // Prefer stepwise motion (1-4 semitones)
            if (dist >= 1 && dist <= 4 && dist < bestDist) {
                bestDist = dist;
                bestInversion = inv;
                bestTopMidi = topMidi;
            } else if (dist < bestDist && dist <= 7) {
                bestDist = dist;
                bestInversion = inv;
                bestTopMidi = topMidi;
            }
        }
    }
    
    if (bestTopMidi < 0) {
        // Fallback: just pick middle register
        bestTopMidi = 76;
        bestInversion = 0;
    }
    
    // Build the MIDI notes from bottom to top
    const QVector<int>& pcs = inversions[bestInversion];
    int topMidi = bestTopMidi;
    int topPc = pcs.last();
    
    // Find top note first
    while (normalizePc(topMidi) != topPc && topMidi >= c.rhLo) {
        topMidi--;
    }
    topMidi = bestTopMidi; // Use the calculated top
    
    // Stack from top down (closest voicing)
    QVector<int> midiNotes;
    midiNotes.push_back(topMidi);
    
    // Middle note
    int middlePc = pcs[1];
    int middleMidi = topMidi - 3;
    while (normalizePc(middleMidi) != middlePc && middleMidi > topMidi - 12) {
        middleMidi--;
    }
    if (middleMidi >= c.rhLo) {
        midiNotes.insert(midiNotes.begin(), middleMidi);
    }
    
    // Bottom note
    int bottomPc = pcs[0];
    int bottomMidi = (midiNotes.size() > 1) ? midiNotes.first() - 3 : topMidi - 6;
    while (normalizePc(bottomMidi) != bottomPc && bottomMidi > topMidi - 14) {
        bottomMidi--;
    }
    if (bottomMidi >= c.rhLo && (midiNotes.isEmpty() || bottomMidi < midiNotes.first())) {
        midiNotes.insert(midiNotes.begin(), bottomMidi);
    }
    
    std::sort(midiNotes.begin(), midiNotes.end());
    
    rh.midiNotes = midiNotes;
    rh.topNoteMidi = midiNotes.isEmpty() ? -1 : midiNotes.last();
    rh.melodicDirection = (rh.topNoteMidi > lastTop) ? 1 : ((rh.topNoteMidi < lastTop) ? -1 : 0);
    
    // Map UST to ontology key based on interval from chord root
    // e.g., rootPc offset 2 → II, 3 → bIII, 4 → III, etc.
    const int interval = normalizePc(ust.rootPc - c.chord.rootPc);
    QString romanNumeral;
    switch (interval) {
        case 0:  romanNumeral = "I"; break;
        case 1:  romanNumeral = "bII"; break;
        case 2:  romanNumeral = "II"; break;
        case 3:  romanNumeral = "bIII"; break;
        case 4:  romanNumeral = "III"; break;
        case 5:  romanNumeral = "IV"; break;
        case 6:  romanNumeral = "bV"; break;
        case 7:  romanNumeral = "V"; break;
        case 8:  romanNumeral = "bVI"; break;
        case 9:  romanNumeral = "VI"; break;
        case 10: romanNumeral = "bVII"; break;
        case 11: romanNumeral = "VII"; break;
        default: romanNumeral = "I"; break;
    }
    // Use ontology key format: piano_ust_bIII or piano_ust_ii_min
    if (ust.isMajor) {
        rh.ontologyKey = QString("piano_ust_%1").arg(romanNumeral);
    } else {
        rh.ontologyKey = QString("piano_ust_%1_min").arg(romanNumeral.toLower());
    }
    rh.isColorTone = true;
    
    return rh;
}

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
    
    const double tensionLevel = c.weights.tension * 0.6 + c.energy * 0.4;
    const double creativity = c.weights.creativity;
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
    
    return patterns;
}

int JazzBalladPianoPlanner::selectPhrasePattern(const Context& c, quint32 hash) const {
    const auto patterns = getAvailablePhrasePatterns(c);
    if (patterns.isEmpty()) return -1;
    
    // Select based on musical context
    double targetDensity = 0.15; // Default: very sparse
    
    // Higher energy = slightly more active
    targetDensity += c.energy * 0.15;
    
    // Near cadence = more activity for resolution
    if (c.cadence01 > 0.5) targetDensity += 0.08;
    
    // User active = much sparser (let them lead)
    if (c.userBusy || c.userDensityHigh) targetDensity = 0.10;
    
    // Find pattern with closest density
    int bestIdx = 0;
    double bestDiff = 999.0;
    for (int i = 0; i < patterns.size(); ++i) {
        double diff = qAbs(patterns[i].densityRating - targetDensity);
        // Add some randomness to avoid always picking the same pattern
        diff += (double)((hash + i * 17) % 10) * 0.01;
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIdx = i;
        }
    }
    
    return bestIdx;
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
        double prob = 0.70 + 0.20 * c.weights.density;
        // Higher at phrase boundaries (need to be present)
        if (c.barInPhrase == 0 || c.phraseEndBar) prob = 0.85;
        // Groove lock: if bass very active, be slightly sparser
        if (complementBass) prob -= 0.15;
        return (hash % 100) < int(prob * 100);
    }
    
    // Beat 3: secondary strong beat - good for comping
    if (c.beatInBar == 2) {
        double prob = 0.45 + 0.30 * c.weights.density;
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
    
    // Beat 2: syncopated anticipation opportunity
    if (c.beatInBar == 1) {
        // This is the "and of 1" feel - creates forward motion
        double prob = 0.15 + 0.30 * c.energy + 0.20 * c.weights.rhythm;
        // More likely approaching cadences
        if (c.cadence01 >= 0.3) prob += 0.15;
        return (hash % 100) < int(prob * 100);
    }
    
    // Beat 4: pickup/anticipation to next bar
    if (c.beatInBar == 3) {
        double prob = 0.10 + 0.25 * c.energy;
        // More likely if next beat is a chord change
        if (c.beatsUntilChordChange <= 1) prob += 0.25;
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
    // RESOLVING PHASE (after phrase peak): Can breathe, but still play
    // This is where the music breathes - but not silence!
    // ================================================================
    if (arcPhase == 2) {
        // Resolving: sparse but present (1-2)
        if (c.chordIsNew) return 2;  // Chord changes still get activity
        return (hash % 100) < 60 ? 1 : 2; // Mostly single notes
    }
    
    // ================================================================
    // WEAK BEATS (2 and 4): Lighter but not silent
    // Use for syncopation and color
    // ================================================================
    const bool isWeakBeat = (c.beatInBar == 1 || c.beatInBar == 3);
    if (isWeakBeat && !c.chordIsNew) {
        // Weak beats: lighter activity
        return (hash % 100) < 65 ? 1 : 2;
    }
    
    // ================================================================
    // BUILDING PHASE: Start with 1, gradually increase to 2-3
    // ================================================================
    if (arcPhase == 0) {
        const double phraseProg = double(c.barInPhrase) / qMax(1, c.phraseBars);
        
        // Early in phrase: 1-2 notes
        if (phraseProg < 0.3) {
            if (c.chordIsNew) return 2;
            return (hash % 100) < 60 ? 1 : 2;
        }
        // Mid-phrase building: 1-2 notes
        if (phraseProg < 0.7) {
            if (c.chordIsNew) return (c.energy > 0.5) ? 3 : 2;
            return (hash % 100) < 50 ? 2 : 1;
        }
        // Approaching peak: 2-3 notes
        if (c.chordIsNew) return qMin(3, int(2 + c.energy));
        return (hash % 100) < 60 ? 2 : 1;
    }
    
    // ================================================================
    // PEAK PHASE: Most active - 2-3 hits per beat
    // Maximum activity here
    // ================================================================
    if (arcPhase == 1) {
        if (c.chordIsNew) {
            // Chord changes at peak: 3 or even 4 based on energy/density
            int peakActivity = 3;
            if (c.energy > 0.7 && c.weights.density > 0.6) {
                peakActivity = 4;
            }
            return peakActivity;
        }
        // Non-chord-change beats at peak: 2-3
        return (c.energy > 0.5) ? 3 : 2;
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
    const double tensionLevel = c.weights.tension * 0.6 + c.energy * 0.4;
    
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
        const_cast<JazzBalladPianoPlanner*>(this)->generatePhraseMotif(adjusted);
    }
    
    // Get current phrase arc phase for decisions below
    const int arcPhase = computePhraseArcPhase(adjusted);
    
    // ================================================================
    // CALL-AND-RESPONSE: Update interactive state
    // Detects when user stops playing and enables fill mode
    // ================================================================
    const_cast<JazzBalladPianoPlanner*>(this)->updateResponseState(adjusted);
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
    // VELOCITY: Must respect user's dynamics!
    // When user is playing/singing, piano BACKS OFF significantly
    // Base velocity is lower and scales with user activity
    // ================================================================
    int baseVel;
    
    if (adjusted.userBusy || adjusted.userDensityHigh || adjusted.userIntensityPeak) {
        // USER IS ACTIVE: Play SOFT to support, not overpower
        // Base around 40-55, much lower than solo playing
        baseVel = 40 + int(15.0 * adjusted.energy);
    } else if (adjusted.userSilence) {
        // USER IS SILENT: Can play with more presence
        // Base around 50-70
        baseVel = 50 + int(20.0 * adjusted.energy);
    } else {
        // NORMAL: Moderate dynamics
        baseVel = 45 + int(20.0 * adjusted.energy);
    }
    
    // Additional velocity reduction based on intensity weight (respects CC2)
    // If user is playing softly (low intensity), we should also be soft
    if (adjusted.weights.intensity < 0.4) {
        baseVel = int(baseVel * (0.7 + 0.3 * adjusted.weights.intensity / 0.4));
    }
    
    // ================================================================
    // PHRASE ARC DYNAMICS: Shape velocity across the phrase
    // Building: crescendo toward peak
    // Peak: maximum dynamics
    // Resolving: diminuendo
    // ================================================================
    switch (arcPhase) {
        case 0: { // Building - gradual crescendo
            const double buildProgress = double(adjusted.barInPhrase) / (0.4 * adjusted.phraseBars);
            baseVel = int(baseVel * (0.85 + 0.15 * buildProgress));
            break;
        }
        case 1: // Peak - full dynamics
            baseVel = int(baseVel * 1.08); // Slight boost at climax
            break;
        case 2: { // Resolving - diminuendo
            const int resolveStart = adjusted.barInPhrase - int(0.7 * adjusted.phraseBars);
            const int resolveTotal = adjusted.phraseBars - int(0.7 * adjusted.phraseBars);
            const double resolveProgress = double(resolveStart) / qMax(1, resolveTotal);
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
    // LEFT HAND: Rootless voicings (Bill Evans Type A/B)
    // - Always plays (doesn't back off for user)
    // - Multiple hits per chord with variation
    // - Sometimes syncopates (anticipates chord changes)
    // ==========================================================================
    
    const bool lhPlays = shouldLhPlayBeat(adjusted, lhHash);
    LhVoicing lhVoicing;
    
    // Check for intentional rest (breath and space)
    const bool wantsRest = shouldRest(adjusted, lhHash);
    if (wantsRest && !adjusted.chordIsNew) {
        // Intentional silence - skip LH this beat for musical breath
        // But never rest on chord changes
    } else if (lhPlays) {
        // ================================================================
        // LH VOICING SELECTION: Choose between rootless and quartal
        // Quartal voicings create open, modern sound (Bill Evans, McCoy Tyner)
        // Use quartal ~15-20% of the time for variety
        // ================================================================
        const bool useQuartal = (styleProfile.quartalPreference > 0) && 
                                ((lhHash % 100) < int(styleProfile.quartalPreference * 100)) &&
                                !adjusted.chordIsNew;  // Always use standard on chord changes
        
        if (useQuartal) {
            lhVoicing = generateLhQuartalVoicing(adjusted);
        } else {
            lhVoicing = generateLhRootlessVoicing(adjusted);
        }
        
        // ================================================================
        // INNER VOICE MOVEMENT: On beat 3, add subtle melodic motion
        // This makes sustained chords breathe and feel alive
        // ================================================================
        if (adjusted.beatInBar == 2 && !adjusted.chordIsNew && styleProfile.innerVoiceMovement > 0) {
            const bool doInnerMovement = (lhHash % 100) < int(styleProfile.innerVoiceMovement * 100);
            if (doInnerMovement) {
                lhVoicing = applyInnerVoiceMovement(lhVoicing, adjusted, adjusted.beatInBar);
                // Update state for alternation
                const_cast<JazzBalladPianoPlanner*>(this)->m_state.lastInnerVoiceIndex++;
            }
        }
        
        if (!lhVoicing.midiNotes.isEmpty()) {
            // ================================================================
            // LH RHYTHM PATTERN: Determine how many hits and when
            // ================================================================
            struct LhHit {
                int sub = 0;           // subdivision (0=beat, 1=e, 2=and, 3=a)
                int velDelta = 0;      // velocity adjustment
                bool useAltVoicing = false; // use alternate voicing (Type B if was A, etc.)
                bool layBack = false;       // play slightly late (jazz feel)
            };
            
            QVector<LhHit> lhHits;
            
            // ================================================================
            // MUSICAL INTENT: Pattern selection based on phrase position, 
            // energy, and cadence - NOT random hash
            // ================================================================
            
            // Determine phrase context
            const bool phraseStart = (adjusted.barInPhrase == 0);
            const bool phraseMid = (adjusted.barInPhrase >= 1 && adjusted.barInPhrase < adjusted.phraseBars - 1);
            const bool phraseEnd = adjusted.phraseEndBar || (adjusted.barInPhrase >= adjusted.phraseBars - 1);
            const bool isCadence = (adjusted.cadence01 >= 0.4);
            const bool isHighEnergy = (adjusted.energy >= 0.6);
            const bool isMedEnergy = (adjusted.energy >= 0.35 && adjusted.energy < 0.6);
            
            // Beat 1 of a bar is structurally important
            const bool isDownbeat = (adjusted.beatInBar == 0);
            // Beat 3 is secondary strong beat
            const bool isSecondaryDownbeat = (adjusted.beatInBar == 2);
            // Beats 2 and 4 are weak beats
            const bool isWeakBeat = (adjusted.beatInBar == 1 || adjusted.beatInBar == 3);
            
            // Use minimal variation from hash (just for small details, not pattern choice)
            const bool slightVariation = ((lhHash / 7) % 3) == 0;
            
            // ================================================================
            // PROFESSIONAL JAZZ COMPING APPROACH:
            // 1. Play on the chord change (usually on the beat, rarely lay back)
            // 2. Add 1-2 tasteful additional hits per chord (not every chord)
            // 3. Lay back is RARE (10-15%), used for special moments
            // 4. Additional hits use same or slightly varied voicing
            // ================================================================
            
            // Lay back is RARE - only ~12% of the time, and only on specific beats
            const bool shouldLayBack = ((lhHash % 100) < 12) && !phraseStart && isDownbeat;
            
            if (adjusted.chordIsNew) {
                // ============================================================
                // CHORD CHANGE: Always play, usually on the beat
                // ============================================================
                
                // Main hit: on the beat (rarely lay back)
                lhHits.push_back({0, 0, false, shouldLayBack});
                
                // ============================================================
                // ADDITIONAL COMPING HITS (1-2 per chord, tastefully placed)
                // Classic jazz piano comp placements:
                // - "and of 1" (sub=2 on beat 1): rhythmic push
                // - Beat 3: secondary accent
                // - "and of 3" (sub=2 on beat 3): anticipates beat 4
                // - "and of 4" (sub=2 on beat 4): anticipates next bar!
                // ============================================================
                
                // Determine how many additional hits (0, 1, or 2)
                int extraHits = 0;
                if (isHighEnergy) {
                    extraHits = (lhHash % 3); // 0, 1, or 2
                } else if (isMedEnergy) {
                    extraHits = (lhHash % 3 == 0) ? 1 : 0; // ~33% chance of 1
                } else {
                    extraHits = (lhHash % 5 == 0) ? 1 : 0; // ~20% chance of 1
                }
                
                // Choose comp placement based on hash for variety
                int compPattern = (lhHash / 3) % 6;
                
                if (extraHits >= 1) {
                    bool useAltVoicing = (lhHash % 3 == 0); // ~33% use different voicing
                    
                    switch (compPattern) {
                        case 0:
                            // "and of 1" - classic rhythmic push
                            lhHits.push_back({2, -5, useAltVoicing, false});
                            break;
                        case 1:
                            // Beat 3 - secondary accent
                            // (This will be handled in the beat 3 scheduling)
                            break;
                        case 2:
                            // "and of 2" - syncopated
                            lhHits.push_back({2, -6, useAltVoicing, false});
                            break;
                        case 3:
                            // "and of 3" - anticipates beat 4
                            lhHits.push_back({2, -5, useAltVoicing, false});
                            break;
                        case 4:
                        case 5:
                            // "and of 4" - anticipates next bar (very common in jazz!)
                            lhHits.push_back({2, -4, useAltVoicing, false});
                            break;
                    }
                }
                
                if (extraHits >= 2 && isHighEnergy) {
                    // Second hit: use a DIFFERENT voicing for interest
                    bool useAltVoicing2 = true; // Always vary the second hit
                    int compPattern2 = (compPattern + 2) % 4;
                    
                    switch (compPattern2) {
                        case 0:
                            lhHits.push_back({2, -8, useAltVoicing2, false}); // "and"
                            break;
                        case 1:
                            lhHits.push_back({1, -10, useAltVoicing2, false}); // "e" 
                            break;
                        case 2:
                            lhHits.push_back({3, -7, useAltVoicing2, false}); // "a"
                            break;
                        case 3:
                            lhHits.push_back({2, -9, useAltVoicing2, false}); // "and"
                            break;
                    }
                }
                
            } else {
                // ============================================================
                // NON-CHORD-CHANGE: Supportive comps within the chord
                // These add rhythmic life without changing harmony
                // ============================================================
                
                if (isDownbeat && (lhHash % 6 == 0)) {
                    // Beat 1 (no chord change): occasional reinforcement
                    bool useAltVoicing = (lhHash % 2 == 0);
                    lhHits.push_back({0, -4, useAltVoicing, false});
                } else if (isSecondaryDownbeat) {
                    // Beat 3: Good spot for supportive comp
                    if (isMedEnergy || isHighEnergy) {
                        bool useAltVoicing = (lhHash % 3 == 0);
                        lhHits.push_back({0, -3, useAltVoicing, false});
                    }
                    // Sometimes add "and of 3" as well
                    if (isHighEnergy && slightVariation) {
                        lhHits.push_back({2, -7, true, false});
                    }
                } else if (adjusted.beatInBar == 3) {
                    // Beat 4: Classic spot for "and of 4" anticipation!
                    if (isHighEnergy || isCadence || (lhHash % 4 == 0)) {
                        bool useAltVoicing = (lhHash % 2 == 0);
                        lhHits.push_back({2, -5, useAltVoicing, false}); // "and of 4"
                    }
                } else if (adjusted.beatInBar == 1 && isHighEnergy && slightVariation) {
                    // Beat 2: Rare comp, only at high energy
                    lhHits.push_back({2, -8, true, false}); // "and of 2"
                }
            }
            
            // Safety: ensure at least one hit on chord changes
            if (adjusted.chordIsNew && lhHits.isEmpty()) {
                lhHits.push_back({0, 0, false, false});
            }
            
            // Generate notes for each LH hit
            for (const auto& hit : lhHits) {
                QVector<int> hitMidi = lhVoicing.midiNotes;
                QString hitKey = lhVoicing.ontologyKey;
                
                // Alternate voicing: create meaningful variation
                if (hit.useAltVoicing && hitMidi.size() >= 2) {
                    // Several ways to vary the voicing:
                    int variationType = (timingHash + hit.sub) % 4;
                    
                    switch (variationType) {
                        case 0:
                            // Inversion: Move lowest note up an octave
                            if (hitMidi.size() >= 2 && hitMidi[0] + 12 <= 67) {
                                hitMidi[0] += 12;
                                std::sort(hitMidi.begin(), hitMidi.end());
                            }
                            hitKey = "LH_Inversion_Up";
                            break;
                        case 1:
                            // Inversion: Move highest note down an octave
                            if (hitMidi.size() >= 2 && hitMidi.last() - 12 >= 48) {
                                hitMidi.last() -= 12;
                                std::sort(hitMidi.begin(), hitMidi.end());
                            }
                            hitKey = "LH_Inversion_Down";
                            break;
                        case 2:
                            // Lighter texture: just use the shell (3rd and 7th only)
                            // Safer than spreading which can create clusters
                            if (hitMidi.size() >= 3) {
                                // Keep only first and last (typically 3rd and 7th)
                                QVector<int> shell;
                                shell.push_back(hitMidi.first());
                                shell.push_back(hitMidi.last());
                                hitMidi = shell;
                            }
                            hitKey = "LH_Shell_Var";
                            break;
                        case 3:
                            // Drop 2: Move second-from-top note down an octave
                            if (hitMidi.size() >= 3) {
                                int idx = hitMidi.size() - 2;
                                if (hitMidi[idx] - 12 >= 48) {
                                    hitMidi[idx] -= 12;
                                    std::sort(hitMidi.begin(), hitMidi.end());
                                }
                            }
                            hitKey = "LH_Drop2";
                            break;
                    }
                }
                
                // Calculate timing using SUBDIVISIONS (not milliseconds!)
                // This ensures timing feels musical regardless of tempo
                int timingSub = hit.sub;  // Base subdivision (0=beat, 1=e, 2=and, 3=a)
                
                if (hit.layBack && timingSub == 0) {
                    // LAY BACK: Shift from beat to the "e" (1/16 note late)
                    // This is RARE and tasteful, not sloppy
                    timingSub = 1;
                }
                
                // Minimal humanization - just a few ms, not enough to be noticeable
                int timingOffsetMs = ((timingHash + hit.sub) % 11) - 5; // -5 to +5 ms only
                
                // GROOVE LOCK: Adjust timing relative to bass for ensemble cohesion
                if (adjusted.bassPlayingThisBeat) {
                    timingOffsetMs += getGrooveLockLhOffset(adjusted);
                }
                
                virtuoso::groove::GridPos lhPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                    adjusted.playbackBarIndex, adjusted.beatInBar, timingSub, 4, ts);
                lhPos = applyTimingOffset(lhPos, timingOffsetMs, adjusted.bpm, ts);
                
                // Velocity: accent first hit, softer subsequent
                // When user is active, cap velocity MUCH lower to avoid overpowering
                int lhVel = int(baseVel * mappings.velocityMod * 0.85) + hit.velDelta;
                int maxLhVel = (adjusted.userBusy || adjusted.userDensityHigh) ? 65 : 85;
                lhVel = qBound(30, lhVel, maxLhVel);
                
                // Duration: shorter for repeated hits
                double lhDurBeats = (hit.sub == 0 && !hit.useAltVoicing) ? 1.5 : 0.8;
                lhDurBeats *= mappings.durationMod;
                const virtuoso::groove::Rational lhDurWhole(qint64(lhDurBeats * 1000), 4000);
                
                for (int midi : hitMidi) {
                    virtuoso::engine::AgentIntentNote note;
                    note.agent = "Piano";
                    note.channel = midiChannel;
                    note.note = midi;
                    note.baseVelocity = lhVel;
                    note.startPos = lhPos;
                    note.durationWhole = lhDurWhole;
                    note.structural = (adjusted.chordIsNew && adjusted.beatInBar == 0 && hit.sub == 0);
                    note.chord_context = adjusted.chordText;
                    note.voicing_type = hitKey;
                    note.logic_tag = "LH";
                    
                    plan.notes.push_back(note);
                }
            }
            
            // Update LH state
            m_state.lastLhMidi = lhVoicing.midiNotes;
            m_state.lastLhWasTypeA = lhVoicing.isTypeA;
        }
    }
    
    // ==========================================================================
    // RIGHT HAND: PHRASE-PATTERN-BASED COMPING (THE CORE INNOVATION!)
    // ==========================================================================
    // 
    // Instead of deciding beat-by-beat "how many notes to play", we use 
    // PHRASE-LEVEL PATTERNS that define WHERE to play across 2-4 bars.
    // 
    // This is how real jazz pianists think:
    //   "I'll catch beat 1, lay out for a bar, hit the 'and of 3' 
    //    in bar 2, then land on beat 1 of bar 3."
    //
    // The default is REST. Only play when the pattern says so.
    // This creates musical SPACE - the hallmark of great ballad playing.
    // ==========================================================================
    
    const bool userActive = adjusted.userBusy || adjusted.userDensityHigh || adjusted.userIntensityPeak;
    
    // Reset RH motion counter on chord change
    if (chordChanged) {
        m_state.rhMotionsThisChord = 0;
        m_state.lastChordForRh = c.chord;
    }
    
    // ========================================================================
    // PHRASE PATTERN MANAGEMENT
    // At phrase start (bar 0, beat 0), select a new pattern
    // Otherwise, continue using the current pattern
    // ========================================================================
    
    // Note: newPhrase already defined above
    
    if (newPhrase || m_state.phrasePatternIndex < 0) {
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
    
    if (hasPattern) {
        const auto& pattern = patterns[m_state.phrasePatternIndex];
        
        // Calculate our position within the pattern
        const int barInPattern = adjusted.barInPhrase % pattern.bars;
        
        // Check if this position has a hit
        currentHit = getPhraseHitAt(pattern, barInPattern, adjusted.beatInBar);
        shouldPlayRh = (currentHit != nullptr);
    }
    
    // ========================================================================
    // OVERRIDE: When user is active, be MUCH more sparse
    // Only play on chord changes, and only dyads
    // ========================================================================
    if (userActive) {
        // Override pattern - only play on chord changes, and rarely
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
        
        if (userActive) {
            voicingType = RhVoicingType::Dyad;  // Simple when user is playing
        } else {
            switch (hitVoicingType) {
                case 0: voicingType = RhVoicingType::Drop2; break;
                case 1: voicingType = RhVoicingType::Triad; break;
                case 2: voicingType = RhVoicingType::Dyad; break;
                case 3: voicingType = RhVoicingType::Single; break;
            }
        }
        
        // ================================================================
        // PHRASE-PATTERN-DRIVEN VOICING GENERATION
        // Only ONE voicing per phrase hit - no beat-level loops!
        // ================================================================
        
        // Get phrase context
        const int curArcPhase = computePhraseArcPhase(adjusted);
        const bool isCadence = (adjusted.cadence01 >= 0.4);
        
        // Contextual overrides to voicing type
        if (adjusted.phraseEndBar && isCadence) {
            voicingType = RhVoicingType::Drop2;  // Full voicing for resolution
        }
        if (curArcPhase == 2 && !isCadence) {
            voicingType = RhVoicingType::Dyad;   // Breathing, lighter
        }
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
        
        // Find melody note using singing approach
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
        
        const double hitTensionLevel = c.weights.tension * 0.6 + c.energy * 0.4;
        const bool allowExtensions = hitTensionLevel > 0.3;
        
        // ================================================================
        // UPPER STRUCTURE TRIADS (Bill Evans Signature!)
        // On dominant chords with sufficient tension, use USTs for color
        // ================================================================
        const bool isDominant = (adjusted.chord.quality == music::ChordQuality::Dominant);
        const bool isMajor7 = (adjusted.chord.quality == music::ChordQuality::Major && 
                               adjusted.chord.seventh == music::SeventhQuality::Major7);
        const bool isMinor7 = (adjusted.chord.quality == music::ChordQuality::Minor);
        const bool wantsUST = (isDominant || isMajor7) && 
                              hitTensionLevel > 0.35 && 
                              !userActive &&
                              curArcPhase != 2;  // Not during resolution phase
        
        // Probability of using UST: higher tension = more likely
        const bool useUST = wantsUST && ((rhHash % 100) < int(hitTensionLevel * 70 + 15));
        
        QVector<int> rhMidiNotes;
        QString voicingName;
        bool usedUST = false;
        
        if (useUST) {
            // Get UST candidates for this chord
            const auto ustCandidates = getUpperStructureTriads(adjusted.chord);
            
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
                
                // Build the UST voicing
                const RhMelodic ustVoicing = buildUstVoicing(adjusted, ustCandidates[ustIndex]);
                
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
        // TRIAD VOICING
        // ================================================================
        else if (!usedUST && (voicingType == RhVoicingType::Triad || voicingPcs.size() < 3)) {
            rhMidiNotes.push_back(bestTarget);
            
            for (int interval : {4, 3, 5}) {
                int candidate = bestTarget - interval;
                if (candidate >= adjusted.lhHi - 5 && !wouldClashWithLh(candidate)) {
                    rhMidiNotes.push_back(candidate);
                    break;
                }
            }
            
            for (int interval : {8, 9, 7, 10}) {
                int candidate = bestTarget - interval;
                if (candidate >= adjusted.lhHi - 8 && !wouldClashWithLh(candidate)) {
                    if (rhMidiNotes.size() < 2 || candidate != rhMidiNotes.last()) {
                        rhMidiNotes.push_back(candidate);
                        break;
                    }
                }
            }
            voicingName = "piano_triad_root";
        }
        // ================================================================
        // DYAD VOICING
        // ================================================================
        else if (!usedUST && voicingType == RhVoicingType::Dyad) {
            rhMidiNotes.push_back(bestTarget);
            
            for (int interval : {4, 3, 5, 8, 9, 7, 6}) {
                int candidateMidi = bestTarget - interval;
                if (candidateMidi >= adjusted.lhHi - 5 && !wouldClashWithLh(candidateMidi)) {
                    rhMidiNotes.push_back(candidateMidi);
                    break;
                }
            }
            if (rhMidiNotes.size() < 2) {
                int fallback = bestTarget - 4;
                if (fallback >= adjusted.lhHi - 8) rhMidiNotes.push_back(fallback);
            }
            voicingName = "piano_rh_dyad_guide";
        }
        // ================================================================
        // SINGLE NOTE (only when user is playing)
        // ================================================================
        else if (!usedUST) {
            rhMidiNotes.push_back(bestTarget);
            if (!userActive) {
                int support = bestTarget - 4;
                if (support >= adjusted.lhHi - 5 && !wouldClashWithLh(support)) {
                    rhMidiNotes.push_back(support);
                }
            }
            voicingName = userActive ? "piano_rh_single_guide" : "piano_rh_dyad_guide";
        }
        
        std::sort(rhMidiNotes.begin(), rhMidiNotes.end());
        
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
            // ORNAMENTS: Grace notes, turns, appoggiaturas (~12% probability)
            // Add expressive ornaments before the main voicing on special moments
            // ================================================================
            const quint32 ornHash = virtuoso::util::StableHash::mix(rhHash, adjusted.playbackBarIndex * 41);
            if (shouldAddOrnament(adjusted, ornHash) && !rhMidiNotes.isEmpty()) {
                const int topNote = rhMidiNotes.last();  // Ornament the top (melodic) note
                const Ornament orn = generateOrnament(adjusted, topNote, ornHash);
                
                if (orn.type != OrnamentType::None && !orn.notes.isEmpty()) {
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
            
            // Add all notes of voicing
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
            
            // Update state
            m_state.lastRhTopMidi = bestTarget;
            if (bestTarget > currentTopMidi) m_state.rhMelodicDirection = 1;
            else if (bestTarget < currentTopMidi) m_state.rhMelodicDirection = -1;
            m_state.rhMotionsThisChord++;
            m_state.phrasePatternHitIndex++;
        }
        
        // Update register tracking for variety calculation - SAFETY: bounds check
        if (currentTopMidi >= 0 && currentTopMidi <= 127) {
            const_cast<JazzBalladPianoPlanner*>(this)->updateRegisterTracking(currentTopMidi);
        }
    }
    
    // Track phrase peak alternation at phrase boundaries
    // SAFETY: Validate MIDI values are in reasonable range before state updates
    if (adjusted.phraseEndBar && adjusted.beatInBar == 3) {
        const int safeMidi = qBound(0, m_state.lastRhTopMidi, 127);
        const bool wasHigh = (safeMidi > (adjusted.rhLo + adjusted.rhHi) / 2 + 3);
        const_cast<JazzBalladPianoPlanner*>(this)->m_state.lastPhraseWasHigh = wasHigh;
        
        // Update Q/A state for next phrase - validate inputs
        const int safePeak = qBound(0, m_state.currentPhrasePeakMidi, 127);
        const int safeLast = qBound(0, m_state.currentPhraseLastMidi, 127);
        const_cast<JazzBalladPianoPlanner*>(this)->updateQuestionAnswerState(
            adjusted, safePeak, safeLast
        );
    }
    
    // Track melodic peaks for Q/A phrasing - SAFETY: bounds check
    const int safeLastRhTop = qBound(0, m_state.lastRhTopMidi, 127);
    if (safeLastRhTop > 0 && safeLastRhTop > m_state.currentPhrasePeakMidi) {
        const_cast<JazzBalladPianoPlanner*>(this)->m_state.currentPhrasePeakMidi = safeLastRhTop;
    }
    if (safeLastRhTop > 0) {
        const_cast<JazzBalladPianoPlanner*>(this)->m_state.currentPhraseLastMidi = safeLastRhTop;
    }
    
    // Reset phrase tracking on new phrase
    if (newPhrase) {
        const_cast<JazzBalladPianoPlanner*>(this)->m_state.currentPhrasePeakMidi = 60;
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
    m_state.lastVoicingKey = lhVoicing.ontologyKey.isEmpty() ? "piano_rh_melodic" : lhVoicing.ontologyKey;

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
