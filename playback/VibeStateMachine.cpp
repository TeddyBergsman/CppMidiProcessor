#include "playback/VibeStateMachine.h"

#include <QtMath>
#include <cmath>

namespace playback {

void VibeStateMachine::reset() {
    m_vibe = Vibe::Simmer;
    m_lastStateChangeMs = 0;
    m_intensitySinceMs = -1;
    m_silenceSinceMs = -1;
    m_buildSinceMs = -1;
    m_calmSinceMs = -1;
    m_energy = 0.35;
    m_lastEnergyUpdateMs = -1;
}

QString VibeStateMachine::vibeName(Vibe v) {
    switch (v) {
        case Vibe::Simmer: return "Simmer";
        case Vibe::Build: return "Build";
        case Vibe::Climax: return "Climax";
        case Vibe::CoolDown: return "CoolDown";
    }
    return "Simmer";
}

VibeStateMachine::Output VibeStateMachine::update(const SemanticMidiAnalyzer::IntentState& intent, qint64 nowMs) {
    Output out;
    out.vibe = m_vibe;
    out.energy = m_energy;
    out.reason = "default";

    // Track intensity / silence spans.
    if (intent.intensityPeak) {
        if (m_intensitySinceMs < 0) m_intensitySinceMs = nowMs;
    } else {
        m_intensitySinceMs = -1;
    }

    // Build signal: lower-threshold and continuous (notesPerSec) + the old boolean flags.
    const bool buildSignal = (!intent.silence) && ((intent.notesPerSec >= m_s.buildEnterNotesPerSec) || intent.densityHigh || intent.registerHigh);
    if (buildSignal) {
        if (m_buildSinceMs < 0) m_buildSinceMs = nowMs;
    } else {
        m_buildSinceMs = -1;
    }

    if (intent.silence) {
        if (m_silenceSinceMs < 0) m_silenceSinceMs = nowMs;
    } else {
        m_silenceSinceMs = -1;
    }

    // Calm span: used for linger when exiting Build/Climax.
    const bool calm = (!intent.intensityPeak) && (!buildSignal);
    if (calm) {
        if (m_calmSinceMs < 0) m_calmSinceMs = nowMs;
    } else {
        m_calmSinceMs = -1;
    }

    const bool canChange = (nowMs - m_lastStateChangeMs) >= qint64(qMax(0, m_s.minStateHoldMs));

    // Transitions (simple but musical):
    // - sustained intensity => Climax
    // - sustained silence => CoolDown
    // - sustained build signal => Build
    // - otherwise Simmer
    if (canChange) {
        if (m_intensitySinceMs >= 0 && (nowMs - m_intensitySinceMs) >= qint64(qMax(0, m_s.climaxEnterMs))) {
            m_vibe = Vibe::Climax;
            m_lastStateChangeMs = nowMs;
            out.reason = "enter_climax:intensity";
        } else if (m_silenceSinceMs >= 0 && (nowMs - m_silenceSinceMs) >= qint64(qMax(0, m_s.coolDownEnterMs))) {
            m_vibe = Vibe::CoolDown;
            m_lastStateChangeMs = nowMs;
            out.reason = "enter_cooldown:silence";
        } else if (m_buildSinceMs >= 0 && (nowMs - m_buildSinceMs) >= qint64(qMax(0, m_s.buildEnterMs))) {
            m_vibe = Vibe::Build;
            m_lastStateChangeMs = nowMs;
            out.reason = "enter_build:signal";
        } else {
            m_vibe = Vibe::Simmer;
            m_lastStateChangeMs = nowMs;
            out.reason = "enter_simmer:calm";
        }
    }

    // Exit Climax after a while without intensity.
    if (m_vibe == Vibe::Climax && !intent.intensityPeak) {
        // We don't require canChange here; we use a separate decay timer but still respect min hold.
        const bool decayOk = (nowMs - m_lastStateChangeMs) >= qint64(qMax(0, m_s.minStateHoldMs));
        const bool calmLongEnough = (m_calmSinceMs >= 0) && ((nowMs - m_calmSinceMs) >= qint64(qMax(0, m_s.climaxExitMs)));
        if (decayOk && calmLongEnough) {
            // If silent: cooldown; else relax to simmer/build based on input.
            if (intent.silence) {
                m_vibe = Vibe::CoolDown;
                m_lastStateChangeMs = nowMs;
                out.reason = "exit_climax->cooldown";
            } else {
                m_vibe = buildSignal ? Vibe::Build : Vibe::Simmer;
                m_lastStateChangeMs = nowMs;
                out.reason = "exit_climax->relax";
            }
        }
    }

    // Exit Build only after sustained calm (linger).
    if (m_vibe == Vibe::Build && !buildSignal && !intent.intensityPeak) {
        const bool decayOk = (nowMs - m_lastStateChangeMs) >= qint64(qMax(0, m_s.minStateHoldMs));
        const bool calmLongEnough = (m_calmSinceMs >= 0) && ((nowMs - m_calmSinceMs) >= qint64(qMax(0, m_s.buildExitMs)));
        if (decayOk && calmLongEnough) {
            m_vibe = intent.silence ? Vibe::CoolDown : Vibe::Simmer;
            m_lastStateChangeMs = nowMs;
            out.reason = intent.silence ? "exit_build->cooldown" : "exit_build->simmer";
        }
    }

    out.vibe = m_vibe;
    // Continuous energy target: base vibe + continuous contributions.
    double base = 0.35;
    switch (m_vibe) {
        case Vibe::Simmer: base = 0.34; break;
        case Vibe::Build: base = 0.62; break;
        case Vibe::Climax: base = 0.88; break;
        case Vibe::CoolDown: base = 0.22; break;
    }
    const double nps01 = qBound(0.0, intent.notesPerSec / 8.0, 1.0);
    const double cc201 = qBound(0.0, double(intent.lastCc2) / 127.0, 1.0);
    double target = base
        + 0.10 * nps01
        + 0.08 * cc201
        + 0.05 * (intent.registerHigh ? 1.0 : 0.0)
        + 0.04 * (intent.densityHigh ? 1.0 : 0.0);
    target = qBound(0.0, target, 1.0);

    // Smooth energy (attack/release).
    const qint64 prevMs = (m_lastEnergyUpdateMs >= 0) ? m_lastEnergyUpdateMs : nowMs;
    const double dt = double(qMax<qint64>(0, nowMs - prevMs));
    m_lastEnergyUpdateMs = nowMs;
    const int tauMs = (target >= m_energy) ? qMax(1, m_s.energyRiseTauMs) : qMax(1, m_s.energyFallTauMs);
    const double a = 1.0 - std::exp(-dt / double(tauMs));
    m_energy = m_energy + a * (target - m_energy);
    out.energy = m_energy;

    return out;
}

} // namespace playback

