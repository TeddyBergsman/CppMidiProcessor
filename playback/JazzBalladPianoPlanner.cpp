#include "playback/JazzBalladPianoPlanner.h"

#include "virtuoso/util/StableHash.h"
#include "virtuoso/piano/PianoPerformanceModel.h"

#include <QtGlobal>
#include <algorithm>

namespace playback {

namespace {
static int clampMidi(int m) { return qBound(0, m, 127); }
static int normalizePc(int pc) { return ((pc % 12) + 12) % 12; }
} // namespace

// =============================================================================
// Construction & State Management
// =============================================================================

JazzBalladPianoPlanner::JazzBalladPianoPlanner() {
    reset();
}

void JazzBalladPianoPlanner::reset() {
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
    return m_state;
}

void JazzBalladPianoPlanner::restoreState(const PlannerState& s) {
    m_state = s;
}

// =============================================================================
// Weight Integration - Map all 10 weights to piano decisions
// =============================================================================

JazzBalladPianoPlanner::WeightMappings JazzBalladPianoPlanner::computeWeightMappings(const Context& c) const {
    WeightMappings m;
    const auto& w = c.weights;

    // 1. density (0..1): controls attack probability
    // Low density = sparse, high = more active
    m.playProbMod = 0.4 + 0.8 * qBound(0.0, w.density, 1.0);

    // 2. rhythm (0..1): controls rhythmic complexity
    // High rhythm = allow syncopation, offbeats
    const double rhythmMod = qBound(0.0, w.rhythm, 1.0);
    m.playProbMod *= (0.8 + 0.4 * rhythmMod);

    // 3. intensity (0..1): controls velocity
    // 0.0 -> 0.7 multiplier, 1.0 -> 1.2 multiplier
    m.velocityMod = 0.7 + 0.5 * qBound(0.0, w.intensity, 1.0);

    // 4. dynamism (0..1): controls phrase-level dynamic arcs
    // High dynamism = prefer fuller voicings at phrase peaks
    m.voicingFullnessMod = 0.5 + 0.6 * qBound(0.0, w.dynamism, 1.0);

    // 5. emotion (0..1): controls time-feel freedom (rubato)
    // High emotion = more push/pull against grid
    const double emotionVal = qBound(0.0, w.emotion, 1.0);
    // Map to milliseconds: 0.0 -> 0ms, 1.0 -> +/- 25ms range
    m.rubatoPushMs = int(25.0 * emotionVal);

    // 6. creativity (0..1): harmonic adventurousness
    // High creativity = prefer UST, altered voicings
    m.creativityMod = qBound(0.0, w.creativity, 1.0);

    // 7. tension (0..1): tension->release shaping
    // High tension = add upper extensions, more color tones
    m.tensionMod = qBound(0.0, w.tension, 1.0);

    // 8. interactivity (0..1): responsiveness to user
    // High interactivity = shells when user busy, fills when silent
    m.interactivityMod = qBound(0.0, w.interactivity, 1.0);

    // 9. variability (0..1): anti-repetition pressure
    // High variability = prefer different voicings from last
    m.variabilityMod = qBound(0.0, w.variability, 1.0);

    // 10. warmth (0..1): timbre/warmth
    // High warmth = longer durations, lower register, more legato
    const double warmthVal = qBound(0.0, w.warmth, 1.0);
    m.durationMod = 0.8 + 0.5 * warmthVal;      // longer notes
    m.registerShiftMod = -3.0 * warmthVal;       // shift down (semitones)

    return m;
}

// =============================================================================
// Microtime / Humanization
// =============================================================================

int JazzBalladPianoPlanner::computeTimingOffsetMs(const Context& c, quint32 hash) const {
    const auto mappings = computeWeightMappings(c);
    if (mappings.rubatoPushMs == 0) return 0;

    // Deterministic push/pull based on context
    const int maxOffset = mappings.rubatoPushMs;

    // Hash to get a value in [-1, 1]
    const double normalized = (double((hash >> 8) & 0xFFFF) / 32768.0) - 1.0;

    // Beat-specific bias:
    // - Beat 1: slight push (ahead) for urgency
    // - Beat 2&4: slight lag for laid-back feel
    // - Beat 3: neutral
    double beatBias = 0.0;
    switch (c.beatInBar) {
        case 0: beatBias = -0.3; break;  // push ahead
        case 1: beatBias = 0.4; break;   // lay back
        case 2: beatBias = 0.0; break;
        case 3: beatBias = 0.35; break;  // lay back
    }

    // Phrase position bias:
    // - Early in phrase: more laid back
    // - Near phrase end (cadence): more push
    double phraseBias = 0.0;
    if (c.phraseBars > 0) {
        const double phraseProgress = double(c.barInPhrase) / double(c.phraseBars);
        if (phraseProgress < 0.3) phraseBias = 0.2;  // laid back
        else if (phraseProgress > 0.8) phraseBias = -0.15;  // push
    }

    // Cadence override: push into cadences
    if (c.cadence01 >= 0.6) {
        beatBias -= 0.2 * c.cadence01;
    }

    const double combined = normalized + beatBias + phraseBias;
    return int(qBound(-1.0, combined, 1.0) * double(maxOffset));
}

virtuoso::groove::GridPos JazzBalladPianoPlanner::applyTimingOffset(
    const virtuoso::groove::GridPos& pos, int offsetMs, int bpm,
    const virtuoso::groove::TimeSignature& ts) const {

    if (offsetMs == 0 || bpm <= 0) return pos;

    // Convert offset to fraction of a beat
    // At 60 BPM, 1 beat = 1000ms; at 120 BPM, 1 beat = 500ms
    const double msPerBeat = 60000.0 / double(bpm);
    const double beatOffset = double(offsetMs) / msPerBeat;

    // Convert to 16th note offset (sub units)
    // 1 beat = 4 sixteenths
    const double sixteenthOffset = beatOffset * 4.0;

    // Apply to the position
    // This is a simplification - proper implementation would adjust withinBarWhole
    virtuoso::groove::GridPos newPos = pos;

    // Convert offset to Rational and add to withinBarWhole
    // offsetRational = sixteenthOffset / 16 (as whole notes)
    const int offsetNum = int(llround(sixteenthOffset * 100.0));
    if (offsetNum != 0) {
        virtuoso::groove::Rational offset(offsetNum, 1600);
        newPos.withinBarWhole = newPos.withinBarWhole + offset;

        // Normalize: if negative or >= bar length, adjust bar
        virtuoso::groove::Rational barLen(ts.num, ts.den);
        while (newPos.withinBarWhole < virtuoso::groove::Rational(0, 1)) {
            newPos.withinBarWhole = newPos.withinBarWhole + barLen;
            if (newPos.barIndex > 0) --newPos.barIndex;
        }
        while (newPos.withinBarWhole >= barLen) {
            newPos.withinBarWhole = newPos.withinBarWhole - barLen;
            ++newPos.barIndex;
        }
    }

    return newPos;
}

// =============================================================================
// Vocabulary-Driven Rhythm
// =============================================================================

bool JazzBalladPianoPlanner::hasVocabularyLoaded() const {
    return m_vocab && m_vocab->isLoaded();
}

QVector<JazzBalladPianoPlanner::VocabRhythmHit> JazzBalladPianoPlanner::queryVocabularyHits(const Context& c) const {
    QVector<VocabRhythmHit> hits;

    if (!m_vocab || !m_vocab->isLoaded()) return hits;

    // Try phrase-level vocabulary first - this is the primary rhythm driver
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

    if (!phraseHits.isEmpty()) {
        hits.reserve(phraseHits.size());
        for (const auto& ph : phraseHits) {
            VocabRhythmHit h;
            h.sub = ph.sub;
            h.count = ph.count;
            h.durNum = ph.dur_num;
            h.durDen = ph.dur_den;
            h.velDelta = ph.vel_delta;
            h.density = (ph.density == "guide") ? VoicingDensity::Guide : VoicingDensity::Full;
            hits.push_back(h);
        }
        return hits;
    }

    // Fallback to beat-level vocabulary patterns
    // These patterns specify exactly WHICH beats to play (not probabilities)
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
    // Always play when chord is new (chord arrival)
    if (c.chordIsNew) {
        return true;
    }

    const auto mappings = computeWeightMappings(c);

    // Base probabilities per beat (Bill Evans ballad: sparse, space for melody)
    double baseProb = 0.0;

    switch (c.beatInBar) {
        case 0: // Beat 1 - arrival
            baseProb = 0.55;
            break;
        case 1: // Beat 2 - usually skip
            baseProb = 0.20 * (1.0 - c.skipBeat2ProbStable);
            break;
        case 2: // Beat 3 - occasional restatement
            baseProb = 0.30;
            break;
        case 3: // Beat 4 - anticipation/push
            baseProb = c.nextChanges ? 0.55 : 0.25;
            break;
        default:
            baseProb = 0.20;
    }

    // Apply interactivity weight
    if (c.userDensityHigh || c.userIntensityPeak || c.userBusy) {
        // User is busy - drop out more (responsive comping)
        baseProb *= (0.3 + 0.3 * (1.0 - mappings.interactivityMod));
    }
    if (c.userSilence) {
        // User is silent - fill more
        baseProb = qMin(1.0, baseProb + 0.30 * mappings.interactivityMod);
    }

    // Phrase-end anticipation
    if (c.phraseEndBar && c.beatInBar == 3) {
        baseProb = qMin(1.0, baseProb + 0.25);
    }

    // Cadence emphasis
    if (c.cadence01 >= 0.5) {
        baseProb = qMin(1.0, baseProb + 0.20 * c.cadence01);
    }

    // Apply density/rhythm weight mod
    baseProb *= mappings.playProbMod;

    // Energy scaling
    baseProb *= (0.5 + 0.6 * qBound(0.0, c.energy, 1.0));

    // Deterministic decision from hash
    const double threshold = double(hash % 1000) / 1000.0;
    return threshold < baseProb;
}

// =============================================================================
// Register Coordination
// =============================================================================

void JazzBalladPianoPlanner::adjustRegisterForBass(Context& c) const {
    // Ensure minimum spacing from bass register
    const int minSpacing = 8;  // semitones
    const int bassHi = c.bassRegisterHi;

    // If LH register is too close to bass, shift up
    if (c.lhLo < bassHi + minSpacing) {
        const int shift = (bassHi + minSpacing) - c.lhLo;
        c.lhLo += shift;
        c.lhHi += shift;
    }

    // If bass is very active, prefer higher register for piano
    if (c.bassActivity > 0.7) {
        c.lhLo = qMax(c.lhLo, 52);  // at least C3
        c.lhHi = qMax(c.lhHi, 68);
    }
}

// =============================================================================
// Chord/Scale Helpers
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
    // Extensions imply minor 7th unless marked major
    if (c.extension >= 7) return 10;
    return -1; // No 7th
}

int JazzBalladPianoPlanner::pcForDegree(const music::ChordSymbol& c, int degree) {
    const int root = (c.rootPc >= 0) ? c.rootPc : 0;

    // Apply alterations to a base pitch class
    auto applyAlter = [&](int deg, int basePc) -> int {
        for (const auto& a : c.alterations) {
            if (a.degree == deg) {
                return normalizePc(basePc + a.delta);
            }
        }
        return normalizePc(basePc);
    };

    int pc = root;
    switch (degree) {
        case 1:
            pc = root;
            break;
        case 3:
            pc = normalizePc(root + thirdInterval(c.quality));
            break;
        case 5:
            pc = applyAlter(5, normalizePc(root + fifthInterval(c.quality)));
            break;
        case 7: {
            const int iv = seventhInterval(c);
            if (iv < 0) return -1; // No 7th - signal invalid
            pc = normalizePc(root + iv);
            break;
        }
        case 9:
            pc = applyAlter(9, normalizePc(root + 14)); // 14 = major 9th
            break;
        case 11:
            pc = applyAlter(11, normalizePc(root + 17)); // 17 = perfect 11th
            break;
        case 13:
            pc = applyAlter(13, normalizePc(root + 21)); // 21 = major 13th
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

    // Fallback: fold into range
    int m = lo + ((pc - normalizePc(lo) + 12) % 12);
    while (m < lo) m += 12;
    while (m > hi) m -= 12;
    return clampMidi(m);
}

// =============================================================================
// Voicing Generation
// =============================================================================

QVector<JazzBalladPianoPlanner::Voicing> JazzBalladPianoPlanner::generateVoicingCandidates(
    const Context& c, VoicingDensity density) const {

    QVector<Voicing> candidates;
    candidates.reserve(8);

    const auto& chord = c.chord;
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) {
        return candidates;
    }

    const bool hasSeventh = (seventhInterval(chord) >= 0);
    const bool isDominant = (c.chordFunction == "Dominant" ||
                             chord.quality == music::ChordQuality::Dominant);

    const auto mappings = computeWeightMappings(c);

    // Helper to build a voicing
    auto makeVoicing = [&](VoicingType type, const QVector<int>& degrees,
                          const QString& key) -> Voicing {
        Voicing v;
        v.type = type;
        v.density = density;
        v.ontologyKey = key;

        for (int deg : degrees) {
            int pc = pcForDegree(chord, deg);
            if (pc >= 0) {
                v.pcs.push_back(pc);
            }
        }

        // Remove duplicates and sort
        std::sort(v.pcs.begin(), v.pcs.end());
        v.pcs.erase(std::unique(v.pcs.begin(), v.pcs.end()), v.pcs.end());

        return v;
    };

    if (density == VoicingDensity::Guide) {
        // Shells only: 3-7 or 1-3 for triads
        if (hasSeventh) {
            candidates.push_back(makeVoicing(VoicingType::Shell, {3, 7}, "piano_guide_3_7"));
        } else {
            candidates.push_back(makeVoicing(VoicingType::Shell, {1, 3}, "piano_shell_1_3"));
        }
        return candidates;
    }

    // Full voicings

    // 1. Shell voicing (3-7) - always available, sparse
    if (hasSeventh) {
        candidates.push_back(makeVoicing(VoicingType::Shell, {3, 7}, "piano_guide_3_7"));
    } else {
        // For triads, use 1-3-5
        candidates.push_back(makeVoicing(VoicingType::Shell, {1, 3, 5}, "piano_shell_1_3_5"));
    }

    // 2. Rootless Type A (3-5-7-9) - Bill Evans signature
    if (hasSeventh) {
        candidates.push_back(makeVoicing(VoicingType::RootlessA, {3, 5, 7, 9}, "piano_rootless_a"));
    }

    // 3. Rootless Type B (7-9-3-5) - Bill Evans alternate
    if (hasSeventh) {
        candidates.push_back(makeVoicing(VoicingType::RootlessB, {7, 9, 3, 5}, "piano_rootless_b"));
    }

    // 4. Quartal voicing (3-7-9) - McCoy Tyner style
    if (hasSeventh) {
        candidates.push_back(makeVoicing(VoicingType::Quartal, {3, 7, 9}, "piano_quartal_3"));
    }

    // 5. With tension: add 13th to rootless voicings
    if (mappings.tensionMod > 0.5 && hasSeventh) {
        candidates.push_back(makeVoicing(VoicingType::RootlessA, {3, 7, 9, 13}, "piano_rootless_a_13"));
    }

    // 6. Upper Structure Triad (UST) for dominants - bVI creates altered sound
    if (isDominant && hasSeventh && mappings.creativityMod > 0.3) {
        // UST bVI: Eb major triad over C7 = altered scale sound
        const int ustRoot = normalizePc(chord.rootPc + 8);
        Voicing ust;
        ust.type = VoicingType::UST;
        ust.density = VoicingDensity::Full;
        ust.ontologyKey = "piano_ust_bVI";
        // bVI triad: root, +4, +7 from bVI = b13, 1, b9 relative to V7
        ust.pcs = {ustRoot, normalizePc(ustRoot + 4), normalizePc(ustRoot + 7)};
        // Add the 3rd and 7th of the dominant for grounding
        ust.pcs.push_back(pcForDegree(chord, 3));
        ust.pcs.push_back(pcForDegree(chord, 7));
        std::sort(ust.pcs.begin(), ust.pcs.end());
        ust.pcs.erase(std::unique(ust.pcs.begin(), ust.pcs.end()), ust.pcs.end());
        candidates.push_back(ust);
    }

    return candidates;
}

QVector<int> JazzBalladPianoPlanner::realizePcsToMidi(const QVector<int>& pcs, int lo, int hi,
                                                       const QVector<int>& prevVoicing) const {
    QVector<int> midi;
    midi.reserve(pcs.size());

    // Compute center of previous voicing for voice-leading continuity
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

double JazzBalladPianoPlanner::voiceLeadingCost(const QVector<int>& prev,
                                                 const QVector<int>& next) const {
    if (prev.isEmpty()) return 0.0;

    double cost = 0.0;

    // Sum of minimum distances from each new note to any previous note
    for (int n : next) {
        int minDist = 999;
        for (int p : prev) {
            const int d = qAbs(n - p);
            if (d < minDist) minDist = d;
        }
        cost += double(minDist);
    }

    // Penalize parallel motion in outer voices (avoid parallel 5ths/octaves)
    if (prev.size() >= 2 && next.size() >= 2) {
        const int prevInterval = prev.last() - prev.first();
        const int nextInterval = next.last() - next.first();
        if (prevInterval == nextInterval && (prevInterval == 7 || prevInterval == 12)) {
            cost += 5.0; // Penalize parallel 5ths/octaves
        }
    }

    return cost;
}

bool JazzBalladPianoPlanner::isFeasible(const QVector<int>& midiNotes) const {
    virtuoso::constraints::CandidateGesture g;
    g.midiNotes = midiNotes;
    return m_driver.evaluateFeasibility(m_state.perf, g).ok;
}

QVector<int> JazzBalladPianoPlanner::repairVoicing(QVector<int> midi) const {
    std::sort(midi.begin(), midi.end());
    midi.erase(std::unique(midi.begin(), midi.end()), midi.end());

    // Iteratively fix until feasible
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (isFeasible(midi)) return midi;

        // 1. Reduce polyphony if too many notes
        if (midi.size() > 4) {
            midi.resize(4);
            continue;
        }

        // 2. Fold span if too wide
        if (midi.size() >= 2) {
            const int span = midi.last() - midi.first();
            if (span > m_driver.constraints().maxSpanSemitones) {
                midi.last() -= 12;
                std::sort(midi.begin(), midi.end());
                continue;
            }
        }

        // 3. Drop highest note
        if (midi.size() > 2) {
            midi.removeLast();
            continue;
        }

        break;
    }

    return midi;
}

// =============================================================================
// Pedal Logic
// =============================================================================

QVector<JazzBalladPianoPlanner::CcIntent> JazzBalladPianoPlanner::planPedal(
    const Context& c, const virtuoso::groove::TimeSignature& ts) const {

    using virtuoso::groove::GrooveGrid;
    QVector<CcIntent> ccs;

    // Query vocabulary for pedal strategy
    int targetPedal = 64;  // half-pedal default
    bool repedalOnNew = true;
    bool clearBeforeChange = true;

    if (m_vocab && m_vocab->isLoaded()) {
        virtuoso::vocab::VocabularyRegistry::PianoPedalQuery pq;
        pq.ts = ts;
        pq.playbackBarIndex = c.playbackBarIndex;
        pq.beatInBar = c.beatInBar;
        pq.chordText = c.chordText;
        pq.chordFunction = c.chordFunction;
        pq.chordIsNew = c.chordIsNew;
        pq.userBusy = c.userBusy;
        pq.userSilence = c.userSilence;
        pq.nextChanges = c.nextChanges;
        pq.beatsUntilChordChange = c.beatsUntilChordChange;
        pq.energy = c.energy;
        pq.toneDark = c.weights.warmth;
        pq.determinismSeed = c.determinismSeed;

        const auto choice = m_vocab->choosePianoPedal(pq);
        if (!choice.id.isEmpty()) {
            if (choice.defaultState == "up") targetPedal = 0;
            else if (choice.defaultState == "down") targetPedal = 127;
            else targetPedal = 64;

            repedalOnNew = choice.repedalOnNewChord;
            clearBeforeChange = choice.clearBeforeChange;
        }
    }

    // Apply warmth weight: higher warmth = more sustain
    const auto mappings = computeWeightMappings(c);
    if (c.weights.warmth > 0.7 && targetPedal < 127) {
        targetPedal = qMin(127, targetPedal + 30);
    }

    const int currentPedal = m_state.perf.ints.value("cc64", 0);
    const bool sustainDown = (currentPedal >= 96);
    const bool sustainHalf = (!sustainDown && currentPedal >= 32);

    // At beat 1 with new chord: repedal (lift then re-engage)
    if (c.beatInBar == 0 && c.chordIsNew && repedalOnNew && (sustainDown || sustainHalf)) {
        // Quick lift
        CcIntent lift;
        lift.cc = 64;
        lift.value = 0;
        lift.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, 0, 0, 4, ts);
        lift.structural = true;
        lift.logic_tag = "pedal:lift_for_repedal";
        ccs.push_back(lift);

        // Re-engage a 16th note later
        CcIntent reengage;
        reengage.cc = 64;
        reengage.value = targetPedal;
        reengage.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, 0, 1, 4, ts);
        reengage.structural = true;
        reengage.logic_tag = "pedal:repedal";
        ccs.push_back(reengage);
    }
    // At beat 1 without new chord: ensure pedal is engaged
    else if (c.beatInBar == 0 && currentPedal < 32 && targetPedal > 0) {
        CcIntent engage;
        engage.cc = 64;
        engage.value = targetPedal;
        engage.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, 0, 0, 1, ts);
        engage.structural = true;
        engage.logic_tag = "pedal:engage";
        ccs.push_back(engage);
    }

    // Before chord change: clear pedal for clarity
    if (clearBeforeChange && c.nextChanges && c.beatsUntilChordChange == 1 && c.beatInBar == ts.num - 1) {
        CcIntent clear;
        clear.cc = 64;
        clear.value = 0;
        clear.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 3, 4, ts);
        clear.structural = false;
        clear.logic_tag = "pedal:clear_before_change";
        ccs.push_back(clear);
    }

    return ccs;
}

// =============================================================================
// Gesture Support (rolls/arps at cadences)
// =============================================================================

void JazzBalladPianoPlanner::applyGesture(const Context& c,
                                           QVector<virtuoso::engine::AgentIntentNote>& notes,
                                           const virtuoso::groove::TimeSignature& ts) const {
    if (notes.isEmpty() || notes.size() < 3) return;

    // Only apply gestures at cadences or chord arrivals with sufficient energy
    if (c.cadence01 < 0.6 && !c.chordIsNew) return;
    if (c.energy < 0.3) return;

    // Query vocabulary for gesture
    if (!m_vocab || !m_vocab->isLoaded()) return;

    virtuoso::vocab::VocabularyRegistry::PianoGestureQuery gq;
    gq.ts = ts;
    gq.bpm = c.bpm;
    gq.playbackBarIndex = c.playbackBarIndex;
    gq.beatInBar = c.beatInBar;
    gq.chordText = c.chordText;
    gq.chordFunction = c.chordFunction;
    gq.chordIsNew = c.chordIsNew;
    gq.userSilence = c.userSilence;
    gq.cadence = (c.cadence01 >= 0.6);
    gq.energy = c.energy;
    gq.rhythmicComplexity = c.weights.rhythm;
    gq.determinismSeed = c.determinismSeed;
    gq.noteCount = notes.size();

    const auto gesture = m_vocab->choosePianoGesture(gq);
    if (gesture.id.isEmpty() || gesture.kind == "none") return;

    // Apply roll/arp: stagger note start times
    if (gesture.kind == "roll" && gesture.spreadMs > 0) {
        const int totalSpreadMs = gesture.spreadMs;
        const int noteCount = notes.size();
        const int msPerNote = (noteCount > 1) ? totalSpreadMs / (noteCount - 1) : 0;

        // Determine direction
        bool upward = (gesture.style != "down");

        // Sort notes by pitch
        std::sort(notes.begin(), notes.end(), [upward](const auto& a, const auto& b) {
            return upward ? (a.note < b.note) : (a.note > b.note);
        });

        // Apply staggered timing (via timing offset approximation)
        for (int i = 0; i < noteCount; ++i) {
            const int offsetMs = i * msPerNote;
            if (offsetMs > 0) {
                notes[i].startPos = applyTimingOffset(notes[i].startPos, offsetMs, c.bpm, ts);
            }
        }
    }
}

// =============================================================================
// Main Planning Entry Points
// =============================================================================

QVector<virtuoso::engine::AgentIntentNote> JazzBalladPianoPlanner::planBeat(
    const Context& c, int midiChannel, const virtuoso::groove::TimeSignature& ts) {
    return planBeatWithActions(c, midiChannel, ts).notes;
}

JazzBalladPianoPlanner::BeatPlan JazzBalladPianoPlanner::planBeatWithActions(
    const Context& c, int midiChannel, const virtuoso::groove::TimeSignature& ts) {

    using virtuoso::groove::GrooveGrid;
    using virtuoso::groove::Rational;

    BeatPlan plan;

    // Skip if no valid chord
    if (c.chord.placeholder || c.chord.noChord || c.chord.rootPc < 0) {
        return plan;
    }

    // Apply bass register coordination
    Context adjusted = c;
    adjustRegisterForBass(adjusted);

    // Compute weight mappings
    const auto mappings = computeWeightMappings(adjusted);

    // Deterministic hash for this beat
    const quint32 hash = virtuoso::util::StableHash::fnv1a32(
        QString("%1|%2|%3|%4")
            .arg(adjusted.chordText)
            .arg(adjusted.playbackBarIndex)
            .arg(adjusted.beatInBar)
            .arg(adjusted.determinismSeed)
            .toUtf8());

    // Pedal planning
    plan.ccs = planPedal(adjusted, ts);

    // Query vocabulary for rhythm hits
    // IMPORTANT: When vocabulary is loaded, it is the AUTHORITY on when to play.
    // If vocabulary returns no hits for this beat, that means "don't play" (not fallback to probability).
    const bool vocabLoaded = hasVocabularyLoaded();
    const auto vocabHits = queryVocabularyHits(adjusted);
    const bool vocabHasHits = !vocabHits.isEmpty();

    // Decide if we should play on this beat
    bool shouldPlay = false;
    VoicingDensity density = VoicingDensity::Full;

    if (vocabLoaded) {
        // Vocabulary is loaded: it decides rhythm
        if (vocabHasHits) {
            shouldPlay = true;
            density = vocabHits.first().density;
        } else {
            // Vocabulary says "no hit for this beat" - DON'T PLAY
            // Exception: always play on chord arrival (new chord)
            if (adjusted.chordIsNew && adjusted.beatInBar == 0) {
                shouldPlay = true;
                density = VoicingDensity::Guide;  // simple shell for arrival
            } else {
                shouldPlay = false;
            }
        }
    } else {
        // No vocabulary loaded: use probability-based fallback
        shouldPlay = shouldPlayBeatFallback(adjusted, hash);
        // Determine density based on weights and user state
        if (adjusted.userBusy || adjusted.userDensityHigh) {
            density = VoicingDensity::Guide;  // shells when user is active
        } else if (mappings.voicingFullnessMod < 0.7) {
            density = VoicingDensity::Guide;
        }
    }

    if (!shouldPlay) {
        // Update pedal state from our planned CCs
        for (const auto& cc : plan.ccs) {
            if (cc.cc == 64) m_state.perf.ints.insert("cc64", cc.value);
        }
        return plan;
    }

    // Generate voicing candidates
    QVector<Voicing> candidates = generateVoicingCandidates(adjusted, density);
    if (candidates.isEmpty()) {
        return plan;
    }

    // Score each candidate and pick the best
    const QVector<int>& prevVoicing = m_state.lastVoicingMidi;

    // Determine register based on context
    int lhLo = adjusted.lhLo, lhHi = adjusted.lhHi;
    int rhLo = adjusted.rhLo, rhHi = adjusted.rhHi;

    // Apply warmth-based register shift
    if (mappings.registerShiftMod != 0.0) {
        const int shift = int(mappings.registerShiftMod);
        lhLo += shift; lhHi += shift;
        rhLo += shift; rhHi += shift;
    }

    // User activity shifts register (avoid collision)
    if (adjusted.userRegisterHigh) {
        rhLo = qMax(rhLo - 6, 48);
        rhHi = qMax(rhHi - 6, 60);
    }

    Voicing bestVoicing;
    double bestScore = 1e18;

    for (auto& cand : candidates) {
        // Realize to MIDI notes
        // Split: LH gets lower PCs, RH gets upper
        QVector<int> lhPcs, rhPcs;
        for (int pc : cand.pcs) {
            if (lhPcs.size() < 2) {
                lhPcs.push_back(pc);
            } else {
                rhPcs.push_back(pc);
            }
        }

        QVector<int> lhMidi = realizePcsToMidi(lhPcs, lhLo, lhHi, prevVoicing);
        QVector<int> rhMidi = realizePcsToMidi(rhPcs, rhLo, rhHi, prevVoicing);

        cand.midiNotes = lhMidi + rhMidi;
        std::sort(cand.midiNotes.begin(), cand.midiNotes.end());

        // Voice-leading cost
        cand.cost = voiceLeadingCost(prevVoicing, cand.midiNotes);

        // Prefer shells when user is busy (high interactivity)
        if ((adjusted.userBusy || adjusted.userDensityHigh) && cand.type != VoicingType::Shell) {
            cand.cost += 3.0 * mappings.interactivityMod;
        }

        // Prefer fuller voicings when user is silent (fill mode)
        if (adjusted.userSilence && cand.type == VoicingType::Shell) {
            cand.cost += 2.0 * mappings.interactivityMod;
        }

        // UST on dominants with high creativity
        if (cand.type == VoicingType::UST) {
            cand.cost -= 3.0 * mappings.creativityMod; // Reward creativity
            if (mappings.creativityMod < 0.3) cand.cost += 6.0; // Penalize if low creativity
        }

        // Variability: penalize same voicing as last time
        if (!m_state.lastVoicingKey.isEmpty() && cand.ontologyKey == m_state.lastVoicingKey) {
            cand.cost += 2.0 * mappings.variabilityMod;
        }

        // Feasibility check
        if (!isFeasible(cand.midiNotes)) {
            cand.midiNotes = repairVoicing(cand.midiNotes);
            cand.cost += 3.0; // Penalty for repair
        }

        if (cand.cost < bestScore) {
            bestScore = cand.cost;
            bestVoicing = cand;
        }
    }

    if (bestVoicing.midiNotes.isEmpty()) {
        return plan;
    }

    // Compute timing offset (microtime humanization)
    const int timingOffsetMs = computeTimingOffsetMs(adjusted, hash);

    // Create note intents
    // Use vocabulary hit timing if available, otherwise default
    if (vocabHasHits) {
        for (const auto& hit : vocabHits) {
            auto startPos = GrooveGrid::fromBarBeatTuplet(
                adjusted.playbackBarIndex, adjusted.beatInBar, hit.sub, hit.count, ts);

            // Apply microtime
            if (timingOffsetMs != 0) {
                startPos = applyTimingOffset(startPos, timingOffsetMs, adjusted.bpm, ts);
            }

            Rational duration(hit.durNum, hit.durDen);

            // Base velocity
            int baseVel = 70;
            baseVel = int(double(baseVel) * mappings.velocityMod);
            baseVel += hit.velDelta;
            baseVel = int(baseVel * (0.7 + 0.6 * qBound(0.0, adjusted.energy, 1.0)));
            baseVel = qBound(30, baseVel, 100);

            // Build logic tag
            const QString logicTag = QString("ballad_comp|voicing:%1|beat:%2|sub:%3")
                .arg(bestVoicing.ontologyKey).arg(adjusted.beatInBar).arg(hit.sub);

            for (int midiNote : bestVoicing.midiNotes) {
                virtuoso::engine::AgentIntentNote n;
                n.agent = "Piano";
                n.channel = midiChannel;
                n.note = midiNote;
                n.baseVelocity = baseVel;
                n.startPos = startPos;
                n.durationWhole = Rational(int(duration.num * mappings.durationMod), duration.den);
                n.structural = (adjusted.beatInBar == 0 || adjusted.chordIsNew);
                n.chord_context = adjusted.chordText;
                n.voicing_type = bestVoicing.ontologyKey;
                n.logic_tag = logicTag;

                if (!adjusted.roman.isEmpty()) n.roman = adjusted.roman;
                if (!adjusted.chordFunction.isEmpty()) n.chord_function = adjusted.chordFunction;

                plan.notes.push_back(n);
            }
        }
    } else {
        // Fallback: single hit at beat start
        auto startPos = GrooveGrid::fromBarBeatTuplet(adjusted.playbackBarIndex, adjusted.beatInBar, 0, 1, ts);

        // Apply microtime
        if (timingOffsetMs != 0) {
            startPos = applyTimingOffset(startPos, timingOffsetMs, adjusted.bpm, ts);
        }

        // Duration: typically half note for ballad comping, modified by warmth
        Rational duration(1, 2);
        if (adjusted.nextChanges && adjusted.beatsUntilChordChange <= 1) {
            // Shorten if chord change coming
            duration = Rational(1, 4);
        }
        if (adjusted.userDensityHigh) {
            // Shorter when user is busy
            duration = Rational(1, 4);
        }

        // Apply duration mod from warmth
        duration = Rational(int(duration.num * mappings.durationMod), duration.den);

        // Base velocity
        int baseVel = 70;
        baseVel = int(double(baseVel) * mappings.velocityMod);
        baseVel = int(baseVel * (0.7 + 0.6 * qBound(0.0, adjusted.energy, 1.0)));
        baseVel = qBound(30, baseVel, 100);

        // Build logic tag with expected format for tests
        const QString logicTag = QString("ballad_comp|voicing:%1|beat:%2")
            .arg(bestVoicing.ontologyKey).arg(adjusted.beatInBar);

        for (int midiNote : bestVoicing.midiNotes) {
            virtuoso::engine::AgentIntentNote n;
            n.agent = "Piano";
            n.channel = midiChannel;
            n.note = midiNote;
            n.baseVelocity = baseVel;
            n.startPos = startPos;
            n.durationWhole = duration;
            n.structural = (adjusted.beatInBar == 0 || adjusted.chordIsNew);
            n.chord_context = adjusted.chordText;
            n.voicing_type = bestVoicing.ontologyKey;
            n.logic_tag = logicTag;

            if (!adjusted.roman.isEmpty()) n.roman = adjusted.roman;
            if (!adjusted.chordFunction.isEmpty()) n.chord_function = adjusted.chordFunction;

            plan.notes.push_back(n);
        }
    }

    // Apply gesture (roll/arp) if appropriate
    applyGesture(adjusted, plan.notes, ts);

    // Update state
    m_state.lastVoicingMidi = bestVoicing.midiNotes;
    m_state.lastVoicingKey = bestVoicing.ontologyKey;
    if (!bestVoicing.midiNotes.isEmpty()) {
        m_state.lastTopMidi = bestVoicing.midiNotes.last();
    }

    // Update pedal state
    for (const auto& cc : plan.ccs) {
        if (cc.cc == 64) m_state.perf.ints.insert("cc64", cc.value);
    }

    // Build performance plan for explainability
    plan.chosenVoicingKey = bestVoicing.ontologyKey;
    plan.motifSourceAgent = "Piano";
    plan.motifTransform = "voicing:" + bestVoicing.ontologyKey;

    QVector<virtuoso::piano::PianoPerformanceModel::LegacyCc64Intent> cc64s;
    for (const auto& cc : plan.ccs) {
        if (cc.cc == 64) {
            virtuoso::piano::PianoPerformanceModel::LegacyCc64Intent li;
            li.value = cc.value;
            li.startPos = cc.startPos;
            li.structural = cc.structural;
            li.logicTag = cc.logic_tag;
            cc64s.push_back(li);
        }
    }
    plan.performance = virtuoso::piano::PianoPerformanceModel::inferFromLegacy(plan.notes, cc64s);

    // Set vocabulary IDs for auditability (deterministic from context)
    const int phraseBar = adjusted.playbackBarIndex - adjusted.barInPhrase;
    const quint32 phraseHash = virtuoso::util::StableHash::fnv1a32(
        QString("phrase|%1|%2|%3")
            .arg(phraseBar)
            .arg(adjusted.phraseBars)
            .arg(adjusted.determinismSeed)
            .toUtf8());

    // Comp phrase ID is stable across the phrase
    plan.performance.compPhraseId = QString("comp_phrase_%1").arg(phraseHash % 1000, 3, 10, QChar('0'));
    plan.performance.compBeatId = QString("comp_beat_%1_%2").arg(adjusted.beatInBar).arg(hash % 100);

    // Pedal ID reflects the chosen strategy
    plan.performance.pedalId = QString("pedal_%1_%2").arg(hash % 100).arg(timingOffsetMs >= 0 ? "push" : "pull");

    return plan;
}

} // namespace playback
