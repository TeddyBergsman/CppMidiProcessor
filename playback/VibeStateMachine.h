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
        int climaxExitMs = 5200; // require sustained calm before dropping from Climax
        // Additional Climax hysteresis: don't fall just because of a brief breath.
        // Require either sustained silence, or sustained low CC2 + low note density.
        int climaxDownSilenceMs = 3200;
        int climaxDownConfirmMs = 4200;
        int climaxDownCc2Max = 46;             // "less CC2 intensity"
        double climaxDownNotesPerSecMax = 1.2; // "less note density"
        // If user silence persists, we relax.
        int coolDownEnterMs = 2400; // don't drop to cooldown on brief rests

        // Energy smoothing (continuous transitions).
        // Musicians respond over PHRASES, not notes - these should be slow!
        int energyRiseTauMs = 1800;   // ~2 bars at 120bpm - gradual build
        int energyFallTauMs = 5000;   // VERY slow release - musicians don't drop on pauses
        int energyFallTauMsClimax = 8000; // extremely sticky at climax
        int energyFallTauMsBuild = 6000;  // very sticky in build mode
        
        // Grace period: don't start falling AT ALL for this duration after activity stops
        // This handles natural breathing/phrasing without any energy loss
        int energyFallGracePeriodMs = 2000;  // 2 seconds of pause before ANY decay starts
        
        // Input smoothing - smooth the raw intensity signals before using them
        int inputSmoothingTauMs = 1200; // increased - hear trends over longer window
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
    qint64 m_climaxDownSinceMs = -1;

    double m_energy = 0.35;
    qint64 m_lastEnergyUpdateMs = -1;
    
    // Smoothed input signals (to avoid reacting to individual notes)
    double m_smoothedNps = 0.0;      // smoothed notes-per-second
    double m_smoothedCc2 = 0.0;      // smoothed CC2 (expression)
    double m_smoothedRegister = 0.0; // smoothed register-high signal
    double m_smoothedDensity = 0.0;  // smoothed density-high signal
    
    // Grace period tracking - don't decay during brief pauses
    qint64 m_lastActivityMs = -1;    // when we last saw meaningful input
    double m_peakEnergy = 0.35;      // highest energy reached (for grace period)
};

} // namespace playback

