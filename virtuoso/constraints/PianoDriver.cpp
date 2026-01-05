#include "virtuoso/constraints/PianoDriver.h"

#include <algorithm>

namespace virtuoso::constraints {

FeasibilityResult PianoDriver::evaluateFeasibility(const PerformanceState&,
                                                   const CandidateGesture& candidate) const {
    FeasibilityResult r;

    if (candidate.midiNotes.isEmpty()) {
        r.ok = true;
        r.cost = 0.0;
        r.reasons.push_back("OK: empty gesture");
        return r;
    }

    // Finger budget.
    if (candidate.midiNotes.size() > m_c.maxFingers) {
        r.ok = false;
        r.reasons.push_back(QString("FAIL: polyphony %1 exceeds maxFingers=%2")
                                .arg(candidate.midiNotes.size())
                                .arg(m_c.maxFingers));
        return r;
    }

    // Span constraint.
    const auto [minIt, maxIt] = std::minmax_element(candidate.midiNotes.begin(), candidate.midiNotes.end());
    const int span = *maxIt - *minIt;
    if (span > m_c.maxSpanSemitones) {
        r.ok = false;
        r.reasons.push_back(QString("FAIL: span %1 semitones exceeds maxSpanSemitones=%2")
                                .arg(span)
                                .arg(m_c.maxSpanSemitones));
        return r;
    }

    // Cost: prefer smaller spans and fewer notes, all else equal.
    r.ok = true;
    r.cost = double(span) + 0.25 * double(candidate.midiNotes.size());
    r.reasons.push_back(QString("OK: polyphony=%1 span=%2").arg(candidate.midiNotes.size()).arg(span));

    // Pedaling is intentionally a placeholder in Stage 1.
    return r;
}

} // namespace virtuoso::constraints

