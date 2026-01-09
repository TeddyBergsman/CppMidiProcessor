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
// NEW: Bill Evans Style LH/RH Separation
// =============================================================================

// Bill Evans convention:
// - Type A (3-5-7-9): Use when root is C, Db, D, Eb, E, F (lower half)
// - Type B (7-9-3-5): Use when root is Gb, G, Ab, A, Bb, B (upper half)
// This creates smooth voice-leading as chords progress around the circle.
JazzBalladPianoPlanner::LhVoicing JazzBalladPianoPlanner::generateLhRootlessVoicing(const Context& c) const {
    LhVoicing lh;
    const auto& chord = c.chord;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return lh;
    
    const bool hasSeventh = (seventhInterval(chord) >= 0);
    const bool is6thChord = (chord.extension == 6 && chord.seventh == music::SeventhQuality::None);
    
    // Determine Type A or B based on root and voice-leading from previous
    // Root in [0,5] (C-F) -> Type A; Root in [6,11] (F#-B) -> Type B
    // BUT: alternate if staying on same type creates large motion
    bool preferTypeA = (chord.rootPc <= 5);
    
    // Voice-leading consideration: if last was Type A and this root is close,
    // prefer opposite type for smoother motion
    if (!m_state.lastLhMidi.isEmpty()) {
        // Simple heuristic: alternate when possible for variety
        preferTypeA = !m_state.lastLhWasTypeA;
    }
    
    // Build the voicing
    QVector<int> degrees;
    int baseMidi;
    
    if (preferTypeA) {
        // Type A: 3-5-7-9 (stacked from 3rd)
        degrees = {3, 5, 7, 9};
        lh.isTypeA = true;
        lh.ontologyKey = "LH_RootlessA";
        baseMidi = 52; // Start around E3
    } else {
        // Type B: 7-9-3-5 (stacked from 7th)
        degrees = {7, 9, 3, 5};
        lh.isTypeA = false;
        lh.ontologyKey = "LH_RootlessB";
        baseMidi = 48; // Start around C3 (7th is lower)
    }
    
    // For triads without 7th, use simpler shell
    if (!hasSeventh && !is6thChord) {
        degrees = {3, 5};
        lh.ontologyKey = "LH_Shell";
        baseMidi = 52;
    }
    
    // Realize the voicing
    QVector<int> pcs;
    for (int deg : degrees) {
        int pc = pcForDegree(chord, deg);
        if (pc >= 0) pcs.push_back(pc);
    }
    
    if (pcs.isEmpty()) return lh;
    
    // Voice-lead from previous LH voicing
    if (!m_state.lastLhMidi.isEmpty()) {
        int lastCenter = 0;
        for (int m : m_state.lastLhMidi) lastCenter += m;
        lastCenter /= m_state.lastLhMidi.size();
        baseMidi = qBound(48, lastCenter - 4, 60);
    }
    
    // Stack notes upward from base
    lh.midiNotes = realizeVoicingTemplate(degrees, chord, baseMidi, c.lhHi);
    
    // Ensure within LH range (48-68)
    for (int& m : lh.midiNotes) {
        while (m < 48) m += 12;
        while (m > 68) m -= 12;
    }
    std::sort(lh.midiNotes.begin(), lh.midiNotes.end());
    
    // Calculate voice-leading cost
    lh.cost = voiceLeadingCost(m_state.lastLhMidi, lh.midiNotes);
    
    return lh;
}

// RH Melodic: Create dyads/triads that move melodically
// Top note follows stepwise motion, inner voice provides harmony
JazzBalladPianoPlanner::RhMelodic JazzBalladPianoPlanner::generateRhMelodicVoicing(
    const Context& c, int targetTopMidi) const {
    
    RhMelodic rh;
    const auto& chord = c.chord;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return rh;
    
    // Collect available color tones for RH (prefer extensions)
    QVector<int> colorPcs;
    
    // Priority order: 9, 13, 11, then guide tones 7, 3
    int ninth = pcForDegree(chord, 9);
    int thirteenth = pcForDegree(chord, 13);
    int eleventh = pcForDegree(chord, 11);
    int seventh = pcForDegree(chord, 7);
    int third = pcForDegree(chord, 3);
    int fifth = pcForDegree(chord, 5);
    
    if (ninth >= 0) colorPcs.push_back(ninth);
    if (thirteenth >= 0) colorPcs.push_back(thirteenth);
    if (seventh >= 0) colorPcs.push_back(seventh);
    if (third >= 0) colorPcs.push_back(third);
    if (eleventh >= 0 && chord.quality != music::ChordQuality::Major) {
        colorPcs.push_back(eleventh); // Avoid 11 on major chords
    }
    if (fifth >= 0) colorPcs.push_back(fifth);
    
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
            
            // Slight preference for extensions (9, 13)
            if (pc == ninth || pc == thirteenth) cost -= 0.5;
            
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
    
    // Find a second voice (3rd or 6th below top)
    int secondPc = -1;
    int secondMidi = -1;
    
    // Try 3rd below (interval of 3-4 semitones)
    for (int pc : colorPcs) {
        if (pc == topPc) continue;
        int interval = (topPc - pc + 12) % 12;
        if (interval >= 3 && interval <= 5) { // Minor 3rd to perfect 4th
            secondPc = pc;
            break;
        }
    }
    
    // Fallback: try 6th below
    if (secondPc < 0) {
        for (int pc : colorPcs) {
            if (pc == topPc) continue;
            int interval = (topPc - pc + 12) % 12;
            if (interval >= 8 && interval <= 10) { // Major 6th to minor 7th
                secondPc = pc;
                break;
            }
        }
    }
    
    // Last resort: just use the 7th or 3rd
    if (secondPc < 0) {
        secondPc = (seventh >= 0 && seventh != topPc) ? seventh : third;
    }
    
    if (secondPc >= 0) {
        // Place second voice 3-6 semitones below top
        secondMidi = rh.topNoteMidi - 3;
        while (normalizePc(secondMidi) != secondPc && secondMidi > rh.topNoteMidi - 12) {
            secondMidi--;
        }
        if (secondMidi >= c.rhLo) {
            rh.midiNotes.push_back(secondMidi);
        }
    }
    
    rh.midiNotes.push_back(rh.topNoteMidi);
    std::sort(rh.midiNotes.begin(), rh.midiNotes.end());
    
    // Determine ontology key
    if (topPc == ninth || topPc == thirteenth) {
        rh.isColorTone = true;
        rh.ontologyKey = (rh.midiNotes.size() == 2) ? "RH_Dyad_Color" : "RH_Single_Color";
    } else {
        rh.isColorTone = false;
        rh.ontologyKey = (rh.midiNotes.size() == 2) ? "RH_Dyad_Guide" : "RH_Single_Guide";
    }
    
    return rh;
}

// LH: Provides harmonic foundation. ALWAYS plays regardless of user activity.
// The LH is the anchor - it doesn't back off, only the RH does.
bool JazzBalladPianoPlanner::shouldLhPlayBeat(const Context& c, quint32 hash) const {
    // ================================================================
    // LH NEVER backs off for user activity - it's the foundation
    // (Only RH becomes sparse when user is playing)
    // ================================================================
    
    // Chord changes: always play
    if (c.chordIsNew) {
        return true;
    }
    
    // Beat 1 (without chord change): usually play to reinforce
    if (c.beatInBar == 0) {
        double prob = 0.55 + 0.30 * c.weights.density;
        return (hash % 100) < int(prob * 100);
    }
    
    // Beat 3: often play for rhythmic pulse
    if (c.beatInBar == 2) {
        double prob = 0.30 + 0.25 * c.weights.density;
        if (c.cadence01 >= 0.4) prob += 0.20;
        if (c.phraseEndBar) prob += 0.15;
        return (hash % 100) < int(prob * 100);
    }
    
    // Beats 2/4: occasionally at higher energy
    if (c.energy >= 0.5) {
        double prob = 0.15 * c.energy;
        return (hash % 100) < int(prob * 100);
    }
    
    return false;
}

// RH activity: Melodic color and movement. Backs off when user plays, but doesn't disappear.
int JazzBalladPianoPlanner::rhActivityLevel(const Context& c, quint32 hash) const {
    // ================================================================
    // WHEN USER IS PLAYING: RH becomes sparse but NOT silent
    // Still provides occasional color, just much less
    // ================================================================
    if (c.userBusy || c.userDensityHigh || c.userIntensityPeak) {
        // Sparse mode: occasional single notes, mostly on chord changes
        if (c.chordIsNew) {
            return (hash % 100) < 50 ? 1 : 0; // 50% single note on chord changes
        }
        // Otherwise very rare
        return (hash % 100) < 10 ? 1 : 0; // 10% chance of single note
    }
    
    // ================================================================
    // NORMAL COMPING: RH adds color and melodic interest
    // ================================================================
    
    double activityScore = 0.5; // Base: modest activity
    
    // Energy contribution
    activityScore += 1.0 * c.energy;
    
    // Chord changes: more activity
    if (c.chordIsNew) {
        activityScore += 0.8;
    }
    
    // User silence: fill opportunity
    if (c.userSilence) {
        activityScore += 0.5 * c.weights.interactivity;
    }
    
    // Density weight
    activityScore += 0.6 * c.weights.density;
    
    // Cadence/phrase endings
    if (c.cadence01 >= 0.4) {
        activityScore += 0.4 * c.cadence01;
    }
    
    // Phrase breathing: reduce in middle of phrases
    const bool middleOfPhrase = (c.barInPhrase >= 1 && c.barInPhrase <= c.phraseBars - 2);
    if (middleOfPhrase && !c.chordIsNew) {
        activityScore *= 0.6;
    }
    
    // Offbeats: reduce unless energetic
    if ((c.beatInBar == 1 || c.beatInBar == 3) && !c.chordIsNew) {
        activityScore *= (0.3 + 0.5 * c.energy);
    }
    
    // Random variation
    int variation = (hash % 3) - 1; // -1, 0, +1
    
    int activity = qBound(0, int(activityScore) + variation, 4);
    
    // Final gate: low density = cap at 2
    if (c.weights.density < 0.35) {
        activity = qMin(activity, 2);
    }
    
    return activity;
}

// Select next melodic target for RH top voice (stepwise preferred)
int JazzBalladPianoPlanner::selectNextRhMelodicTarget(const Context& c) const {
    int lastTop = (m_state.lastRhTopMidi > 0) ? m_state.lastRhTopMidi : 74;
    
    // Collect scale tones for melodic motion
    QVector<int> scalePcs;
    int third = pcForDegree(c.chord, 3);
    int fifth = pcForDegree(c.chord, 5);
    int seventh = pcForDegree(c.chord, 7);
    int ninth = pcForDegree(c.chord, 9);
    int thirteenth = pcForDegree(c.chord, 13);
    
    if (ninth >= 0) scalePcs.push_back(ninth);
    if (thirteenth >= 0) scalePcs.push_back(thirteenth);
    if (seventh >= 0) scalePcs.push_back(seventh);
    if (third >= 0) scalePcs.push_back(third);
    if (fifth >= 0) scalePcs.push_back(fifth);
    
    if (scalePcs.isEmpty()) return lastTop;
    
    // Determine direction based on recent motion and register
    int dir = m_state.rhMelodicDirection;
    
    // Tendency to reverse near boundaries
    if (lastTop >= 80) dir = -1;
    else if (lastTop <= 70) dir = 1;
    else if (m_state.rhMotionsThisChord >= 3) {
        // After a few motions, tend to reverse
        dir = -dir;
    }
    
    // Find nearest scale tone in preferred direction
    int bestTarget = lastTop;
    int bestDist = 999;
    
    for (int pc : scalePcs) {
        for (int oct = 5; oct <= 7; ++oct) {
            int midi = pc + 12 * oct;
            if (midi < c.rhLo || midi > c.rhHi) continue;
            
            int motion = midi - lastTop;
            bool rightDirection = (dir == 0) || (dir > 0 && motion > 0) || (dir < 0 && motion < 0);
            
            if (rightDirection && qAbs(motion) >= 1 && qAbs(motion) <= 4) {
                if (qAbs(motion) < bestDist) {
                    bestDist = qAbs(motion);
                    bestTarget = midi;
                }
            }
        }
    }
    
    // If no good target in direction, allow contrary motion
    if (bestDist == 999) {
        for (int pc : scalePcs) {
            for (int oct = 5; oct <= 7; ++oct) {
                int midi = pc + 12 * oct;
                if (midi < c.rhLo || midi > c.rhHi) continue;
                int motion = qAbs(midi - lastTop);
                if (motion >= 1 && motion <= 5 && motion < bestDist) {
                    bestDist = motion;
                    bestTarget = midi;
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

    BeatPlan plan;

    Context adjusted = c;
    adjustRegisterForBass(adjusted);
    
    // Check if chord changed - reset RH melodic motion counter
    const bool chordChanged = c.chordIsNew || 
        (c.chord.rootPc != m_state.lastChordForRh.rootPc) ||
        (c.chord.quality != m_state.lastChordForRh.quality);
    
    // Determinism hashes
    const quint32 lhHash = virtuoso::util::StableHash::mix(
        adjusted.determinismSeed, adjusted.playbackBarIndex * 17 + adjusted.beatInBar);
    const quint32 rhHash = virtuoso::util::StableHash::mix(
        adjusted.determinismSeed, adjusted.playbackBarIndex * 23 + adjusted.beatInBar * 3);
    const quint32 timingHash = virtuoso::util::StableHash::mix(
        adjusted.determinismSeed, adjusted.playbackBarIndex * 31 + adjusted.beatInBar * 7);
    
    const auto mappings = computeWeightMappings(adjusted);
    const int baseVel = 55 + int(25.0 * adjusted.energy);
    
    QString pedalId;
    
    // Get pedal from vocabulary if available
    if (hasVocabularyLoaded()) {
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
    
    if (lhPlays) {
        lhVoicing = generateLhRootlessVoicing(adjusted);
        
        if (!lhVoicing.midiNotes.isEmpty()) {
            // ================================================================
            // LH RHYTHM PATTERN: Determine how many hits and when
            // ================================================================
            struct LhHit {
                int sub = 0;           // subdivision (0=beat, 1=e, 2=and, 3=a)
                int velDelta = 0;      // velocity adjustment
                bool useAltVoicing = false; // use alternate voicing (Type B if was A, etc.)
                bool anticipate = false;    // play early (syncopation)
            };
            
            QVector<LhHit> lhHits;
            const int patternSeed = (lhHash / 11) % 8;
            
            // Chord changes get more interesting patterns
            if (adjusted.chordIsNew) {
                switch (patternSeed) {
                    case 0: // Simple: just on the beat
                        lhHits.push_back({0, 0, false, false});
                        break;
                    case 1: // Syncopated: anticipate the chord change
                        lhHits.push_back({0, 0, false, true}); // Plays early
                        break;
                    case 2: // Two hits: beat + and
                        lhHits.push_back({0, 0, false, false});
                        lhHits.push_back({2, -10, true, false}); // Alt voicing
                        break;
                    case 3: // Two hits: syncopated + beat
                        lhHits.push_back({0, 0, false, true}); // Anticipate
                        lhHits.push_back({2, -8, false, false}); // Repeat
                        break;
                    case 4: // Three hits at higher energy
                        if (adjusted.energy >= 0.5) {
                            lhHits.push_back({0, 0, false, false});
                            lhHits.push_back({2, -8, true, false});
                            lhHits.push_back({3, -12, false, false});
                        } else {
                            lhHits.push_back({0, 0, false, false});
                        }
                        break;
                    case 5: // Delayed: and only (very syncopated)
                        lhHits.push_back({2, -3, false, false});
                        break;
                    case 6: // Beat + pickup
                        lhHits.push_back({0, 0, false, false});
                        if (adjusted.cadence01 >= 0.3) {
                            lhHits.push_back({3, -10, true, false});
                        }
                        break;
                    default: // Beat + repeat with same voicing
                        lhHits.push_back({0, 0, false, false});
                        if (adjusted.energy >= 0.4) {
                            lhHits.push_back({2, -6, false, false}); // Same voicing
                        }
                        break;
                }
            } else {
                // Non-chord-change: simpler patterns
                switch (patternSeed % 3) {
                    case 0:
                        lhHits.push_back({0, 0, false, false});
                        break;
                    case 1:
                        lhHits.push_back({0, 0, false, false});
                        if (adjusted.energy >= 0.6) {
                            lhHits.push_back({2, -10, false, false});
                        }
                        break;
                    case 2: // Syncopated
                        lhHits.push_back({2, -3, false, false});
                        break;
                }
            }
            
            // Generate notes for each LH hit
            for (const auto& hit : lhHits) {
                QVector<int> hitMidi = lhVoicing.midiNotes;
                QString hitKey = lhVoicing.ontologyKey;
                
                // Alternate voicing: regenerate with opposite Type
                if (hit.useAltVoicing && hitMidi.size() >= 3) {
                    // Shift voicing: move middle notes up/down
                    for (int i = 1; i < hitMidi.size() - 1; ++i) {
                        hitMidi[i] += (lhVoicing.isTypeA ? 2 : -2);
                    }
                    hitKey = lhVoicing.isTypeA ? "LH_RootlessB_Alt" : "LH_RootlessA_Alt";
                }
                
                // Calculate timing
                int timingOffsetMs = computeTimingOffsetMs(adjusted, timingHash + hit.sub);
                if (hit.anticipate) {
                    // Syncopation: play 1/8 note early
                    timingOffsetMs -= int(60000.0 / adjusted.bpm / 2.0);
                }
                
                virtuoso::groove::GridPos lhPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                    adjusted.playbackBarIndex, adjusted.beatInBar, hit.sub, 4, ts);
                lhPos = applyTimingOffset(lhPos, timingOffsetMs, adjusted.bpm, ts);
                
                // Velocity: accent first hit, softer subsequent
                int lhVel = int(baseVel * mappings.velocityMod * 0.85) + hit.velDelta;
                lhVel = qBound(35, lhVel, 95);
                
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
    // RIGHT HAND: Melodic dyads with voice-leading
    // More active, creates an independent melodic line in the upper register
    // ==========================================================================
    
    const int rhActivity = rhActivityLevel(adjusted, rhHash);
    
    // Reset RH motion counter on chord change
    if (chordChanged) {
        m_state.rhMotionsThisChord = 0;
        m_state.lastChordForRh = c.chord;
    }
    
    if (rhActivity > 0) {
        // Generate RH hits based on activity level
        // EACH hit moves melodically - no repetition!
        // Use varied patterns for interest, not the same pattern every time
        
        QVector<std::tuple<int, int, bool>> rhTimings; // sub, velDelta, preferDyad
        
        // Pattern selection based on context for variety
        const int patternSeed = (rhHash / 7) % 6;
        const bool preferSparse = (c.energy < 0.4) || (c.weights.density < 0.4);
        
        switch (rhActivity) {
            case 1:
                // Single hit - vary between dyad and single note
                if (preferSparse || (patternSeed % 2) == 0) {
                    rhTimings.push_back({0, 0, false}); // Single note
                } else {
                    rhTimings.push_back({0, 0, true}); // Dyad
                }
                break;
                
            case 2:
                // Two hits - multiple pattern options
                switch (patternSeed % 4) {
                    case 0: // Beat + and
                        rhTimings.push_back({0, 0, true});
                        rhTimings.push_back({2, -10, false});
                        break;
                    case 1: // Just and (syncopated)
                        rhTimings.push_back({2, -3, true});
                        break;
                    case 2: // Beat + pickup
                        rhTimings.push_back({0, 0, true});
                        rhTimings.push_back({3, -12, false});
                        break;
                    case 3: // E-and + and (offbeat feel)
                        rhTimings.push_back({1, -5, false});
                        rhTimings.push_back({2, -3, true});
                        break;
                }
                break;
                
            case 3:
                // Three hits - still varied
                switch (patternSeed % 3) {
                    case 0: // Beat + and + pickup
                        rhTimings.push_back({0, 0, true});
                        rhTimings.push_back({2, -6, false});
                        rhTimings.push_back({3, -12, false});
                        break;
                    case 1: // E-and + and + a-and
                        rhTimings.push_back({1, -4, false});
                        rhTimings.push_back({2, -2, true});
                        rhTimings.push_back({3, -10, false});
                        break;
                    case 2: // Beat + e-and + and (front-loaded)
                        rhTimings.push_back({0, 0, true});
                        rhTimings.push_back({1, -8, false});
                        rhTimings.push_back({2, -4, true});
                        break;
                }
                break;
                
            case 4:
                // Four hits - climax moments only, still musical
                switch (patternSeed % 2) {
                    case 0:
                        rhTimings.push_back({0, 0, true});
                        rhTimings.push_back({1, -8, false});
                        rhTimings.push_back({2, -4, true});
                        rhTimings.push_back({3, -10, false});
                        break;
                    case 1: // Swing feel (1-and-a)
                        rhTimings.push_back({0, 0, true});
                        rhTimings.push_back({2, -3, true});
                        rhTimings.push_back({3, -8, false});
                        break;
                }
                break;
                
            default:
                break;
        }
        
        // Slight delay for RH relative to LH (rubato feel)
        const int rhTimingOffset = computeTimingOffsetMs(adjusted, rhHash) + 5;
        
        // Track melodic line through this beat - each hit advances the melody
        int currentTopMidi = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 74;
        int currentDirection = m_state.rhMelodicDirection;
        int motionCount = 0;
        
        // Motivic memory: occasionally return to a "home" note for coherence
        const int motifHomeMidi = (m_state.rhMotionsThisChord == 0) ? currentTopMidi : 
                                   ((currentTopMidi + 74) / 2); // Tend toward middle register
        
        for (int hitIndex = 0; hitIndex < rhTimings.size(); ++hitIndex) {
            const auto& timing = rhTimings[hitIndex];
            const int sub = std::get<0>(timing);
            const int velDelta = std::get<1>(timing);
            const bool preferDyad = std::get<2>(timing);
            
            // ================================================================
            // MUSICAL DECISION: Move, Hold, or Return?
            // Great pianists don't always move - they make choices
            // ================================================================
            
            enum class MelodicChoice { Move, Hold, ReturnHome };
            MelodicChoice choice = MelodicChoice::Move;
            
            // Probability of hold (staying on same note) - more common at low energy
            const double holdProb = 0.10 + 0.20 * (1.0 - c.energy) + 0.15 * (1.0 - c.weights.variability);
            // Probability of return (going back to motif home)
            const double returnProb = (m_state.rhMotionsThisChord >= 3) ? 0.25 : 0.08;
            
            const int choiceRoll = (rhHash + hitIndex * 17) % 100;
            if (choiceRoll < int(holdProb * 100) && hitIndex > 0) {
                choice = MelodicChoice::Hold;
            } else if (choiceRoll < int((holdProb + returnProb) * 100) && m_state.rhMotionsThisChord >= 2) {
                choice = MelodicChoice::ReturnHome;
            }
            
            // Collect scale/chord tones for melodic motion
            QVector<int> melodicPcs;
            int ninth = pcForDegree(adjusted.chord, 9);
            int thirteenth = pcForDegree(adjusted.chord, 13);
            int seventh = pcForDegree(adjusted.chord, 7);
            int third = pcForDegree(adjusted.chord, 3);
            int fifth = pcForDegree(adjusted.chord, 5);
            int root = adjusted.chord.rootPc;
            
            // Priority: extensions first for color, then guide tones
            if (ninth >= 0) melodicPcs.push_back(ninth);
            if (thirteenth >= 0) melodicPcs.push_back(thirteenth);
            if (seventh >= 0) melodicPcs.push_back(seventh);
            if (third >= 0) melodicPcs.push_back(third);
            if (fifth >= 0) melodicPcs.push_back(fifth);
            if (root >= 0 && motionCount > 0) melodicPcs.push_back(root); // Root OK for passing
            
            if (melodicPcs.isEmpty()) continue;
            
            int bestTarget = currentTopMidi;
            
            if (choice == MelodicChoice::Hold) {
                // Stay on current note (creates sustain effect)
                bestTarget = currentTopMidi;
            } else if (choice == MelodicChoice::ReturnHome) {
                // Return toward motif home (creates coherence)
                bestTarget = motifHomeMidi;
                // Snap to nearest chord tone
                int bestDist = 999;
                for (int pc : melodicPcs) {
                    for (int oct = 5; oct <= 7; ++oct) {
                        int midi = pc + 12 * oct;
                        if (midi < adjusted.rhLo || midi > adjusted.rhHi) continue;
                        int dist = qAbs(midi - motifHomeMidi);
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestTarget = midi;
                        }
                    }
                }
            } else {
                // Move: find next scale tone in direction
                // Determine direction: tend to continue, reverse near boundaries
                if (currentTopMidi >= 82) currentDirection = -1;
                else if (currentTopMidi <= 68) currentDirection = 1;
                else if (motionCount >= 3 && ((rhHash + hitIndex) % 4) == 0) currentDirection = -currentDirection;
                
                int bestScore = 999;
            
                for (int pc : melodicPcs) {
                    for (int oct = 5; oct <= 7; ++oct) {
                        int midi = pc + 12 * oct;
                        if (midi < adjusted.rhLo || midi > adjusted.rhHi) continue;
                        
                        int motion = midi - currentTopMidi;
                        int absMotion = qAbs(motion);
                        
                        // Skip if no motion (we want MOVEMENT!)
                        if (absMotion == 0) continue;
                        
                        // Prefer stepwise motion (1-3 semitones)
                        int score = 0;
                        if (absMotion <= 2) score = 0;
                        else if (absMotion <= 4) score = 2;
                        else if (absMotion <= 7) score = 5;
                        else score = 10;
                        
                        // Bonus for matching direction
                        bool rightDirection = (currentDirection == 0) ||
                                              (currentDirection > 0 && motion > 0) ||
                                              (currentDirection < 0 && motion < 0);
                        if (!rightDirection) score += 3;
                        
                        // Slight preference for color tones (9, 13)
                        if (pc == ninth || pc == thirteenth) score -= 1;
                        
                        // Sweet spot bonus
                        if (midi >= 72 && midi <= 80) score -= 1;
                        
                        if (score < bestScore) {
                            bestScore = score;
                            bestTarget = midi;
                        }
                    }
                }
            } // end of Move choice
            
            // Create voicing for this target
            QVector<int> rhMidiNotes;
            int topPc = normalizePc(bestTarget);
            
            rhMidiNotes.push_back(bestTarget);
            
            // Add second voice for dyads (3rd-6th below top)
            if (preferDyad) {
                int secondPc = -1;
                // Try 3rd below
                for (int pc : melodicPcs) {
                    if (pc == topPc) continue;
                    int interval = (topPc - pc + 12) % 12;
                    if (interval >= 3 && interval <= 5) {
                        secondPc = pc;
                        break;
                    }
                }
                // Try 6th below
                if (secondPc < 0) {
                    for (int pc : melodicPcs) {
                        if (pc == topPc) continue;
                        int interval = (topPc - pc + 12) % 12;
                        if (interval >= 8 && interval <= 10) {
                            secondPc = pc;
                            break;
                        }
                    }
                }
                // Fallback to 7th or 3rd
                if (secondPc < 0) {
                    secondPc = (seventh >= 0 && seventh != topPc) ? seventh : third;
                }
                
                if (secondPc >= 0) {
                    int secondMidi = bestTarget - 3;
                    while (normalizePc(secondMidi) != secondPc && secondMidi > bestTarget - 12) {
                        secondMidi--;
                    }
                    if (secondMidi >= adjusted.rhLo && secondMidi < bestTarget) {
                        rhMidiNotes.insert(rhMidiNotes.begin(), secondMidi);
                    }
                }
            }
            
            if (rhMidiNotes.isEmpty()) continue;
            
            virtuoso::groove::GridPos rhPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                adjusted.playbackBarIndex, adjusted.beatInBar, sub, 4, ts);
            rhPos = applyTimingOffset(rhPos, rhTimingOffset, adjusted.bpm, ts);
            
            // Velocity: emphasize downbeats, softer passing tones
            int rhVel = int(baseVel * mappings.velocityMod + velDelta);
            if (sub == 0) rhVel += 5; // Emphasize beat
            rhVel = qBound(40, rhVel, 105);
            
            // Duration: shorter for faster passages, longer for sustained
            double rhDurBeats = (sub == 0) ? 0.65 : 0.4;
            if (rhTimings.size() >= 3) rhDurBeats *= 0.8; // Tighter for busier patterns
            const virtuoso::groove::Rational rhDurWhole(qint64(rhDurBeats * mappings.durationMod * 1000), 4000);
            
            QString ontologyKey = preferDyad ? "RH_Melodic_Dyad" : "RH_Melodic_Single";
            if (topPc == ninth || topPc == thirteenth) ontologyKey += "_Color";
            
            for (int midi : rhMidiNotes) {
                virtuoso::engine::AgentIntentNote note;
                note.agent = "Piano";
                note.channel = midiChannel;
                note.note = midi;
                note.baseVelocity = rhVel;
                note.startPos = rhPos;
                note.durationWhole = rhDurWhole;
                note.structural = false;
                note.chord_context = adjusted.chordText;
                note.voicing_type = ontologyKey;
                note.logic_tag = "RH";
                
                plan.notes.push_back(note);
            }
            
            // ================================================================
            // CRITICAL: Update state for NEXT hit to continue melodic motion
            // ================================================================
            int oldTop = currentTopMidi;
            currentTopMidi = bestTarget;
            if (bestTarget > oldTop) currentDirection = 1;
            else if (bestTarget < oldTop) currentDirection = -1;
            motionCount++;
        }
        
        // Persist final melodic state for next beat
        m_state.lastRhTopMidi = currentTopMidi;
        m_state.rhMelodicDirection = currentDirection;
        m_state.rhMotionsThisChord += motionCount;
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
    m_state.lastVoicingKey = lhVoicing.ontologyKey.isEmpty() ? "RH_Melodic" : lhVoicing.ontologyKey;

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
