#pragma once

#include <QString>
#include <QVector>

#include "virtuoso/groove/GrooveGrid.h"

namespace virtuoso::groove {

// How the groove template defines offsets.
enum class OffsetUnit {
    // Offset value is in milliseconds (tempo-independent).
    Ms = 0,
    // Offset value is expressed as a fraction of the current beat duration (tempo-scaled).
    BeatFraction = 1,
};

// Musical subdivision grid (v1 focuses on jazz-relevant grids).
enum class GrooveGridKind {
    Straight = 0,     // no systematic offsets
    Swing8,           // swing the upbeat 8th
    Triplet8,         // triplet feel (1/3 grid)
    Shuffle12_8,      // 12/8 shuffle (jazz/blues)
    Straight16,       // placeholder for future (funk etc.)
};

struct OffsetPoint {
    // Within-beat position as a normalized fraction of the beat (0..1).
    // Examples:
    // - upbeat 8th: 1/2
    // - triplet partials: 1/3, 2/3
    Rational withinBeat{0, 1};

    OffsetUnit unit = OffsetUnit::Ms;
    double value = 0.0; // ms or beat-fraction depending on unit
};

// A groove template is a reusable, deterministic offset-map over a grid.
// This is intended to become a large vocabulary over time, but remains code-defined for now.
struct GrooveTemplate {
    QString key;          // stable id, e.g. "jazz_swing_2to1"
    QString name;         // display label
    QString category;     // e.g. "Jazz/Swing"
    GrooveGridKind gridKind = GrooveGridKind::Straight;

    // 0..1 scaling of this template's offsets (0 disables the template offsets).
    double amount = 1.0;

    QVector<OffsetPoint> offsetMap;

    // Compute template-only offset in ms for the given grid position.
    // This does NOT include per-instrument push/jitter/drift.
    int offsetMsFor(const GridPos& pos, const TimeSignature& ts, int bpm) const;
};

} // namespace virtuoso::groove

