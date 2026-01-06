#pragma once

#include "playback/SemanticMidiAnalyzer.h"

#include <QtGlobal>
#include <QString>

namespace playback {

// Module 4.2/5.1-ish (MVP): a simple macro-dynamics state machine.
// It turns intent signals into ensemble-level "vibe" (Simmer/Build/Climax/CoolDown).
//
// Deterministic contract: no RNG; state depends only on the observed intent timeline.
class VibeStateMachine {
public:
    enum class Vibe { Simmer, Build, Climax, CoolDown };

    struct Settings {
        // Hysteresis to avoid rapid flipping.
        int minStateHoldMs = 2500;

        // Build thresholds (lower + more continuous than the old boolean gates).
        double buildEnterNotesPerSec = 2.0;
        int buildEnterMs = 250;
        int buildExitMs = 2600; // linger: require calm for a while before dropping from Build

        // How quickly we enter Climax on sustained intensity peaks.
        int climaxEnterMs = 500;   // lowered threshold
        // How quickly we exit Climax once intensity drops (linger).
        int climaxExitMs = 2600;
        // If user silence persists, we relax.
        int coolDownEnterMs = 2400; // don't drop to cooldown on brief rests

        // Energy smoothing (continuous transitions).
        int energyRiseTauMs = 520;   // faster attack
        int energyFallTauMs = 1500;  // slower release
    };

    struct Output {
        Vibe vibe = Vibe::Simmer;
        // 0..1 summary knob for how "active" the band should be.
        double energy = 0.25;
        // Optional label for TheoryEvent tagging.
        QString reason;
    };

    VibeStateMachine() = default;
    explicit VibeStateMachine(const Settings& s) : m_s(s) {}

    void reset();

    Output update(const SemanticMidiAnalyzer::IntentState& intent, qint64 nowMs);

    static QString vibeName(Vibe v);

private:
    Settings m_s;
    Vibe m_vibe = Vibe::Simmer;
    qint64 m_lastStateChangeMs = 0;

    qint64 m_intensitySinceMs = -1;
    qint64 m_silenceSinceMs = -1;
    qint64 m_buildSinceMs = -1;
    qint64 m_calmSinceMs = -1;

    double m_energy = 0.35;
    qint64 m_lastEnergyUpdateMs = -1;
};

} // namespace playback

