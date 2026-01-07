#pragma once

#include "virtuoso/constraints/IInstrumentDriver.h"

namespace virtuoso::constraints {

struct DrumConstraints {
    int maxSimultaneousHands = 2;
    int maxSimultaneousFeet = 2;

    // Simple traversal penalty between zones (stored in PerformanceState.ints["lastDrumZone"]).
    double zoneChangeCost = 0.25;
};

class DrumDriver final : public IInstrumentDriver {
public:
    explicit DrumDriver(DrumConstraints c = {}) : m_c(std::move(c)) {}

    FeasibilityResult evaluateFeasibility(const PerformanceState& state,
                                          const CandidateGesture& candidate) const override;

    const DrumConstraints& constraints() const { return m_c; }

private:
    enum class LimbKind { Hand, Foot };

    struct HitClass {
        LimbKind limb = LimbKind::Hand;
        int zone = 0; // 0..N
        QString name;
    };

    static HitClass classify(int midiNote);

    DrumConstraints m_c;
};

} // namespace virtuoso::constraints

