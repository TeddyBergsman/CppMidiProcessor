#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace virtuoso::theory {

struct GrooveTemplate {
    QString key;
    QString name;
    QStringList tags;
    int order = 1000;

    // Swing ratio for 8th-note swing within one beat (2 subdivisions).
    // 0.50 = straight; 0.66 ~= triplet swing.
    double swing = 0.50;

    // Additional offset (ms) applied to offbeats (odd steps) to get "laid back" / "pushed" feel.
    int pocketMs = 0;

    // Random jitter (ms) per step, symmetric [-humanizeMs, +humanizeMs].
    int humanizeMs = 0;
};

class GrooveEngine {
public:
    static QVector<GrooveTemplate> builtins();

    // Returns absolute due times in milliseconds from start, one per step.
    // Guarantees: non-decreasing due times (monotonic).
    static QVector<int> scheduleDueMs(int steps,
                                      int baseStepMs,
                                      int stepsPerBeat,
                                      const GrooveTemplate& tpl,
                                      quint32 seed);
};

} // namespace virtuoso::theory

