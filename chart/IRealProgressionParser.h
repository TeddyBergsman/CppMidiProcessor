#pragma once

#include <QString>

#include "chart/ChartModel.h"

namespace chart {

// Parses a decoded iReal progression/token string into a grid model.
// This is intentionally a “v1 minimal” parser tuned to iReal exports:
// - default 4 cells per bar, 4 bars per line (16 cells/line)
// - recognizes barlines: |, [, ], {, }, Z
// - recognizes section marks: *A, *B, ...
// - recognizes time signature tokens: T44, T34, T68, etc (applies globally for now)
// - treats single spaces as cell separators; runs of 2-3 spaces indicate empty cells
ChartModel parseIRealProgression(const QString& decodedProgression);

} // namespace chart

