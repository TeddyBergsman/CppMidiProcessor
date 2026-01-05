#pragma once

#include "virtuoso/constraints/IInstrumentDriver.h"

namespace virtuoso::constraints {

struct PianoConstraints {
    int maxFingers = 10;
    // A 10th is 16 semitones (e.g., C to E an octave higher).
    int maxSpanSemitones = 16;
};

class PianoDriver final : public IInstrumentDriver {
public:
    explicit PianoDriver(PianoConstraints c = {}) : m_c(std::move(c)) {}

    FeasibilityResult evaluateFeasibility(const PerformanceState& state,
                                          const CandidateGesture& candidate) const override;

    const PianoConstraints& constraints() const { return m_c; }

private:
    PianoConstraints m_c;
};

} // namespace virtuoso::constraints

