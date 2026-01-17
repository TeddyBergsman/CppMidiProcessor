#include "HarmonyGenerator.h"
#include "PitchConformanceEngine.h"
#include <algorithm>
#include <cmath>

namespace playback {

// ============================================================================
// Parallel Harmony Generator
//
// Generates harmony at a fixed diatonic interval (default: 3rd below).
// The interval is adjusted to stay within the scale/chord.
// ============================================================================

class ParallelGenerator : public IHarmonyGenerator {
public:
    HarmonyType type() const override { return HarmonyType::PARALLEL; }

    std::array<int, 4> generate(
        int leadPitch,
        int velocity,
        const ActiveChord& chord,
        int voiceCount
    ) override {
        (void)velocity;
        std::array<int, 4> result = {-1, -1, -1, -1};

        if (voiceCount < 1 || leadPitch < 0) {
            return result;
        }

        // Default intervals for each voice: 3rd below, 5th below, 6th below, octave below
        static const int kDefaultIntervals[] = {-3, -7, -9, -12};

        for (int v = 0; v < voiceCount && v < 4; ++v) {
            int rawPitch = leadPitch + kDefaultIntervals[v];

            // Clamp to valid MIDI range
            if (rawPitch < 0) rawPitch += 12;
            if (rawPitch > 127) rawPitch -= 12;
            rawPitch = std::clamp(rawPitch, 0, 127);

            // Conform to chord/scale (snap T3/T4 to T1/T2)
            int pc = ChordOntology::normalizePc(rawPitch);
            int tier = ChordOntology::instance().getTier(pc, chord);

            if (tier > 2) {
                // Find nearest valid pitch (T1 or T2)
                int bestPc = pc;
                int bestDist = 12;

                // Search T1 first
                for (int t : chord.tier1Absolute) {
                    int dist = ChordOntology::minDistance(pc, t);
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestPc = t;
                    }
                }

                // Then T2
                for (int t : chord.tier2Absolute) {
                    int dist = ChordOntology::minDistance(pc, t);
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestPc = t;
                    }
                }

                rawPitch = ChordOntology::findNearestInOctave(rawPitch, bestPc);
            }

            result[v] = std::clamp(rawPitch, 0, 127);
        }

        return result;
    }
};

// ============================================================================
// Placeholder generators for other types (to be fully implemented in Phase 5)
// ============================================================================

class ContraryGenerator : public IHarmonyGenerator {
public:
    HarmonyType type() const override { return HarmonyType::CONTRARY; }

    std::array<int, 4> generate(int leadPitch, int velocity, const ActiveChord& chord, int voiceCount) override {
        // Placeholder: opposite direction movement
        // For now, use parallel with inverted direction hint
        (void)velocity;
        std::array<int, 4> result = {-1, -1, -1, -1};
        if (voiceCount >= 1 && leadPitch >= 0) {
            int harmony = leadPitch - 4;  // Major 3rd below
            result[0] = std::clamp(harmony, 0, 127);
        }
        (void)chord;
        return result;
    }
};

class ObliqueGenerator : public IHarmonyGenerator {
public:
    HarmonyType type() const override { return HarmonyType::OBLIQUE; }

    std::array<int, 4> generate(int leadPitch, int velocity, const ActiveChord& chord, int voiceCount) override {
        // Placeholder: pedal tone held while lead moves
        (void)velocity;
        (void)leadPitch;
        std::array<int, 4> result = {-1, -1, -1, -1};
        if (voiceCount >= 1) {
            // Hold root as pedal
            int rootMidi = chord.rootPc + 48;  // Middle octave
            result[0] = std::clamp(rootMidi, 0, 127);
        }
        return result;
    }
};

class ConvergentGenerator : public IHarmonyGenerator {
public:
    HarmonyType type() const override { return HarmonyType::CONVERGENT; }

    std::array<int, 4> generate(int leadPitch, int velocity, const ActiveChord& chord, int voiceCount) override {
        // Placeholder: voices move toward unison
        (void)velocity;
        (void)chord;
        std::array<int, 4> result = {-1, -1, -1, -1};
        if (voiceCount >= 1 && leadPitch >= 0) {
            result[0] = std::clamp(leadPitch - 2, 0, 127);  // Approaching from below
        }
        return result;
    }
};

class DivergentGenerator : public IHarmonyGenerator {
public:
    HarmonyType type() const override { return HarmonyType::DIVERGENT; }

    std::array<int, 4> generate(int leadPitch, int velocity, const ActiveChord& chord, int voiceCount) override {
        // Placeholder: voices spread from unison
        (void)velocity;
        (void)chord;
        std::array<int, 4> result = {-1, -1, -1, -1};
        if (voiceCount >= 1 && leadPitch >= 0) {
            result[0] = std::clamp(leadPitch - 5, 0, 127);  // Perfect 4th below
        }
        return result;
    }
};

class IsorhythmicGenerator : public IHarmonyGenerator {
public:
    HarmonyType type() const override { return HarmonyType::ISORHYTHMIC; }

    std::array<int, 4> generate(int leadPitch, int velocity, const ActiveChord& chord, int voiceCount) override {
        // Placeholder: same rhythm, independent chord-tone pitch
        (void)velocity;
        (void)leadPitch;
        std::array<int, 4> result = {-1, -1, -1, -1};
        if (voiceCount >= 1 && !chord.tier1Absolute.empty()) {
            // Pick a chord tone that's not the lead
            int pc = *chord.tier1Absolute.begin();
            result[0] = std::clamp(pc + 48, 0, 127);
        }
        return result;
    }
};

class HeterophonicGenerator : public IHarmonyGenerator {
public:
    HarmonyType type() const override { return HarmonyType::HETEROPHONIC; }

    std::array<int, 4> generate(int leadPitch, int velocity, const ActiveChord& chord, int voiceCount) override {
        // Placeholder: near-unison with micro-variation
        (void)velocity;
        (void)chord;
        std::array<int, 4> result = {-1, -1, -1, -1};
        if (voiceCount >= 1 && leadPitch >= 0) {
            result[0] = leadPitch;  // Unison (micro-variation via pitch bend)
        }
        return result;
    }
};

class CallResponseGenerator : public IHarmonyGenerator {
public:
    HarmonyType type() const override { return HarmonyType::CALL_RESPONSE; }

    std::array<int, 4> generate(int leadPitch, int velocity, const ActiveChord& chord, int voiceCount) override {
        // Placeholder: delayed echo/imitation
        (void)velocity;
        (void)chord;
        std::array<int, 4> result = {-1, -1, -1, -1};
        // Store for delayed playback
        m_pendingPitch = leadPitch;
        m_pendingVoiceCount = voiceCount;
        // Return nothing immediately - response comes later
        return result;
    }

    void onLeadNoteOff(int leadPitch) override {
        (void)leadPitch;
        // Could trigger the response here
    }

    void update(float deltaMs) override {
        (void)deltaMs;
        // Time-based triggering would go here
    }

private:
    int m_pendingPitch = -1;
    int m_pendingVoiceCount = 0;
};

class DescantGenerator : public IHarmonyGenerator {
public:
    HarmonyType type() const override { return HarmonyType::DESCANT; }

    std::array<int, 4> generate(int leadPitch, int velocity, const ActiveChord& chord, int voiceCount) override {
        // Placeholder: high obligato above lead
        (void)velocity;
        (void)chord;
        std::array<int, 4> result = {-1, -1, -1, -1};
        if (voiceCount >= 1 && leadPitch >= 0) {
            result[0] = std::clamp(leadPitch + 7, 0, 127);  // Perfect 5th above
        }
        return result;
    }
};

class ShadowGenerator : public IHarmonyGenerator {
public:
    HarmonyType type() const override { return HarmonyType::SHADOW; }

    std::array<int, 4> generate(int leadPitch, int velocity, const ActiveChord& chord, int voiceCount) override {
        // Placeholder: delayed + harmonized (pitched reverb)
        (void)velocity;
        (void)chord;
        std::array<int, 4> result = {-1, -1, -1, -1};
        if (voiceCount >= 1 && leadPitch >= 0) {
            result[0] = std::clamp(leadPitch - 3, 0, 127);  // Minor 3rd below
        }
        return result;
    }
};

// ============================================================================
// Generator Factory
// ============================================================================

std::unique_ptr<IHarmonyGenerator> createHarmonyGenerator(HarmonyType type) {
    switch (type) {
        case HarmonyType::PARALLEL:
            return std::make_unique<ParallelGenerator>();
        case HarmonyType::CONTRARY:
            return std::make_unique<ContraryGenerator>();
        case HarmonyType::OBLIQUE:
            return std::make_unique<ObliqueGenerator>();
        case HarmonyType::CONVERGENT:
            return std::make_unique<ConvergentGenerator>();
        case HarmonyType::DIVERGENT:
            return std::make_unique<DivergentGenerator>();
        case HarmonyType::ISORHYTHMIC:
            return std::make_unique<IsorhythmicGenerator>();
        case HarmonyType::HETEROPHONIC:
            return std::make_unique<HeterophonicGenerator>();
        case HarmonyType::CALL_RESPONSE:
            return std::make_unique<CallResponseGenerator>();
        case HarmonyType::DESCANT:
            return std::make_unique<DescantGenerator>();
        case HarmonyType::SHADOW:
            return std::make_unique<ShadowGenerator>();
    }
    return std::make_unique<ParallelGenerator>();  // Default fallback
}

// ============================================================================
// Harmony Voice Manager Implementation
// ============================================================================

HarmonyVoiceManager::HarmonyVoiceManager()
    : m_generator(createHarmonyGenerator(HarmonyType::PARALLEL))
{
    // Initialize voices with their channel assignments
    m_voices[0].channel = channels::HARMONY_1;
    m_voices[1].channel = channels::HARMONY_2;
    m_voices[2].channel = channels::HARMONY_3;
    m_voices[3].channel = channels::HARMONY_4;
}

HarmonyVoiceManager::~HarmonyVoiceManager() = default;

void HarmonyVoiceManager::setHarmonyType(HarmonyType type) {
    if (m_harmonyType != type) {
        m_harmonyType = type;
        m_generator = createHarmonyGenerator(type);
        reset();
    }
}

void HarmonyVoiceManager::setVoiceCount(int count) {
    m_voiceCount = std::clamp(count, 1, 4);
}

void HarmonyVoiceManager::setVelocityRatio(float ratio) {
    m_velocityRatio = std::clamp(ratio, 0.0f, 1.0f);
}

std::array<HarmonyVoice, 4> HarmonyVoiceManager::processLeadNote(
    int leadPitch,
    int leadVelocity,
    const ActiveChord& chord
) {
    // Generate harmony pitches
    auto pitches = m_generator->generate(leadPitch, leadVelocity, chord, m_voiceCount);

    // Scale velocity
    int harmonyVelocity = static_cast<int>(leadVelocity * m_velocityRatio);
    harmonyVelocity = std::clamp(harmonyVelocity, 1, 127);

    // Update voice states
    for (int i = 0; i < 4; ++i) {
        m_voices[i].currentPitch = pitches[i];
        m_voices[i].velocity = (pitches[i] >= 0) ? harmonyVelocity : 0;
    }

    return m_voices;
}

void HarmonyVoiceManager::onLeadNoteOff(int leadPitch) {
    if (m_generator) {
        m_generator->onLeadNoteOff(leadPitch);
    }

    // Clear all voices
    for (auto& voice : m_voices) {
        voice.currentPitch = -1;
        voice.velocity = 0;
    }
}

void HarmonyVoiceManager::update(float deltaMs) {
    if (m_generator) {
        m_generator->update(deltaMs);
    }
}

void HarmonyVoiceManager::reset() {
    if (m_generator) {
        m_generator->reset();
    }

    for (auto& voice : m_voices) {
        voice.currentPitch = -1;
        voice.velocity = 0;
        voice.bendState = BendState{};
    }
}

int HarmonyVoiceManager::channelForVoice(int voiceIndex) {
    static const int channels[] = {
        channels::HARMONY_1,
        channels::HARMONY_2,
        channels::HARMONY_3,
        channels::HARMONY_4
    };
    return channels[std::clamp(voiceIndex, 0, 3)];
}

} // namespace playback
