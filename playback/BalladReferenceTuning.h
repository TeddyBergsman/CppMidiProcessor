#pragma once

#include <QString>

namespace playback {

// Reference-tuned parameters for our Ballad Brain (bass/piano planners).
// This is used both by the playback engine and the UI inspector so it’s not a black box.
struct BalladRefTuning {
    double bassApproachProbBeat3 = 0.55;
    double bassSkipBeat3ProbStable = 0.25;
    bool bassAllowApproachFromAbove = true;

    double pianoSkipBeat2ProbStable = 0.45;
    double pianoAddSecondColorProb = 0.25;
    double pianoSparkleProbBeat4 = 0.18;
    bool pianoPreferShells = true;

    int pianoLhLo = 50, pianoLhHi = 66;
    int pianoRhLo = 67, pianoRhHi = 84;
    int pianoSparkleLo = 84, pianoSparkleHi = 96;
};

BalladRefTuning tuningForReferenceTrack(const QString& presetKey);

} // namespace playback

