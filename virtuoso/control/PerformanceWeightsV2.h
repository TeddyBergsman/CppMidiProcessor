#pragma once

#include <QString>
#include <QJsonObject>

namespace virtuoso::control {

// Global ensemble-level performance weights (0..1).
// These are intended to be exhaustive enough to describe "how the band should feel",
// not instrument-specific knobs.
struct PerformanceWeightsV2 final {
    // Texture / time
    double density = 0.35;   // overall activity (rests vs events)
    double rhythm = 0.35;    // rhythmic complexity (syncopation/subdiv richness)
    double emotion = 0.35;   // time-feel freedom (more rubato/laidback/elasticity)

    // Touch / dynamics
    double intensity = 0.40; // average impact/velocity
    double dynamism = 0.45;  // phrase-level dynamic arcs (contrast/shape)

    // Harmony / narrative
    double creativity = 0.25; // harmonic adventurousness (subs/colors)
    double tension = 0.45;    // tension->release shaping near cadences

    // Interaction / novelty
    double interactivity = 0.55; // responsiveness to user
    double variability = 0.35;   // concept variety / anti-repetition pressure

    // Timbre / warmth (replaces old toneDark)
    double warmth = 0.60; // 0 bright/dry, 1 warm/dark/legato

    void clamp01();
    QJsonObject toJson() const;
    static PerformanceWeightsV2 fromJson(const QJsonObject& o);
};

} // namespace virtuoso::control

