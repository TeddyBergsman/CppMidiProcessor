#include "virtuoso/constraints/PianoDriver.h"

#include <algorithm>

namespace virtuoso::constraints {

FeasibilityResult PianoDriver::evaluateFeasibility(const PerformanceState& state,
                                                   const CandidateGesture& candidate) const {
    FeasibilityResult r;

    if (candidate.midiNotes.isEmpty()) {
        r.ok = true;
        r.cost = 0.0;
        r.reasons.push_back("OK: empty gesture");
        return r;
    }

    const int cc64 = state.ints.value("cc64", 0);
    const bool sustainDown = (cc64 >= 96);
    const bool sustainHalf = (!sustainDown && cc64 >= 32);
    const bool sustainAny = sustainDown || sustainHalf;

    QVector<int> notes = candidate.midiNotes;
    std::sort(notes.begin(), notes.end());
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());

    // Finger budget.
    if (notes.size() > m_c.maxFingers) {
        r.ok = false;
        r.reasons.push_back(QString("FAIL: polyphony %1 exceeds maxFingers=%2")
                                .arg(notes.size())
                                .arg(m_c.maxFingers));
        return r;
    }

    // Two-hand feasibility: choose a min-cost assignment of notes to LH/RH.
    struct Split {
        bool ok = false;
        double cost = 0.0;
        int lhCount = 0;
        int rhCount = 0;
        int lhMin = 127;
        int lhMax = 0;
        int rhMin = 127;
        int rhMax = 0;
    };
    Split best;
    best.ok = false;
    best.cost = 1e18;

    const int n = notes.size();
    const int maxMask = (n >= 31) ? 0 : (1 << n); // n is small (<=10), safe.
    for (int mask = 0; mask < maxMask; ++mask) {
        const int lhCount = __builtin_popcount((unsigned int)mask);
        const int rhCount = n - lhCount;
        if (lhCount == 0 || rhCount == 0) {
            // Allow one-handed chords, but with a cost bias.
        }
        if (m_c.maxFingersPerHand > 0) {
            if (lhCount > m_c.maxFingersPerHand) continue;
            if (rhCount > m_c.maxFingersPerHand) continue;
        }

        int lhMin = 127, lhMax = 0, rhMin = 127, rhMax = 0;
        for (int i = 0; i < n; ++i) {
            const int m = notes[i];
            if (mask & (1 << i)) { lhMin = qMin(lhMin, m); lhMax = qMax(lhMax, m); }
            else { rhMin = qMin(rhMin, m); rhMax = qMax(rhMax, m); }
        }
        const int lhSpan = (lhCount >= 2) ? (lhMax - lhMin) : 0;
        const int rhSpan = (rhCount >= 2) ? (rhMax - rhMin) : 0;
        const int globalSpan = notes.last() - notes.first();

        // Each hand must be able to reach its assigned cluster.
        if (lhSpan > m_c.maxSpanSemitones) continue;
        if (rhSpan > m_c.maxSpanSemitones) continue;
        // Global span is still a sanity check (massive stretches are not realistic even with split).
        if (globalSpan > (m_c.maxSpanSemitones + 12)) continue;

        double cost = 0.0;
        cost += 0.25 * double(n);
        cost += 0.30 * double(lhSpan + rhSpan);

        // Penalize awkward crossings (LH above RH).
        if (lhCount > 0 && rhCount > 0 && lhMax > rhMin) {
            cost += 2.0 + 0.55 * double((lhMax - rhMin) + 1);
        }
        // Prefer LH lower than RH when both exist.
        if (lhCount > 0 && rhCount > 0) {
            const double lhMean = 0.5 * double(lhMin + lhMax);
            const double rhMean = 0.5 * double(rhMin + rhMax);
            if (lhMean > rhMean) cost += 2.5 + 0.35 * (lhMean - rhMean);
        }
        // One-hand bias (avoid overusing one-handed 6+ note grips).
        if (lhCount == 0 || rhCount == 0) {
            cost += 1.25 + 0.25 * double(globalSpan);
        }

        if (cost < best.cost) {
            best.ok = true;
            best.cost = cost;
            best.lhCount = lhCount;
            best.rhCount = rhCount;
            best.lhMin = lhMin; best.lhMax = lhMax;
            best.rhMin = rhMin; best.rhMax = rhMax;
        }
    }

    if (!best.ok) {
        r.ok = false;
        r.reasons.push_back("FAIL: no feasible two-hand assignment under span/finger limits");
        return r;
    }

    r.ok = true;
    r.cost = best.cost;

    // Pedaling logic (approx):
    // - sustainDown allows accumulation of sounding notes (heldNotes + new notes)
    // - too many sustained notes adds cost; extreme counts fail (mud / unrealistic)
    const int held = state.heldNotes.size();
    const int effHeld = sustainDown ? held : (sustainHalf ? int(llround(double(held) * 0.55)) : 0);
    const int sounding = sustainAny ? (effHeld + notes.size()) : notes.size();
    if (sustainAny) {
        const int hard = sustainDown ? m_c.maxSustainedNotesHard : int(llround(double(m_c.maxSustainedNotesHard) * 0.75));
        const int soft = sustainDown ? m_c.maxSustainedNotesSoft : int(llround(double(m_c.maxSustainedNotesSoft) * 0.75));
        if (sounding > hard) {
            r.ok = false;
            r.reasons.push_back(QString("FAIL: sustained sounding notes %1 exceeds maxSustainedNotesHard=%2")
                                    .arg(sounding)
                                    .arg(hard));
            return r;
        }
        if (sounding > soft) {
            r.cost += 0.35 * double(sounding - soft);
            r.reasons.push_back(QString("WARN: sustain wash (sounding=%1 > soft=%2)")
                                    .arg(sounding)
                                    .arg(soft));
        }
    }

    // Re-strike penalty (avoid repeated attacks on the same keys under sustain).
    if (sustainAny && m_c.maxRestrikesUnderSustainHard > 0) {
        int restrikes = 0;
        for (int m : notes) {
            if (state.heldNotes.contains(m)) ++restrikes;
        }
        const int hard = sustainDown ? m_c.maxRestrikesUnderSustainHard : qMax(1, int(llround(double(m_c.maxRestrikesUnderSustainHard) * 0.7)));
        const int soft = sustainDown ? m_c.maxRestrikesUnderSustainSoft : qMax(0, int(llround(double(m_c.maxRestrikesUnderSustainSoft) * 0.7)));
        if (restrikes > hard) {
            r.ok = false;
            r.reasons.push_back(QString("FAIL: restrikes=%1 exceeds maxRestrikesUnderSustainHard=%2")
                                    .arg(restrikes)
                                    .arg(hard));
            return r;
        }
        if (restrikes > soft) {
            r.cost += 0.50 * double(restrikes - soft);
            r.reasons.push_back(QString("WARN: restrike smear (restrikes=%1 > soft=%2)")
                                    .arg(restrikes)
                                    .arg(soft));
        }
    }

    r.reasons.push_back(QString("OK: notes=%1 cc64=%2 pedal=%3 lh=%4(%5..%6) rh=%7(%8..%9) sounding=%10")
                            .arg(notes.size())
                            .arg(cc64)
                            .arg(sustainDown ? "down" : (sustainHalf ? "half" : "up"))
                            .arg(best.lhCount).arg(best.lhMin).arg(best.lhMax)
                            .arg(best.rhCount).arg(best.rhMin).arg(best.rhMax)
                            .arg(sounding));
    return r;
}

} // namespace virtuoso::constraints

