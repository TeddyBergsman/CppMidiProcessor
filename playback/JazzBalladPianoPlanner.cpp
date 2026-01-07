#include "playback/JazzBalladPianoPlanner.h"

#include "virtuoso/util/StableHash.h"
#include "virtuoso/solver/CspSolver.h"

#include <QtGlobal>
#include <algorithm>
#include <limits>

namespace playback {
namespace {
static int clampMidi(int m) { return (m < 0) ? 0 : (m > 127 ? 127 : m); }
} // namespace

JazzBalladPianoPlanner::JazzBalladPianoPlanner() {
    reset();
}

void JazzBalladPianoPlanner::reset() {
    m_lastVoicing.clear();
    m_state.heldNotes.clear();
    m_state.ints.insert("cc64", 0);
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
    m_anchorLhPcs.clear();
    m_anchorRhPcs.clear();
    m_lastArpBar = -1;
    m_lastArpStyle = -1;
    m_phraseGuideStartBar = -1;
    m_phraseGuideBars = 4;
    m_phraseGuidePcByBar.clear();
    m_lastUpperBar = -1;
    m_lastUpperPcs.clear();
}

JazzBalladPianoPlanner::PlannerState JazzBalladPianoPlanner::snapshotState() const {
    PlannerState s;
    s.lastVoicing = m_lastVoicing;
    s.perf = m_state;
    s.lastRhythmBar = m_lastRhythmBar;
    s.barHits = m_barHits;
    s.lastTopMidi = m_lastTopMidi;
    s.barTopHits = m_barTopHits;
    s.phraseGuideStartBar = m_phraseGuideStartBar;
    s.phraseGuideBars = m_phraseGuideBars;
    s.phraseGuidePcByBar = m_phraseGuidePcByBar;
    s.lastUpperBar = m_lastUpperBar;
    s.lastUpperPcs = m_lastUpperPcs;
    s.motifBlockStartBar = m_motifBlockStartBar;
    s.motifA = m_motifA;
    s.motifB = m_motifB;
    s.motifC = m_motifC;
    s.motifD = m_motifD;
    s.phraseMotifStartBar = m_phraseMotifStartBar;
    s.anchorBlockStartBar = m_anchorBlockStartBar;
    s.anchorChordText = m_anchorChordText;
    s.anchorVoicingKey = m_anchorVoicingKey;
    s.anchorVoicingName = m_anchorVoicingName;
    s.anchorCspTag = m_anchorCspTag;
    s.anchorPcs = m_anchorPcs;
    s.anchorLhPcs = m_anchorLhPcs;
    s.anchorRhPcs = m_anchorRhPcs;
    s.lastArpBar = m_lastArpBar;
    s.lastArpStyle = m_lastArpStyle;
    return s;
}

void JazzBalladPianoPlanner::restoreState(const PlannerState& s) {
    m_lastVoicing = s.lastVoicing;
    m_state = s.perf;
    m_lastRhythmBar = s.lastRhythmBar;
    m_barHits = s.barHits;
    m_lastTopMidi = s.lastTopMidi;
    m_barTopHits = s.barTopHits;
    m_phraseGuideStartBar = s.phraseGuideStartBar;
    m_phraseGuideBars = s.phraseGuideBars;
    m_phraseGuidePcByBar = s.phraseGuidePcByBar;
    m_lastUpperBar = s.lastUpperBar;
    m_lastUpperPcs = s.lastUpperPcs;
    m_motifBlockStartBar = s.motifBlockStartBar;
    m_motifA = s.motifA;
    m_motifB = s.motifB;
    m_motifC = s.motifC;
    m_motifD = s.motifD;
    m_phraseMotifStartBar = s.phraseMotifStartBar;
    m_anchorBlockStartBar = s.anchorBlockStartBar;
    m_anchorChordText = s.anchorChordText;
    m_anchorVoicingKey = s.anchorVoicingKey;
    m_anchorVoicingName = s.anchorVoicingName;
    m_anchorCspTag = s.anchorCspTag;
    m_anchorPcs = s.anchorPcs;
    m_anchorLhPcs = s.anchorLhPcs;
    m_anchorRhPcs = s.anchorRhPcs;
    m_lastArpBar = s.lastArpBar;
    m_lastArpStyle = s.lastArpStyle;
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
    return planBeatWithActions(c, midiChannel, ts).notes;
}

JazzBalladPianoPlanner::BeatPlan JazzBalladPianoPlanner::planBeatWithActions(const Context& c,
                                                                             int midiChannel,
                                                                             const virtuoso::groove::TimeSignature& ts) {
    BeatPlan plan;
    auto& out = plan.notes;

    using virtuoso::groove::GrooveGrid;
    using virtuoso::groove::Rational;

    const bool climaxDense = c.forceClimax && (c.energy >= 0.75);

    // Deterministic hash for this beat.
    const quint32 h = virtuoso::util::StableHash::fnv1a32(QString("%1|%2|%3")
                                                              .arg(c.chordText)
                                                              .arg(c.playbackBarIndex)
                                                              .arg(c.determinismSeed)
                                                              .toUtf8());
    const double progress01 = qBound(0.0, double(qMax(0, c.playbackBarIndex)) / 24.0, 1.0); // ~24 bars to reach "later in song"

    // Bar-coherent rhythmic planning: choose a small set of syncopated comp hits once per bar.
    ensureBarRhythmPlanned(c);
    QVector<CompHit> hitsThisBeat;
    hitsThisBeat.reserve(4);
    for (const auto& hit : m_barHits) {
        if (hit.beatInBar == c.beatInBar) hitsThisBeat.push_back(hit);
    }

    // --- PerformanceAction v1: sustain pedal (CC64) planning ---
    // We model pedaling as an action with constraint costs (wash / over-sustain) using PianoDriver.
    // This gives phrase-level realism without forcing fixed register lanes.
    const bool phraseLandmark = c.chordIsNew || (c.cadence01 >= 0.55) || c.phraseEndBar;
    const bool chordBoundarySoon = c.nextChanges && (c.beatsUntilChordChange <= 1);
    // IMPORTANT: do NOT gate pedal decisions on comp hits on *this beat*.
    // Ballad comping often places hits on offbeats; if we gate on hitsThisBeat,
    // we may never engage sustain at all.
    const bool considerPedalNow = (c.beatInBar == 0) || phraseLandmark || chordBoundarySoon;
    if (considerPedalNow) {
        const int cc64Now = m_state.ints.value("cc64", 0);
        const bool sustainNow = (cc64Now >= 64);

        // Heuristic "want sustain": ballads generally pedal unless the user is busy or harmony is about to change.
        // IMPORTANT: bias toward using sustain; otherwise "keep pedal up" dominates every time.
        const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
        const double wantDownScore =
            (0.90 * qBound(0.0, c.toneDark, 1.0)) +
            (0.55 * (c.userSilence ? 1.0 : 0.0)) +
            (0.35 * (1.0 - qBound(0.0, c.energy, 1.0))) -
            (1.10 * (userBusy ? 1.0 : 0.0)) -
            (0.75 * (chordBoundarySoon ? 1.0 : 0.0));
        const bool preferDown = (wantDownScore >= 0.15) && !chordBoundarySoon;

        struct PedalCand { QString id; int cc64 = 0; bool clearHeld = false; double actionCost = 0.0; };
        QVector<PedalCand> cands;
        cands.reserve(3);
        // Keep current pedal state.
        cands.push_back({"keep", cc64Now, false, 0.0});
        // Lift: clear sustain/held notes (good at chord boundaries).
        cands.push_back({"lift", 0, true, chordBoundarySoon ? 0.0 : 0.10});
        // Press: only meaningful if not already down.
        if (!sustainNow) cands.push_back({"down", 127, false, preferDown ? -0.10 : 0.10});

        // Evaluate candidates by simulating a small chordal gesture.
        // IMPORTANT: keep span <= maxSpanSemitones, otherwise all candidates become infeasible and pedal never engages.
        virtuoso::constraints::CandidateGesture g;
        {
            int a = qBound(0, (c.lhLo + c.lhHi) / 2, 127);
            int b = qBound(0, (c.rhLo + c.rhHi) / 2, 127);
            // Fold to keep within a 10th span.
            while (b - a > m_driver.constraints().maxSpanSemitones && (b - 12) >= 0) b -= 12;
            while (b - a > m_driver.constraints().maxSpanSemitones && (a + 12) <= 127) a += 12;
            const int c3 = qBound(0, b + 4, 127);
            g.midiNotes = {a, b, c3};
        }

        int bestIdx = 0;
        double bestScore = 1e18;
        for (int i = 0; i < cands.size(); ++i) {
            virtuoso::constraints::PerformanceState s = m_state;
            s.ints.insert("cc64", cands[i].cc64);
            if (cands[i].clearHeld) s.heldNotes.clear();
            const auto fr = m_driver.evaluateFeasibility(s, g);
            if (!fr.ok) continue;

            double score = fr.cost + cands[i].actionCost;
            // Bias toward the musical intent (down when desired, lift near chord boundaries).
            if (preferDown && cands[i].cc64 < 64) score += 1.00;
            if (chordBoundarySoon && cands[i].cc64 >= 64) score += 1.10;
            // If we're currently up and we want down, discourage "keep" strongly.
            if (preferDown && !sustainNow && cands[i].id == "keep") score += 0.85;
            // Tiny deterministic tie-break.
            score += (double(virtuoso::util::StableHash::fnv1a32(cands[i].id.toUtf8())) / double(std::numeric_limits<uint>::max())) * 1e-6;

            if (score < bestScore) { bestScore = score; bestIdx = i; }
        }

        const PedalCand chosen = cands[bestIdx];
        if (chosen.cc64 != cc64Now) {
            // Emit pedal action slightly ahead of the chord (16th before the beat when possible),
            // to avoid blurring the new harmony.
            const int sub = (c.beatInBar == 0) ? 0 : 0;
            const int count = 4;
            CcIntent ci;
            ci.cc = 64;
            ci.value = chosen.cc64;
            ci.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, sub, count, ts);
            ci.structural = (c.beatInBar == 0) || phraseLandmark;
            ci.logic_tag = QString("Piano:pedal_%1").arg(chosen.id);
            plan.ccs.push_back(ci);
            m_state.ints.insert("cc64", chosen.cc64);
            if (chosen.clearHeld) m_state.heldNotes.clear();
        }
    }

    // Lush ballad broken-chord comp gestures (Bill-ish):
    // Occasionally split the same harmonic event into two small bites (LH then RH, or RH then LH)
    // instead of a single block chord. This feels conversational without turning into arpeggios.
    {
        const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
        const bool lush = (c.toneDark >= 0.55) && (c.interaction >= 0.35);
        const bool allow = !userBusy && lush && (c.rhythmicComplexity >= 0.35) && !climaxDense;
        if (allow && !hitsThisBeat.isEmpty()) {
            const quint32 hb = virtuoso::util::StableHash::fnv1a32(QString("pno_broken|%1|%2|%3|%4")
                                                                       .arg(c.chordText)
                                                                       .arg(c.playbackBarIndex)
                                                                       .arg(c.beatInBar)
                                                                       .arg(c.determinismSeed)
                                                                       .toUtf8());
            const int p = qBound(0, int(llround(10.0 + 28.0 * c.rhythmicComplexity + 18.0 * (c.userSilence ? 1.0 : 0.0))), 55);
            if (int(hb % 100u) < p) {
                // Duplicate one of the hits and offset by a 16th (count=4), with lighter velocity.
                const int pick = int((hb / 3u) % uint(hitsThisBeat.size()));
                const CompHit base = hitsThisBeat[pick];
                if (base.count != 3) { // don't disturb triplets
                    CompHit a = base;
                    CompHit b = base;
                    a.count = 4; a.sub = 0;
                    b.count = 4; b.sub = 1;
                    // Keep the original beat; align to beat start.
                    a.beatInBar = c.beatInBar;
                    b.beatInBar = c.beatInBar;
                    // Make them short-ish so it reads as a gesture, not a smear.
                    a.dur = qMin(base.dur, virtuoso::groove::Rational(1, 4));
                    b.dur = qMin(base.dur, virtuoso::groove::Rational(1, 4));
                    const bool lhFirst = ((hb / 11u) % 2u) == 0u;
                    a.velDelta = base.velDelta + (lhFirst ? +2 : -4);
                    b.velDelta = base.velDelta + (lhFirst ? -6 : +1);
                    a.density = "guide";
                    b.density = base.density;
                    a.rhythmTag = base.rhythmTag + (lhFirst ? "|broken_lh_first" : "|broken_rh_first");
                    b.rhythmTag = base.rhythmTag + (lhFirst ? "|broken_rh_second" : "|broken_lh_second");
                    // Replace the picked hit with the two-part gesture.
                    hitsThisBeat.removeAt(pick);
                    hitsThisBeat.push_back(a);
                    hitsThisBeat.push_back(b);
                }
            }
        }
    }
    // Ensure we land on chord changes: if downbeat of a new chord has no scheduled hit, add one.
    // IMPORTANT: do this even if the user is active; otherwise harmony updates feel "late" (old chord rings, new chord appears on beat 4).
    if (hitsThisBeat.isEmpty() && !climaxDense) {
        if (c.chordIsNew && c.beatInBar == 0) {
            CompHit arrival;
            arrival.beatInBar = 0;
            arrival.sub = 0;
            arrival.count = 1;
            arrival.density = "guide";
            // If user is busy, keep it short/quiet but present so harmony updates on time.
            const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
            arrival.velDelta = userBusy ? -6 : +2;
            // Ring into the bar; long but not full smear.
            arrival.dur = userBusy ? virtuoso::groove::Rational(1, 4)
                                   : (c.userSilence ? virtuoso::groove::Rational(3, 4) : virtuoso::groove::Rational(1, 2));
            arrival.rhythmTag = "forced_arrival1";
            hitsThisBeat.push_back(arrival);
        } else {
            return plan;
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

    // --- Ontology-driven voicing selection (single source of musical truth) ---
    // We select a VoicingDef from the ontology and realize it into LH/RH pitch-class sets.
    // This replaces procedural chord-tone set generation.
    const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
    const bool wantShell = c.preferShells || userBusy || !chordHas7 || (c.harmonicRisk < 0.35);
    const bool wantRootless = chordHas7 && !wantShell && (c.harmonicRisk >= 0.35);
    const bool wantQuartal = chordHas7 && !wantShell && (c.harmonicRisk >= 0.60) && !c.toneDark;
    const bool wantUst = dominant && (c.harmonicRisk >= 0.70);

    auto voicingPcsFor = [&](const virtuoso::ontology::VoicingDef* v) -> QVector<int> {
        QVector<int> outPcs;
        if (!v) return outPcs;
        QSet<int> pcs;
        const int rootPc = (c.chord.rootPc >= 0) ? (c.chord.rootPc % 12) : 0;
        if (!v->intervals.isEmpty()) {
            for (int iv : v->intervals) pcs.insert((rootPc + iv + 1200) % 12);
        } else {
            for (int deg : v->chordDegrees) {
                const int p = pcForDegree(c.chord, deg);
                if (p >= 0) pcs.insert((p + 12) % 12);
            }
        }
        outPcs = pcs.values().toVector();
        sortUnique(outPcs);
        return outPcs;
    };

    const int blockStart = (qMax(0, c.playbackBarIndex) / 2) * 2;
    const bool anchorValid = (m_anchorBlockStartBar == blockStart &&
                              m_anchorChordText == c.chordText &&
                              !m_anchorPcs.isEmpty() &&
                              !m_anchorVoicingKey.trimmed().isEmpty());
    const bool refreshAnchor = (!anchorValid) || c.chordIsNew;

    QString voicingType;
    QString voicingKey;
    QVector<int> pcs;

    // If a RH topline target exists on this beat, we can bias voicing selection to support it.
    int desiredTopPcForScoring = -1;
    for (const auto& th : m_barTopHits) {
        if (th.beatInBar != c.beatInBar) continue;
        if (th.pc >= 0) { desiredTopPcForScoring = (th.pc % 12 + 12) % 12; break; }
    }

    if (refreshAnchor) {
        m_anchorBlockStartBar = blockStart;
        m_anchorChordText = c.chordText;

        const auto* ont = m_ont;
        QVector<const virtuoso::ontology::VoicingDef*> pool;
        if (ont) pool = ont->voicingsFor(virtuoso::ontology::InstrumentKind::Piano);

        QVector<const virtuoso::ontology::VoicingDef*> filtered;
        filtered.reserve(pool.size());
        for (const auto* v : pool) {
            if (!v) continue;
            const QString cat = v->category.trimmed().toLower();
            const bool isShell = (cat == "shell") || v->tags.contains("shell") || v->tags.contains("guide_tones");
            const bool isRootless = (cat == "rootless") || v->tags.contains("rootless");
            const bool isQuartal = (cat == "quartal") || v->tags.contains("quartal");
            const bool isUst = (cat == "ust") || v->tags.contains("ust");
            if (wantShell && isShell) filtered.push_back(v);
            else if (wantRootless && (isRootless || isShell)) filtered.push_back(v);
            else if (wantQuartal && (isQuartal || isRootless || isShell)) filtered.push_back(v);
            else if (wantUst && (isUst || isRootless || isShell)) filtered.push_back(v);
        }
        if (filtered.isEmpty()) filtered = pool;

        QVector<virtuoso::solver::Candidate<const virtuoso::ontology::VoicingDef*>> cands;
        cands.reserve(filtered.size());
        for (const auto* v : filtered) {
            if (!v) continue;
            cands.push_back({v->key, v});
        }

        virtuoso::solver::DecisionTrace trace;
        const int bestIdx = virtuoso::solver::CspSolver::chooseMinCost(cands, [&](const auto& cand) {
            const auto* v = cand.value;
            virtuoso::solver::EvalResult er;
            if (!v) { er.ok = false; return er; }
            const QVector<int> pcsAll = voicingPcsFor(v);
            if (pcsAll.isEmpty()) { er.ok = false; return er; }

            double s = 0.0;
            // Voice-leading stability within 2-bar anchor (unless chord is new).
            if (!m_anchorPcs.isEmpty() && !c.chordIsNew) {
                int common = 0;
                for (int pc : pcsAll) if (m_anchorPcs.contains(pc)) ++common;
                const int tot = qMax(1, pcsAll.size());
                const double overlap = double(common) / double(tot);
                s += (1.0 - overlap) * 1.2;
            }
            // Thickness target.
            const int n = pcsAll.size();
            const double target = 2.0 + 2.0 * qBound(0.0, c.harmonicRisk, 1.0) + 1.0 * progress01;
            s += 0.55 * qAbs(double(n) - target);

            // Harmonic stability rules.
            if (pcsAll.contains((pc1 + 12) % 12) && c.toneDark > 0.55) s += 0.7;
            if (v->tags.contains("ust") && !dominant) s += 0.9;
            if (v->tags.contains("quartal") && c.toneDark > 0.70) s += 0.8;
            if (!c.userSilence && userBusy) s += 0.35 * qMax(0, n - 2);

            // Shared motivic memory + counterpoint heuristic (lightweight):
            // - Prefer including the desired topline pitch-class, if we have one.
            // - Penalize parallel perfect intervals (5ths/8ves) with bass motion.
            if (desiredTopPcForScoring >= 0) {
                if (!pcsAll.contains(desiredTopPcForScoring)) s += 0.35;
            }
            if (m_mem) {
                const int bassNow = m_mem->lastMidi("Bass");
                const int bassPrev = m_mem->prevMidi("Bass");
                if (bassNow >= 0 && bassPrev >= 0 && desiredTopPcForScoring >= 0) {
                    const int around = (m_lastTopMidi >= 0) ? m_lastTopMidi : ((c.rhLo + c.rhHi) / 2);
                    const int topNow = nearestMidiForPc(desiredTopPcForScoring, around, c.rhLo, c.rhHi);
                    const int topPrev = (m_lastTopMidi >= 0) ? m_lastTopMidi : topNow;
                    const int intPrev = ((topPrev - bassPrev) % 12 + 12) % 12;
                    const int intNow = ((topNow - bassNow) % 12 + 12) % 12;
                    const int bassMove = (bassNow > bassPrev) ? 1 : (bassNow < bassPrev ? -1 : 0);
                    const int topMove = (topNow > topPrev) ? 1 : (topNow < topPrev ? -1 : 0);
                    const bool perfectPrev = (intPrev == 0 || intPrev == 7);
                    const bool perfectNow = (intNow == 0 || intNow == 7);
                    if (perfectPrev && perfectNow && bassMove != 0 && topMove != 0 && bassMove == topMove) {
                        s += 0.65;
                    } else if (bassMove != 0 && topMove != 0 && bassMove != topMove) {
                        s -= 0.08;
                    }
                }
            }

            // Deterministic tie breaker.
            s += (double(virtuoso::util::StableHash::fnv1a32(v->key.toUtf8())) / double(std::numeric_limits<uint>::max())) * 1e-6;

            er.ok = true;
            er.cost = s;
            er.reasons = {QString("cost=%1").arg(s, 0, 'f', 3),
                          QString("pcs=%1").arg(n),
                          QString("cat=%1").arg(v->category)};
            return er;
        }, &trace);

        const virtuoso::ontology::VoicingDef* best = (bestIdx >= 0) ? cands[bestIdx].value : nullptr;
        if (!best && !pool.isEmpty()) best = pool.first();

        if (best) {
            m_anchorVoicingKey = best->key;
            m_anchorVoicingName = best->name;
            m_anchorPcs = voicingPcsFor(best);
            m_anchorCspTag = (trace.chosenIndex >= 0 && !trace.chosenId.trimmed().isEmpty())
                ? QString("|csp_voicing=%1|csp_cost=%2").arg(trace.chosenId).arg(trace.chosenCost, 0, 'f', 3)
                : QString();
        } else {
            m_anchorVoicingKey.clear();
            m_anchorVoicingName = "Shell (fallback)";
            m_anchorCspTag.clear();
            // Absolute fallback: guides only.
            m_anchorPcs.clear();
            if (pc3 >= 0) m_anchorPcs.push_back((pc3 + 12) % 12);
            if (pc7 >= 0) m_anchorPcs.push_back((pc7 + 12) % 12);
            else if (pc5 >= 0) m_anchorPcs.push_back((pc5 + 12) % 12);
            sortUnique(m_anchorPcs);
        }

        // Derive LH/RH split from the chosen pitch-class pool:
        // - LH gets guide tones (3/7 or 3/5)
        // - RH gets the remaining colors
        QVector<int> lh;
        if (pc3 >= 0) lh.push_back((pc3 + 12) % 12);
        if (pc7 >= 0) lh.push_back((pc7 + 12) % 12);
        else if (pc5 >= 0) lh.push_back((pc5 + 12) % 12);
        sortUnique(lh);
        // Keep only pcs that exist in the chosen pool (unless pool is tiny).
        if (!m_anchorPcs.isEmpty()) {
            QVector<int> filtered;
            for (int p : lh) if (m_anchorPcs.contains(p)) filtered.push_back(p);
            if (!filtered.isEmpty()) lh = filtered;
        }
        if (lh.size() > 2) lh.resize(2);
        if (lh.isEmpty() && !m_anchorPcs.isEmpty()) {
            lh.push_back(m_anchorPcs.first());
            if (m_anchorPcs.size() > 1) lh.push_back(m_anchorPcs[1]);
            sortUnique(lh);
            if (lh.size() > 2) lh.resize(2);
        }

        QVector<int> rh = m_anchorPcs;
        for (int p : lh) rh.removeAll(p);
        sortUnique(rh);
        // Ensure RH has something if the pool is >2 (avoid all-LH mud).
        if (rh.isEmpty() && m_anchorPcs.size() > lh.size()) {
            for (int p : m_anchorPcs) {
                if (!lh.contains(p)) { rh.push_back(p); break; }
            }
            sortUnique(rh);
        }

        m_anchorLhPcs = lh;
        m_anchorRhPcs = rh;
    }

    pcs = m_anchorPcs;
    QVector<int> lhShellPcs = m_anchorLhPcs;
    QVector<int> rhColorPcs = m_anchorRhPcs;
    voicingType = m_anchorVoicingName.isEmpty() ? QString("Piano voicing") : m_anchorVoicingName;
    voicingKey = m_anchorVoicingKey;

    const QVector<int> prevVoicing = m_lastVoicing;

    // Map to MIDI with an Evans-oriented realizer:
    // - LH: stable shell bed (guide tones) in a comfortable register
    // - RH: clustered colors with controlled spacing
    // - Top note: if a RH topline target exists on this beat, bias the chord top voice to it
    QVector<int> midi;
    midi.reserve(pcs.size());

    const bool cadence = (c.cadence01 >= 0.55) || c.phraseEndBar;

    // Desired top pitch-class for this beat (from RH line), if any.
    int desiredTopPc = -1;
    for (const auto& th : m_barTopHits) {
        if (th.beatInBar != c.beatInBar) continue;
        if (th.pc >= 0) { desiredTopPc = th.pc; break; }
    }

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

    // Fallback if anchors are empty (shouldn't happen, but safe).
    if (lhShellPcs.isEmpty()) lhShellPcs = guides;
    if (rhColorPcs.isEmpty()) rhColorPcs = colors;

    auto normPc = [](int pc) { return (pc % 12 + 12) % 12; };
    desiredTopPc = (desiredTopPc >= 0) ? normPc(desiredTopPc) : -1;

    // If we have a desired top pc and it's not already in the set, consider adding it (and possibly dropping a color).
    if (desiredTopPc >= 0 && !guides.contains(desiredTopPc) && !colors.contains(desiredTopPc)) {
        // Only add if it is a reasonably safe chord-scale color (avoid random chromatic).
        const int p3 = normPc(pc3);
        const int p5 = normPc(pc5);
        const int p7 = normPc(pc7);
        const int p9 = normPc(pcForDegree(c.chord, 9));
        const int p11 = normPc(pcForDegree(c.chord, 11));
        const int p13 = normPc(pcForDegree(c.chord, 13));
        const QSet<int> safe = QSet<int>{p3, p5, p7, p9, p11, p13};
        if (safe.contains(desiredTopPc)) {
            colors.push_back(desiredTopPc);
            sortUnique(colors);
            // Keep chord size bounded: if too many colors, drop the least important (deterministic).
            while (colors.size() > 3) colors.removeAt(0);
        }
    }

    // --- LH shell bed (explicit) ---
    QVector<int> lhPcs = lhShellPcs;
    for (int& pc : lhPcs) pc = normPc(pc);
    sortUnique(lhPcs);

    const int lhLo = c.lhLo;
    const int lhHi = c.lhHi;
    const int rhLo = c.rhLo;
    const int rhHi = c.rhHi;

    // Phrase-level register planning (lush ballad feel):
    // Keep hands in a coherent zone for the full phrase, with a gentle arc into cadences.
    const int phraseBars = qMax(1, c.phraseBars);
    const int phraseStart = (qMax(0, c.playbackBarIndex) / phraseBars) * phraseBars;
    if (m_phraseGuideStartBar != phraseStart || m_phraseGuideBars != phraseBars) {
        m_phraseGuideStartBar = phraseStart;
        m_phraseGuideBars = phraseBars;
        // Note: guide pcs are planned elsewhere; here we just keep the register arc stable per phrase.
    }
    const double phrasePos01 = (phraseBars > 1)
        ? (double(qBound(0, c.barInPhrase, phraseBars - 1)) / double(phraseBars - 1))
        : 0.0;
    // (cadence already computed above)
    const quint32 hrz = virtuoso::util::StableHash::fnv1a32(QString("pno_reg|%1|%2|%3")
                                                                .arg(phraseStart)
                                                                .arg(phraseBars)
                                                                .arg(c.determinismSeed)
                                                                .toUtf8());
    const int baseShift = int((hrz % 3u)) - 1; // -1..+1 semitones
    const int arcShift = int(llround(3.0 * qSin(3.1415926535 * phrasePos01))); // 0..3..0
    const int cadenceLift = cadence ? 1 : 0;
    auto safeQBoundInt = [&](int mn, int v, int mx) -> int {
        // Qt asserts if (mx < mn). This can happen when interaction ducking temporarily
        // narrows ranges (e.g., rhHi close to rhLo). Never crash; just collapse range.
        if (mx < mn) mx = mn;
        return qBound(mn, v, mx);
    };

    const int regShift = safeQBoundInt(-2, baseShift + arcShift + cadenceLift, 4);

    // LH target around: stable bed; avoid jumping.
    int lhAround = safeQBoundInt(lhLo + 3, (lhLo + lhHi) / 2 + regShift, lhHi - 3);
    if (!prevVoicing.isEmpty()) {
        // pick the lowest previous note as LH anchor
        const int prevLow = *std::min_element(prevVoicing.begin(), prevVoicing.end());
        lhAround = safeQBoundInt(lhLo + 2, prevLow, lhHi - 2);
    }
    QVector<int> lhMidi;
    lhMidi.reserve(lhPcs.size());
    int lastLh = -999;
    for (int pc : lhPcs) {
        int m = nearestMidiForPc(pc, lhAround, lhLo, lhHi);
        if (m < 0) continue;
        while (m <= lastLh + 3 && (m + 12) <= lhHi) m += 12;
        lastLh = m;
        lhMidi.push_back(m);
    }

    // --- RH cluster (explicit) ---
    QVector<int> rhPcs = rhColorPcs;
    for (int& pc : rhPcs) pc = normPc(pc);
    sortUnique(rhPcs);
    // On very thin chords, borrow one guide tone into RH (Bill often doubles guide in RH cluster).
    if (rhPcs.size() < 2 && !guides.isEmpty()) {
        rhPcs.push_back(guides.last());
        sortUnique(rhPcs);
    }
    if (rhPcs.isEmpty()) rhPcs = guides;
    sortUnique(rhPcs);

    // Choose top note around last top / mid-high target.
    int topAround = safeQBoundInt(rhLo + 8, (rhLo + rhHi) / 2 + 8 + regShift * 2, rhHi - 4);
    if (m_lastTopMidi >= 0) topAround = safeQBoundInt(rhLo + 6, m_lastTopMidi, rhHi - 4);
    if (cadence) topAround = safeQBoundInt(rhLo + 8, topAround + 2, rhHi - 3);

    int topPc = -1;
    if (desiredTopPc >= 0 && (guides.contains(desiredTopPc) || rhPcs.contains(desiredTopPc))) topPc = desiredTopPc;
    else if (!rhPcs.isEmpty()) topPc = rhPcs.last();
    if (topPc < 0 && pc9 >= 0) topPc = normPc(pc9);
    if (topPc < 0 && pc3 >= 0) topPc = normPc(pc3);

    const int topMidi = nearestMidiForPc(topPc, topAround, rhLo, rhHi);
    if (topMidi >= 0) {
        // Place remaining RH notes under top with preferred spacing.
        QVector<int> remaining = rhPcs;
        remaining.removeAll(topPc);
        std::sort(remaining.begin(), remaining.end());

        QVector<int> rhMidi;
        rhMidi.reserve(remaining.size() + 1);
        rhMidi.push_back(topMidi);
        int prevM = topMidi;

        auto placeBelow = [&](int pc, int around, int lo, int hi) -> int {
            int m = nearestMidiForPc(pc, around, lo, hi);
            if (m < 0) return -1;
            while (m >= prevM && (m - 12) >= lo) m -= 12;
            while ((prevM - m) > 12 && (m + 12) <= hi) m += 12;
            return m;
        };

        for (int i = remaining.size() - 1; i >= 0; --i) {
            const int pc = remaining[i];
            int m = placeBelow(pc, prevM - 4, rhLo, rhHi);
            if (m < 0) continue;
            const int interval = prevM - m;
            // Beauty constraint: avoid seconds in mid register; allow tight clusters higher up.
            const bool highClusterOk = (prevM >= 76) || (cadence && prevM >= 72);
            if (interval <= 2 && !highClusterOk) continue;
            if (interval == 6) continue; // avoid tritone stacking
            prevM = m;
            rhMidi.push_back(m);
        }

        // Combine and sort.
        for (int m : lhMidi) midi.push_back(m);
        for (int m : rhMidi) midi.push_back(m);
        std::sort(midi.begin(), midi.end());
    } else {
        // Fallback: previous behavior-ish (rare).
        for (int pc : guides) midi.push_back(nearestMidiForPc(pc, (lhLo + lhHi) / 2, lhLo, lhHi));
        for (int pc : rhPcs) midi.push_back(nearestMidiForPc(pc, (rhLo + rhHi) / 2, rhLo, rhHi));
        std::sort(midi.begin(), midi.end());
    }

    sortUnique(midi);
    midi = repairToFeasible(midi);
    if (midi.isEmpty()) return plan;

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
    const int baseVel0 = climaxDense ? (c.chordIsNew ? 64 : 58) : (c.chordIsNew ? 50 : 44);
    const double phrasePos01_dyn = (c.phraseBars > 1) ? (double(qBound(0, c.barInPhrase, c.phraseBars - 1)) / double(c.phraseBars - 1)) : 0.0;
    const double phraseArc = 0.90 + 0.08 * qSin(3.1415926535 * phrasePos01_dyn); // gentle arc
    const double cadenceBoost = 1.0 + 0.22 * qBound(0.0, c.cadence01, 1.0);
    const double energyBoost = 0.92 + 0.28 * qBound(0.0, c.energy, 1.0);
    const double silenceBoost = c.userSilence ? 1.08 : 1.0;
    const int baseVel = qBound(18, int(llround(double(baseVel0) * phraseArc * cadenceBoost * energyBoost * silenceBoost)), 100);

    // Key-safe gating for optional high-register ornaments.
    // If key is known, require ornament pitch-classes to be diatonic (helps kill "atonal sparkle").
    QSet<int> keySafe;
    if (c.hasKey) {
        const int t = (c.keyTonicPc % 12 + 12) % 12;
        const int stepsIonian[7] = {0,2,4,5,7,9,11};
        const int stepsAeolian[7] = {0,2,3,5,7,8,10};
        const int* steps = (c.keyMode == virtuoso::theory::KeyMode::Minor) ? stepsAeolian : stepsIonian;
        for (int i = 0; i < 7; ++i) keySafe.insert((t + steps[i]) % 12);
    }
    auto isKeyOk = [&](int pc) -> bool {
        if (pc < 0) return false;
        const int p = (pc % 12 + 12) % 12;
        return keySafe.isEmpty() ? true : keySafe.contains(p);
    };

        // Optional high sparkle on beat 4 (very occasional), but only when there's rhythmic space.
    const int pSp = qBound(0, int(llround(c.sparkleProbBeat4 * 100.0)), 100);
    const bool sparkle = (c.beatInBar == 3) && c.userSilence && (pSp > 0) && (int((h / 13u) % 100u) < pSp);
    int sparklePc = -1;
    if (sparkle) {
        // Hard cap: avoid ultra-high "ice pick" notes in ballads.
        // (These were a major source of jarring highs.)
        const int sparkleHiCap = qMin(c.sparkleHi, 90); // <= F#6

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
        // Also: avoid sparkles on spicy harmony (dominants/altered) since they read as atonal pings up top.
        if (spPc >= 0 && !spicy && isKeyOk(spPc)) sparklePc = ((spPc % 12) + 12) % 12;
        // If key gating rejects it, don't sparkle.
        if (sparklePc < 0) {
            // no-op
        } else {
            // Also ensure the eventual placement can fit in the capped range.
            if (sparkleHiCap <= c.sparkleLo) sparklePc = -1;
        }
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

        const quint32 hr = virtuoso::util::StableHash::fnv1a32(QString("roll|%1|%2|%3|%4")
                                                                   .arg(c.chordText)
                                                                   .arg(c.playbackBarIndex)
                                                                   .arg(hit.beatInBar)
                                                                   .arg(c.determinismSeed)
                                                                   .toUtf8());
        const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
        const bool cadence = (c.cadence01 >= 0.55) || c.phraseEndBar;
        const bool arrival = c.chordIsNew || hit.rhythmTag.contains("arrival") || hit.rhythmTag.contains("forced_arrival1");
        // Rolling/arpeggiation library:
        // - Reduce overuse (esp. after adding strong touch/phrase behavior)
        // - Increase pattern variety
        // - Add deterministic anti-repeat across bars
        const double rc = qBound(0.0, c.rhythmicComplexity, 1.0);
        const int pRoll = qBound(0, int(llround(4.0 + 6.0 * c.energy + 14.0 * rc + 6.0 * progress01 + (cadence ? 6.0 : 0.0))), 45);
        // At higher BPM, reduce rolls (they read like bad time if the spread is too audible).
        const double rollTempoScale = qBound(0.35, 90.0 / double(qMax(1, c.bpm)), 1.0);
        const int pRollAdj = qBound(0, int(llround(double(pRoll) * rollTempoScale)), 100);
        const bool doRoll = !userBusy && ((c.energy >= 0.18) || (rc >= 0.32) || cadence) && (int(hr % 100u) < pRollAdj);
        const int pBigRoll = qBound(0, int(llround((1.0 + 2.0 * c.energy + 5.0 * rc + 2.0 * progress01 + (cadence ? 3.0 : 0.0)) * rollTempoScale)), 12);
        const bool bigRoll = doRoll && (int((hr / 97u) % 100u) < pBigRoll);

        // Arpeggiation should be rarer than rolling, and more likely only at cadences/arrivals.
        const int pArp = qBound(0, int(llround((cadence ? 18.0 : 8.0) + 16.0 * rc + (bigRoll ? 14.0 : 0.0))), 55);
        const bool doArp = doRoll && (notes.size() >= 3) && (hit.dur >= virtuoso::groove::Rational(1, 8)) && (int((hr / 13u) % 100u) < pArp);

        // Style selection (deterministic), with anti-repeat per bar.
        // 0=none/subtle roll, 1=up, 2=down, 3=inside-out, 4=outside-in, 5=skip, 6=triplet (cadence only)
        int style = 0;
        if (doArp) {
            const quint32 hs = virtuoso::util::StableHash::fnv1a32(QString("arpStyle|%1|%2|%3|%4")
                                                                       .arg(c.chordText)
                                                                       .arg(c.playbackBarIndex)
                                                                       .arg(hit.beatInBar)
                                                                       .arg(c.determinismSeed)
                                                                       .toUtf8());
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

        // --- Comp articulation model (deterministic) ---
        // Goal: avoid "MIDI robot" by varying attack + release shapes like a real pianist:
        // - Stab: shorter, clearer, slightly less pedal smear
        // - Tenuto: default, singy
        // - HalfPedalWash: longer ring, more pedal-ish wash
        // - ReStrike: allow re-articulating held notes (esp. arrivals) for touch/definition
        // - Ghost: very soft, short "breath" comp
        enum class CompArt { Stab, Tenuto, HalfPedalWash, ReStrike, Ghost };
        const quint32 ha = virtuoso::util::StableHash::fnv1a32(QString("pno_art|%1|%2|%3|%4|%5")
                                                                   .arg(c.chordText)
                                                                   .arg(c.playbackBarIndex)
                                                                   .arg(hit.beatInBar)
                                                                   .arg(hit.sub)
                                                                   .arg(c.determinismSeed)
                                                                   .toUtf8());
        const bool isUpbeat = (hit.count >= 2) ? (hit.sub > 0) : false;
        const bool wantsGhost = hit.rhythmTag.contains("breath") || hit.rhythmTag.contains("jab") || hit.rhythmTag.contains("delay");
        CompArt art = CompArt::Tenuto;
        if (userBusy) art = CompArt::Stab;
        else if (wantsGhost && int(ha % 100u) < 75) art = CompArt::Ghost;
        else if ((cadence || arrival) && !isUpbeat && int((ha / 3u) % 100u) < int(llround(35.0 + 40.0 * c.cadence01))) art = CompArt::HalfPedalWash;
        else if ((arrival || hit.rhythmTag.contains("touch1") || hit.rhythmTag.contains("arrival")) && int((ha / 5u) % 100u) < 45) art = CompArt::ReStrike;
        else if (int((ha / 7u) % 100u) < int(llround(20.0 + 35.0 * qBound(0.0, c.rhythmicComplexity, 1.0)))) art = CompArt::Stab;

        const bool isPedalWash = !userBusy && (cadence || arrival || (c.userSilence && c.energy <= 0.55) || (art == CompArt::HalfPedalWash));
        const bool forceRestrike = (art == CompArt::ReStrike);
        const int velArtDelta = (art == CompArt::Ghost) ? -10
                              : (art == CompArt::Stab) ? +1
                              : (art == CompArt::HalfPedalWash) ? -2
                              : 0;

        // Timing feel: ballad comp is often slightly laid-back; arrivals can sit more on the grid.
        // At faster tempos, shrink late timing so it doesn't smear against the beat.
        const double tempoScale = qBound(0.45, 90.0 / double(qMax(1, c.bpm)), 1.0);
        const int hitLate128Raw = (userBusy ? 0
                                : (arrival ? 0
                                : (art == CompArt::Stab ? 0
                                : (art == CompArt::Ghost ? 1
                                : (cadence ? 1 : 2)))));
        const int hitLate128 = qBound(0, int(llround(double(hitLate128Raw) * tempoScale)), 2);

        // LH independence: occasionally re-articulate just the LH anchor under a RH wash
        // (feels like the left hand "breathes" while RH stays connected).
        const bool allowLhInd = !userBusy && isPedalWash && !arrival && (art == CompArt::HalfPedalWash || art == CompArt::Tenuto);
        const bool doLhOnly = allowLhInd && (int((ha / 17u) % 100u) < int(llround(10.0 + 22.0 * qBound(0.0, c.cadence01, 1.0) + (c.userSilence ? 12.0 : 0.0))));

        // If harmony changes next bar, avoid letting this hit ring into the new chord.
        // We clamp noteOff earlier than the barline by a small safety margin so humanization doesn't smear.
        auto clampDurBeforeNextChord = [&](const virtuoso::groove::GridPos& pos,
                                           virtuoso::groove::Rational d,
                                           bool structuralNote) -> virtuoso::groove::Rational {
            // If we don't know a boundary, don't clamp.
            if (!c.hasNextChord || !c.nextChanges) return d;
            const int beatsPerBar = qMax(1, ts.num);
            const int beatsUntil = (c.beatsUntilChordChange > 0) ? c.beatsUntilChordChange : 0;
            // If the next explicit change is within this bar, clamp to that beat boundary.
            virtuoso::groove::Rational boundary = GrooveGrid::barDurationWhole(ts);
            if (beatsUntil > 0) {
                const auto beatDur = GrooveGrid::beatDurationWhole(ts);
                const int nextBeat = qBound(0, c.beatInBar + beatsUntil, beatsPerBar);
                boundary = beatDur * nextBeat;
            }
            virtuoso::groove::Rational remaining = boundary - pos.withinBarWhole;
            // Safety: subtract a few ms worth of whole-notes.
            const int safetyMs = structuralNote ? 6 : 14;
            const virtuoso::groove::Rational safetyWhole(qint64(safetyMs) * qint64(qMax(1, c.bpm)), qint64(240000));
            if (remaining <= safetyWhole) return virtuoso::groove::Rational(1, 64);
            remaining = remaining - safetyWhole;
            if (d > remaining) d = remaining;
            if (d < virtuoso::groove::Rational(1, 64)) d = virtuoso::groove::Rational(1, 64);
            return d;
        };

        for (int idx = 0; idx < notes.size(); ++idx) {
            const int m = notes[idx];
            const bool isLow = (m == lowNote);
            const bool isTop = (m == topNote);
            const bool isRh = (m > c.lhHi); // heuristic split at LH range ceiling

            // Pedal-aware re-strike avoidance (articulation-aware):
            // if we're in a "wash" state and the note was already held from the previous voicing,
            // avoid retriggering it on non-arrival hits (lets the harmony ring like pedal).
            if (isPedalWash && !forceRestrike && !arrival && !prevVoicing.isEmpty() && prevVoicing.contains(m)) {
                if (doLhOnly) {
                    // In LH-only mode, skip re-triggering everything except LH anchor.
                    if (!isLow) continue;
                }
                // Allow top voice to be re-articulated occasionally for singing line.
                if (!isTop && (isLow || !isRh || hit.dur >= virtuoso::groove::Rational(1, 4))) continue;
            }

            int vel = baseVel + hit.velDelta + velArtDelta;
            // Touch model v2 (Bill-ish): top voice sings, inner voices disappear, LH is a soft bed.
            // Also keep cadences slightly warmer/rounder.
            if (isLow && !isRh) vel += 1;              // LH bed slightly present
            else if (isTop) vel += (cadence ? 10 : 8); // singing top
            else vel -= 8;                             // inner voices very soft
            if (isRh) vel += 1;
            if (c.toneDark >= 0.65) vel -= 2;
            if (art == CompArt::Ghost) {
                if (isTop) vel -= 6;
                else vel -= 10;
            }
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

            // Global hit-level laid-back timing (128th grid), applied before per-voice micro-spread.
            // Skip triplets to avoid mangling tuplets.
            if (hitLate128 != 0 && count != 3) {
                const int base32 = to16thSub(sub, count) * 8;
                count = 32;
                sub = qBound(0, base32 + hitLate128, 31);
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
            // --- Pedal-ish performance model (session feel) + articulation shaping ---
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

            // Articulation overrides (final pass).
            if (art == CompArt::Stab) {
                if (isLow && !isRh) d = qMin(d, virtuoso::groove::Rational(1, 4));
                if (isRh) d = qMin(d, virtuoso::groove::Rational(1, 8));
                if (isTop && isRh && (cadence || arrival)) d = qMax(d, virtuoso::groove::Rational(1, 4));
            } else if (art == CompArt::Ghost) {
                d = qMin(d, virtuoso::groove::Rational(1, 8));
            } else if (art == CompArt::HalfPedalWash) {
                if (isRh && !isTop) d = qMax(d, virtuoso::groove::Rational(1, 4));
                if (isTop && isRh) d = qMax(d, virtuoso::groove::Rational(3, 8));
                if (cadence) d = qMax(d, barDur());
            } else if (art == CompArt::ReStrike) {
                // Give definition: shorten slightly so the re-attack reads.
                if (isRh && !isTop) d = qMin(d, virtuoso::groove::Rational(3, 16));
            }

            // Final duration clamp to avoid ringing into the next chord when harmony changes.
            d = clampDurBeforeNextChord(n.startPos, d, c.chordIsNew);
            // If RH is rolled late, shorten *inner* RH notes a bit so it doesn't smear (but keep top singing).
            const bool shorten = doRoll && isRh && !isLow && !isTop && (d <= virtuoso::groove::Rational(1, 4));
            n.durationWhole = shorten ? qMin(d, virtuoso::groove::Rational(3, 16)) : d;
        n.structural = c.chordIsNew;
        n.chord_context = c.chordText;
            n.voicing_type = voicingType + (doRoll ? (doArp ? " + Arpeggiated" : " + RolledHands") : "");
            const QString ontTag = voicingKey.trimmed().isEmpty() ? QString() : (QString("|ont=") + voicingKey);
            const QString cspTag = m_anchorCspTag.trimmed().isEmpty() ? QString() : m_anchorCspTag;
            QString artTag = "tenuto";
            if (art == CompArt::Stab) artTag = "stab";
            else if (art == CompArt::HalfPedalWash) artTag = "half_pedal";
            else if (art == CompArt::ReStrike) artTag = "restrike";
            else if (art == CompArt::Ghost) artTag = "ghost";
            n.logic_tag = (hit.rhythmTag.isEmpty() ? "ballad_comp" : ("ballad_comp|" + hit.rhythmTag)) + "|art=" + artTag + ontTag + cspTag;
            n.target_note = isTop ? "Comp (top voice)" : (isLow ? "Comp (LH anchor)" : "Comp (inner)");
        out.push_back(n);
    }
    };

    for (const auto& hit : hitsThisBeat) renderHit(hit);

    // Reintroduce "alive" RH movement the right way: as a transformation of the current comp voicing
    // (same pianist, same hands), not as a separate pitch generator.
    //
    // We do a very soft RH-only dyad/triad re-strike a 16th late on select beats, using chord-safe + key-safe
    // pitch classes near the existing RH notes (physically plausible, beautiful).
    {
        const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
        // Motif-driven: if we have a planned top-line event on this beat, prefer playing a gesture.
        bool hasTopEventThisBeat = false;
        int topTargetPc = -1;
        int topVelDelta = -12;
        for (const auto& th : m_barTopHits) {
            if (th.beatInBar != c.beatInBar) continue;
            if (th.pc < 0) continue;
            hasTopEventThisBeat = true;
            topTargetPc = th.pc;
            topVelDelta = th.velDelta;
            break;
        }

        const bool allow = !userBusy && (c.userSilence || c.interaction >= 0.40 || hasTopEventThisBeat) && (c.rhythmicComplexity >= 0.25);
        if (allow && !m_lastVoicing.isEmpty()) {
            const quint32 hg = virtuoso::util::StableHash::fnv1a32(QString("pno_rh_gesture|%1|%2|%3|%4")
                                                                       .arg(c.chordText)
                                                                       .arg(c.playbackBarIndex)
                                                                       .arg(c.beatInBar)
                                                                       .arg(c.determinismSeed)
                                                                       .toUtf8());
            const bool cadence = (c.cadence01 >= 0.55) || c.phraseEndBar;
            const int p = qBound(0, int(llround((hasTopEventThisBeat ? 35.0 : 6.0)
                                               + 18.0 * qBound(0.0, c.rhythmicComplexity, 1.0)
                                               + 14.0 * qBound(0.0, c.interaction, 1.0)
                                               + (cadence ? 18.0 : 0.0)
                                               + (c.userSilence ? 10.0 : 0.0))), 60);
            if (int(hg % 100u) < p) {
                // RH notes in the current voicing.
                QVector<int> rh;
                for (int m : m_lastVoicing) if (m > c.lhHi) rh.push_back(m);
                std::sort(rh.begin(), rh.end());
                if (rh.size() >= 2) {
                    // Keep gestures in a sweet register (avoid ice-pick highs).
                    const int hiCap = qMin(c.rhHi, 88);
                    for (int& m : rh) m = qMin(m, hiCap);

                    // Chord-safe pool (and key-safe if available).
                    QSet<int> safe;
                    auto addSafe = [&](int pc) { if (pc >= 0) safe.insert((pc % 12 + 12) % 12); };
                    addSafe(pcForDegree(c.chord, 3));
                    addSafe(pcForDegree(c.chord, 5));
                    addSafe(pcForDegree(c.chord, 7));
                    addSafe(pcForDegree(c.chord, 9));
                    if (c.chord.quality == music::ChordQuality::Minor) addSafe(pcForDegree(c.chord, 11));
                    else addSafe(pcForDegree(c.chord, 13));

                    // Key-safe (major/minor) — optional.
                    QSet<int> keySafe;
                    if (c.hasKey) {
                        const int t = (c.keyTonicPc % 12 + 12) % 12;
                        const int stepsIonian[7] = {0,2,4,5,7,9,11};
                        const int stepsAeolian[7] = {0,2,3,5,7,8,10};
                        const int* steps = (c.keyMode == virtuoso::theory::KeyMode::Minor) ? stepsAeolian : stepsIonian;
                        for (int i = 0; i < 7; ++i) keySafe.insert((t + steps[i]) % 12);
                        safe.intersect(keySafe);
                    }
                    if (safe.isEmpty()) return plan;

                    auto nearestPcInSet = [&](int fromPc) -> int {
                        const int f = (fromPc % 12 + 12) % 12;
                        int best = f;
                        int bestCost = 999;
                        for (int pc : safe) {
                            int d = pc - f;
                            while (d > 6) d -= 12;
                            while (d < -6) d += 12;
                            const int cost = qAbs(d);
                            if (cost < bestCost) { bestCost = cost; best = pc; }
                        }
                        return best;
                    };

                    const bool wantTriad = (cadence || hasTopEventThisBeat) && (int((hg / 7u) % 100u) < 45);
                    const int n = wantTriad ? 3 : 2;
                    QVector<int> src = rh;
                    while (src.size() > n) src.removeFirst(); // top n voices

                    QVector<int> gestureNotes;
                    gestureNotes.reserve(src.size());
                    for (int m : src) {
                        const int fromPc = (m % 12 + 12) % 12;
                        const int toPc = (hasTopEventThisBeat && m == src.last() && topTargetPc >= 0)
                            ? nearestPcInSet(topTargetPc)
                            : nearestPcInSet(fromPc);
                        int target = nearestMidiForPc(toPc, m, c.rhLo, hiCap);
                        // Nudge by step if possible (feels like a real small motion).
                        if (qAbs(target - m) > 2) target = nearestMidiForPc(toPc, m - 2, c.rhLo, hiCap);
                        if (target >= 0) gestureNotes.push_back(target);
                    }
                    sortUnique(gestureNotes);
                    if (gestureNotes.size() >= 2) {
                        // Schedule on & of the beat (16th late feel) with short duration.
                        const auto pos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 1, 4, ts);
                        for (int i = 0; i < gestureNotes.size(); ++i) {
                            virtuoso::engine::AgentIntentNote nte;
                            nte.agent = "Piano";
                            nte.channel = midiChannel;
                            nte.note = clampMidi(gestureNotes[i]);
                            const int extraTop = (i == gestureNotes.size() - 1 ? 6 : 0);
                            nte.baseVelocity = qBound(1, baseVel - 22 + extraTop + topVelDelta, 127);
                            nte.startPos = pos;
                            nte.durationWhole = virtuoso::groove::Rational(1, 8);
                            nte.structural = false;
                            nte.chord_context = c.chordText;
                            nte.voicing_type = voicingType + (wantTriad ? " + RH TriadGesture" : " + RH DyadGesture");
                            {
                                const QString ontTag = voicingKey.trimmed().isEmpty() ? QString() : (QString("|ont=") + voicingKey);
                                const QString cspTag = m_anchorCspTag.trimmed().isEmpty() ? QString() : m_anchorCspTag;
                                nte.logic_tag = (wantTriad ? "ballad_comp|rh_gesture|triad" : "ballad_comp|rh_gesture|dyad") + ontTag + cspTag;
                            }
                            nte.target_note = (i == gestureNotes.size() - 1) ? "RH gesture (top)" : "RH gesture";
                            out.push_back(nte);
                        }
                    }
                }
            }
        }
    }

    // NOTE: We intentionally do NOT emit separate cadence dyads/licklets here.
    // Ballad "setup/arrival" feel should come from the same two-handed comp system (voicing + rhythm + articulation),
    // not from an additional pitch generator.

    // NOTE: We also do not emit separate RH dyads here.
    // The RH line is used as a *target* for the top voice of the comp voicing (single two-handed system).

    // NOTE: No separate sparkle dyads.

    // NOTE: No separate upper-structure generator or resolution notes.
    // If we want richer harmony, it should come from selecting a richer *main voicing* candidate.

    // NOTE: No separate silence fill dyads.
    // Track sustained notes approximately for pedaling wash constraints.
    // This is intentionally conservative (adds notes when sustain is down; clears on pedal-up decisions).
    const int cc64After = m_state.ints.value("cc64", 0);
    const bool sustainAfter = (cc64After >= 64);
    if (!out.isEmpty()) {
        if (sustainAfter) {
            for (const auto& n : out) {
                if (n.note < 0) continue;
                if (!m_state.heldNotes.contains(n.note)) m_state.heldNotes.push_back(n.note);
            }
        } else {
            // Without sustain, approximate that only the latest voicing is sounding.
            m_state.heldNotes.clear();
            for (const auto& n : out) {
                if (n.note < 0) continue;
                m_state.heldNotes.push_back(n.note);
            }
        }
    }

    return plan;
}

void JazzBalladPianoPlanner::ensureBarRhythmPlanned(const Context& c) {
    const int bar = qMax(0, c.playbackBarIndex);
    if (bar == m_lastRhythmBar) return;
    m_lastRhythmBar = bar;
    m_barHits = chooseBarCompRhythm(c);
    ensureMotifBlockPlanned(c);
    ensurePhraseGuideLinePlanned(c);
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
    const quint32 h = virtuoso::util::StableHash::fnv1a32(QString("pno_motif4|%1|%2")
                                                              .arg(phraseIndex)
                                                              .arg(c.determinismSeed)
                                                              .toUtf8());
    const double e = qBound(0.0, c.energy, 1.0);
    const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
    const bool allow = !userBusy && (c.userSilence || e >= 0.32 || c.chordIsNew || c.cadence01 >= 0.55 || c.phraseEndBar);
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
    if (userBusy) return {};

    const int bar = qMax(0, c.playbackBarIndex);
    const double e = qBound(0.0, c.energy, 1.0);
    const double rc = qBound(0.0, c.rhythmicComplexity, 1.0);
    const bool cadence = (c.cadence01 >= 0.35) || c.phraseEndBar || c.chordIsNew;

    // Phrase guide tone target for this bar (computed in ensurePhraseGuideLinePlanned()).
    const int phraseBars = qMax(1, m_phraseGuideBars);
    const int phraseStart = (bar / phraseBars) * phraseBars;
    const int idx = qBound(0, bar - phraseStart, phraseBars - 1);
    const int targetGuidePc = (idx >= 0 && idx < m_phraseGuidePcByBar.size()) ? m_phraseGuidePcByBar[idx] : -1;
    const int prevGuidePc = (idx > 0 && (idx - 1) < m_phraseGuidePcByBar.size()) ? m_phraseGuidePcByBar[idx - 1] : -1;

    // Deterministic decision: we want a bit more continuous RH intention than the old sparse motifs,
    // but still back off when user is not silent and energy is very low.
    const quint32 h = virtuoso::util::StableHash::fnv1a32(QString("pno_gt|%1|%2|%3")
                                                              .arg(c.chordText)
                                                              .arg(bar)
                                                              .arg(c.determinismSeed)
                                                              .toUtf8());
    const bool wantAny =
        c.userSilence
        || c.chordIsNew
        || c.nextChanges
        || cadence
        || (rc >= 0.55 && int(h % 100u) < int(llround(20.0 + 45.0 * rc)));
    if (!wantAny && !c.userSilence && e < 0.10) return {};

    // Build a chord-safe pool, then (if we have a key) keep it diatonic for non-spicy chords.
    const int pc1 = (c.chord.rootPc >= 0) ? (c.chord.rootPc % 12) : 0;
    const int pc3 = pcForDegree(c.chord, 3);
    const int pc5 = pcForDegree(c.chord, 5);
    const int pc7 = pcForDegree(c.chord, 7);
    const int pc9 = pcForDegree(c.chord, 9);
    const int pc11 = pcForDegree(c.chord, 11);
    const int pc13 = pcForDegree(c.chord, 13);

    const bool spicy = (c.chord.quality == music::ChordQuality::Dominant
                        || c.chord.quality == music::ChordQuality::Diminished
                        || c.chord.quality == music::ChordQuality::HalfDiminished
                        || c.chord.alt
                        || !c.chord.alterations.isEmpty());

    QSet<int> safe;
    auto addSafe = [&](int pc) { if (pc >= 0) safe.insert((pc % 12 + 12) % 12); };
    // keep guide tones always
    addSafe(pc3);
    addSafe(pc5);
    if (pc7 >= 0) addSafe(pc7);
    if (spicy) addSafe(pc1);
    addSafe(pc9);
    if (c.chord.quality == music::ChordQuality::Minor) addSafe(pc11);
    if (c.chord.quality != music::ChordQuality::Diminished) addSafe(pc13);
    for (const auto& a : c.chord.alterations) {
        if (a.degree == 11) addSafe(pc11);
        if (a.degree == 9) addSafe(pc9);
        if (a.degree == 13) addSafe(pc13);
    }

    // Key diatonic filter (only for non-spicy chords; we want secondary dominants to exist).
    QSet<int> keySafe;
    if (c.hasKey) {
        const int t = (c.keyTonicPc % 12 + 12) % 12;
        const int stepsIonian[7] = {0,2,4,5,7,9,11};
        const int stepsAeolian[7] = {0,2,3,5,7,8,10};
        const int* steps = stepsIonian;
        switch (c.keyMode) {
            case virtuoso::theory::KeyMode::Minor: steps = stepsAeolian; break;
            default: steps = stepsIonian; break;
        }
        for (int i = 0; i < 7; ++i) keySafe.insert((t + steps[i]) % 12);
    }

    auto tonalSafe = [&]() -> QSet<int> {
        if (spicy || keySafe.isEmpty()) return safe;
        QSet<int> x = safe;
        x.intersect(keySafe);
        // Always keep the chord's guide tones, even if local key estimate is off.
        if (pc3 >= 0) x.insert((pc3 % 12 + 12) % 12);
        if (pc7 >= 0) x.insert((pc7 % 12 + 12) % 12);
        if (pc7 < 0 && pc5 >= 0) x.insert((pc5 % 12 + 12) % 12);
        return x;
    }();

    auto nearestInSet = [&](int aroundPc, const QSet<int>& set) -> int {
        if (set.isEmpty()) return aroundPc;
        const int a = (aroundPc % 12 + 12) % 12;
        int best = -1;
        int bestDist = 999;
        for (int pc : set) {
            int d = pc - a;
            while (d > 6) d -= 12;
            while (d < -6) d += 12;
            const int dist = qAbs(d);
            if (dist < bestDist) { bestDist = dist; best = pc; }
        }
        return (best >= 0) ? best : a;
    };

    const int tgt = (targetGuidePc >= 0) ? (targetGuidePc % 12 + 12) % 12 : nearestInSet((pc7 >= 0 ? pc7 : pc3), tonalSafe);
    const int prev = (prevGuidePc >= 0) ? nearestInSet(prevGuidePc, tonalSafe) : nearestInSet(tgt, tonalSafe);

    QVector<TopHit> out;
    out.reserve(3);

    auto push = [&](int beat, int sub, int count, virtuoso::groove::Rational dur, int velDelta, int pc, bool resolve, QString tag) {
        TopHit t;
        t.beatInBar = qBound(0, beat, 3);
        t.sub = qMax(0, sub);
        t.count = qMax(1, count);
        if (t.sub >= t.count) t.sub = t.count - 1;
        t.dur = dur;
        t.velDelta = velDelta;
        t.pc = (pc % 12 + 12) % 12;
        t.resolve = resolve;
        t.tag = std::move(tag);
        out.push_back(t);
    };

    // Main gesture: neighbor-ish tone then resolve to the guide-tone target on beat 4.
    if (wantAny && tgt >= 0) {
        // Put the "approach" on & of 3 (keeps ballad float) unless we're very sparse, then & of 2.
        const bool earlier = (!c.userSilence && e < 0.40 && !cadence);
        const int approachBeat = earlier ? 1 : 2; // beat 2 or 3 (0-based)
        const int approachPc = (prev >= 0) ? prev : tgt;
        const int velA = qBound(-30, int(llround(-20 + 10 * rc + (cadence ? 4 : 0))), -6);
        push(approachBeat, 1, 2, {1, 16}, velA, approachPc, false, "gt_approach");
        const int velR = qBound(-18, int(llround(-12 + 10 * rc + (cadence ? 6 : 0))), +6);
        push(3, 0, 1, {1, 8}, velR, tgt, true, "gt_resolve");
    }

    // Extra tiny pickup (more conversational) on & of 4 when harmony changes next bar.
    if (c.nextChanges && !c.userDensityHigh && rc >= 0.50 && int((h / 7u) % 100u) < int(llround(12.0 + 30.0 * rc))) {
        // Anticipate the same guide target very softly; keeps it tonal and avoids random chromaticism.
        push(3, 1, 2, {1, 16}, -20, tgt, false, "gt_pickup");
    }

    return out;
}

void JazzBalladPianoPlanner::ensurePhraseGuideLinePlanned(const Context& c) {
    const int bar = qMax(0, c.playbackBarIndex);
    const int phraseBars = qMax(1, c.phraseBars);
    const int phraseStart = (bar / phraseBars) * phraseBars;
    if (phraseStart != m_phraseGuideStartBar || phraseBars != m_phraseGuideBars) {
        m_phraseGuideStartBar = phraseStart;
        m_phraseGuideBars = phraseBars;
        m_phraseGuidePcByBar = QVector<int>(phraseBars, -1);
    }

    const int idx = qBound(0, bar - phraseStart, phraseBars - 1);
    if (idx < 0 || idx >= m_phraseGuidePcByBar.size()) return;
    if (m_phraseGuidePcByBar[idx] >= 0) return; // already planned

    const int prev = (idx > 0) ? m_phraseGuidePcByBar[idx - 1] : -1;
    const bool preferResolve = (c.cadence01 >= 0.55) || c.phraseEndBar || c.chordIsNew;
    const int pc = chooseGuidePcForChord(c.chord, prev, preferResolve);
    m_phraseGuidePcByBar[idx] = pc;
}

int JazzBalladPianoPlanner::chooseGuidePcForChord(const music::ChordSymbol& chord, int prevGuidePc, bool preferResolve) const {
    const int pc3 = pcForDegree(chord, 3);
    const int pc7 = pcForDegree(chord, 7);
    const int pc5 = pcForDegree(chord, 5);
    auto norm = [](int pc) { return (pc % 12 + 12) % 12; };

    QVector<int> cand;
    cand.reserve(2);
    if (pc3 >= 0) cand.push_back(norm(pc3));
    if (pc7 >= 0) cand.push_back(norm(pc7));
    if (cand.isEmpty() && pc5 >= 0) cand.push_back(norm(pc5));
    if (cand.isEmpty()) return 0;
    sortUnique(cand);

    const int prev = (prevGuidePc >= 0) ? norm(prevGuidePc) : -1;
    auto dist = [&](int a, int b) -> int {
        int d = a - b;
        while (d > 6) d -= 12;
        while (d < -6) d += 12;
        return qAbs(d);
    };

    // Prefer staying close; at cadences, prefer 3rd slightly (sounds like resolution).
    int best = cand.first();
    int bestScore = 999;
    for (int p : cand) {
        int s = 0;
        if (prev >= 0) s += dist(p, prev) * 10;
        if (preferResolve && pc3 >= 0 && p == norm(pc3)) s -= 3;
        if (preferResolve && pc7 >= 0 && p == norm(pc7)) s += 1;
        if (s < bestScore) { bestScore = s; best = p; }
    }
    return best;
}

QVector<JazzBalladPianoPlanner::CompHit> JazzBalladPianoPlanner::chooseBarCompRhythm(const Context& c) const {
    QVector<CompHit> out;
    out.reserve(6);

    // Coherence: keep the comping feel stable for 2-bar blocks (less "random new pattern every bar").
    const int barBlock = qMax(0, c.playbackBarIndex) / 2;
    const quint32 h = virtuoso::util::StableHash::fnv1a32(QString("pno_rhy|%1|%2|%3")
                                                              .arg(c.chordText)
                                                              .arg(barBlock)
                                                              .arg(c.determinismSeed)
                                                              .toUtf8());
    const double e = qBound(0.0, c.energy, 1.0);
    const double progress01 = qBound(0.0, double(qMax(0, c.playbackBarIndex)) / 24.0, 1.0);
    const bool climax = c.forceClimax && (e >= 0.75);
    const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
    const bool phraseEnd = c.phraseEndBar;

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

        // Ballad sanity: keep to a small set of idiomatic feels.
        // We intentionally avoid patterns that sound "stiff" or "random" in slow ballads.
        switch (variant) {
            case 6: // Arrival on 1 + 4 (good for chord changes)
                addV(0, 0, 1, chooseHold("arrival1", /*canPad*/true, /*canLong*/true), -2, "guide", "arrival1");
                addV(3, 0, 1, chooseHold("backbeat4_short", /*canPad*/true, /*canLong*/true), 0, "full", phraseEnd ? "only4_end" : "only4");
                break;
            case 0: // Classic 2&4
                addV(1, 0, 1, chooseHold("backbeat2", /*canPad*/false, /*canLong*/true), -2, "guide", "backbeat2");
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
    // Variant 1 ("delay2") removed; keep search space small and idiomatic.
    for (int v = 0; v <= 6; ++v) {
        if (v == 1) continue;
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

    // If we ended up overly sparse in non-busy contexts, add a soft "breath" comp to keep motion.
    if (!userBusy && complexity > 0.40) {
        const int minHits = (c.userSilence || c.cadence01 >= 0.55) ? 3 : 2;
        if (out.size() < minHits) {
            const bool alreadyHas2 = std::any_of(out.begin(), out.end(), [](const CompHit& x) { return x.beatInBar == 1; });
            if (!alreadyHas2) add(1, 1, 2, virtuoso::groove::Rational(1, 16), -14, "guide", "breath_and2");
            else add(2, 1, 2, virtuoso::groove::Rational(1, 16), -14, "guide", "breath_and3");
        }
    }

    return out;
}

} // namespace playback

