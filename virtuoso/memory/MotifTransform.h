#pragma once

#include <QString>
#include <QVector>

namespace virtuoso::memory {

enum class PitchMotifTransform {
    Repeat = 0,
    Sequence = 1,
    Invert = 2,
    Retrograde = 3,
    RhythmicDisplace = 4,
};

struct PitchMotifTransformResult {
    QVector<int> pcs;          // 0..11, same length as input (or empty on failure)
    PitchMotifTransform kind = PitchMotifTransform::Repeat;
    bool displaceRhythm = false;
    QString tag;              // e.g. "mem:sequence"
};

// Deterministically transform a short pitch-class motif (length >= 3 recommended).
// - `basePcs`: pitch classes (0..11) of the remembered motif.
// - `resolvePc`: target pitch class to resolve toward (used for Sequence).
// - `modeSeed`: a stable seed; kind = modeSeed % 5.
PitchMotifTransformResult transformPitchMotif(const QVector<int>& basePcs, int resolvePc, quint32 modeSeed);

} // namespace virtuoso::memory

