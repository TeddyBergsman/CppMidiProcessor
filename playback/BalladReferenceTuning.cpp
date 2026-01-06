#include "playback/BalladReferenceTuning.h"

namespace playback {

BalladRefTuning tuningForReferenceTrack(const QString& presetKey) {
    // Reference track: Chet Baker – "My Funny Valentine" (brushes ballad, airy/sparse).
    // If using the Evans preset, keep it a bit denser and more rootless.
    BalladRefTuning t;
    if (presetKey.contains("evans", Qt::CaseInsensitive)) {
        t.bassApproachProbBeat3 = 0.62;
        t.bassSkipBeat3ProbStable = 0.18;
        t.pianoSkipBeat2ProbStable = 0.30;
        t.pianoAddSecondColorProb = 0.40;
        t.pianoSparkleProbBeat4 = 0.22;
        t.pianoPreferShells = false;
        t.pianoLhLo = 48; t.pianoLhHi = 67;
        t.pianoRhLo = 65; t.pianoRhHi = 86;
        t.pianoSparkleLo = 82; t.pianoSparkleHi = 98;
    }
    return t;
}

} // namespace playback

