#pragma once

#include <rubberband/RubberBandLiveShifter.h>
#include <vector>
#include <cmath>
#include <algorithm>

/**
 * PitchShifter - Rubber Band LiveShifter wrapper.
 *
 * LiveShifter processes fixed-size blocks (getBlockSize() samples in, same out).
 * We accumulate input until we have a full block, process it, and accumulate
 * output. No ring buffer needed — LiveShifter is 1:1.
 */
class PitchShifter
{
public:
    PitchShifter() = default;

    void prepare(double sampleRate, int maxBlockSize)
    {
        m_shifter = std::make_unique<RubberBand::RubberBandLiveShifter>(
            sampleRate, 1,
            RubberBand::RubberBandLiveShifter::OptionWindowShort
        );

        m_blockSize = static_cast<int>(m_shifter->getBlockSize());
        m_startDelay = static_cast<int>(m_shifter->getStartDelay());
        m_inputAccum.resize(m_blockSize, 0.0f);
        m_outputAccum.resize(m_blockSize, 0.0f);
        m_inputPos = 0;
        m_outputPos = 0;
        m_outputAvail = 0;
        m_delayRemaining = m_startDelay;
    }

    void process(const float* input, float* output, int numSamples, float semitones)
    {
        if (!m_shifter) {
            std::copy(input, input + numSamples, output);
            return;
        }

        double ratio = std::pow(2.0, static_cast<double>(semitones) / 12.0);
        ratio = std::max(0.25, std::min(ratio, 4.0));
        m_shifter->setPitchScale(ratio);

        int outWritten = 0;

        for (int i = 0; i < numSamples; ++i) {
            // If we have buffered output, emit it
            if (m_outputAvail > 0) {
                output[outWritten++] = m_outputAccum[m_outputPos++];
                m_outputAvail--;
            }

            // Accumulate input
            m_inputAccum[m_inputPos++] = input[i];

            // When we have a full block, process it
            if (m_inputPos >= m_blockSize) {
                const float* inPtr = m_inputAccum.data();
                float* outPtr = m_outputAccum.data();
                m_shifter->shift(&inPtr, &outPtr);
                m_inputPos = 0;

                if (m_delayRemaining > 0) {
                    // Skip startup delay samples
                    int skip = std::min(m_delayRemaining, m_blockSize);
                    m_outputPos = skip;
                    m_outputAvail = m_blockSize - skip;
                    m_delayRemaining -= skip;
                } else {
                    m_outputPos = 0;
                    m_outputAvail = m_blockSize;
                }
            }
        }

        // Fill any remaining output (shouldn't happen in steady state)
        while (outWritten < numSamples) {
            if (m_outputAvail > 0) {
                output[outWritten++] = m_outputAccum[m_outputPos++];
                m_outputAvail--;
            } else {
                output[outWritten++] = 0.0f;
            }
        }
    }

private:
    std::unique_ptr<RubberBand::RubberBandLiveShifter> m_shifter;
    int m_blockSize = 0;
    int m_startDelay = 0;
    int m_delayRemaining = 0;

    std::vector<float> m_inputAccum;
    std::vector<float> m_outputAccum;
    int m_inputPos = 0;
    int m_outputPos = 0;
    int m_outputAvail = 0;
};
