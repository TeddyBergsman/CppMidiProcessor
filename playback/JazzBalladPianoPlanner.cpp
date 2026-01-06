#include "playback/JazzBalladPianoPlanner.h"

#include <QtGlobal>
#include <algorithm>

namespace playback {
namespace {
static int clampMidi(int m) { return (m < 0) ? 0 : (m > 127 ? 127 : m); }
} // namespace

JazzBalladPianoPlanner::JazzBalladPianoPlanner() {
    reset();
}

void JazzBalladPianoPlanner::reset() {
    m_lastVoicing.clear();
    m_lastRhythmBar = -1;
    m_barHits.clear();
    m_lastTopMidi = -1;
    m_barTopHits.clear();
    m_motifBlockStartBar = -1;
    m_motifA.clear();
    m_motifB.clear();
    m_motifC.clear();
    m_motifD.clear();
    m_phraseMotifStartBar = -1;
    m_anchorBlockStartBar = -1;
    m_anchorChordText.clear();
    m_anchorPcs.clear();
    m_lastArpBar = -1;
    m_lastArpStyle = -1;
}

int JazzBalladPianoPlanner::thirdIntervalForQuality(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::Minor:
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished: return 3;
        case music::ChordQuality::Sus2: return 2;
        case music::ChordQuality::Sus4: return 5;
        default: return 4;
    }
}

int JazzBalladPianoPlanner::fifthIntervalForQuality(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished: return 6;
        case music::ChordQuality::Augmented: return 8;
        default: return 7;
    }
}

int JazzBalladPianoPlanner::seventhIntervalFor(const music::ChordSymbol& c) {
    if (c.seventh == music::SeventhQuality::Major7) return 11;
    if (c.seventh == music::SeventhQuality::Dim7) return 9;
    if (c.seventh == music::SeventhQuality::Minor7) return 10;
    // If extensions imply a 7th, default to minor7 unless explicitly maj-marked (handled in parser).
    if (c.extension >= 7) return 10;
    return -1;
}

int JazzBalladPianoPlanner::pcForDegree(const music::ChordSymbol& c, int degree) {
    const int root = (c.rootPc >= 0) ? c.rootPc : 0;
    auto applyAlter = [&](int deg, int basePc) -> int {
        for (const auto& a : c.alterations) {
            if (a.degree != deg) continue;
            return (basePc + a.delta + 1200) % 12;
        }
        return basePc;
    };

    int pc = root;
    switch (degree) {
        case 1: pc = root; break;
        case 3: pc = (root + thirdIntervalForQuality(c.quality)) % 12; break;
        case 5: pc = (root + fifthIntervalForQuality(c.quality)) % 12; pc = applyAlter(5, pc); break;
        case 7: {
            const int iv = seventhIntervalFor(c);
            // IMPORTANT: do NOT invent a b7 on triads/6-chords. If there is no 7th, return sentinel.
            if (iv < 0) return -1;
            pc = (root + iv) % 12;
        } break;
        case 9: pc = (root + 14) % 12; pc = applyAlter(9, pc); break;
        case 11: pc = (root + 17) % 12; pc = applyAlter(11, pc); break;
        case 13: pc = (root + 21) % 12; pc = applyAlter(13, pc); break;
        default: pc = root; break;
    }
    return (pc + 12) % 12;
}

int JazzBalladPianoPlanner::nearestMidiForPc(int pc, int around, int lo, int hi) {
    if (pc < 0) pc = 0;
    around = clampMidi(around);
    // Find candidate in [lo,hi] with matching pc, closest to around.
    int best = -1;
    int bestDist = 999999;
    for (int m = lo; m <= hi; ++m) {
        if ((m % 12 + 12) % 12 != pc) continue;
        const int d = qAbs(m - around);
        if (d < bestDist) { bestDist = d; best = m; }
    }
    if (best >= 0) return best;
    // Fallback: fold
    int m = lo + ((pc - (lo % 12) + 1200) % 12);
    while (m < lo) m += 12;
    while (m > hi) m -= 12;
    return clampMidi(m);
}

int JazzBalladPianoPlanner::bestNearestToPrev(int pc, const QVector<int>& prev, int lo, int hi) {
    if (prev.isEmpty()) {
        const int around = (lo + hi) / 2;
        return nearestMidiForPc(pc, around, lo, hi);
    }
    // Choose around the closest previous note for continuity.
    int around = prev[0];
    int bestD = qAbs(prev[0] - (lo + hi) / 2);
    for (int m : prev) {
        const int d = qAbs(m - (lo + hi) / 2);
        if (d < bestD) { bestD = d; around = m; }
    }
    return nearestMidiForPc(pc, around, lo, hi);
}

void JazzBalladPianoPlanner::sortUnique(QVector<int>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

QVector<int> JazzBalladPianoPlanner::makeRootlessA(const music::ChordSymbol& c) {
    // Type A: 3-5-7-9 (rootless)
    return {pcForDegree(c, 3), pcForDegree(c, 5), pcForDegree(c, 7), pcForDegree(c, 9)};
}

QVector<int> JazzBalladPianoPlanner::makeRootlessB(const music::ChordSymbol& c) {
    // Type B: 7-9-3-5 (rootless)
    return {pcForDegree(c, 7), pcForDegree(c, 9), pcForDegree(c, 3), pcForDegree(c, 5)};
}

QVector<int> JazzBalladPianoPlanner::makeShell(const music::ChordSymbol& c) {
    // Shell: 3-7 (and optionally 9)
    return {pcForDegree(c, 3), pcForDegree(c, 7), pcForDegree(c, 9)};
}

bool JazzBalladPianoPlanner::feasible(const QVector<int>& midiNotes) const {
    virtuoso::constraints::CandidateGesture g;
    g.midiNotes = midiNotes;
    virtuoso::constraints::PerformanceState s;
    const auto r = m_driver.evaluateFeasibility(s, g);
    return r.ok;
}

QVector<int> JazzBalladPianoPlanner::repairToFeasible(QVector<int> midiNotes) const {
    sortUnique(midiNotes);
    // If too many notes or too wide, progressively reduce and fold octaves.
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (feasible(midiNotes)) return midiNotes;

        // Reduce polyphony first.
        if (midiNotes.size() > 4) midiNotes.resize(4);
        if (midiNotes.size() > 3) midiNotes.resize(3);

        // Fold the top note down an octave if span is too big.
        if (midiNotes.size() >= 2) {
            const int span = midiNotes.last() - midiNotes.first();
            if (span > m_driver.constraints().maxSpanSemitones) {
                midiNotes.last() -= 12;
                sortUnique(midiNotes);
                continue;
            }
        }

        // If still not feasible, drop the highest color tone.
        if (midiNotes.size() > 2) {
            midiNotes.removeLast();
            continue;
        }

        // Last resort: 2-note shell.
        return midiNotes;
    }
    return midiNotes;
}

QVector<virtuoso::engine::AgentIntentNote> JazzBalladPianoPlanner::planBeat(const Context& c,
                                                                           int midiChannel,
                                                                           const virtuoso::groove::TimeSignature& ts) {
    QVector<virtuoso::engine::AgentIntentNote> out;

    using virtuoso::groove::GrooveGrid;
    using virtuoso::groove::Rational;

    const bool climaxDense = c.forceClimax && (c.energy >= 0.75);

    // Deterministic hash for this beat.
    const quint32 h = quint32(qHash(QString("%1|%2|%3").arg(c.chordText).arg(c.playbackBarIndex).arg(c.determinismSeed)));
    const double progress01 = qBound(0.0, double(qMax(0, c.playbackBarIndex)) / 24.0, 1.0); // ~24 bars to reach "later in song"

    // Bar-coherent rhythmic planning: choose a small set of syncopated comp hits once per bar.
    ensureBarRhythmPlanned(c);
    QVector<CompHit> hitsThisBeat;
    hitsThisBeat.reserve(4);
    for (const auto& hit : m_barHits) {
        if (hit.beatInBar == c.beatInBar) hitsThisBeat.push_back(hit);
    }
    // Ensure we land on chord changes: if downbeat of a new chord has no scheduled hit, add one.
    if (hitsThisBeat.isEmpty() && !climaxDense) {
        if (c.chordIsNew && c.beatInBar == 0 && !c.userDensityHigh && !c.userIntensityPeak) {
            CompHit arrival;
            arrival.beatInBar = 0;
            arrival.sub = 0;
            arrival.count = 1;
            arrival.density = "guide";
            arrival.velDelta = +2;
            // Ring into the bar; long but not full smear.
            arrival.dur = (c.userSilence ? virtuoso::groove::Rational(3, 4) : virtuoso::groove::Rational(1, 2));
            arrival.rhythmTag = "forced_arrival1";
            hitsThisBeat.push_back(arrival);
        } else {
            return out;
        }
    }

    // Stage 3: candidate + cost-function selection (deterministic).
    const bool chordHas7 = (c.chord.extension >= 7 || c.chord.seventh != music::SeventhQuality::None);
    const bool dominant = (c.chord.quality == music::ChordQuality::Dominant);
    const bool alt = c.chord.alt || !c.chord.alterations.isEmpty();
    const bool spicy = (dominant
                        || c.chord.quality == music::ChordQuality::Diminished
                        || c.chord.quality == music::ChordQuality::HalfDiminished
                        || alt);
    const bool pickA = ((h % 2u) == 0u);

    // Build a ballad-appropriate set:
    // - Guide tones when chord has a 7th: 3 & 7
    // - Otherwise: 3 & 5 (avoid inventing a b7 on plain triads)
    // - Add 1–2 colors (9/13 on maj, 9/11 on min, altered tensions only if chart indicates)
    const int pc1 = (c.chord.rootPc >= 0) ? (c.chord.rootPc % 12) : 0;
    const int pc3 = pcForDegree(c.chord, 3);
    const int pc7 = chordHas7 ? pcForDegree(c.chord, 7) : -1;
    const int pc5 = pcForDegree(c.chord, 5);
    const int pc9 = pcForDegree(c.chord, 9);
    const int pc11 = pcForDegree(c.chord, 11);
    const int pc13 = pcForDegree(c.chord, 13);

    struct Cand {
        QString id;
        QString voicingType;
        QVector<int> pcs;
        bool prefersRootless = false;
    };
    auto mk = [&](QString id, QString vt, QVector<int> pcs, bool rootless = false) -> Cand {
        sortUnique(pcs);
        return {std::move(id), std::move(vt), std::move(pcs), rootless};
    };

    const bool allowRootless = chordHas7 && (c.harmonicRisk >= 0.35);
    const bool wantMoreColor = (c.harmonicRisk >= 0.45) || (progress01 >= 0.55);
    const bool allowQuartal = chordHas7 && (c.harmonicRisk >= 0.60);
    const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);

    QVector<Cand> candidates;
    candidates.reserve(8);

    // Shell/guide core
    QVector<int> guide;
    if (pc3 >= 0) guide.push_back(pc3);
    if (pc7 >= 0) guide.push_back(pc7);
    else if (pc5 >= 0) guide.push_back(pc5);
    candidates.push_back(mk("shell", chordHas7 ? "Ballad Shell (3-7)" : "Ballad Shell (3-5)", guide, false));

    // Sweet colors
    if (pc9 >= 0) candidates.push_back(mk("shell+9", "Ballad Shell + 9", guide + QVector<int>{pc9}, false));
    const bool isMinor = (c.chord.quality == music::ChordQuality::Minor);
    const int sweet2 = isMinor ? pc11 : pc13;
    if (sweet2 >= 0) candidates.push_back(mk("shell+sweet2", isMinor ? "Ballad Shell + 11" : "Ballad Shell + 13", guide + QVector<int>{sweet2}, false));
    if (wantMoreColor && pc9 >= 0 && sweet2 >= 0) candidates.push_back(mk("shell+2colors", "Ballad Shell + 2 colors", guide + QVector<int>{pc9, sweet2}, false));
    if (!c.toneDark && pc1 >= 0 && (c.harmonicRisk >= 0.55) && !spicy) candidates.push_back(mk("shell+root", "Ballad Shell + Root (open)", guide + QVector<int>{pc1}, false));

    // Rootless A/B
    if (allowRootless) {
        if (pc3 >= 0 && pc5 >= 0 && pc7 >= 0 && pc9 >= 0) {
            candidates.push_back(mk("rootlessA", "Ballad Rootless (A-ish)", QVector<int>{pc3, pc5, pc7, pc9}, true));
            candidates.push_back(mk("rootlessB", "Ballad Rootless (B-ish)", QVector<int>{pc7, pc9, pc3, pc5}, true));
        }
    }

    // Quartal flavor (beauty-first: only if safe and not too dark)
    if (allowQuartal && !c.toneDark) {
        QVector<int> qv;
        if (pc3 >= 0) qv.push_back(pc3);
        if (pc7 >= 0) qv.push_back(pc7);
        if (pc9 >= 0) qv.push_back(pc9);
        if (isMinor && pc11 >= 0 && c.harmonicRisk >= 0.70) qv.push_back(pc11);
        if (qv.size() >= 3) candidates.push_back(mk("quartal", "Ballad Quartal", qv, true));
    }

    // Bill-ish clustered colors / drop-ish stacks (procedural vocab).
    // Keep it tasteful: only when there is space and risk is moderate+.
    const bool allowBillVocab = chordHas7 && (c.harmonicRisk >= 0.45) && !userBusy;
    if (allowBillVocab) {
        // 6/9-ish cluster on majors (Evans-y) and 9/11 cluster on minors.
        if (!spicy && c.chord.quality == music::ChordQuality::Major) {
            if (pc3 >= 0 && pc7 >= 0 && pc9 >= 0) candidates.push_back(mk("evans_clusterA", "Evans Cluster (3-7-9)", QVector<int>{pc3, pc7, pc9}, true));
            if (pc3 >= 0 && pc7 >= 0 && pc13 >= 0) candidates.push_back(mk("evans_clusterB", "Evans Cluster (3-7-13)", QVector<int>{pc3, pc7, pc13}, true));
            if (pc3 >= 0 && pc7 >= 0 && pc9 >= 0 && pc13 >= 0 && (progress01 >= 0.45 || c.harmonicRisk >= 0.70))
                candidates.push_back(mk("evans_6_9", "Evans 6/9 (3-7-9-13)", QVector<int>{pc3, pc7, pc9, pc13}, true));
        }
        if (!spicy && c.chord.quality == music::ChordQuality::Minor) {
            if (pc3 >= 0 && pc7 >= 0 && pc9 >= 0) candidates.push_back(mk("evans_minA", "Evans Minor (3-7-9)", QVector<int>{pc3, pc7, pc9}, true));
            if (pc3 >= 0 && pc7 >= 0 && pc11 >= 0) candidates.push_back(mk("evans_minB", "Evans Minor (3-7-11)", QVector<int>{pc3, pc7, pc11}, true));
            if (pc3 >= 0 && pc7 >= 0 && pc9 >= 0 && pc11 >= 0 && (progress01 >= 0.45 || c.harmonicRisk >= 0.70))
                candidates.push_back(mk("evans_min_9_11", "Evans Minor (3-7-9-11)", QVector<int>{pc3, pc7, pc9, pc11}, true));
        }
        // Dominants: allow a slightly spicier upper-structure triad-ish stack, but still beauty-first.
        if (c.chord.quality == music::ChordQuality::Dominant && pc3 >= 0 && pc7 >= 0 && pc9 >= 0 && (c.harmonicRisk >= 0.65)) {
            candidates.push_back(mk("evans_domA", "Evans Dom (3-7-9)", QVector<int>{pc3, pc7, pc9}, true));
            if (pc13 >= 0 && (progress01 >= 0.55 || c.cadence01 >= 0.60))
                candidates.push_back(mk("evans_domB", "Evans Dom (3-7-9-13)", QVector<int>{pc3, pc7, pc9, pc13}, true));
        }
    }

    auto scoreCand = [&](const Cand& cand) -> double {
        double s = 0.0;
        // Prefer consistency within the 2-bar anchor unless chord is new.
        if (!m_anchorPcs.isEmpty() && !c.chordIsNew) {
            int common = 0;
            for (int pc : cand.pcs) if (m_anchorPcs.contains(pc)) ++common;
            const int total = qMax(1, cand.pcs.size());
            const double overlap = double(common) / double(total);
            s += (1.0 - overlap) * 1.4; // penalize big PC-set change
        }
        // Penalize thickness early / low risk.
        const int n = cand.pcs.size();
        const double target = 2.0 + 2.0 * c.harmonicRisk + 1.0 * progress01;
        s += 0.55 * qAbs(double(n) - target);
        // Penalize root-in-RH when dark
        if (cand.pcs.contains(pc1) && c.toneDark > 0.55) s += 0.8;
        // Penalize spicy colors on non-spicy chords at low risk
        if (!spicy && c.harmonicRisk < 0.45) {
            if (cand.pcs.contains(pc11) && !isMinor) s += 0.8; // natural 11 over major-ish is rough
        }
        // Interaction: if user is active, bias simpler (fewer pcs).
        if (!c.userSilence && (c.userDensityHigh || c.userIntensityPeak)) s += 0.35 * qMax(0, n - 2);
        // Small bias: shells early; richer later.
        if (progress01 < 0.25 && cand.prefersRootless) s += 0.4;
        if (progress01 > 0.60 && cand.id == "shell") s += 0.25;
        // Deterministic tie breaker
        s += (double(qHash(cand.id)) / double(std::numeric_limits<uint>::max())) * 1e-6;
        return s;
    };

    // Coherence: keep a stable pitch-class set across a 2-bar block (hand position),
    // refreshing at chord changes.
    const int blockStart = (qMax(0, c.playbackBarIndex) / 2) * 2;
    const bool anchorValid = (m_anchorBlockStartBar == blockStart && m_anchorChordText == c.chordText && !m_anchorPcs.isEmpty());
    const bool refreshAnchor = (!anchorValid) || c.chordIsNew;

    QString voicingType;
    QVector<int> pcs;
    if (refreshAnchor) {
        m_anchorBlockStartBar = blockStart;
        m_anchorChordText = c.chordText;

        const Cand* best = nullptr;
        double bestScore = 1e9;
        for (const auto& cand : candidates) {
            if (cand.pcs.isEmpty()) continue;
            const double sc = scoreCand(cand);
            if (sc < bestScore) { bestScore = sc; best = &cand; }
        }
        if (best) {
            m_anchorPcs = best->pcs;
            voicingType = best->voicingType;
        } else {
            m_anchorPcs = guide;
            voicingType = chordHas7 ? "Ballad Shell (3-7)" : "Ballad Shell (3-5)";
        }
    } else {
        pcs = m_anchorPcs;
        // Reconstruct the label from the stored pcs (best-effort); keeps old behavior if UI relies on text.
        voicingType = chordHas7 ? "Ballad Shell (3-7)" : "Ballad Shell (3-5)";
    }

    pcs = m_anchorPcs;

    const QVector<int> prevVoicing = m_lastVoicing;

    // Map to MIDI with LH/RH ranges and voice-leading to last voicing.
    // v2: assign stable voice indices (inner voices move like fingers, not re-sorted blocks).
    QVector<int> midi;
    midi.reserve(pcs.size());

    QVector<int> guides;
    QVector<int> colors;
    guides.reserve(pcs.size());
    colors.reserve(pcs.size());
    for (int pc : pcs) {
        if (pc < 0) continue;
        const bool isGuide = (pc == pc3 || (pc7 >= 0 && pc == pc7) || (pc7 < 0 && pc == pc5));
        (isGuide ? guides : colors).push_back(pc);
    }
    sortUnique(guides);
    sortUnique(colors);

    QVector<int> desired;
    desired.reserve(guides.size() + colors.size());
    for (int pc : guides) desired.push_back(pc);
    for (int pc : colors) desired.push_back(pc);

    QVector<int> prev = m_lastVoicing;
    std::sort(prev.begin(), prev.end());

    int lastChosen = -999;
    for (int i = 0; i < desired.size(); ++i) {
        const int pc = desired[i];
        const bool isGuide = guides.contains(pc);
        const int lo = isGuide ? c.lhLo : c.rhLo;
        const int hi = isGuide ? c.lhHi : c.rhHi;
        int around = (lo + hi) / 2;
        if (!prev.isEmpty()) {
            const int j = qBound(0, i, prev.size() - 1);
            around = prev[j];
        }
        int chosen = nearestMidiForPc(pc, around, lo, hi);
        if (chosen < 0) continue;
        // Keep voices ordered low->high; if collision, prefer octave shifts (deterministic).
        while (chosen <= (lastChosen + 2) && (chosen + 12) <= hi) chosen += 12;
        while (chosen <= (lastChosen + 2) && (chosen - 12) >= lo) chosen -= 12;
        lastChosen = chosen;
        midi.push_back(chosen);
    }

    midi = repairToFeasible(midi);
    if (midi.isEmpty()) return out;

    // Avoid playing the exact same voicing twice in a row (can feel procedural).
    // Deterministic: if identical, gently nudge one RH color tone by an octave (within range), or
    // swap in an alternate safe color if needed.
    if (!m_lastVoicing.isEmpty() && midi == m_lastVoicing) {
        const bool isSpicy = (dominant
                              || c.chord.quality == music::ChordQuality::Diminished
                              || c.chord.quality == music::ChordQuality::HalfDiminished
                              || alt);
        // Identify RH candidates (not guides) to move.
        QVector<int> rhIdx;
        rhIdx.reserve(midi.size());
        for (int i = 0; i < midi.size(); ++i) {
            const int m = midi[i];
            if (m > c.lhHi) rhIdx.push_back(i);
        }
        const int pick = rhIdx.isEmpty() ? -1 : rhIdx[int((h / 41u) % uint(rhIdx.size()))];
        bool changed = false;
        if (pick >= 0) {
            const int m = midi[pick];
            // Prefer moving by +12 if possible (brighten), else -12.
            if ((m + 12) <= c.rhHi) { midi[pick] = m + 12; changed = true; }
            else if ((m - 12) >= c.rhLo) { midi[pick] = m - 12; changed = true; }
        }
        if (!changed && !isSpicy) {
            // Swap a color degree (9<->13 for major-ish; 9<->11 for minor) if it exists in pcs.
            const int pc9 = pcForDegree(c.chord, 9);
            const int pc13 = pcForDegree(c.chord, 13);
            const int pc11 = pcForDegree(c.chord, 11);
            const bool isMinor = (c.chord.quality == music::ChordQuality::Minor);
            const int swapFrom = pc9;
            const int swapTo = isMinor ? pc11 : pc13;
            if (swapFrom >= 0 && swapTo >= 0) {
                const int fromPc = (swapFrom % 12 + 12) % 12;
                const int toPc = (swapTo % 12 + 12) % 12;
                for (int i = 0; i < midi.size(); ++i) {
                    const int m = midi[i];
                    if (m <= c.lhHi) continue;
                    if (((m % 12) + 12) % 12 != fromPc) continue;
                    const int around = m;
                    const int chosen = nearestMidiForPc(toPc, around, c.rhLo, c.rhHi);
                    if (chosen >= 0) { midi[i] = chosen; changed = true; break; }
                }
            }
        }
        if (changed) {
            sortUnique(midi);
            midi = repairToFeasible(midi);
        }
    }

    // Save for voice-leading.
    m_lastVoicing = midi;

    // voicingType chosen by solver (above)
    const int baseVel = climaxDense ? (c.chordIsNew ? 64 : 58) : (c.chordIsNew ? 50 : 44);

    // Optional high sparkle on beat 4 (very occasional), but only when there's rhythmic space.
    const int pSp = qBound(0, int(llround(c.sparkleProbBeat4 * 100.0)), 100);
    const bool sparkle = (c.beatInBar == 3) && c.userSilence && (pSp > 0) && (int((h / 13u) % 100u) < pSp);
    int sparklePc = -1;
    if (sparkle) {
        // Choose a tasteful color and place it high.
        // IMPORTANT: avoid dissonant natural 11 over major unless chart explicitly implies #11.
        int spPc = -1;
        if (c.chord.quality == music::ChordQuality::Major) {
            const int pc9 = pcForDegree(c.chord, 9);
            const int pc13 = pcForDegree(c.chord, 13);
            // Allow 11 only if explicitly altered (#11) in chart.
            bool hasSharp11 = false;
            for (const auto& a : c.chord.alterations) {
                if (a.degree == 11 && a.delta == 1) { hasSharp11 = true; break; }
            }
            const int pc11 = hasSharp11 ? pcForDegree(c.chord, 11) : -1;
            spPc = ((h % 3u) == 0u && pc13 >= 0) ? pc13 : (pc9 >= 0 ? pc9 : pc11);
        } else if (c.chord.quality == music::ChordQuality::Minor) {
            const int pc9 = pcForDegree(c.chord, 9);
            const int pc11 = pcForDegree(c.chord, 11);
            spPc = ((h % 3u) == 0u && pc11 >= 0) ? pc11 : pc9;
        } else if (c.chord.quality == music::ChordQuality::Dominant) {
            const int pc9 = pcForDegree(c.chord, 9);
            const int pc13 = pcForDegree(c.chord, 13);
            spPc = ((h % 3u) == 0u && pc13 >= 0) ? pc13 : pc9;
        } else {
            spPc = pcForDegree(c.chord, 9);
        }
        if (spPc >= 0) sparklePc = ((spPc % 12) + 12) % 12;
    }

    // Helper: derive a "guide tones only" subset from current voicing.
    auto guideSubset = [&](const QVector<int>& full) -> QVector<int> {
        QVector<int> g;
        g.reserve(2);
        for (int m : full) {
            const int pc = ((m % 12) + 12) % 12;
            if (pc == pc3 || (pc7 >= 0 && pc == pc7) || (pc7 < 0 && pc == pc5)) g.push_back(m);
        }
        sortUnique(g);
        if (g.size() > 2) g.resize(2);
        if (g.isEmpty() && !full.isEmpty()) g.push_back(full.first());
        return g;
    };

    auto renderHit = [&](const CompHit& hit) {
        QVector<int> notes = (hit.density.trimmed().toLower() == "guide") ? guideSubset(midi) : midi;
        if (notes.isEmpty()) return;

        // Session-player feel: treat pianist as two hands.
        // - LH anchors are earlier and can sustain
        // - RH colors/top can be lightly rolled/arpeggiated for floaty/dreamy feel
        std::sort(notes.begin(), notes.end());
        const int topNote = notes.last();
        const int lowNote = notes.first();

        const quint32 hr = quint32(qHash(QString("roll|%1|%2|%3|%4")
                                             .arg(c.chordText)
                                             .arg(c.playbackBarIndex)
                                             .arg(hit.beatInBar)
                                             .arg(c.determinismSeed)));
        const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
        const bool cadence = (c.cadence01 >= 0.55) || c.phraseEndBar;
        const bool arrival = c.chordIsNew || hit.rhythmTag.contains("arrival") || hit.rhythmTag.contains("forced_arrival1");
        // Rolling/arpeggiation library:
        // - Reduce overuse (esp. after adding strong touch/phrase behavior)
        // - Increase pattern variety
        // - Add deterministic anti-repeat across bars
        const double rc = qBound(0.0, c.rhythmicComplexity, 1.0);
        const int pRoll = qBound(0, int(llround(4.0 + 6.0 * c.energy + 14.0 * rc + 6.0 * progress01 + (cadence ? 6.0 : 0.0))), 45);
        const bool doRoll = !userBusy && ((c.energy >= 0.18) || (rc >= 0.32) || cadence) && (int(hr % 100u) < pRoll);
        const int pBigRoll = qBound(0, int(llround(1.0 + 2.0 * c.energy + 5.0 * rc + 2.0 * progress01 + (cadence ? 3.0 : 0.0))), 12);
        const bool bigRoll = doRoll && (int((hr / 97u) % 100u) < pBigRoll);

        // Arpeggiation should be rarer than rolling, and more likely only at cadences/arrivals.
        const int pArp = qBound(0, int(llround((cadence ? 18.0 : 8.0) + 16.0 * rc + (bigRoll ? 14.0 : 0.0))), 55);
        const bool doArp = doRoll && (notes.size() >= 3) && (hit.dur >= virtuoso::groove::Rational(1, 8)) && (int((hr / 13u) % 100u) < pArp);

        // Style selection (deterministic), with anti-repeat per bar.
        // 0=none/subtle roll, 1=up, 2=down, 3=inside-out, 4=outside-in, 5=skip, 6=triplet (cadence only)
        int style = 0;
        if (doArp) {
            const quint32 hs = quint32(qHash(QString("arpStyle|%1|%2|%3|%4")
                                                 .arg(c.chordText)
                                                 .arg(c.playbackBarIndex)
                                                 .arg(hit.beatInBar)
                                                 .arg(c.determinismSeed)));
            const int wantTrip = (cadence && (int((hs / 11u) % 100u) < 18)) ? 1 : 0;
            const int pool = wantTrip ? 6 : 5;
            style = 1 + int((hs % uint(pool)) % uint(pool)); // 1..pool
            if (m_lastArpBar == c.playbackBarIndex && style == m_lastArpStyle) {
                style = 1 + ((style) % pool); // rotate
            }
            m_lastArpBar = c.playbackBarIndex;
            m_lastArpStyle = style;
        } else if (doRoll) {
            style = 0; // subtle roll only
        }

        // Order helper (used for both velocity shaping and timing spread).
        auto orderForIdx = [&](int i) -> int {
            if (!doArp) return i;
            auto ordUp = [&](int j) { return j; };
            auto ordDown = [&](int j) { return (notes.size() - 1 - j); };
            auto ordInsideOut = [&](int j) {
                const int a = j / 2;
                return (j % 2 == 0) ? a : (notes.size() - 1 - a);
            };
            auto ordOutsideIn = [&](int j) {
                const int a = j / 2;
                return (j % 2 == 0) ? (notes.size() - 1 - a) : a;
            };
            auto ordSkip = [&](int j) {
                if (notes.size() < 4) return ordUp(j);
                static const int map4[4] = {0, 2, 1, 3};
                return (j < 4) ? map4[j] : j;
            };

            if (style == 1) return ordUp(i);
            if (style == 2) return ordDown(i);
            if (style == 3) return ordInsideOut(i);
            if (style == 4) return ordOutsideIn(i);
            if (style == 5) return ordSkip(i);
            if (style == 6) return ordUp(i);
            return ordUp(i);
        };

        auto to16thSub = [&](int sub, int count) -> int {
            if (count <= 1) return 0;
            if (count == 2) return qBound(0, sub * 2, 3);
            if (count == 4) return qBound(0, sub, 3);
            // generic mapping
            const int s = int(llround(double(sub) * 4.0 / double(count)));
            return qBound(0, s, 3);
        };
        const int base16 = to16thSub(hit.sub, hit.count);

        const bool isPedalWash = !userBusy && (cadence || arrival || (c.userSilence && c.energy <= 0.55));

        for (int idx = 0; idx < notes.size(); ++idx) {
            const int m = notes[idx];
            const bool isLow = (m == lowNote);
            const bool isTop = (m == topNote);
            const bool isRh = (m > c.lhHi); // heuristic split at LH range ceiling

            // Pedal-aware re-strike avoidance:
            // if we're in a "wash" state and the note was already held from the previous voicing,
            // avoid retriggering it on non-arrival hits (lets the harmony ring like pedal).
            if (isPedalWash && !arrival && !prevVoicing.isEmpty() && prevVoicing.contains(m)) {
                // Allow top voice to be re-articulated occasionally for singing line.
                if (!isTop && (isLow || !isRh || hit.dur >= virtuoso::groove::Rational(1, 4))) continue;
            }

            int vel = baseVel + hit.velDelta;
            // Touch model v2 (Bill-ish): top voice sings, inner voices disappear, LH is a soft bed.
            // Also keep cadences slightly warmer/rounder.
            if (isLow && !isRh) vel += 1;              // LH bed slightly present
            else if (isTop) vel += (cadence ? 10 : 8); // singing top
            else vel -= 8;                             // inner voices very soft
            if (isRh) vel += 1;
            if (c.toneDark >= 0.65) vel -= 2;
            // If arpeggiating upward, slightly crescendo; downward, slightly decrescendo.
            if (doArp) {
                const int order = orderForIdx(idx);
                // Many styles are effectively "up-ish" in time; keep a mild crescendo with order.
                vel += qMin(4, order);
            }
            vel = qBound(1, vel, 127);

            int beat = hit.beatInBar;
            int sub = hit.sub;
            int count = hit.count;

            // Intra-chord roll/arp: spread notes using a small pattern library.
            if (doRoll) {
                count = 4;
                int step = 0;
                if (!doArp) {
                    step = isLow ? -1 : (isTop ? +1 : 0); // subtle roll
                    sub = qBound(0, base16 + step, 3);
                } else {
                    const int order = orderForIdx(idx);

                    const int maxStep = bigRoll ? 3 : 2;
                    step = qBound(0, qMin(maxStep, order), 3);
                    sub = qBound(0, base16 + step, 3);

                    // Triplet option (cadence only): use count=3 and spread within the beat.
                    if (style == 6 && cadence) {
                        count = 3;
                        sub = qBound(0, qMin(2, order), 2);
                    }
                }
            }

            // Deterministic micro-spread even when not rolling:
            // - top voice slightly late
            // - LH bed slightly early cannot be represented; we keep it at the grid and rely on humanizer
            if (!doRoll) {
                // Remap current (sub,count) to 128th grid and add a tiny late offset for the top.
                const int base32 = to16thSub(sub, count) * 8; // 0..24
                if (isTop && isRh) {
                    count = 32;               // 128th-note grid
                    sub = qBound(0, base32 + (cadence ? 2 : 1), 31); // +1..2 = ~15–30ms at 60bpm
                } else if (!isLow && isRh) {
                    count = 32;
                    sub = qBound(0, base32 + 1, 31); // slight late for RH non-top
                }
            }

        virtuoso::engine::AgentIntentNote n;
        n.agent = "Piano";
        n.channel = midiChannel;
        n.note = clampMidi(m);
            n.baseVelocity = vel;
            n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, beat, sub, count, ts);
            // --- Pedal-ish performance model (session feel) ---
            // We don't have real CC64, but we can approximate:
            // - LH anchors ring longer (pedal bed)
            // - RH answers are shorter (clarity)
            // - Phrase cadences + chord arrivals ring more
            auto barDur = [&]() -> virtuoso::groove::Rational { return virtuoso::groove::Rational(ts.num, ts.den); };
            const bool cadence = (c.cadence01 >= 0.55) || c.phraseEndBar;
            const bool arrival = c.chordIsNew || hit.rhythmTag.contains("arrival") || hit.rhythmTag.contains("forced_arrival1");
            virtuoso::groove::Rational d = hit.dur;
            if (!userBusy) {
                if (isLow && !isRh) {
                    // LH pedal bed: longer, especially at cadences/arrivals.
                    d = qMax(d, virtuoso::groove::Rational(3, 8));
                    if (arrival) d = qMax(d, virtuoso::groove::Rational(1, 2));
                    if (cadence) d = qMax(d, barDur());
                    // Tiny overlap to avoid "MIDI choking".
                    d = d + virtuoso::groove::Rational(1, 16);
                } else if (isRh) {
                    // RH clarity: usually shorter, but let top voice sing at cadences.
                    if (!isTop) d = qMin(d, virtuoso::groove::Rational(1, 4));
                    if (cadence && isTop) d = qMax(d, virtuoso::groove::Rational(3, 8));
                    if (arrival && isTop) d = qMax(d, virtuoso::groove::Rational(1, 4));
                } else {
                    // Inner (near LH ceiling): medium.
                    d = qMax(d, virtuoso::groove::Rational(1, 4));
                }
            }
            // If RH is rolled late, shorten *inner* RH notes a bit so it doesn't smear (but keep top singing).
            const bool shorten = doRoll && isRh && !isLow && !isTop && (d <= virtuoso::groove::Rational(1, 4));
            n.durationWhole = shorten ? qMin(d, virtuoso::groove::Rational(3, 16)) : d;
        n.structural = c.chordIsNew;
        n.chord_context = c.chordText;
            n.voicing_type = voicingType + (doRoll ? (doArp ? " + Arpeggiated" : " + RolledHands") : "");
            n.logic_tag = hit.rhythmTag.isEmpty() ? "ballad_comp" : ("ballad_comp|" + hit.rhythmTag);
            n.target_note = isTop ? "Comp (top voice)" : (isLow ? "Comp (LH anchor)" : "Comp (inner)");
        out.push_back(n);
    }
    };

    for (const auto& hit : hitsThisBeat) renderHit(hit);

    // Phrase cadence anticipation: a soft guide-tone dyad on the and-of-4 into the next chord.
    // This is one of the quickest "session player" tells (setting up the turnaround).
    if (c.phraseEndBar && c.beatInBar == 3 && c.cadence01 >= 0.55 && c.hasNextChord && c.nextChanges &&
        !c.userDensityHigh && !c.userIntensityPeak) {
        const quint32 ha = quint32(qHash(QString("pno_cad_ant|%1|%2|%3")
                                             .arg(c.chordText)
                                             .arg(c.playbackBarIndex)
                                             .arg(c.determinismSeed)));
        // Keep it tasteful/rare-ish unless user is silent.
        const int p = qBound(0, int(llround(18.0 + 35.0 * c.cadence01 + (c.userSilence ? 18.0 : 0.0))), 75);
        if (int(ha % 100u) < p) {
            const int n3 = pcForDegree(c.nextChord, 3);
            const int n7 = pcForDegree(c.nextChord, 7);
            const int n5 = pcForDegree(c.nextChord, 5);
            const int topPc = (n7 >= 0) ? n7 : (n3 >= 0 ? n3 : n5);
            const int lowPc = (n3 >= 0 && n3 != topPc) ? n3 : (n5 >= 0 ? n5 : -1);
            const int lo = c.rhLo;
            const int hi = c.rhHi;
            const int around = (lo + hi) / 2;
            const int mTop = nearestMidiForPc(topPc, around + 7, lo, hi);
            const int mLow = (lowPc >= 0) ? nearestMidiForPc(lowPc, mTop - 4, lo, hi) : -1;
            const auto pos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, 3, /*sub*/1, /*count*/2, ts); // and-of-4
            const auto dur = virtuoso::groove::Rational(1, 8); // short pickup
            const int vel = qBound(1, baseVel - 16, 127);

            auto push = [&](int m, const QString& target) {
                if (m < 0) return;
        virtuoso::engine::AgentIntentNote n;
        n.agent = "Piano";
        n.channel = midiChannel;
                n.note = clampMidi(m);
                n.baseVelocity = vel;
                n.startPos = pos;
                n.durationWhole = dur;
        n.structural = false;
        n.chord_context = c.chordText;
                n.voicing_type = voicingType + " + CadenceAnticipation";
                n.logic_tag = "ballad_cadence_anticipation";
                n.target_note = target;
        out.push_back(n);
            };
            // Keep dyad consonant-ish: if low is too close, skip it.
            if (mLow >= 0 && qAbs(mTop - mLow) >= 3) push(mLow, "Cadence anticipation (low)");
            push(mTop, "Cadence anticipation (top)");
        }
    }

    // Cadence vocabulary (tiny licklet): a soft preparatory dyad on the and-of-3 before the barline.
    // This increases the "session player" sense of setup -> arrival without being a full lick library.
    if (c.phraseEndBar && c.beatInBar == 2 && c.cadence01 >= 0.70 && c.hasNextChord && c.nextChanges &&
        !c.userDensityHigh && !c.userIntensityPeak) {
        const quint32 hl = quint32(qHash(QString("pno_cad_lick|%1|%2|%3")
                                             .arg(c.chordText)
                                             .arg(c.playbackBarIndex)
                                             .arg(c.determinismSeed)));
        const int p = qBound(0, int(llround(16.0 + 34.0 * c.cadence01 + (c.userSilence ? 15.0 : 0.0))), 70);
        if (int(hl % 100u) < p) {
            // Target: next chord guide tones (3/7) or 9 if available; keep consonant dyad.
            const int n3 = pcForDegree(c.nextChord, 3);
            const int n7 = pcForDegree(c.nextChord, 7);
            const int n9 = pcForDegree(c.nextChord, 9);
            int topPc = (n9 >= 0 && int((hl / 3u) % 100u) < 55) ? n9 : (n7 >= 0 ? n7 : n3);
            int lowPc = (n3 >= 0 && n3 != topPc) ? n3 : (n7 >= 0 && n7 != topPc ? n7 : -1);
            const int lo = c.rhLo;
            const int hi = c.rhHi;
            const int around = (lo + hi) / 2 + 4;
            const int mTop = nearestMidiForPc(topPc, around, lo, hi);
            const int mLow = (lowPc >= 0) ? nearestMidiForPc(lowPc, mTop - 4, lo, hi) : -1;
            const auto pos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, 2, /*sub*/1, /*count*/2, ts); // and-of-3
            const auto dur = virtuoso::groove::Rational(1, 16);
            const int vel = qBound(1, baseVel - 18, 127);

            auto push = [&](int m, const QString& tgt) {
                if (m < 0) return;
                virtuoso::engine::AgentIntentNote n;
                n.agent = "Piano";
                n.channel = midiChannel;
                n.note = clampMidi(m);
                n.baseVelocity = vel;
                n.startPos = pos;
                n.durationWhole = dur;
                n.structural = false;
                n.chord_context = c.chordText;
                n.voicing_type = voicingType + " + CadenceLicklet";
                n.logic_tag = "ballad_cadence_licklet";
                n.target_note = tgt;
                out.push_back(n);
            };
            if (mLow >= 0 && qAbs(mTop - mLow) >= 3) push(mLow, "Cadence licklet (low)");
            push(mTop, "Cadence licklet (top)");
        }
    }

    // RH top-line motifs (2–3 note micro-phrases with tension->release).
    {
        const bool allowTop = !c.userDensityHigh && !c.userIntensityPeak;
        if (allowTop) {
            // Safe pitch-class pool for dyads (avoid stray dissonance).
            const int pc1 = (c.chord.rootPc >= 0) ? (c.chord.rootPc % 12) : 0;
            const int pc3 = pcForDegree(c.chord, 3);
            const int pc5 = pcForDegree(c.chord, 5);
            const int pc7 = pcForDegree(c.chord, 7);
            const int pc9 = pcForDegree(c.chord, 9);
            const int pc11 = pcForDegree(c.chord, 11);
            const int pc13 = pcForDegree(c.chord, 13);

            QSet<int> safe;
            auto addSafe = [&](int pc) { if (pc >= 0) safe.insert((pc % 12 + 12) % 12); };
            addSafe(pc1);
            addSafe(pc3);
            addSafe(pc5);
            if (pc7 >= 0) addSafe(pc7);
            if (c.chord.quality == music::ChordQuality::Major) {
                addSafe(pc9);
                addSafe(pc13);
                bool hasSharp11 = false;
                for (const auto& a : c.chord.alterations) {
                    if (a.degree == 11 && a.delta == 1) { hasSharp11 = true; break; }
                }
                if (hasSharp11) addSafe(pc11);
            } else if (c.chord.quality == music::ChordQuality::Minor) {
                addSafe(pc9);
                addSafe(pc11);
                if (c.chord.extension >= 13) addSafe(pc13);
            } else if (c.chord.quality == music::ChordQuality::Dominant) {
                addSafe(pc9);
                addSafe(pc13);
                for (const auto& a : c.chord.alterations) {
                    if (a.degree == 11) addSafe(pc11);
                }
            } else {
                addSafe(pc9);
            }

            // Pick a dyad partner that is consonant (avoid seconds + tritones).
            auto chooseDyadLowMidi = [&](int topPc, int mTop, int lo, int hi) -> int {
                const int t = ((topPc % 12) + 12) % 12;
                int bestMidi = -1;
                int bestScore = 999999;
                for (int pc : safe) {
                    if (pc == t) continue;
                    int m = nearestMidiForPc(pc, mTop - 5, lo, hi);
                    if (m < 0) continue;
                    while (m >= mTop && (m - 12) >= lo) m -= 12;
                    while ((mTop - m) > 12 && (m + 12) <= hi) m += 12;
                    if (m >= mTop) continue;
                    const int interval = mTop - m; // semitones
                    // Hard reject: seconds and tritone and major 7.
                    if (interval <= 2) continue;
                    if (interval == 6) continue;
                    if (interval >= 11) continue;
                    // Score preference: 3rds/6ths highest, then 5ths/4ths.
                    int s = 0;
                    if (interval == 3 || interval == 4) s += 0;
                    else if (interval == 8 || interval == 9) s += 1;
                    else if (interval == 7 || interval == 5) s += 3;
                    else if (interval == 10) s += 7;
                    else s += 12;
                    // Prefer the lower note not to be too low (keep dyads in a sweet mid-high zone).
                    const int mid = (lo + hi) / 2;
                    s += qAbs(mid - m) / 3;
                    if (s < bestScore) { bestScore = s; bestMidi = m; }
                }
                return bestMidi;
            };

            for (const auto& th : m_barTopHits) {
                if (th.beatInBar != c.beatInBar) continue;
                const int pc = th.pc;
                if (pc < 0) continue;
                // Range: mid->high (avoid only very top), but allow reaching sparkleHi occasionally.
                const int lo = c.rhLo;
                const int hi = c.sparkleHi;
                int around = (c.rhLo + c.rhHi) / 2 + 6;
                if (m_lastTopMidi >= 0) around = m_lastTopMidi;
                around = qBound(lo + 4, around, hi - 4);

                const int mTop = nearestMidiForPc(pc, around, lo, hi);
                if (mTop < 0) continue;

                // Prefer consonant dyads (no seconds/tritones).
                const int mLow = chooseDyadLowMidi(pc, mTop, lo, hi);

                if (th.resolve) m_lastTopMidi = mTop;

                const auto pos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, th.beatInBar, th.sub, th.count, ts);

                if (mLow >= 0) {
                    // Lower dyad tone
                    virtuoso::engine::AgentIntentNote a;
                    a.agent = "Piano";
                    a.channel = midiChannel;
                    a.note = clampMidi(mLow);
                    a.baseVelocity = qBound(1, baseVel - 16 + th.velDelta, 127);
                    a.startPos = pos;
                    a.durationWhole = th.dur;
                    a.structural = c.chordIsNew;
                    a.chord_context = c.chordText;
                    a.voicing_type = voicingType + " + TopDyad";
                    a.logic_tag = th.resolve ? ("ballad_topline|resolve|" + th.tag + "|dyad") : ("ballad_topline|tension|" + th.tag + "|dyad");
                    a.target_note = th.resolve ? "RH resolve (dyad low)" : "RH tension (dyad low)";
                    out.push_back(a);
                }

                // Upper dyad tone (always)
                virtuoso::engine::AgentIntentNote b;
                b.agent = "Piano";
                b.channel = midiChannel;
                b.note = clampMidi(mTop);
                b.baseVelocity = qBound(1, baseVel - 12 + th.velDelta + 3, 127);
                b.startPos = pos;
                b.durationWhole = th.dur;
                b.structural = c.chordIsNew;
                b.chord_context = c.chordText;
                b.voicing_type = voicingType + " + TopDyad";
                b.logic_tag = th.resolve ? ("ballad_topline|resolve|" + th.tag + "|dyad") : ("ballad_topline|tension|" + th.tag + "|dyad");
                b.target_note = th.resolve ? "RH resolve (dyad high)" : "RH tension (dyad high)";
                out.push_back(b);
            }
        }
    }

    if (sparklePc >= 0) {
        // Sparkle as a small harmony (dyad), and not only the very top register.
        const int lo = c.rhLo;
        const int hi = c.sparkleHi;
        int around = (c.rhHi + c.sparkleLo) / 2;
        if (m_lastTopMidi >= 0) around = qBound(c.rhHi - 4, m_lastTopMidi + 2, hi - 3);
        around = qBound(lo + 6, around, hi - 4);

        // Rebuild a minimal safe set for dyad partner selection.
        QSet<int> safe;
        auto addSafe = [&](int pc) { if (pc >= 0) safe.insert((pc % 12 + 12) % 12); };
        addSafe((c.chord.rootPc >= 0) ? (c.chord.rootPc % 12) : 0);
        addSafe(pcForDegree(c.chord, 3));
        addSafe(pcForDegree(c.chord, 5));
        addSafe(pcForDegree(c.chord, 7));
        addSafe(pcForDegree(c.chord, 9));
        addSafe(pcForDegree(c.chord, 13));
        if (c.chord.quality == music::ChordQuality::Minor) addSafe(pcForDegree(c.chord, 11));
        if (c.chord.quality == music::ChordQuality::Major) {
            for (const auto& a : c.chord.alterations) {
                if (a.degree == 11 && a.delta == 1) { addSafe(pcForDegree(c.chord, 11)); break; }
            }
        }

        auto chooseDyadLowMidi = [&](int topPc, int mTop, int lo, int hi) -> int {
            const int t = ((topPc % 12) + 12) % 12;
            int bestMidi = -1;
            int bestScore = 999999;
            for (int pc : safe) {
                if (pc == t) continue;
                int m = nearestMidiForPc(pc, mTop - 5, lo, hi);
                if (m < 0) continue;
                while (m >= mTop && (m - 12) >= lo) m -= 12;
                while ((mTop - m) > 12 && (m + 12) <= hi) m += 12;
                if (m >= mTop) continue;
                const int interval = mTop - m;
                if (interval <= 2) continue;
                if (interval == 6) continue;
                if (interval >= 11) continue;
                int s = 0;
                if (interval == 3 || interval == 4) s += 0;
                else if (interval == 8 || interval == 9) s += 1;
                else if (interval == 7 || interval == 5) s += 3;
                else if (interval == 10) s += 7;
                else s += 12;
                const int mid = (lo + hi) / 2;
                s += qAbs(mid - m) / 3;
                if (s < bestScore) { bestScore = s; bestMidi = m; }
            }
            return bestMidi;
        };

        const int mTop = nearestMidiForPc(sparklePc, around, lo, hi);
        const int mLow = chooseDyadLowMidi(sparklePc, mTop, lo, hi);

        const auto pos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 0, 1, ts);
        if (mLow >= 0) {
            virtuoso::engine::AgentIntentNote a;
            a.agent = "Piano";
            a.channel = midiChannel;
            a.note = clampMidi(mLow);
            a.baseVelocity = qBound(1, baseVel - 14, 127);
            a.startPos = pos;
            a.durationWhole = Rational(1, ts.den * 2);
            a.structural = false;
            a.chord_context = c.chordText;
            a.voicing_type = voicingType + " + SparkleDyad";
            a.logic_tag = "ballad_sparkle|dyad";
            a.target_note = "Sparkle (dyad low)";
            out.push_back(a);
        }
        virtuoso::engine::AgentIntentNote b;
        b.agent = "Piano";
        b.channel = midiChannel;
        b.note = clampMidi(mTop);
        b.baseVelocity = qBound(1, baseVel - 10, 127);
        b.startPos = pos;
        b.durationWhole = Rational(1, ts.den * 2);
        b.structural = false;
        b.chord_context = c.chordText;
        b.voicing_type = voicingType + " + SparkleDyad";
        b.logic_tag = "ballad_sparkle|dyad";
        b.target_note = "Sparkle (dyad high)";
        out.push_back(b);
    }

    // Upper-structure color cluster: add a second, very soft dyad/triad above the comp sometimes.
    // Beauty-first: keep it consonant (stacked 3rds/6ths) and only when there's space.
    // IMPORTANT: this should respond audibly to Virt sliders (harmonicRisk + toneDark), not just Energy.
    if (!c.userDensityHigh && !c.userIntensityPeak && (c.userSilence || c.harmonicRisk >= 0.55 || c.rhythmicComplexity >= 0.65)) {
        const quint32 hu = quint32(qHash(QString("pno_upper|%1|%2|%3").arg(c.chordText).arg(c.playbackBarIndex / 2).arg(c.determinismSeed)));
        const double progress01 = qBound(0.0, double(qMax(0, c.playbackBarIndex)) / 24.0, 1.0);
        // Start very subtle; open up later in the song.
        // Risk increases probability; dark tone decreases it.
        const double baseP =
            3.0
            + 18.0 * qBound(0.0, c.harmonicRisk, 1.0)
            + 8.0 * qBound(0.0, c.rhythmicComplexity, 1.0)
            + 8.0 * progress01
            + (c.userSilence ? 6.0 : 0.0)
            - 10.0 * qBound(0.0, c.toneDark, 1.0);
        const int p = qBound(0, int(llround(baseP)), 45);
        if (int(hu % 100u) < p) {
            // Candidate pitch-classes (sweet colors + guides)
            const int pc3 = pcForDegree(c.chord, 3);
            const int pc5 = pcForDegree(c.chord, 5);
            const int pc7 = pcForDegree(c.chord, 7);
            const int pc9 = pcForDegree(c.chord, 9);
            const int pc11 = pcForDegree(c.chord, 11);
            const int pc13 = pcForDegree(c.chord, 13);

            QSet<int> pool;
            auto addPc = [&](int pc) { if (pc >= 0) pool.insert((pc % 12 + 12) % 12); };
            addPc(pc3); addPc(pc5);
            if (pc7 >= 0) addPc(pc7);
            // colors by quality
            if (c.chord.quality == music::ChordQuality::Minor) { addPc(pc9); addPc(pc11); }
            else { addPc(pc9); addPc(pc13); }
            // Major: allow 11 only if #11 is explicitly present
            if (c.chord.quality == music::ChordQuality::Major) {
                for (const auto& a : c.chord.alterations) {
                    if (a.degree == 11 && a.delta == 1) { addPc(pc11); break; }
                }
            }

            auto chooseConsonantBelow = [&](int topPc, int mTop, int lo, int hi) -> int {
                const int t = ((topPc % 12) + 12) % 12;
                int bestMidi = -1;
                int bestScore = 999999;
                for (int pc : pool) {
                    if (pc == t) continue;
                    int m = nearestMidiForPc(pc, mTop - 5, lo, hi);
                    if (m < 0) continue;
                    while (m >= mTop && (m - 12) >= lo) m -= 12;
                    while ((mTop - m) > 12 && (m + 12) <= hi) m += 12;
                    if (m >= mTop) continue;
                    const int interval = mTop - m;
                    if (interval <= 2) continue;
                    if (interval == 6) continue;
                    if (interval >= 11) continue;
                    int s = 0;
                    if (interval == 3 || interval == 4) s += 0;
                    else if (interval == 8 || interval == 9) s += 1;
                    else if (interval == 7 || interval == 5) s += 3;
                    else if (interval == 10) s += 7;
                    else s += 12;
                    s += qAbs(((lo + hi) / 2) - m) / 3;
                    if (s < bestScore) { bestScore = s; bestMidi = m; }
                }
                return bestMidi;
            };

            // Place it mid-high, above the main RH comp but below sparkle top.
            const int lo = qMax(c.rhLo, c.rhHi - 8);
            const int hi = c.sparkleLo; // keep it out of the extreme top
            int around = (lo + hi) / 2;
            if (m_lastTopMidi >= 0) around = qBound(lo + 2, m_lastTopMidi + 2, hi - 2);

            // Choose top tone from pool deterministically
            QVector<int> vpool = pool.values().toVector();
            std::sort(vpool.begin(), vpool.end());
            if (!vpool.isEmpty()) {
                const int topPc = vpool[int((hu / 3u) % uint(vpool.size()))];
                const int mTop = nearestMidiForPc(topPc, around, lo, hi);
                const int mLow = chooseConsonantBelow(topPc, mTop, lo, hi);

                // Optional 3rd tone: stack another consonant 3rd/6th below for a triad feel.
                int mThird = -1;
                const bool wantTriad = (int((hu / 7u) % 100u) < qBound(0, int(llround(30.0 + 25.0 * c.energy)), 70));
                if (wantTriad && mLow >= 0) {
                    const int lowPc = ((mLow % 12) + 12) % 12;
                    mThird = chooseConsonantBelow(lowPc, mLow, lo, hi);
                }

                const auto pos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 0, 1, ts);
                const auto dur = Rational(3, 8); // ring a bit
                auto pushNote = [&](int m, int vel, QString target) {
                    if (m < 0) return;
        virtuoso::engine::AgentIntentNote n;
        n.agent = "Piano";
        n.channel = midiChannel;
                    n.note = clampMidi(m);
                    n.baseVelocity = qBound(1, vel, 127);
                    n.startPos = pos;
                    n.durationWhole = dur;
        n.structural = false;
        n.chord_context = c.chordText;
                    n.voicing_type = voicingType + " + UpperStructure";
                    n.logic_tag = "ballad_upper_structure";
                    n.target_note = std::move(target);
        out.push_back(n);
                };
                const int base = baseVel - 22;
                if (mThird >= 0) pushNote(mThird, base - 3, "UpperStructure (triad low)");
                if (mLow >= 0) pushNote(mLow, base, "UpperStructure (low)");
                pushNote(mTop, base + 4, "UpperStructure (top)");
            }
        }
    }

    // Silence-response fill: if user is silent, optionally add a tiny answering dyad on the upbeat of beat 4.
    if (c.userSilence && c.beatInBar == 3 && !m_lastVoicing.isEmpty()) {
        const quint32 hf = quint32(qHash(QString("%1|%2|%3|fill").arg(c.chordText).arg(c.playbackBarIndex).arg(c.determinismSeed)));
        if ((hf % 100u) < 18u) {
            // Tasteful answer as a small harmony (dyad), not a single ping.
            QSet<int> safe;
            auto addSafe = [&](int pc) { if (pc >= 0) safe.insert((pc % 12 + 12) % 12); };
            addSafe((c.chord.rootPc >= 0) ? (c.chord.rootPc % 12) : 0);
            addSafe(pcForDegree(c.chord, 3));
            addSafe(pcForDegree(c.chord, 5));
            addSafe(pcForDegree(c.chord, 7));
            addSafe(pcForDegree(c.chord, 9));
            addSafe(pcForDegree(c.chord, 13));
            if (c.chord.quality == music::ChordQuality::Minor) addSafe(pcForDegree(c.chord, 11));
            if (c.chord.quality == music::ChordQuality::Major) {
                for (const auto& a : c.chord.alterations) {
                    if (a.degree == 11 && a.delta == 1) { addSafe(pcForDegree(c.chord, 11)); break; }
                }
            }

            auto chooseDyadLowMidi = [&](int topPc, int mTop, int lo, int hi) -> int {
                const int t = ((topPc % 12) + 12) % 12;
                int bestMidi = -1;
                int bestScore = 999999;
                for (int pc : safe) {
                    if (pc == t) continue;
                    int m = nearestMidiForPc(pc, mTop - 5, lo, hi);
                    if (m < 0) continue;
                    while (m >= mTop && (m - 12) >= lo) m -= 12;
                    while ((mTop - m) > 12 && (m + 12) <= hi) m += 12;
                    if (m >= mTop) continue;
                    const int interval = mTop - m;
                    if (interval <= 2) continue;
                    if (interval == 6) continue;
                    if (interval >= 11) continue;
                    int s = 0;
                    if (interval == 3 || interval == 4) s += 0;
                    else if (interval == 8 || interval == 9) s += 1;
                    else if (interval == 7 || interval == 5) s += 3;
                    else if (interval == 10) s += 7;
                    else s += 12;
                    const int mid = (lo + hi) / 2;
                    s += qAbs(mid - m) / 3;
                    if (s < bestScore) { bestScore = s; bestMidi = m; }
                }
                return bestMidi;
            };

            const int pc9 = pcForDegree(c.chord, 9);
            const int around = m_lastVoicing.last();
            const int mTop = nearestMidiForPc(pc9, around + 7, c.rhLo, c.rhHi);
            const int mLow = chooseDyadLowMidi(pc9, mTop, c.rhLo, c.rhHi);

            const auto pos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, 3, /*sub*/1, /*count*/2, ts); // and-of-4
            if (mLow >= 0) {
                virtuoso::engine::AgentIntentNote a;
                a.agent = "Piano";
                a.channel = midiChannel;
                a.note = clampMidi(mLow);
                a.baseVelocity = qMax(1, baseVel - 18);
                a.startPos = pos;
                a.durationWhole = Rational(1, 8);
                a.structural = false;
                a.chord_context = c.chordText;
                a.voicing_type = voicingType + " + FillDyad";
                a.logic_tag = "ballad_silence_fill|dyad";
                a.target_note = "Answer fill (dyad low)";
                out.push_back(a);
            }
            virtuoso::engine::AgentIntentNote b;
            b.agent = "Piano";
            b.channel = midiChannel;
            b.note = clampMidi(mTop);
            b.baseVelocity = qMax(1, baseVel - 14);
            b.startPos = pos;
            b.durationWhole = Rational(1, 8);
            b.structural = false;
            b.chord_context = c.chordText;
            b.voicing_type = voicingType + " + FillDyad";
            b.logic_tag = "ballad_silence_fill|dyad";
            b.target_note = "Answer fill (dyad high)";
            out.push_back(b);
        }
    }
    return out;
}

void JazzBalladPianoPlanner::ensureBarRhythmPlanned(const Context& c) {
    const int bar = qMax(0, c.playbackBarIndex);
    if (bar == m_lastRhythmBar) return;
    m_lastRhythmBar = bar;
    m_barHits = chooseBarCompRhythm(c);
    ensureMotifBlockPlanned(c);
    m_barTopHits = chooseBarTopLine(c);
}

void JazzBalladPianoPlanner::ensureMotifBlockPlanned(const Context& c) {
    const int bar = qMax(0, c.playbackBarIndex);
    const int phraseStart = (bar / qMax(1, c.phraseBars)) * qMax(1, c.phraseBars);
    // We still keep the legacy 2-bar field around, but now we plan a full phrase (default 4 bars)
    // so RH lines can repeat/sequence coherently.
    if (phraseStart == m_phraseMotifStartBar) return;
    m_phraseMotifStartBar = phraseStart;
    m_motifBlockStartBar = phraseStart;
    buildMotifBlockTemplates(c);
}

void JazzBalladPianoPlanner::buildMotifBlockTemplates(const Context& c) {
    m_motifA.clear();
    m_motifB.clear();
    m_motifC.clear();
    m_motifD.clear();

    const int bar = qMax(0, c.playbackBarIndex);
    const int phraseBars = qMax(1, c.phraseBars);
    const int phraseStart = (bar / phraseBars) * phraseBars;
    const int phraseIndex = phraseStart / phraseBars;
    const quint32 h = quint32(qHash(QString("pno_motif4|%1|%2")
                                        .arg(phraseIndex)
                                        .arg(c.determinismSeed)));
    const double e = qBound(0.0, c.energy, 1.0);
    const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
    const bool allow = !userBusy && (c.userSilence || e >= 0.45);
    if (!allow) return;

    auto addT = [&](QVector<TopTemplateHit>& v,
                    int beat, int sub, int count,
                    virtuoso::groove::Rational dur,
                    int velDelta,
                    int degree,
                    int neighborDir,
                    bool resolve,
                    QString tag) {
        TopTemplateHit t;
        t.beatInBar = qBound(0, beat, 3);
        t.sub = qMax(0, sub);
        t.count = qMax(1, count);
        if (t.sub >= t.count) t.sub = t.count - 1;
        t.dur = dur;
        t.velDelta = velDelta;
        t.degree = degree;
        t.neighborDir = neighborDir;
        t.resolve = resolve;
        t.tag = std::move(tag);
        v.push_back(t);
    };

    // Choose a degree center for this phrase. (Degree resolution happens per-chord in realizeTopTemplate.)
    // We deliberately make this independent of the current chord quality so it can "carry" across changes.
    const int degPool[4] = {9, 13, 7, 3};
    const int deg = degPool[int(h % 4u)];
    const int degSeq = degPool[int((h / 5u) % 4u)];

    const int variant = int(h % 4u);
    const int dir = (((h / 7u) % 2u) == 0u) ? -1 : +1;

    // Bar A: statement (late, floaty)
    if (variant == 0) {
        addT(m_motifA, 2, 1, 2, {1, 16}, -16, deg, dir, false, "neighbor_and3");
        addT(m_motifA, 3, 0, 1, {1, 8}, -10, deg, 0, true, "resolve4");
    } else if (variant == 1) {
        addT(m_motifA, 2, 1, 2, {1, 16}, -18, deg, -dir, false, "enclosure_a");
        addT(m_motifA, 3, 0, 1, {1, 16}, -18, deg, +dir, false, "enclosure_b");
        addT(m_motifA, 3, 1, 2, {1, 8}, -10, deg, 0, true, "enclosure_resolve");
    } else if (variant == 2) {
        int deg2 = deg;
        if (deg == 9) deg2 = (c.chord.quality == music::ChordQuality::Major) ? 13 : (c.chord.quality == music::ChordQuality::Minor ? 11 : 13);
        addT(m_motifA, 3, 0, 1, {1, 16}, -14, deg, 0, false, "step_a");
        addT(m_motifA, 3, 1, 2, {1, 8}, -10, deg2, 0, true, "step_b");
    } else {
        addT(m_motifA, 3, 0, 1, {1, 8}, -12, deg, 0, true, "answer4");
    }

    // Bar B: echo/sequence (same contour, slightly earlier sometimes)
    if (!m_motifA.isEmpty()) {
        for (const auto& t : m_motifA) {
            TopTemplateHit u = t;
            if (int((h / 11u) % 100u) < qBound(0, int(llround(35.0 + 25.0 * e)), 80)) {
                u.beatInBar = qMax(0, u.beatInBar - 1);
                if (u.beatInBar == 0 && u.count == 2 && u.sub > 0) u.sub = 0;
            }
            u.velDelta = qBound(-24, u.velDelta - 2, 6);
            u.tag = "echo_" + u.tag;
            m_motifB.push_back(u);
        }
        // Sequence: sometimes swap resolution degree (9<->13, 9<->11) on bar B.
        if (int((h / 13u) % 100u) < 35) {
            for (auto& u : m_motifB) {
                if (!u.resolve) continue;
                if (c.chord.quality == music::ChordQuality::Minor) u.degree = (u.degree == 9) ? 11 : 9;
                else u.degree = (u.degree == 9) ? 13 : 9;
                u.tag += "_seq";
            }
        }
    }

    // Bars C/D: repeat/sequence for 4-bar storytelling.
    // C is a "repeat" with a different degree center; D is an answer with slightly stronger resolve.
    if (!m_motifA.isEmpty()) {
        for (const auto& t : m_motifA) {
            TopTemplateHit u = t;
            u.degree = (int((h / 9u) % 2u) == 0u) ? degSeq : deg;
            u.velDelta = qBound(-24, u.velDelta - 1, 6);
            u.tag = "repeat_" + u.tag;
            m_motifC.push_back(u);
        }
    }
    if (!m_motifB.isEmpty()) {
        for (const auto& t : m_motifB) {
            TopTemplateHit u = t;
            u.degree = (int((h / 11u) % 2u) == 0u) ? degSeq : deg;
            if (u.resolve) u.velDelta = qBound(-24, u.velDelta + 2, 10);
            u.tag = "answer_" + u.tag;
            m_motifD.push_back(u);
        }
    }
}

QVector<JazzBalladPianoPlanner::TopHit> JazzBalladPianoPlanner::realizeTopTemplate(
    const Context& c, const QVector<TopTemplateHit>& tmpl) const {
    QVector<TopHit> out;
    out.reserve(tmpl.size());

    const int pc1 = (c.chord.rootPc >= 0) ? (c.chord.rootPc % 12) : 0;
    const int pc3 = pcForDegree(c.chord, 3);
    const int pc5 = pcForDegree(c.chord, 5);
    const int pc7 = pcForDegree(c.chord, 7);
    const int pc9 = pcForDegree(c.chord, 9);
    const int pc11 = pcForDegree(c.chord, 11);
    const int pc13 = pcForDegree(c.chord, 13);

    const bool chordHas7 = (c.chord.extension >= 7 || c.chord.seventh != music::SeventhQuality::None);
    const bool spicy = (c.chord.quality == music::ChordQuality::Dominant
                        || c.chord.quality == music::ChordQuality::Diminished
                        || c.chord.quality == music::ChordQuality::HalfDiminished
                        || c.chord.alt
                        || !c.chord.alterations.isEmpty());

    QSet<int> safe;
    auto addSafe = [&](int pc) { if (pc >= 0) safe.insert((pc % 12 + 12) % 12); };
    // Beauty-first: avoid putting the root in RH color pool on stable non-spicy chords (it muddies dyads).
    if (spicy) addSafe(pc1);
    addSafe(pc3);
    addSafe(pc5);
    if (pc7 >= 0) addSafe(pc7);
    if (c.chord.quality == music::ChordQuality::Major) {
        addSafe(pc9);
        addSafe(pc13);
        bool hasSharp11 = false;
        for (const auto& a : c.chord.alterations) {
            if (a.degree == 11 && a.delta == 1) { hasSharp11 = true; break; }
        }
        if (hasSharp11) addSafe(pc11);
    } else if (c.chord.quality == music::ChordQuality::Minor) {
        addSafe(pc9);
        addSafe(pc11);
        if (c.chord.extension >= 13) addSafe(pc13);
    } else if (c.chord.quality == music::ChordQuality::Dominant) {
        addSafe(pc9);
        addSafe(pc13);
        for (const auto& a : c.chord.alterations) {
            if (a.degree == 11) addSafe(pc11);
        }
    } else {
        addSafe(pc9);
    }

    auto pickDegreePc = [&](int degree, bool isResolve) -> int {
        // Force clearer releases: resolve tones should land on guide/chord tones.
        if (isResolve) {
            const QVector<int> degs = chordHas7
                ? QVector<int>{3, 7, 5}
                : QVector<int>{3, 5};
            for (int d : degs) {
                int pc = pcForDegree(c.chord, d);
                if (pc >= 0) {
                    const int p = (pc % 12 + 12) % 12;
                    if (safe.contains(p)) return p;
                }
            }
            // fallback: whatever we were asked for
        }

        // Beauty-first: on non-spicy chords, keep tensions sweet and familiar (6/9 world).
        if (!spicy && !isResolve) {
            if (c.chord.quality == music::ChordQuality::Minor) {
                // 9/11 are ok in minor
                if (degree != 9 && degree != 11 && degree != 13) degree = 9;
            } else {
                // major-ish: prefer 9 or 13; avoid 11 unless explicitly #11 handled in safe pool
                if (degree != 9 && degree != 13) degree = 9;
            }
        }

        int pc = -1;
        switch (degree) {
            case 1: pc = pc1; break;
            case 3: pc = pc3; break;
            case 5: pc = pc5; break;
            case 7: pc = pc7; break;
            case 9: pc = pc9; break;
            case 11: pc = pc11; break;
            case 13: pc = pc13; break;
            default: pc = pc9; break;
        }
        if (pc < 0) pc = pc9;
        const int p = (pc % 12 + 12) % 12;
        if (safe.contains(p)) return p;
        // fallback to a safe color
        const int p9 = (pc9 % 12 + 12) % 12;
        const int p13 = (pc13 % 12 + 12) % 12;
        const int p3 = (pc3 % 12 + 12) % 12;
        if (safe.contains(p9)) return p9;
        if (safe.contains(p13)) return p13;
        if (safe.contains(p3)) return p3;
        return p;
    };

    // Guide-tone line bias (Bill-ish): on phrase cadences and chord changes, prefer guide tones
    // even if the template asks for a color degree, so the line "tells the harmony".
    const bool cadence = (c.cadence01 >= 0.55) || c.phraseEndBar;
    auto guideify = [&](int chosenPc, bool isResolve) -> int {
        if (isResolve) return chosenPc; // already forced to guides above
        if (!c.chordIsNew && !cadence) return chosenPc;
        const bool isColor = (chosenPc == ((pc9 % 12 + 12) % 12) ||
                              chosenPc == ((pc11 % 12 + 12) % 12) ||
                              chosenPc == ((pc13 % 12 + 12) % 12) ||
                              chosenPc == ((pc1 % 12 + 12) % 12));
        if (!isColor) return chosenPc;
        const int p7 = (pc7 % 12 + 12) % 12;
        const int p3 = (pc3 % 12 + 12) % 12;
        if (pc7 >= 0 && safe.contains(p7)) return p7;
        if (pc3 >= 0 && safe.contains(p3)) return p3;
        return chosenPc;
    };

    auto nearestOtherSafePc = [&](int aroundPc, int preferDir) -> int {
        int best = -1;
        int bestDist = 999;
        for (int pc : safe) {
            if (pc == aroundPc) continue;
            int d = pc - aroundPc;
            while (d > 6) d -= 12;
            while (d < -6) d += 12;
            const int dist = qAbs(d) * 10 + ((preferDir < 0 && d > 0) || (preferDir > 0 && d < 0) ? 3 : 0);
            if (dist < bestDist) { bestDist = dist; best = pc; }
        }
        return best;
    };

    for (const auto& t : tmpl) {
        const int targetPc = guideify(pickDegreePc(t.degree, t.resolve), t.resolve);
        int pc = targetPc;
        if (t.neighborDir != 0) {
            const int nb = nearestOtherSafePc(targetPc, t.neighborDir);
            if (nb >= 0) pc = nb;
        }
        TopHit o;
        o.beatInBar = t.beatInBar;
        o.sub = t.sub;
        o.count = t.count;
        o.dur = t.dur;
        o.velDelta = t.velDelta;
        o.pc = pc;
        o.resolve = t.resolve;
        o.tag = t.tag;
        out.push_back(o);
    }
    return out;
}

QVector<JazzBalladPianoPlanner::TopHit> JazzBalladPianoPlanner::chooseBarTopLine(const Context& c) const {
    const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
    const double e = qBound(0.0, c.energy, 1.0);
    if (userBusy) return {};
    // As the song progresses and on chord changes, allow more RH melodic motion.
    const double progress01 = qBound(0.0, double(qMax(0, c.playbackBarIndex)) / 24.0, 1.0);
    if (!c.userSilence && !c.chordIsNew && (e < (0.40 - 0.10 * progress01))) return {};
    const int bar = qMax(0, c.playbackBarIndex);
    const int phraseBars = qMax(1, c.phraseBars);
    const int barInPhrase = (phraseBars > 0) ? (bar % phraseBars) : (bar % 4);
    // Default 4-bar storytelling, but works for any phraseBars >= 1 by mapping into our 4 templates.
    const int idx = qBound(0, (phraseBars >= 4) ? barInPhrase : (barInPhrase % 4), 3);
    const QVector<TopTemplateHit>* tmpl = nullptr;
    if (idx == 0) tmpl = &m_motifA;
    else if (idx == 1) tmpl = &m_motifB;
    else if (idx == 2) tmpl = &m_motifC;
    else tmpl = &m_motifD;
    return (tmpl && !tmpl->isEmpty()) ? realizeTopTemplate(c, *tmpl) : QVector<TopHit>{};
}

QVector<JazzBalladPianoPlanner::CompHit> JazzBalladPianoPlanner::chooseBarCompRhythm(const Context& c) const {
    QVector<CompHit> out;
    out.reserve(6);

    // Coherence: keep the comping feel stable for 2-bar blocks (less "random new pattern every bar").
    const int barBlock = qMax(0, c.playbackBarIndex) / 2;
    const quint32 h = quint32(qHash(QString("pno_rhy|%1|%2|%3").arg(c.chordText).arg(barBlock).arg(c.determinismSeed)));
    const double e = qBound(0.0, c.energy, 1.0);
    const double progress01 = qBound(0.0, double(qMax(0, c.playbackBarIndex)) / 24.0, 1.0);
    const bool climax = c.forceClimax && (e >= 0.75);
    const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
    const bool phraseEnd = ((qMax(1, c.playbackBarIndex) % 4) == 3);

    // Complexity driver:
    // - previously was mostly energy-driven
    // - Stage 3: rhythmicComplexity should be the primary dial (so Virt slider is audible)
    double complexity = qBound(0.0, c.rhythmicComplexity, 1.0);
    if (userBusy) complexity *= 0.55;
    if (c.userSilence) complexity = qMin(1.0, complexity + 0.10);
    // As the song progresses, we want a gentle density ramp (starts sparse, becomes a bit fuller).
    complexity = qMax(complexity, 0.25 + 0.45 * progress01);

    auto add = [&](int beat, int sub, int count, virtuoso::groove::Rational dur, int velDelta, QString density, QString tag) {
        CompHit hit;
        hit.beatInBar = qBound(0, beat, 3);
        hit.sub = qMax(0, sub);
        hit.count = qMax(1, count);
        hit.dur = dur;
        hit.velDelta = velDelta;
        hit.density = density;
        hit.rhythmTag = std::move(tag);
        out.push_back(hit);
    };

    // Sustain shaping: session players vary holds (stabs vs pads) depending on space/energy.
    // Deterministic per bar, so lookahead + audition always match.
    const int holdRoll = int((h / 29u) % 100u); // 0..99
    auto shortDur = [&]() -> virtuoso::groove::Rational { return userBusy ? virtuoso::groove::Rational(1, 16) : virtuoso::groove::Rational(1, 8); };
    auto medDur = [&]() -> virtuoso::groove::Rational { return virtuoso::groove::Rational(1, 4); };  // 1 beat
    auto longDur = [&]() -> virtuoso::groove::Rational { return virtuoso::groove::Rational(3, 8); }; // dotted quarter (~1.5 beats)
    auto padDur = [&]() -> virtuoso::groove::Rational { return virtuoso::groove::Rational(1, 2); };  // 2 beats

    auto chooseHold = [&](const QString& tag, bool canPad, bool canLong) -> virtuoso::groove::Rational {
        // Base: short stabs when user is busy; otherwise medium.
        virtuoso::groove::Rational d = userBusy ? shortDur() : medDur();
        // Let chords ring more in ballads, especially when user is silent.
        const bool isDownbeatLike = tag.contains("arrival") || tag.contains("touch1") || tag.contains("charleston_1");
        auto barDur = [&]() -> virtuoso::groove::Rational { return virtuoso::groove::Rational(3, 4); }; // ~3 beats
        const double progress01 = qBound(0.0, double(qMax(0, c.playbackBarIndex)) / 24.0, 1.0);
        const bool space = (!userBusy) && (c.userSilence || e <= (0.50 - 0.12 * progress01));
        // Phrase cadence: at phrase ends, let arrivals/pads ring longer (session "breath").
        const bool cadence = (c.cadence01 >= 0.55) || c.phraseEndBar;
        if (canLong && space && (holdRoll < (c.userSilence ? 55 : 35))) d = longDur();
        if (canPad && space && (holdRoll < (c.userSilence ? 40 : 22))) d = padDur();
        if (isDownbeatLike && canPad && space && (holdRoll < (c.userSilence ? 18 : 10))) d = barDur();
        if (cadence && canPad && space && isDownbeatLike && (holdRoll < 55)) d = barDur();
        // In higher energy, shorten slightly (more motion); in very low energy, lengthen slightly.
        if (!userBusy && e >= 0.65 && d == medDur() && (holdRoll < 35)) d = shortDur();
        if (!userBusy && e <= 0.30 && d == medDur() && (holdRoll < 35)) d = longDur();
        return d;
    };

    // --- Base feel: jazz ballad comping is sparse but syncopated. ---
    // Stage 3: choose a bar-coherent rhythm via candidate scoring (still deterministic).
    if (climax) {
        // Denser, but still musical: quarters + occasional upbeat pushes.
        add(0, 0, 1, chooseHold("climax_q1", /*canPad*/false, /*canLong*/false), +4, "full", "climax_q1");
        add(1, ((h / 3u) % 2u) ? 1 : 0, 2, shortDur(), 0, "guide", "climax_push2");
        add(2, 0, 1, chooseHold("climax_q3", /*canPad*/false, /*canLong*/false), +2, "full", "climax_q3");
        add(3, 1, 2, shortDur(), -6, "guide", phraseEnd ? "climax_push4_end" : "climax_push4");
        return out;
    }

    auto buildBase = [&](int variant) -> QVector<CompHit> {
        QVector<CompHit> v;
        v.reserve(6);
        auto addV = [&](int beat, int sub, int count, virtuoso::groove::Rational dur, int velDelta, QString density, QString tag) {
            CompHit hit;
            hit.beatInBar = qBound(0, beat, 3);
            hit.sub = qMax(0, sub);
            hit.count = qMax(1, count);
            hit.dur = dur;
            hit.velDelta = velDelta;
            hit.density = density;
            hit.rhythmTag = std::move(tag);
            v.push_back(hit);
        };

        switch (variant) {
            case 6: // Arrival on 1 + 4 (good for chord changes)
                addV(0, 0, 1, chooseHold("arrival1", /*canPad*/true, /*canLong*/true), -2, "guide", "arrival1");
                addV(3, 0, 1, chooseHold("backbeat4_short", /*canPad*/true, /*canLong*/true), 0, "full", phraseEnd ? "only4_end" : "only4");
                break;
            case 0: // Classic 2&4
                addV(1, 0, 1, chooseHold("backbeat2", /*canPad*/false, /*canLong*/true), -2, "guide", "backbeat2");
                addV(3, 0, 1, chooseHold("backbeat4_short", /*canPad*/true, /*canLong*/true), 0, "full", "backbeat4_short");
                break;
            case 1: // Delayed 2 + 4
                addV(1, 1, 2, chooseHold("delay2", /*canPad*/false, /*canLong*/true), -4, "guide", "delay2");
                addV(3, 0, 1, chooseHold("backbeat4_short", /*canPad*/true, /*canLong*/true), 0, "full", "backbeat4_short");
                break;
            case 2: // Charleston-ish: beat 1 + and-of-2
                if (!c.userSilence && !c.chordIsNew && (int((h / 11u) % 100u) < int(llround(c.skipBeat2ProbStable * 100.0)))) {
                    addV(1, 1, 2, shortDur(), -4, "guide", "charleston_push_only");
                } else {
                    addV(0, 0, 1, chooseHold("charleston_1", /*canPad*/false, /*canLong*/true), -6, "guide", "charleston_1");
                    addV(1, 1, 2, chooseHold("charleston_and2", /*canPad*/true, /*canLong*/true), 0, "full", "charleston_and2");
                }
                break;
            case 3: // Anticipate 4 (and-of-4) + maybe 2
                if (complexity > 0.35) addV(1, 0, 1, shortDur(), -2, "guide", "light2");
                addV(3, 1, 2, chooseHold("push4", /*canPad*/true, /*canLong*/true), -6, "full", phraseEnd ? "push4_end" : "push4");
                break;
            case 4: // 2 only
                addV(1, 0, 1, chooseHold("only2", /*canPad*/true, /*canLong*/true), -4, "guide", "only2");
                break;
            case 5: // 4 only (+ tiny answer)
                addV(3, 0, 1, chooseHold("only4", /*canPad*/true, /*canLong*/true), -4, "full", "only4");
                if (c.userSilence && complexity > 0.30) addV(2, 1, 2, virtuoso::groove::Rational(1, 16), -10, "guide", "answer_and3");
                break;
            default: // and2 + and4
                addV(1, 1, 2, shortDur(), -6, "guide", "and2");
                addV(3, 1, 2, chooseHold("and4", /*canPad*/true, /*canLong*/true), -8, "full", phraseEnd ? "and4_end" : "and4");
                break;
        }
        return v;
    };

    auto scoreBase = [&](const QVector<CompHit>& v, int variant) -> double {
        double s = 0.0;
        const int nHits = v.size();
        int nOff = 0;
        bool hasBeat0 = false;
        bool hasBeat1 = false;
        for (const auto& hit : v) {
            if (hit.beatInBar == 0) hasBeat0 = true;
            if (hit.beatInBar == 1) hasBeat1 = true;
            if (hit.sub != 0) nOff++;
        }
        const double targetHits = qBound(1.0, 1.2 + 2.2 * c.rhythmicComplexity + 1.2 * progress01, 4.0);
        s += 0.8 * qAbs(double(nHits) - targetHits);
        // Land on chord changes: if chord is new, prefer beat-1 arrivals.
        if (c.chordIsNew && !hasBeat0) s += 1.2;
        // Phrase cadence: strongly prefer an end-of-bar push (or at least beat 4) to set up the turnaround.
        if (c.cadence01 >= 0.55 || c.phraseEndBar) {
            const bool hasBeat3 = std::any_of(v.begin(), v.end(), [](const CompHit& x) { return x.beatInBar == 3; });
            const bool hasAnd4 = std::any_of(v.begin(), v.end(), [](const CompHit& x) { return x.beatInBar == 3 && x.sub != 0; });
            if (!hasBeat3) s += 1.0;
            if (c.nextChanges && !hasAnd4) s += 0.7;
        }
        // Dark tone wants fewer offbeats.
        s += 0.35 * c.toneDark * double(nOff);
        // Interaction: if user isn't silent and interaction is low, keep sparser.
        if (!c.userSilence && c.interaction < 0.40) s += 0.25 * double(nHits);
        // Avoid super-sparse later.
        if (!userBusy && progress01 >= 0.55 && (variant == 4 || variant == 5)) s += 0.9;
        // If user is busy, strongly prefer patterns that include beat 4 only.
        if (userBusy) s += 0.6 * double(nHits);
        // Prefer having a beat-2 component as song progresses.
        if (!hasBeat1 && progress01 >= 0.60) s += 0.35;
        // Deterministic tie breaker
        s += (double((h ^ quint32(variant * 131u)) & 1023u) / 1024.0) * 1e-6;
        return s;
    };

    int bestVar = 0;
    double bestScore = 1e9;
    QVector<CompHit> bestBase;
    for (int v = 0; v <= 6; ++v) {
        const QVector<CompHit> base = buildBase(v);
        const double sc = scoreBase(base, v);
        if (sc < bestScore) { bestScore = sc; bestVar = v; bestBase = base; }
    }
    out = bestBase;

    // Chord arrivals: sometimes hit on beat 1 (or and-of-1) so we don't always avoid the downbeat.
    // This is especially important for chart changes and keeps it from sounding "student-ish".
    if (c.chordIsNew && !userBusy) {
        const int pArr = qBound(0, int(llround(18.0 + 30.0 * complexity)), 60);
        if (int((h / 5u) % 100u) < pArr) {
            const bool onBeat = int((h / 7u) % 100u) < 70;
            add(0, onBeat ? 0 : 1, onBeat ? 1 : 2,
                chooseHold("arrival", /*canPad*/true, /*canLong*/true),
                +1, "guide", onBeat ? "arrival1" : "arrival_and1");
        }
    }

    // Also allow a *very soft* beat-1 touch occasionally even when harmony is stable,
    // to avoid feeling like comping is "missing the downbeat" forever.
    if (!c.chordIsNew && !userBusy && complexity > 0.40) {
        const int pTouch = qBound(0, int(llround(6.0 + 18.0 * complexity)), 28);
        if (int((h / 31u) % 100u) < pTouch) {
            add(0, 0, 1, chooseHold("touch1", /*canPad*/false, /*canLong*/true), -8, "guide", "touch1");
        }
    }

    // Occasional extra jab (and-of-1 or and-of-3) when there is space and energy.
    // Keep it rare; we're aiming for beauty over busy-ness.
    if (!userBusy && complexity > 0.65 && (int((h / 17u) % 100u) < 10)) {
        const bool on1 = ((h / 19u) % 2u) == 0u;
        add(on1 ? 0 : 2, 1, 2, virtuoso::groove::Rational(1, 16), -10, "guide", on1 ? "jab_and1" : "jab_and3");
    }

    // Later-song gentle fill-in: add a light extra backbeat if we're too sparse.
    if (!userBusy && progress01 >= 0.55 && complexity > 0.55) {
        const int pFill = qBound(0, int(llround(10.0 + 18.0 * progress01)), 30);
        if (int((h / 43u) % 100u) < pFill) {
            const bool alreadyHas2 = std::any_of(out.begin(), out.end(), [](const CompHit& x) { return x.beatInBar == 1; });
            if (!alreadyHas2) add(1, 0, 1, chooseHold("late_fill2", /*canPad*/false, /*canLong*/true), -6, "guide", "late_fill2");
        }
    }

    // Phrase end: slightly more likely to add a light push into next bar.
    if (phraseEnd && !userBusy && (int((h / 23u) % 100u) < 28)) {
        add(3, 1, 2, chooseHold("phrase_end_push", /*canPad*/true, /*canLong*/true), -8, "guide", "phrase_end_push");
    }

    // If user is very active, bias to single backbeat (beat 4) to stay out of the way.
    if (userBusy) {
        QVector<CompHit> filtered;
        for (const auto& hit : out) {
            if (hit.beatInBar == 3 && hit.sub == 0) filtered.push_back(hit);
        }
        if (!filtered.isEmpty()) return filtered;
        // fallback: any beat-4 event
        for (const auto& hit : out) {
            if (hit.beatInBar == 3) filtered.push_back(hit);
        }
        if (!filtered.isEmpty()) return filtered;
    }

    return out;
}

} // namespace playback

