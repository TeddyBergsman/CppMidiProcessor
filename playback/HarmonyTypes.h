#pragma once

#include <QString>

#include "virtuoso/theory/FunctionalHarmony.h"

namespace playback {

// Shared harmony context types (kept small and stable).
struct LocalKeyEstimate {
    int tonicPc = 0;
    QString scaleKey;
    QString scaleName;
    virtuoso::theory::KeyMode mode = virtuoso::theory::KeyMode::Major;
    double score = 0.0;
    double coverage = 0.0;
};

} // namespace playback

