#pragma once

#include "virtuoso/constraints/IInstrumentDriver.h"

namespace virtuoso::constraints {

struct BassConstraints {
    // Stage 1: 4-string bass with up to this fret inclusive.
    int maxFret = 24;

    // If we track a lastFret, disallow shifting more than this per note.
    int maxFretShiftPerNote = 7;

    // If we track a lastString, disallow jumping more than this many strings per note.
    int maxStringJumpPerNote = 1;

    // Legato/slide modeling for multi-note gestures:
    // - Favor same-string legato for small fret deltas.
    // - Allow slides up to this delta (higher cost).
    int maxLegatoFretDelta = 4;
    int maxSlideFretDelta = 12;

    // Prefer open strings slightly (tone/effort).
    double openStringBonus = 0.35;
};

class BassDriver final : public IInstrumentDriver {
public:
    explicit BassDriver(BassConstraints c = {}) : m_c(std::move(c)) {}

    FeasibilityResult evaluateFeasibility(const PerformanceState& state,
                                          const CandidateGesture& candidate) const override;

    const BassConstraints& constraints() const { return m_c; }

private:
    BassConstraints m_c;
};

} // namespace virtuoso::constraints

