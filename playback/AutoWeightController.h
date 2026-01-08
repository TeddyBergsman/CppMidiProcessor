#pragma once

#include <QString>
#include <QtGlobal>

#include "virtuoso/control/PerformanceWeightsV2.h"

namespace playback {

// Deterministic macro controller that derives ensemble-level weights from:
// - song form section (Intro/Verse/Bridge/Chorus/Outro, etc)
// - phrase position + cadence strength
// - user activity snapshot flags
struct AutoWeightController final {
    struct Inputs {
        QString sectionLabel;   // from ChartModel (already present in iReal exports)
        int repeatIndex = 0;    // 0-based
        int repeatsTotal = 1;
        int playbackBarIndex = 0;
        int phraseBars = 4;
        int barInPhrase = 0;
        bool phraseEndBar = false;
        double cadence01 = 0.0; // 0..1

        // User now
        bool userSilence = false;
        bool userBusy = false;
        bool userRegisterHigh = false;
        bool userIntensityPeak = false;
    };

    static virtuoso::control::PerformanceWeightsV2 compute(const Inputs& in);
};

} // namespace playback

