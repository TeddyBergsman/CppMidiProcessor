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
    m_climaxDownSinceMs = -1;
    m_energy = 0.17;  // Start low (15-20% range)
    m_lastEnergyUpdateMs = -1;
    
    // Reset smoothed inputs
    m_smoothedNps = 0.0;
    m_smoothedCc2 = 0.0;
    m_smoothedRegister = 0.0;
    m_smoothedDensity = 0.0;
    
    // Reset grace period tracking
    m_lastActivityMs = -1;
    m_peakEnergy = 0.35;
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

    // Climax down-signal: require *sustained* calm evidence, not just a brief breath.
    const double cc201 = qBound(0.0, double(intent.lastCc2) / 127.0, 1.0);
    const bool lowCc2 = (intent.lastCc2 <= m_s.climaxDownCc2Max);
    const bool lowDensity = (intent.notesPerSec <= m_s.climaxDownNotesPerSecMax) && (!intent.densityHigh);
    const bool downSignal = (!intent.intensityPeak) && (intent.silence || (lowCc2 && lowDensity));
    if (downSignal) {
        if (m_climaxDownSinceMs < 0) m_climaxDownSinceMs = nowMs;
    } else {
        m_climaxDownSinceMs = -1;
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
        const bool downSilenceOk = (intent.silence && m_silenceSinceMs >= 0
                                    && (nowMs - m_silenceSinceMs) >= qint64(qMax(0, m_s.climaxDownSilenceMs)));
        const bool downConfirmOk = (m_climaxDownSinceMs >= 0
                                    && (nowMs - m_climaxDownSinceMs) >= qint64(qMax(0, m_s.climaxDownConfirmMs)));
        if (decayOk && calmLongEnough && (downSilenceOk || downConfirmOk)) {
            // If silent: cooldown; else relax to simmer/build based on input.
            if (intent.silence) {
                m_vibe = Vibe::CoolDown;
                m_lastStateChangeMs = nowMs;
                out.reason = "exit_climax->cooldown";
            } else {
                m_vibe = buildSignal ? Vibe::Build : Vibe::Simmer;
                m_lastStateChangeMs = nowMs;
                out.reason = "exit_climax->relax:hysteresis";
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
    
    // ================================================================
    // INPUT SMOOTHING: Musicians don't react to individual notes!
    // They hear trends over several beats/bars.
    // ================================================================
    const qint64 prevMs = (m_lastEnergyUpdateMs >= 0) ? m_lastEnergyUpdateMs : nowMs;
    const double dt = double(qMax<qint64>(0, nowMs - prevMs));
    m_lastEnergyUpdateMs = nowMs;
    
    // Smooth all input signals with a single time constant
    const int inputTau = qMax(1, m_s.inputSmoothingTauMs);
    const double inputAlpha = 1.0 - std::exp(-dt / double(inputTau));
    
    // Raw input values (0-1 normalized)
    const double rawNps = qBound(0.0, intent.notesPerSec / 8.0, 1.0);
    const double rawCc2 = cc201;  // already computed above
    const double rawRegister = intent.registerHigh ? 1.0 : 0.0;
    const double rawDensity = intent.densityHigh ? 1.0 : 0.0;
    
    // Apply smoothing (exponential moving average)
    m_smoothedNps = m_smoothedNps + inputAlpha * (rawNps - m_smoothedNps);
    m_smoothedCc2 = m_smoothedCc2 + inputAlpha * (rawCc2 - m_smoothedCc2);
    m_smoothedRegister = m_smoothedRegister + inputAlpha * (rawRegister - m_smoothedRegister);
    m_smoothedDensity = m_smoothedDensity + inputAlpha * (rawDensity - m_smoothedDensity);
    
    // ================================================================
    // ENERGY TARGET: Base from vibe state + smoothed input contributions
    // Note: Reduced coefficients since musicians don't respond as much
    // to moment-to-moment variations
    // ================================================================
    double base = 0.35;
    switch (m_vibe) {
        case Vibe::Simmer: base = 0.34; break;
        case Vibe::Build: base = 0.62; break;
        case Vibe::Climax: base = 0.88; break;
        case Vibe::CoolDown: base = 0.22; break;
    }
    
    // Use SMOOTHED inputs with REDUCED coefficients
    // (musicians respond to sustained intensity, not spikes)
    double target = base
        + 0.06 * m_smoothedNps       // reduced from 0.10
        + 0.05 * m_smoothedCc2       // reduced from 0.08
        + 0.03 * m_smoothedRegister  // reduced from 0.05
        + 0.02 * m_smoothedDensity;  // reduced from 0.04
    target = qBound(0.0, target, 1.0);

    // ================================================================
    // ACTIVITY TRACKING: Detect when user is actively playing
    // ================================================================
    const bool hasActivity = (rawNps > 0.05) || (rawCc2 > 0.1) || rawRegister > 0.5 || rawDensity > 0.5;
    if (hasActivity) {
        m_lastActivityMs = nowMs;
    }
    
    // ================================================================
    // CC2 DYNAMICS: Soft playing = faster decay signal
    // This is a PRIMARY musical signal from the performer!
    // ================================================================
    // If user is playing (notes happening) but CC2 is low, they're signaling "bring it down"
    const bool isPlayingSoftly = (rawNps > 0.05) && (rawCc2 < 0.35);
    const bool isPlayingVerySoftly = (rawNps > 0.05) && (rawCc2 < 0.20);
    
    // ================================================================
    // ENERGY SMOOTHING: Gradual transitions (attack/release)
    // Musicians build and release over PHRASES, not beats
    // ================================================================
    
    // Track peak energy for grace period (don't decay below recent peak during pause)
    if (target > m_peakEnergy) {
        m_peakEnergy = target;
    }
    
    int tauMs;
    if (target >= m_energy) {
        // RISING: use rise tau, BUT soft playing slows/prevents rise
        if (isPlayingVerySoftly) {
            // Very soft playing: DON'T build energy at all
            tauMs = 999999;  // Effectively frozen
        } else if (isPlayingSoftly) {
            // Soft playing: build VERY slowly (4x slower)
            tauMs = qMax(1, m_s.energyRiseTauMs) * 4;
        } else {
            // Normal/loud playing: standard rise
            tauMs = qMax(1, m_s.energyRiseTauMs);
        }
        // Update peak when rising
        m_peakEnergy = m_energy;
    } else {
        // FALLING: check grace period first (but soft playing bypasses it!)
        const qint64 timeSinceActivity = (m_lastActivityMs >= 0) ? (nowMs - m_lastActivityMs) : 999999;
        
        // Soft playing SHORTENS or BYPASSES the grace period
        // (playing softly is an active signal to bring energy down)
        int effectiveGracePeriod = m_s.energyFallGracePeriodMs;
        if (isPlayingVerySoftly) {
            effectiveGracePeriod = 0;  // No grace - start decaying immediately
        } else if (isPlayingSoftly) {
            effectiveGracePeriod = m_s.energyFallGracePeriodMs / 3;  // Shortened grace
        }
        
        if (timeSinceActivity < qint64(effectiveGracePeriod)) {
            // GRACE PERIOD: Don't decay at all! Hold at current energy.
            // This handles natural breathing/phrasing.
            tauMs = 999999; // effectively infinite - no decay
        } else {
            // Past grace period: calculate fall tau
            tauMs = qMax(1, m_s.energyFallTauMs);
            
            // Extra stickiness when in elevated states
        if (m_vibe == Vibe::Climax) tauMs = qMax(tauMs, qMax(1, m_s.energyFallTauMsClimax));
        if (m_vibe == Vibe::Build) tauMs = qMax(tauMs, qMax(1, m_s.energyFallTauMsBuild));
            
            // BUT: Soft playing ACCELERATES decay (shorter tau = faster fall)
            // Low CC2 while playing = "bring it down please"
            if (isPlayingVerySoftly) {
                tauMs = tauMs / 4;  // 4x faster decay when playing very softly
            } else if (isPlayingSoftly) {
                tauMs = tauMs / 2;  // 2x faster decay when playing softly
            }
            
            tauMs = qMax(500, tauMs);  // Floor: never faster than 500ms
        }
        
        // Reset peak tracking once we start actually falling
        if (timeSinceActivity >= qint64(effectiveGracePeriod)) {
            m_peakEnergy = m_energy;
        }
    }
    
    const double a = 1.0 - std::exp(-dt / double(tauMs));
    m_energy = m_energy + a * (target - m_energy);
    out.energy = m_energy;

    return out;
}

} // namespace playback

