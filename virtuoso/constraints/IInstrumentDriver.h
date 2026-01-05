#pragma once

#include "virtuoso/constraints/ConstraintsTypes.h"

namespace virtuoso::constraints {

class IInstrumentDriver {
public:
    virtual ~IInstrumentDriver() = default;

    virtual FeasibilityResult evaluateFeasibility(const PerformanceState& state,
                                                  const CandidateGesture& candidate) const = 0;
};

} // namespace virtuoso::constraints

