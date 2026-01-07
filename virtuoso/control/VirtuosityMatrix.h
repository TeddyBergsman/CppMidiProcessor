#pragma once

#include <QJsonObject>

namespace virtuoso::control {

// Stage 3: user-facing "Virtuosity Matrix" (glass-box controls).
// All values are normalized 0..1.
struct VirtuosityMatrix {
    double harmonicRisk = 0.20;
    double rhythmicComplexity = 0.25;
    double interaction = 0.50;
    double toneDark = 0.60;

    QJsonObject toJsonObject() const {
        QJsonObject o;
        o.insert("harmonic_risk", harmonicRisk);
        o.insert("rhythmic_complexity", rhythmicComplexity);
        o.insert("interaction", interaction);
        o.insert("tone_dark", toneDark);
        return o;
    }
};

} // namespace virtuoso::control

