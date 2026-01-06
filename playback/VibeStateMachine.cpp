#include "playback/VibeStateMachine.h"

#include <QtMath>

namespace playback {

void VibeStateMachine::reset() {
    m_vibe = Vibe::Simmer;
    m_lastStateChangeMs = 0;
    m_intensitySinceMs = -1;
    m_silenceSinceMs = -1;
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
    out.energy = 0.25;
    out.reason = "default";

    // Track intensity / silence spans.
    if (intent.intensityPeak) {
        if (m_intensitySinceMs < 0) m_intensitySinceMs = nowMs;
    } else {
        m_intensitySinceMs = -1;
    }

    if (intent.silence) {
        if (m_silenceSinceMs < 0) m_silenceSinceMs = nowMs;
    } else {
        m_silenceSinceMs = -1;
    }

    const bool canChange = (nowMs - m_lastStateChangeMs) >= qint64(qMax(0, m_s.minStateHoldMs));

    // Transitions (simple but musical):
    // - sustained intensity => Climax
    // - sustained silence => CoolDown
    // - density high or register high => Build
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
        } else if ((intent.densityHigh || intent.registerHigh) && !intent.silence) {
            m_vibe = Vibe::Build;
            m_lastStateChangeMs = nowMs;
            out.reason = "enter_build:density_or_register";
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
        if (decayOk && (intent.silence || (!intent.densityHigh && !intent.registerHigh))) {
            // If silent: cooldown; else relax to simmer/build based on input.
            if (intent.silence) {
                m_vibe = Vibe::CoolDown;
                m_lastStateChangeMs = nowMs;
                out.reason = "exit_climax->cooldown";
            } else {
                m_vibe = (intent.densityHigh || intent.registerHigh) ? Vibe::Build : Vibe::Simmer;
                m_lastStateChangeMs = nowMs;
                out.reason = "exit_climax->relax";
            }
        }
    }

    out.vibe = m_vibe;
    // Energy mapping (subtle; used to scale density/velocities).
    switch (m_vibe) {
        case Vibe::Simmer: out.energy = 0.25; break;
        case Vibe::Build: out.energy = 0.55; break;
        case Vibe::Climax: out.energy = 0.90; break;
        case Vibe::CoolDown: out.energy = 0.18; break;
    }
    return out;
}

} // namespace playback

