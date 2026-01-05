#include "virtuoso/constraints/BassDriver.h"

#include <QtGlobal>
#include <limits>

namespace virtuoso::constraints {
namespace {

struct Fingering {
    int stringIndex = -1; // 0..3 (E,A,D,G)
    int fret = -1;        // 0..maxFret
    double cost = 0.0;
};

} // namespace

FeasibilityResult BassDriver::evaluateFeasibility(const PerformanceState& state,
                                                  const CandidateGesture& candidate) const {
    FeasibilityResult r;

    if (candidate.midiNotes.isEmpty()) {
        r.ok = true;
        r.reasons.push_back("OK: empty gesture");
        return r;
    }
    if (candidate.midiNotes.size() != 1) {
        r.ok = false;
        r.reasons.push_back("FAIL: BassDriver Stage-1 expects exactly 1 note per gesture");
        return r;
    }

    const int note = candidate.midiNotes[0];

    // Standard 4-string bass open strings: E1(40), A1(45), D2(50), G2(55)
    const int open[4] = {40, 45, 50, 55};

    const int lastFret = state.ints.value("lastFret", -1);

    Fingering best;
    best.cost = std::numeric_limits<double>::infinity();

    for (int s = 0; s < 4; ++s) {
        const int fret = note - open[s];
        if (fret < 0 || fret > m_c.maxFret) continue;

        double cost = 0.0;

        // Prefer open strings.
        if (fret == 0) cost -= m_c.openStringBonus;

        // Penalize large shifts from last fret if known.
        if (lastFret >= 0) cost += 0.10 * double(qAbs(fret - lastFret));

        // Slight preference for lower strings (thicker tone) when ambiguous.
        cost += 0.02 * double(s);

        if (cost < best.cost) {
            best.stringIndex = s;
            best.fret = fret;
            best.cost = cost;
        }
    }

    if (best.stringIndex < 0) {
        r.ok = false;
        r.reasons.push_back(QString("FAIL: note %1 not playable on 4-string bass within maxFret=%2")
                                .arg(note)
                                .arg(m_c.maxFret));
        return r;
    }

    if (lastFret >= 0 && qAbs(best.fret - lastFret) > m_c.maxFretShiftPerNote) {
        r.ok = false;
        r.reasons.push_back(QString("FAIL: fret shift %1 exceeds maxFretShiftPerNote=%2")
                                .arg(qAbs(best.fret - lastFret))
                                .arg(m_c.maxFretShiftPerNote));
        return r;
    }

    r.ok = true;
    r.cost = best.cost;
    r.reasons.push_back(QString("OK: note=%1 string=%2 fret=%3 cost=%4")
                            .arg(note)
                            .arg(best.stringIndex)
                            .arg(best.fret)
                            .arg(best.cost, 0, 'f', 3));
    return r;
}

} // namespace virtuoso::constraints

