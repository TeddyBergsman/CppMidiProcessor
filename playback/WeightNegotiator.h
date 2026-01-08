#pragma once

#include <QJsonObject>

#include "virtuoso/control/PerformanceWeightsV2.h"

namespace playback {

// Per-agent negotiated allocation of the global weights (fully free but smoothed).
struct WeightNegotiator final {
    struct AgentWeights {
        virtuoso::control::PerformanceWeightsV2 w;
    };

    struct Output {
        virtuoso::control::PerformanceWeightsV2 global;
        AgentWeights piano;
        AgentWeights bass;
        AgentWeights drums;
        QJsonObject toJson() const;
    };

    struct State {
        // EMA for stability (per agent, per axis).
        AgentWeights piano;
        AgentWeights bass;
        AgentWeights drums;
        bool initialized = false;
    };

    struct Inputs {
        virtuoso::control::PerformanceWeightsV2 global;
        bool userBusy = false;
        bool userSilence = false;
        bool cadence = false;
        bool phraseEnd = false;
        QString sectionLabel;
    };

    static Output negotiate(const Inputs& in, State& state, double smoothingAlpha = 0.25);
};

} // namespace playback

