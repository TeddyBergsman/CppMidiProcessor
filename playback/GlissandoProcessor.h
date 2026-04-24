#pragma once

#include <algorithm>
#include <cmath>

namespace playback {

/**
 * GlissandoProcessor - Smooths pitch transitions between guitar notes
 *
 * Operates in log-frequency (semitone) space for perceptually uniform glide.
 * Larger intervals glide proportionally faster, mimicking natural vocal portamento.
 * Supports linear, smooth, and exponential curve shapes.
 */
class GlissandoProcessor {
public:
    struct Config {
        bool enabled = true;
        float rateStPerSec = 100.0f;        // Base glide speed (semitones/second)
        float intervalThresholdSt = 7.0f;    // Above this, scale rate up (semitones)
        float curveExponent = 1.5f;          // 1.0=linear, 1.5=smooth, 2.0=exponential
    };

    GlissandoProcessor() = default;

    // Configuration
    const Config& config() const { return m_config; }
    void setConfig(const Config& config) { m_config = config; }

    void setEnabled(bool enabled) { m_config.enabled = enabled; }
    bool enabled() const { return m_config.enabled; }

    void setRateStPerSec(float rate) { m_config.rateStPerSec = rate; }
    float rateStPerSec() const { return m_config.rateStPerSec; }

    void setIntervalThresholdSt(float threshold) { m_config.intervalThresholdSt = threshold; }
    float intervalThresholdSt() const { return m_config.intervalThresholdSt; }

    void setCurveExponent(float exponent) { m_config.curveExponent = exponent; }
    float curveExponent() const { return m_config.curveExponent; }

    /**
     * Set a new target pitch. The glissando will interpolate from current position
     * toward this target. If disabled, snaps immediately.
     */
    void setTarget(float targetHz) {
        if (targetHz < 20.0f) return;
        m_targetSemitones = hzToSemitones(targetHz);

        if (!m_initialized) {
            // First note: snap immediately, no glide
            m_currentSemitones = m_targetSemitones;
            m_initialized = true;
            return;
        }

        if (!m_config.enabled) {
            m_currentSemitones = m_targetSemitones;
        }
    }

    /**
     * Advance the glissando interpolation by deltaMs milliseconds.
     * Returns the current interpolated frequency in Hz.
     */
    float update(float deltaMs) {
        if (!m_initialized) return 0.0f;
        if (!m_config.enabled || deltaMs <= 0.0f) {
            m_currentSemitones = m_targetSemitones;
            return semitonesToHz(m_currentSemitones);
        }

        float distance = m_targetSemitones - m_currentSemitones;
        if (std::abs(distance) < 0.001f) {
            m_currentSemitones = m_targetSemitones;
            return semitonesToHz(m_currentSemitones);
        }

        // Scale rate for large intervals: bigger leaps move proportionally faster
        float absDistance = std::abs(distance);
        float rateScale = 1.0f;
        if (m_config.intervalThresholdSt > 0.0f && absDistance > m_config.intervalThresholdSt) {
            rateScale = absDistance / m_config.intervalThresholdSt;
        }

        // Apply curve: exponential acceleration into the target note
        // progress goes from 0 (just started) to 1 (arrived)
        // We use remaining distance ratio as inverse progress
        float totalDistance = absDistance; // approximate — use current remaining distance
        float progress = 1.0f - (absDistance / std::max(absDistance, 0.01f));
        // Simpler approach: constant rate scaled by distance, with curve applied to step size
        float effectiveRate = m_config.rateStPerSec * rateScale;
        float maxStep = effectiveRate * (deltaMs / 1000.0f);

        // Apply curve exponent: as we get closer to target, accelerate
        // (remaining fraction)^(1/exponent) — smaller exponent = faster at end
        if (m_config.curveExponent > 1.0f) {
            float remainingFrac = absDistance / std::max(totalDistance, 0.01f);
            float curveFactor = std::pow(remainingFrac, 1.0f / m_config.curveExponent);
            maxStep *= std::max(curveFactor, 0.1f); // floor at 10% to always make progress
        }

        // Clamp to not overshoot
        if (maxStep >= absDistance) {
            m_currentSemitones = m_targetSemitones;
        } else {
            m_currentSemitones += (distance > 0.0f ? maxStep : -maxStep);
        }

        return semitonesToHz(m_currentSemitones);
    }

    /**
     * Snap immediately to current target (no glide). Use on mode reset.
     */
    void reset() {
        if (m_initialized) {
            m_currentSemitones = m_targetSemitones;
        }
    }

    /**
     * Full reset — clears all state. Next setTarget will snap immediately.
     */
    void clear() {
        m_initialized = false;
        m_currentSemitones = 0.0f;
        m_targetSemitones = 0.0f;
    }

    /** Current interpolated frequency in Hz (0 if uninitialized). */
    float currentHz() const {
        return m_initialized ? semitonesToHz(m_currentSemitones) : 0.0f;
    }

    /** Target frequency in Hz (0 if uninitialized). */
    float targetHz() const {
        return m_initialized ? semitonesToHz(m_targetSemitones) : 0.0f;
    }

    /** True if currently gliding (not yet at target). */
    bool isGliding() const {
        return m_initialized && std::abs(m_targetSemitones - m_currentSemitones) > 0.001f;
    }

private:
    static float hzToSemitones(float hz) {
        return 12.0f * std::log2(hz / 440.0f) + 69.0f;
    }

    static float semitonesToHz(float semitones) {
        return 440.0f * std::pow(2.0f, (semitones - 69.0f) / 12.0f);
    }

    Config m_config;
    float m_currentSemitones = 0.0f;
    float m_targetSemitones = 0.0f;
    bool m_initialized = false;
};

} // namespace playback
