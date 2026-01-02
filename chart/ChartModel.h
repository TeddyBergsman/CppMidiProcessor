#pragma once

#include <QString>
#include <QVector>

namespace chart {

struct Cell {
    // Chord text shown in this cell (empty means blank)
    QString chord;
    bool isPlaceholder = false; // e.g. explicit | x placeholders
};

struct Bar {
    // Up to 4 cells per bar (common iReal default). Some time signatures may use fewer/more later.
    QVector<Cell> cells;
    QString barlineLeft;   // e.g. "{", "[", "|", "" (visual marker before bar)
    QString barlineRight;  // e.g. "}", "]", "Z", "|" (visual marker after bar)

    // First/second endings (iReal uses N1/N2 prefixes inside the token stream).
    // If endingStart>0, this bar begins an ending. If endingEnd>0, this bar ends the active ending.
    int endingStart = 0;
    int endingEnd = 0;

    // Text annotations like "Fine" shown near the barline.
    QString annotation;
};

struct Line {
    // Typically 4 bars per line.
    QVector<Bar> bars;
    QString sectionLabel; // e.g. "A", "B" etc. if a *A token starts this line
};

struct ChartModel {
    QVector<Line> lines;
    int timeSigNum = 4;
    int timeSigDen = 4;

    // Footer annotation such as "D.C. al Fine" shown at bottom right.
    QString footerText;
};

} // namespace chart

