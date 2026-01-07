#pragma once

#include <QtGlobal>

namespace virtuoso::util {

// StableRng: frozen deterministic PRNG (no Qt RNG dependency).
// Implementation: SplitMix64 for seeding + xoroshiro128+ for generation.
// This is used for humanization and any \"stochastic\" behavior that must remain deterministic.
class StableRng final {
public:
    StableRng() = default;
    explicit StableRng(quint64 seed) { this->seed(seed); }

    void seed(quint64 s) {
        // SplitMix64 expands a single seed into two non-zero states.
        quint64 x = (s == 0ull) ? 0x9E3779B97F4A7C15ull : s;
        m_s0 = splitmix64(x);
        m_s1 = splitmix64(x);
        if (m_s0 == 0ull && m_s1 == 0ull) m_s1 = 0xD1342543DE82EF95ull;
    }

    quint64 nextU64() {
        // xoroshiro128+ (public domain reference algorithm)
        const quint64 s0 = m_s0;
        quint64 s1 = m_s1;
        const quint64 result = s0 + s1;

        s1 ^= s0;
        m_s0 = rotl(s0, 55) ^ s1 ^ (s1 << 14);
        m_s1 = rotl(s1, 36);
        return result;
    }

    quint32 nextU32() { return quint32(nextU64() >> 32); }

    // Uniform integer in [0, upperExclusive). Uses rejection sampling to avoid modulo bias.
    quint32 bounded(quint32 upperExclusive) {
        if (upperExclusive <= 1u) return 0u;
        const quint32 threshold = quint32((0x1'0000'0000ull % upperExclusive));
        for (;;) {
            const quint32 r = nextU32();
            if (r >= threshold) return r % upperExclusive;
        }
    }

    // Uniform double in [0,1).
    double nextDouble01() {
        // Use top 53 bits.
        const quint64 r = nextU64();
        const quint64 mant = r >> 11;
        return double(mant) * (1.0 / double(1ull << 53));
    }

private:
    static quint64 rotl(quint64 x, int k) { return (x << k) | (x >> (64 - k)); }

    static quint64 splitmix64(quint64& x) {
        x += 0x9E3779B97F4A7C15ull;
        quint64 z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    quint64 m_s0 = 0x1234567890ABCDEFull;
    quint64 m_s1 = 0x0FEDCBA098765432ull;
};

} // namespace virtuoso::util

