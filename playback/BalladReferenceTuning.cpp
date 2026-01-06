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
    // Lush ballads: warmer, more legato/connected, slightly fuller comping.
    if (presetKey.contains("lush", Qt::CaseInsensitive)) {
        t.bassApproachProbBeat3 = qMin(0.85, t.bassApproachProbBeat3 + 0.06);
        t.bassSkipBeat3ProbStable = qMax(0.05, t.bassSkipBeat3ProbStable - 0.06);
        // Piano: fewer skipped beats, more color, less sparkle ping, more mid-high warmth.
        t.pianoSkipBeat2ProbStable = qMax(0.08, t.pianoSkipBeat2ProbStable - 0.10);
        t.pianoAddSecondColorProb = qMin(0.70, t.pianoAddSecondColorProb + 0.12);
        t.pianoSparkleProbBeat4 = qMax(0.10, t.pianoSparkleProbBeat4 - 0.08);
        t.pianoPreferShells = false;
        t.pianoLhLo = qMax(44, t.pianoLhLo - 2);
        t.pianoLhHi = qMin(70, t.pianoLhHi + 1);
        t.pianoRhLo = qMax(62, t.pianoRhLo - 1);
        t.pianoRhHi = qMin(90, t.pianoRhHi + 2);
        t.pianoSparkleLo = qMin(86, qMax(t.pianoRhHi, t.pianoSparkleLo));
        t.pianoSparkleHi = qMin(100, t.pianoSparkleHi);
    }
    return t;
}

} // namespace playback

