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
    const bool sustainDown = (cc64 >= 64);

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

    // Pedaling logic (approx):
    // - sustainDown allows accumulation of sounding notes (heldNotes + new notes)
    // - too many sustained notes adds cost; extreme counts fail (mud / unrealistic)
    const int held = state.heldNotes.size();
    const int sounding = sustainDown ? (held + candidate.midiNotes.size()) : candidate.midiNotes.size();
    if (sustainDown) {
        if (sounding > m_c.maxSustainedNotesHard) {
            r.ok = false;
            r.reasons.push_back(QString("FAIL: sustained sounding notes %1 exceeds maxSustainedNotesHard=%2")
                                    .arg(sounding)
                                    .arg(m_c.maxSustainedNotesHard));
            return r;
        }
        if (sounding > m_c.maxSustainedNotesSoft) {
            r.cost += 0.35 * double(sounding - m_c.maxSustainedNotesSoft);
            r.reasons.push_back(QString("WARN: sustain wash (sounding=%1 > soft=%2)")
                                    .arg(sounding)
                                    .arg(m_c.maxSustainedNotesSoft));
        }
    }

    r.reasons.push_back(QString("OK: polyphony=%1 span=%2 cc64=%3 sustain=%4 sounding=%5")
                            .arg(candidate.midiNotes.size())
                            .arg(span)
                            .arg(cc64)
                            .arg(sustainDown ? "down" : "up")
                            .arg(sounding));
    return r;
}

} // namespace virtuoso::constraints

