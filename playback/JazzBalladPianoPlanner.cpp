#include "playback/JazzBalladPianoPlanner.h"

#include "virtuoso/util/StableHash.h"
#include "virtuoso/piano/PianoPerformanceModel.h"

#include <QtGlobal>
#include <algorithm>
#include <functional>

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
QVector<VoicingTemplate> getVoicingTemplates(bool hasSeventh, bool is6thChord) {
    QVector<VoicingTemplate> templates;

    if (hasSeventh || is6thChord) {
        // Type A: 3-5-7-9 (start from 3rd, stack upward)
        templates.push_back({"RootlessA", {3, 5, 7, 9}, 3, true});

        // Type B: 7-9-3-5 (start from 7th, 3 and 5 are inverted up)
        templates.push_back({"RootlessB", {7, 9, 3, 5}, 7, true});

        // Shell: just 3-7
        templates.push_back({"Shell_3_7", {3, 7}, 3, true});

        // Quartal: 3-7-9
        templates.push_back({"Quartal", {3, 7, 9}, 3, true});
    } else {
        // Triads
        templates.push_back({"Triad_135", {1, 3, 5}, 1, false});
        templates.push_back({"Triad_351", {3, 5, 1}, 3, false});
    }

    return templates;
}

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

    const int rubato = int(mappings.rubatoPushMs);
    if (rubato > 0) {
        const int jitter = int(hash % (2 * rubato + 1)) - rubato;
        offset += jitter;
    }

    if (c.beatInBar == 1 || c.beatInBar == 3) {
        offset += 5 + int(mappings.rubatoPushMs * 0.3);
    }

    if (c.cadence01 >= 0.7 && c.beatInBar == 3) {
        offset -= 8;
    }

    return qBound(-40, offset, 40);
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

    const bool isAlt = c.alt && (c.quality == music::ChordQuality::Dominant);
    const bool is6thChord = (c.extension == 6 && c.seventh == music::SeventhQuality::None);

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
                pc = normalizePc(root + 6);
            } else {
                pc = applyAlter(5, normalizePc(root + fifthInterval(c.quality)));
            }
            break;
        case 6:
            pc = applyAlter(6, normalizePc(root + 9));
            break;
        case 7:
            if (is6thChord) {
                pc = normalizePc(root + 9);
            } else {
                const int iv = seventhInterval(c);
                if (iv < 0) return -1;
                pc = normalizePc(root + iv);
            }
            break;
        case 9:
            if (isAlt) {
                pc = normalizePc(root + 1);
            } else {
                pc = applyAlter(9, normalizePc(root + 2));
            }
            break;
        case 11:
            if (isAlt || c.quality == music::ChordQuality::Major ||
                c.quality == music::ChordQuality::Dominant) {
                pc = applyAlter(11, normalizePc(root + 6));
            } else {
                pc = applyAlter(11, normalizePc(root + 5));
            }
            break;
        case 13:
            if (isAlt) {
                pc = normalizePc(root + 8);
            } else {
                pc = applyAlter(13, normalizePc(root + 9));
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

        if (tmpl.name.startsWith("RootlessA")) v.type = VoicingType::RootlessA;
        else if (tmpl.name.startsWith("RootlessB")) v.type = VoicingType::RootlessB;
        else if (tmpl.name.startsWith("Shell")) v.type = VoicingType::Shell;
        else if (tmpl.name.startsWith("Quartal")) v.type = VoicingType::Quartal;
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
            baseMidi = qBound(voicingFloor, baseMidi - 6, voicingCeiling - 12);
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
// Pedal Logic
// =============================================================================

QVector<JazzBalladPianoPlanner::CcIntent> JazzBalladPianoPlanner::planPedal(
    const Context& c, const virtuoso::groove::TimeSignature& ts) const {

    QVector<CcIntent> ccs;

    if (c.chordIsNew && c.beatInBar == 0) {
        CcIntent lift;
        lift.cc = 64;
        lift.value = 0;
        lift.startPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
            c.playbackBarIndex, c.beatInBar, 0, 4, ts);
        lift.structural = true;
        lift.logic_tag = "chord_change_lift";
        ccs.push_back(lift);

        CcIntent engage;
        engage.cc = 64;
        engage.value = 100;
        engage.startPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
            c.playbackBarIndex, c.beatInBar, 1, 4, ts);
        engage.structural = true;
        engage.logic_tag = "chord_change_engage";
        ccs.push_back(engage);
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
// Main Planning Function
// =============================================================================

QVector<virtuoso::engine::AgentIntentNote> JazzBalladPianoPlanner::planBeat(
    const Context& c, int midiChannel, const virtuoso::groove::TimeSignature& ts) {

    auto plan = planBeatWithActions(c, midiChannel, ts);
    return plan.notes;
}

JazzBalladPianoPlanner::BeatPlan JazzBalladPianoPlanner::planBeatWithActions(
    const Context& c, int midiChannel, const virtuoso::groove::TimeSignature& ts) {

    BeatPlan plan;

    Context adjusted = c;
    adjustRegisterForBass(adjusted);

    bool shouldPlay = false;
    QVector<VocabRhythmHit> vocabHits;
    QString phraseId;
    QString pedalId;

    if (hasVocabularyLoaded()) {
        vocabHits = queryVocabularyHits(adjusted, &phraseId);
        if (!vocabHits.isEmpty()) {
            shouldPlay = true;
        } else if (adjusted.chordIsNew && adjusted.beatInBar == 0) {
            shouldPlay = true;
            VocabRhythmHit defaultHit;
            defaultHit.sub = 0;
            defaultHit.count = 1;
            defaultHit.density = VoicingDensity::Full;
            vocabHits.push_back(defaultHit);
        }
        
        // Get pedal choice
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
    } else {
        const quint32 hash = virtuoso::util::StableHash::mix(
            adjusted.determinismSeed, adjusted.playbackBarIndex * 17 + adjusted.beatInBar);
        shouldPlay = shouldPlayBeatFallback(adjusted, hash);
        if (shouldPlay) {
            VocabRhythmHit defaultHit;
            defaultHit.sub = 0;
            defaultHit.count = 1;
            defaultHit.density = computeContextDensity(adjusted);
            vocabHits.push_back(defaultHit);
        }
    }

    if (!shouldPlay || vocabHits.isEmpty()) {
        return plan;
    }

    VoicingDensity density = computeContextDensity(adjusted);

    for (const auto& hit : vocabHits) {
        if (hit.density < density) {
            density = hit.density;
        }
    }

    QVector<Voicing> candidates = generateVoicingCandidates(adjusted, density);
    if (candidates.isEmpty()) {
        return plan;
    }

    Voicing bestVoicing = candidates.first();
    for (const auto& cand : candidates) {
        if (cand.cost < bestVoicing.cost) {
            bestVoicing = cand;
        }
    }

    // Variability: prefer different voicing from last
    if (!m_state.lastVoicingKey.isEmpty() && candidates.size() > 1) {
        if (bestVoicing.ontologyKey == m_state.lastVoicingKey) {
            for (const auto& cand : candidates) {
                if (cand.ontologyKey != m_state.lastVoicingKey) {
                    if (cand.cost < bestVoicing.cost + 3.0) {
                        bestVoicing = cand;
                        break;
                    }
                }
            }
        }
    }

    if (bestVoicing.midiNotes.isEmpty()) {
        return plan;
    }

    const quint32 timingHash = virtuoso::util::StableHash::mix(
        adjusted.determinismSeed, adjusted.playbackBarIndex * 31 + adjusted.beatInBar * 7);
    const int timingOffsetMs = computeTimingOffsetMs(adjusted, timingHash);

    const auto mappings = computeWeightMappings(adjusted);
    const int baseVel = 60 + int(20.0 * adjusted.energy);
    int velocity = int(baseVel * mappings.velocityMod);
    velocity = qBound(40, velocity, 110);

    const double durationBeats = 1.0 * mappings.durationMod;
    const virtuoso::groove::Rational durWhole(qint64(durationBeats * 1000), 4000);

    for (const auto& hit : vocabHits) {
        virtuoso::groove::GridPos pos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
            adjusted.playbackBarIndex, adjusted.beatInBar, hit.sub, 4, ts);
        pos = applyTimingOffset(pos, timingOffsetMs, adjusted.bpm, ts);

        const int noteCount = bestVoicing.midiNotes.size();
        int lhCount = 0;

        if (noteCount <= 2) lhCount = 1;
        else if (noteCount <= 3) lhCount = 1;
        else lhCount = 2;

        for (int i = 0; i < noteCount; ++i) {
            virtuoso::engine::AgentIntentNote note;
            note.agent = "Piano";
            note.channel = midiChannel;
            note.note = bestVoicing.midiNotes[i];
            note.baseVelocity = qBound(40, velocity + hit.velDelta, 110);
            note.startPos = pos;
            note.durationWhole = durWhole;
            note.structural = (adjusted.chordIsNew && adjusted.beatInBar == 0);
            note.chord_context = adjusted.chordText;
            note.voicing_type = bestVoicing.ontologyKey;
            note.logic_tag = (i < lhCount) ? "LH" : "RH";

            plan.notes.push_back(note);
        }
    }

    m_state.lastVoicingMidi = bestVoicing.midiNotes;
    m_state.lastTopMidi = bestVoicing.topNoteMidi;
    m_state.lastVoicingKey = bestVoicing.ontologyKey;
    
    // Update phrase state
    if (!phraseId.isEmpty()) {
        m_state.currentPhraseId = phraseId;
    }

    plan.chosenVoicingKey = bestVoicing.ontologyKey;
    plan.ccs = planPedal(adjusted, ts);

    virtuoso::piano::PianoPerformancePlan perf;
    perf.compPhraseId = phraseId.isEmpty() ? m_state.currentPhraseId : phraseId;
    perf.pedalId = pedalId;
    perf.gestureProfile = bestVoicing.ontologyKey;
    plan.performance = perf;

    return plan;
}

} // namespace playback
