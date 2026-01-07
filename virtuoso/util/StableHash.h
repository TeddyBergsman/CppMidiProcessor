#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace virtuoso::util {

// Canonical deterministic hash for the Virtuoso framework.
//
// IMPORTANT:
// - Do NOT use qHash() for musical decision determinism (Qt may change hashing).
// - Use FNV-1a 32-bit for stable cross-platform/cross-version reproducibility.
//
// Hash versioning:
// - Bump kHashVersion only when you intentionally want to change deterministic behavior.
struct StableHash final {
    static constexpr quint32 kHashVersion = 1u;

    // FNV-1a 32-bit over raw bytes.
    static quint32 fnv1a32(const QByteArray& bytes) {
        quint32 h = 2166136261u;
        for (unsigned char c : bytes) {
            h ^= quint32(c);
            h *= 16777619u;
        }
        return h;
    }

    // Mixes multiple 32-bit values into one deterministically.
    static quint32 mix(quint32 a, quint32 b) {
        // FNV-style mixing (not crypto, just stable).
        quint32 h = 2166136261u;
        auto add = [&](quint32 v) {
            for (int i = 0; i < 4; ++i) {
                const unsigned char c = static_cast<unsigned char>((v >> (i * 8)) & 0xFFu);
                h ^= quint32(c);
                h *= 16777619u;
            }
        };
        add(kHashVersion);
        add(a);
        add(b);
        return h;
    }
};

} // namespace virtuoso::util

