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
        lh.ontologyKey = "LH_Voicing_3";
    } else if (lh.midiNotes.size() == 2) {
        lh.ontologyKey = "LH_Shell";
    } else {
        lh.ontologyKey = "LH_Single";
    }
    
    lh.isTypeA = (chord.rootPc <= 5);
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
        rh.ontologyKey = (rh.midiNotes.size() == 2) ? "RH_Dyad_Color" : "RH_Single_Color";
    } else {
        rh.isColorTone = false;
        rh.ontologyKey = (rh.midiNotes.size() == 2) ? "RH_Dyad_Guide" : "RH_Single_Guide";
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
    rh.ontologyKey = QString("UST_%1_%2").arg(ust.isMajor ? "Maj" : "Min").arg(ust.colorDescription);
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
        // CONSONANCE CHECK: Snap to chord scale
        // Exception: the final target note (interval 0) stays as-is
        // Exception: very brief chromatic approaches (±1) are OK if leading to target
        // ================================================================
        bool isTargetNote = (fragment.intervalPattern[i] == 0);
        bool isImmediateApproach = (qAbs(fragment.intervalPattern[i]) == 1 && 
                                    i == fragment.intervalPattern.size() - 2);
        
        if (isTargetNote) {
            // Target stays as-is (should already be a chord tone)
            fn.midiNote = rawMidi;
        } else if (isImmediateApproach) {
            // Brief chromatic approach right before target is OK
            fn.midiNote = rawMidi;
        } else {
            // All other notes: snap to chord scale for consonance
            fn.midiNote = snapToChordScale(rawMidi);
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
    
    // Chord changes: always play
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
// CONSONANCE-FIRST: Prioritize guide tones, extensions only when tension warrants
int JazzBalladPianoPlanner::selectNextRhMelodicTarget(const Context& c) const {
    int lastTop = (m_state.lastRhTopMidi > 0) ? m_state.lastRhTopMidi : 74;
    
    // Determine tension level for extension usage
    const double tensionLevel = c.weights.tension * 0.6 + c.energy * 0.4;
    
    // Collect scale tones for melodic motion - CONSONANCE FIRST
    // pcForDegree now returns -1 for inappropriate extensions
    QVector<int> scalePcs;
    int third = pcForDegree(c.chord, 3);
    int fifth = pcForDegree(c.chord, 5);
    int seventh = pcForDegree(c.chord, 7);
    int ninth = pcForDegree(c.chord, 9);
    int thirteenth = pcForDegree(c.chord, 13);
    
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
                            // Spread: Move all notes slightly (creates movement)
                            for (int i = 0; i < hitMidi.size(); ++i) {
                                int shift = (i % 2 == 0) ? 0 : 1;
                                hitMidi[i] = qBound(48, hitMidi[i] + shift, 67);
                            }
                            hitKey = "LH_Spread";
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
                
                virtuoso::groove::GridPos lhPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                    adjusted.playbackBarIndex, adjusted.beatInBar, timingSub, 4, ts);
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
        
        QVector<std::tuple<int, int, bool>> rhTimings; // sub, velDelta, preferDyad
        
        // ================================================================
        // MUSICAL INTENT: RH patterns based on phrase position and context
        // NOT random hash - purposeful rhythmic choices
        // ================================================================
        
        const bool phraseStart = (adjusted.barInPhrase == 0);
        const bool phraseMid = (adjusted.barInPhrase >= 1 && adjusted.barInPhrase < adjusted.phraseBars - 1);
        const bool phraseEnd = adjusted.phraseEndBar;
        const bool isCadence = (adjusted.cadence01 >= 0.4);
        const bool isDownbeat = (adjusted.beatInBar == 0);
        const bool isBeat3 = (adjusted.beatInBar == 2);
        const bool isWeakBeat = (adjusted.beatInBar == 1 || adjusted.beatInBar == 3);
        const bool preferSparse = (c.energy < 0.4) || (c.weights.density < 0.4);
        const bool preferRich = (c.energy >= 0.7) || (c.weights.density >= 0.7);
        
        // Favor dyads in jazz context (richer harmony)
        // Singles for melodic lines, dyads for color
        
        switch (rhActivity) {
            case 1:
                // ============================================================
                // SINGLE HIT: Placement matters for phrasing
                // ============================================================
                if (phraseStart && isDownbeat) {
                    // Phrase opening: clear dyad on the beat
                    rhTimings.push_back({0, 0, true});
                } else if (phraseEnd && isCadence) {
                    // Cadence resolution: dyad on beat
                    rhTimings.push_back({0, 0, true});
                } else if (isBeat3 && !preferSparse) {
                    // Beat 3: secondary emphasis, slight delay for groove
                    rhTimings.push_back({1, -3, !preferSparse}); // Slightly off the beat
                } else if (isWeakBeat) {
                    // Weak beats: syncopated fill, single note for lightness
                    rhTimings.push_back({2, -5, false});
                } else if (preferSparse) {
                    // Sparse: single note on the beat
                    rhTimings.push_back({0, 0, false});
                } else {
                    // Default: dyad on the beat
                    rhTimings.push_back({0, 0, true});
                }
                break;
                
            case 2:
                // ============================================================
                // TWO HITS: Create forward motion, avoid static patterns
                // ============================================================
                if (phraseStart && isDownbeat) {
                    // Phrase opening: establish, then move forward
                    rhTimings.push_back({0, 0, true});  // Statement
                    rhTimings.push_back({2, -8, false}); // Motion
                } else if (phraseEnd && isCadence) {
                    // Cadence: resolve clearly
                    rhTimings.push_back({0, 0, true});
                    if (c.energy >= 0.5) {
                        rhTimings.push_back({3, -6, true}); // Pickup to next bar
                    }
                } else if (isBeat3) {
                    // Beat 3: syncopated, create anticipation
                    rhTimings.push_back({2, -2, true}); // And of 3
                } else if (isWeakBeat && !preferSparse) {
                    // Weak beat: offbeat interest
                    rhTimings.push_back({1, -3, false});
                    rhTimings.push_back({3, -8, true});
                } else if (preferRich) {
                    // Rich texture: two dyads
                    rhTimings.push_back({0, 0, true});
                    rhTimings.push_back({2, -6, true});
                } else {
                    // Standard: beat + and
                    rhTimings.push_back({0, 0, true});
                    rhTimings.push_back({2, -10, false});
                }
                break;
                
            case 3:
                // ============================================================
                // THREE HITS: Active comping, melodic development
                // ============================================================
                if (phraseStart) {
                    // Phrase opening: establish and develop
                    rhTimings.push_back({0, 0, true});   // Statement
                    rhTimings.push_back({2, -6, false}); // Development
                    rhTimings.push_back({3, -10, false}); // Motion
                } else if (phraseEnd || isCadence) {
                    // Cadence: build and resolve
                    rhTimings.push_back({0, 0, true});
                    rhTimings.push_back({2, -4, true});
                    rhTimings.push_back({3, -8, false});
                } else if (phraseMid && preferRich) {
                    // Mid-phrase climax: lush texture
                    rhTimings.push_back({0, 0, true});
                    rhTimings.push_back({1, -4, true});
                    rhTimings.push_back({3, -8, true});
                } else {
                    // Standard melodic line
                    rhTimings.push_back({0, 0, true});
                    rhTimings.push_back({2, -5, false});
                    rhTimings.push_back({3, -10, false});
                }
                break;
                
            case 4:
                // ============================================================
                // FOUR HITS: Climax/high energy only
                // ============================================================
                if (phraseEnd && isCadence) {
                    // Climactic cadence
                    rhTimings.push_back({0, 0, true});
                    rhTimings.push_back({1, -4, true});
                    rhTimings.push_back({2, -3, true});
                    rhTimings.push_back({3, -8, false});
                } else if (phraseStart && preferRich) {
                    // Bold phrase opening
                    rhTimings.push_back({0, 0, true});
                    rhTimings.push_back({1, -6, false});
                    rhTimings.push_back({2, -4, true});
                    rhTimings.push_back({3, -10, false});
                } else {
                    // Standard 4-hit: swing feel
                    rhTimings.push_back({0, 0, true});
                    rhTimings.push_back({2, -3, true});
                    rhTimings.push_back({3, -8, false});
                }
                break;
                
            default:
                break;
        }
        
        // Slight delay for RH relative to LH (rubato feel)
        const int rhTimingOffset = computeTimingOffsetMs(adjusted, rhHash) + 5;
        
        // ================================================================
        // UPPER STRUCTURE TRIAD DECISION
        // Bill Evans signature: sometimes play a simple triad that creates
        // sophisticated extensions. Use more at higher creativity/tension.
        // ================================================================
        const double tensionLevel = c.weights.tension * 0.6 + c.energy * 0.4;
        const double creativityLevel = c.weights.creativity;
        
        // Probability of using UST instead of regular melodic voicing
        // UST can introduce complex colors - use VERY sparingly to avoid dissonance
        // DISABLE on dominant chords for now - too risky for dissonance
        double ustProb = 0.0;
        
        const bool isDominant = (adjusted.chord.quality == music::ChordQuality::Dominant);
        const bool isAugmented = (adjusted.chord.quality == music::ChordQuality::Augmented);
        const bool isDiminished = (adjusted.chord.quality == music::ChordQuality::Diminished);
        const bool isProblematic = isDominant || isAugmented || isDiminished;
        
        // Only use UST on "safe" chords (major/minor 7ths) and at high creativity
        if (!isProblematic && creativityLevel > 0.6 && tensionLevel > 0.4) {
            if (chordChanged) {
                ustProb = 0.08 + 0.12 * (creativityLevel - 0.6);
            }
        }
        
        // Get available USTs
        const auto ustCandidates = getUpperStructureTriads(adjusted.chord);
        const bool useUst = !ustCandidates.isEmpty() && ((rhHash % 100) < int(ustProb * 100));
        
        // If using UST, select one based on tension level
        RhMelodic ustVoicing;
        if (useUst) {
            // Select UST: lower tension = safer (earlier in list), higher = more tense
            int ustIndex = 0;
            if (tensionLevel > 0.6 && ustCandidates.size() > 1) {
                // Allow more tense options
                ustIndex = qMin(int(tensionLevel * ustCandidates.size()), ustCandidates.size() - 1);
            } else if (tensionLevel > 0.4 && ustCandidates.size() > 1) {
                ustIndex = 1; // Second safest
            }
            
            ustVoicing = buildUstVoicing(adjusted, ustCandidates[ustIndex]);
        }
        
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
            // UPPER STRUCTURE TRIAD: Use on FIRST hit of beat if selected
            // This creates the Bill Evans signature sound
            // ================================================================
            if (useUst && hitIndex == 0 && !ustVoicing.midiNotes.isEmpty()) {
                // Play the UST triad as the first hit
                virtuoso::groove::GridPos rhPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                    adjusted.playbackBarIndex, adjusted.beatInBar, sub, 4, ts);
                rhPos = applyTimingOffset(rhPos, rhTimingOffset, adjusted.bpm, ts);
                
                int rhVel = int(baseVel * mappings.velocityMod + velDelta);
                rhVel = qBound(45, rhVel, 100);
                
                // Longer duration for UST triads (they ring beautifully)
                const virtuoso::groove::Rational rhDurWhole(qint64(0.85 * mappings.durationMod * 1000), 4000);
                
                for (int midi : ustVoicing.midiNotes) {
                    virtuoso::engine::AgentIntentNote note;
                    note.agent = "Piano";
                    note.channel = midiChannel;
                    note.note = midi;
                    note.baseVelocity = rhVel;
                    note.startPos = rhPos;
                    note.durationWhole = rhDurWhole;
                    note.structural = (hitIndex == 0 && adjusted.chordIsNew);
                    note.chord_context = adjusted.chordText;
                    note.voicing_type = ustVoicing.ontologyKey;
                    note.logic_tag = "RH_UST";
                    
                    plan.notes.push_back(note);
                }
                
                // Update state for melodic continuity
                currentTopMidi = ustVoicing.topNoteMidi;
                currentDirection = ustVoicing.melodicDirection;
                motionCount++;
                m_state.rhMotionsThisChord++;
                m_state.lastRhTopMidi = ustVoicing.topNoteMidi;
                m_state.rhMelodicDirection = ustVoicing.melodicDirection;
                
                continue; // Skip regular voicing for this hit
            }
            
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
            
            // ================================================================
            // CONSONANCE-FIRST NOTE SELECTION
            // Prioritize chord tones that define the harmony beautifully
            // Extensions only when energy/tension warrant it
            // ================================================================
            
            // Helper: Check if RH note would clash with LH voicing
            // A clash is when:
            // 1. PITCH CLASSES are a minor 2nd apart (1 semitone)
            //    This catches clashes like E (in C7) vs F (avoid note)
            // 2. OR actual MIDI notes are very close (within 3 semitones)
            //    This catches muddy voicings where notes are too close
            auto wouldClashWithLh = [&](int rhMidi) -> bool {
                int rhPc = normalizePc(rhMidi);
                for (int lhNote : m_state.lastLhMidi) {
                    int lhPc = normalizePc(lhNote);
                    
                    // Check 1: Pitch class minor 2nd (will clash regardless of octave)
                    int pcInterval = qAbs(rhPc - lhPc);
                    if (pcInterval > 6) pcInterval = 12 - pcInterval; // Normalize
                    if (pcInterval == 1) {
                        // Minor 2nd pitch class clash - always bad if within 2 octaves
                        if (qAbs(rhMidi - lhNote) <= 24) {
                            return true;
                        }
                    }
                    
                    // Check 2: Too close in absolute MIDI (muddy)
                    int midiDist = qAbs(rhMidi - lhNote);
                    if (midiDist > 0 && midiDist <= 2) {
                        // Very close notes (within 2 semitones) = muddy
                        return true;
                    }
                }
                return false;
            };
            
            QVector<int> melodicPcs;
            
            // Core chord tones - always safe and beautiful
            int third = pcForDegree(adjusted.chord, 3);
            int fifth = pcForDegree(adjusted.chord, 5);
            int seventh = pcForDegree(adjusted.chord, 7);
            int root = adjusted.chord.rootPc;
            
            // Extensions - use carefully based on chord quality and energy
            int ninth = pcForDegree(adjusted.chord, 9);
            int thirteenth = pcForDegree(adjusted.chord, 13);
            
            // Determine how "safe" we should be based on energy and tension
            const double hitTensionLevel = c.weights.tension * 0.6 + c.energy * 0.4;
            const bool allowExtensions = hitTensionLevel > 0.3;
            const bool allowColorTones = hitTensionLevel > 0.5;
            
            // Is this a dominant chord? Extensions are more natural on dominants
            const bool hitIsDominant = (adjusted.chord.quality == music::ChordQuality::Dominant);
            const bool isMajor = (adjusted.chord.quality == music::ChordQuality::Major);
            const bool isMinor = (adjusted.chord.quality == music::ChordQuality::Minor);
            
            // PRIORITY 1: Guide tones (3 and 7) - these DEFINE the chord
            // They are the most consonant and characteristic
            if (third >= 0) melodicPcs.push_back(third);
            if (seventh >= 0) melodicPcs.push_back(seventh);
            
            // PRIORITY 2: Fifth - safe, consonant, but less characteristic
            if (fifth >= 0) melodicPcs.push_back(fifth);
            
            // PRIORITY 3: Extensions - pcForDegree now handles restrictions
            // It returns -1 for unavailable/inappropriate extensions
            if (allowExtensions) {
                // 9th - pcForDegree only returns it when appropriate for chord type
                if (ninth >= 0) {
                    melodicPcs.push_back(ninth);
                }
                
                // 13th - pcForDegree only returns it when appropriate
                if (thirteenth >= 0 && allowColorTones) {
                    melodicPcs.push_back(thirteenth);
                }
            }
            
            // Root is OK for passing tones UNLESS there's a b9 (minor 2nd clash!)
            // Check if the 9th is a b9 (half step above root)
            bool hasFlat9 = false;
            if (ninth >= 0) {
                int expectedB9 = normalizePc(root + 1);
                hasFlat9 = (ninth == expectedB9);
            }
            if (root >= 0 && motionCount > 0 && !hasFlat9) {
                melodicPcs.push_back(root);
            }
            
            if (melodicPcs.isEmpty()) continue;
            
            int bestTarget = currentTopMidi;
            
            if (choice == MelodicChoice::Hold) {
                // Stay on current note (creates sustain effect)
                bestTarget = currentTopMidi;
            } else if (choice == MelodicChoice::ReturnHome) {
                // Return toward motif home (creates coherence)
                bestTarget = motifHomeMidi;
                // Snap to nearest chord tone (avoiding LH clashes)
                int bestDist = 999;
                for (int pc : melodicPcs) {
                    for (int oct = 5; oct <= 7; ++oct) {
                        int midi = pc + 12 * oct;
                        if (midi < adjusted.rhLo || midi > adjusted.rhHi) continue;
                        if (wouldClashWithLh(midi)) continue; // Skip clashing notes
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
                        
                        // DISSONANCE CHECK: Skip if would clash with LH
                        if (wouldClashWithLh(midi)) continue;
                        
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
            
            // ================================================================
            // MELODIC FRAGMENT DECISION
            // Fragments can introduce non-chord tones - use VERY sparingly
            // Only at high creativity AND only on non-dominant chords
            // ================================================================
            double fragmentProb = 0.0;
            
            // DISABLE fragments on problematic chords (dominant, augmented, diminished)
            // These are where dissonance is most likely
            const bool avoidFragments = hitIsDominant || 
                (adjusted.chord.quality == music::ChordQuality::Augmented) ||
                (adjusted.chord.quality == music::ChordQuality::Diminished);
            
            if (!avoidFragments && c.weights.creativity > 0.5) {
                // Very low probability - only at high creativity
                fragmentProb = 0.05 + 0.10 * (c.weights.creativity - 0.5);
            }
            
            const bool useFragment = (sub == 0 || sub == 2) && // Only on main subdivisions
                                     ((rhHash + hitIndex * 23) % 100) < int(fragmentProb * 100);
            
            if (useFragment) {
                // Get available fragments for this context
                const auto fragments = getMelodicFragments(adjusted, normalizePc(bestTarget));
                
                if (!fragments.isEmpty()) {
                    // Select fragment based on tension level
                    int fragIdx = 0;
                    if (hitTensionLevel > 0.5 && fragments.size() > 2) {
                        fragIdx = qMin(int(hitTensionLevel * fragments.size() * 0.6), fragments.size() - 1);
                    } else if (hitTensionLevel > 0.3 && fragments.size() > 1) {
                        fragIdx = 1;
                    }
                    
                    // Apply the fragment
                    const auto& frag = fragments[fragIdx];
                    const auto fragNotes = applyMelodicFragment(adjusted, frag, bestTarget, sub);
                    
                    if (!fragNotes.isEmpty()) {
                        // Add all fragment notes
                        for (const auto& fn : fragNotes) {
                            virtuoso::groove::GridPos fnPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
                                adjusted.playbackBarIndex, adjusted.beatInBar, fn.subBeatOffset, 4, ts);
                            fnPos = applyTimingOffset(fnPos, rhTimingOffset, adjusted.bpm, ts);
                            
                            int fnVel = int(baseVel * mappings.velocityMod + velDelta + fn.velocityDelta);
                            fnVel = qBound(40, fnVel, 105);
                            
                            double fnDur = 0.35 * fn.durationMult * mappings.durationMod;
                            const virtuoso::groove::Rational fnDurWhole(qint64(fnDur * 1000), 4000);
                            
                            virtuoso::engine::AgentIntentNote note;
                            note.agent = "Piano";
                            note.channel = midiChannel;
                            note.note = fn.midiNote;
                            note.baseVelocity = fnVel;
                            note.startPos = fnPos;
                            note.durationWhole = fnDurWhole;
                            note.structural = false;
                            note.chord_context = adjusted.chordText;
                            note.voicing_type = QString("RH_Fragment_%1").arg(frag.name);
                            note.logic_tag = "RH_Frag";
                            
                            plan.notes.push_back(note);
                        }
                        
                        // Update state - last note of fragment becomes the new reference
                        currentTopMidi = fragNotes.last().midiNote;
                        if (fragNotes.size() > 1) {
                            int prevNote = fragNotes[fragNotes.size() - 2].midiNote;
                            currentDirection = (currentTopMidi > prevNote) ? 1 : 
                                              ((currentTopMidi < prevNote) ? -1 : 0);
                        }
                        motionCount++;
                        m_state.rhMotionsThisChord++;
                        
                        continue; // Skip regular voicing for this hit
                    }
                }
            }
            
            // Create voicing for this target
            QVector<int> rhMidiNotes;
            int topPc = normalizePc(bestTarget);
            
            rhMidiNotes.push_back(bestTarget);
            
            // Determine if we should build a triad (3 notes) vs dyad (2 notes)
            // Triads for climactic moments at high energy
            const bool buildTriad = preferDyad && (c.energy > 0.7) && 
                                   (c.weights.density > 0.6) && ((rhHash + hitIndex) % 4 == 0);
            
            // ================================================================
            // CONSONANT DYAD/TRIAD BUILDING
            // Prefer intervals that sound beautiful: 3rds (3-4 semitones)
            // and 6ths (8-9 semitones). Avoid 2nds and tritones.
            // ================================================================
            if (preferDyad) {
                int secondPc = -1;
                int thirdPc = -1; // For triads
                int bestInterval = 99;
                
                // Find best second voice - prioritize CONSONANT intervals
                // Perfect intervals: 3rds (3-4), 6ths (8-9), 4ths (5), 5ths (7)
                // Avoid: 2nds (1-2), tritones (6), 7ths (10-11)
                for (int pc : melodicPcs) {
                    if (pc == topPc) continue;
                    int interval = (topPc - pc + 12) % 12;
                    
                    // Score intervals by consonance (lower is better)
                    int consonanceScore = 99;
                    if (interval == 3 || interval == 4) consonanceScore = 0;  // Minor/major 3rd - sweetest
                    else if (interval == 8 || interval == 9) consonanceScore = 1;  // Minor/major 6th - beautiful
                    else if (interval == 5) consonanceScore = 2;  // Perfect 4th - stable
                    else if (interval == 7) consonanceScore = 3;  // Perfect 5th - open
                    else if (interval == 10 || interval == 11) consonanceScore = 5; // 7ths - some tension
                    else if (interval == 1 || interval == 2) consonanceScore = 8; // 2nds - avoid
                    else if (interval == 6) consonanceScore = 9; // Tritone - avoid unless dominant
                    
                    // Allow tritone on dominant chords for color
                    if (interval == 6 && hitIsDominant && hitTensionLevel > 0.6) {
                        consonanceScore = 4; // OK on dominants
                    }
                    
                    if (consonanceScore < bestInterval) {
                        bestInterval = consonanceScore;
                        secondPc = pc;
                    }
                }
                
                // Fallback to 7th or 3rd of the chord (guaranteed consonant)
                if (secondPc < 0 || bestInterval > 5) {
                    secondPc = (seventh >= 0 && seventh != topPc) ? seventh : third;
                }
                
                // For triads, find a third voice that creates a consonant stack
                if (buildTriad && secondPc >= 0) {
                    int bestTriadScore = 99;
                    for (int pc : melodicPcs) {
                        if (pc == topPc || pc == secondPc) continue;
                        
                        // Check interval from second voice
                        int interval = (secondPc - pc + 12) % 12;
                        int consonanceScore = 99;
                        if (interval == 3 || interval == 4) consonanceScore = 0;
                        else if (interval == 5 || interval == 7) consonanceScore = 2;
                        else if (interval == 8 || interval == 9) consonanceScore = 1;
                        
                        if (consonanceScore < bestTriadScore) {
                            bestTriadScore = consonanceScore;
                            thirdPc = pc;
                        }
                    }
                }
                
                // Build the MIDI notes with proper spacing
                if (secondPc >= 0) {
                    // Find second note 3-9 semitones below top (sweet spot for dyads)
                    int secondMidi = bestTarget - 3;
                    while (normalizePc(secondMidi) != secondPc && secondMidi > bestTarget - 10) {
                        secondMidi--;
                    }
                    
                    // Verify the interval is consonant before adding
                    int actualInterval = bestTarget - secondMidi;
                    bool intervalOk = (actualInterval >= 3 && actualInterval <= 9) || 
                                      (actualInterval == 10 && hitTensionLevel > 0.5);
                    
                    // Also check for LH clash
                    bool noLhClash = !wouldClashWithLh(secondMidi);
                    
                    if (intervalOk && noLhClash && secondMidi >= adjusted.rhLo && secondMidi < bestTarget) {
                        rhMidiNotes.insert(rhMidiNotes.begin(), secondMidi);
                        
                        // Add third voice for triads
                        if (buildTriad && thirdPc >= 0) {
                            int thirdMidi = secondMidi - 3;
                            while (normalizePc(thirdMidi) != thirdPc && thirdMidi > secondMidi - 10) {
                                thirdMidi--;
                            }
                            int thirdInterval = secondMidi - thirdMidi;
                            bool thirdIntervalOk = (thirdInterval >= 3 && thirdInterval <= 9);
                            bool thirdNoLhClash = !wouldClashWithLh(thirdMidi);
                            
                            if (thirdIntervalOk && thirdNoLhClash && thirdMidi >= adjusted.rhLo && thirdMidi < secondMidi) {
                                rhMidiNotes.insert(rhMidiNotes.begin(), thirdMidi);
                            }
                        }
                    }
                }
            }
            
            if (rhMidiNotes.isEmpty()) continue;
            
            // ================================================================
            // FINAL SAFETY FILTER: Be VERY conservative about chord tones
            // On dominant/altered chords, extensions can clash badly
            // Stick to CORE chord tones (root, 3rd, 5th, 7th) for safety
            // Only allow 9th/13th if they're EXPLICITLY in the chord symbol
            // ================================================================
            
            // Determine if this is a "dangerous" chord where extensions can clash
            const bool isProblematicChord = hitIsDominant || 
                (adjusted.chord.quality == music::ChordQuality::Augmented) ||
                (adjusted.chord.quality == music::ChordQuality::Diminished);
            
            // For problematic chords, only use core tones unless extension is explicit
            const bool explicitNinth = (adjusted.chord.extension >= 9);
            const bool explicitThirteenth = (adjusted.chord.extension >= 13);
            
            QVector<int> safeRhNotes;
            for (int midi : rhMidiNotes) {
                int pc = normalizePc(midi);
                
                // Core chord tones - ALWAYS safe
                bool isCoreChordTone = (pc == root || pc == third || 
                                        pc == fifth || pc == seventh);
                
                // Extensions - only safe if chord explicitly has them
                bool isSafeExtension = false;
                if (!isProblematicChord) {
                    // Non-dominant chords: extensions are generally safer
                    isSafeExtension = (pc == ninth || pc == thirteenth);
                } else {
                    // Dominant/augmented/dim chords: only explicit extensions
                    if (pc == ninth && explicitNinth && ninth >= 0) {
                        isSafeExtension = true;
                    }
                    if (pc == thirteenth && explicitThirteenth && thirteenth >= 0) {
                        isSafeExtension = true;
                    }
                }
                
                bool noLhClash = !wouldClashWithLh(midi);
                
                if ((isCoreChordTone || isSafeExtension) && noLhClash) {
                    safeRhNotes.push_back(midi);
                }
            }
            
            // Fallback: If we filtered everything, use the 3rd or 7th (guide tones)
            if (safeRhNotes.isEmpty() && !rhMidiNotes.isEmpty()) {
                // Try to use the 7th (defines chord quality)
                if (seventh >= 0) {
                    int targetPc = seventh;
                    for (int midi : rhMidiNotes) {
                        if (normalizePc(midi) == targetPc && !wouldClashWithLh(midi)) {
                            safeRhNotes.push_back(midi);
                            break;
                        }
                    }
                }
                // If still empty, try the 3rd
                if (safeRhNotes.isEmpty() && third >= 0) {
                    int targetPc = third;
                    for (int midi : rhMidiNotes) {
                        if (normalizePc(midi) == targetPc && !wouldClashWithLh(midi)) {
                            safeRhNotes.push_back(midi);
                            break;
                        }
                    }
                }
            }
            
            if (safeRhNotes.isEmpty()) continue;
            rhMidiNotes = safeRhNotes;
            
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
            
            QString ontologyKey = (rhMidiNotes.size() >= 3) ? "RH_Melodic_Triad" : 
                                  (preferDyad ? "RH_Melodic_Dyad" : "RH_Melodic_Single");
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
