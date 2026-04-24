#pragma once

#include <vector>
#include <cmath>

/**
 * YinPitchDetector - Lightweight monophonic pitch detection using the YIN algorithm.
 *
 * Designed for real-time vocal pitch detection. Uses the autocorrelation-based
 * YIN method which provides reliable fundamental frequency estimation for
 * monophonic signals (single voice).
 *
 * Reference: de Cheveign, A. & Kawahara, H. (2002). "YIN, a fundamental
 * frequency estimator for speech and music."
 */
class YinPitchDetector
{
public:
    YinPitchDetector() = default;

    /**
     * Initialize the detector for a given sample rate.
     * Must be called before process().
     */
    void prepare(double sampleRate, int maxBlockSize)
    {
        m_sampleRate = sampleRate;
        // Window size: ~2 periods at minimum frequency (80 Hz)
        // At 44100 Hz: 44100/80 * 2 = 1102 samples
        m_windowSize = static_cast<int>(sampleRate / m_minFrequency) * 2;
        m_halfWindow = m_windowSize / 2;
        m_buffer.resize(m_windowSize, 0.0f);
        m_yinBuffer.resize(m_halfWindow, 0.0f);
        m_writePos = 0;
    }

    /**
     * Feed audio samples into the detector.
     * Call this every audio block with the input buffer.
     */
    void process(const float* input, int numSamples)
    {
        // Accumulate into circular buffer
        for (int i = 0; i < numSamples; ++i) {
            m_buffer[m_writePos] = input[i];
            m_writePos = (m_writePos + 1) % m_windowSize;
        }

        // Run YIN on the current buffer
        computeYin();
    }

    /** Detected frequency in Hz (0 if unvoiced/no pitch). */
    float detectedHz() const { return m_detectedHz; }

    /** Confidence of the detection (0.0 = no confidence, 1.0 = very confident). */
    float confidence() const { return m_confidence; }

    /** True if a valid pitch was detected. */
    bool isVoiced() const { return m_detectedHz > 0.0f && m_confidence > (1.0f - m_threshold); }

    /** Set the YIN threshold (default 0.15). Lower = stricter. */
    void setThreshold(float threshold) { m_threshold = threshold; }

    /** Set minimum detectable frequency (default 80 Hz). */
    void setMinFrequency(float freq) { m_minFrequency = freq; }

    /** Set maximum detectable frequency (default 1000 Hz). */
    void setMaxFrequency(float freq) { m_maxFrequency = freq; }

private:
    void computeYin()
    {
        // Step 1: Difference function
        for (int tau = 0; tau < m_halfWindow; ++tau) {
            m_yinBuffer[tau] = 0.0f;
            for (int j = 0; j < m_halfWindow; ++j) {
                int idx1 = (m_writePos - m_windowSize + j + m_windowSize * 2) % m_windowSize;
                int idx2 = (idx1 + tau) % m_windowSize;
                float delta = m_buffer[idx1] - m_buffer[idx2];
                m_yinBuffer[tau] += delta * delta;
            }
        }

        // Step 2: Cumulative mean normalized difference
        m_yinBuffer[0] = 1.0f;
        float runningSum = 0.0f;
        for (int tau = 1; tau < m_halfWindow; ++tau) {
            runningSum += m_yinBuffer[tau];
            m_yinBuffer[tau] = (runningSum > 0.0f)
                ? m_yinBuffer[tau] * tau / runningSum
                : 1.0f;
        }

        // Step 3: Absolute threshold — find first dip below threshold
        int tauEstimate = -1;
        int minTau = static_cast<int>(m_sampleRate / m_maxFrequency);
        int maxTau = static_cast<int>(m_sampleRate / m_minFrequency);
        if (maxTau >= m_halfWindow) maxTau = m_halfWindow - 1;
        if (minTau < 2) minTau = 2;

        for (int tau = minTau; tau < maxTau; ++tau) {
            if (m_yinBuffer[tau] < m_threshold) {
                // Find the local minimum after crossing threshold
                while (tau + 1 < maxTau && m_yinBuffer[tau + 1] < m_yinBuffer[tau]) {
                    ++tau;
                }
                tauEstimate = tau;
                break;
            }
        }

        if (tauEstimate < 0) {
            // No pitch detected
            m_detectedHz = 0.0f;
            m_confidence = 0.0f;
            return;
        }

        // Step 4: Parabolic interpolation for sub-sample accuracy
        float betterTau = static_cast<float>(tauEstimate);
        if (tauEstimate > 0 && tauEstimate < m_halfWindow - 1) {
            float s0 = m_yinBuffer[tauEstimate - 1];
            float s1 = m_yinBuffer[tauEstimate];
            float s2 = m_yinBuffer[tauEstimate + 1];
            float denom = 2.0f * s1 - s2 - s0;
            if (std::abs(denom) > 1e-12f) {
                betterTau = tauEstimate + (s0 - s2) / (2.0f * denom);
            }
        }

        m_detectedHz = static_cast<float>(m_sampleRate / betterTau);
        m_confidence = 1.0f - m_yinBuffer[tauEstimate];
    }

    double m_sampleRate = 44100.0;
    float m_threshold = 0.15f;
    float m_minFrequency = 80.0f;
    float m_maxFrequency = 1000.0f;
    int m_windowSize = 0;
    int m_halfWindow = 0;
    int m_writePos = 0;
    float m_detectedHz = 0.0f;
    float m_confidence = 0.0f;
    std::vector<float> m_buffer;
    std::vector<float> m_yinBuffer;
};
