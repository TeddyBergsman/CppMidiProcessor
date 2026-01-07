#pragma once

#include <QtGlobal>
#include <QString>

#include "virtuoso/groove/FeelTemplate.h"
#include "virtuoso/groove/GrooveGrid.h"
#include "virtuoso/groove/GrooveTemplate.h"
#include "virtuoso/util/StableRng.h"

namespace virtuoso::groove {

// Per-instrument timing + velocity shaping parameters.
// This replaces legacy, instrument-specific knobs with a unified groove profile.
struct InstrumentGrooveProfile {
    QString instrument; // e.g. "Bass", "Piano"

    // Timing offsets
    int pushMs = 0;            // negative feel (ahead of beat)
    int laidBackMs = 0;        // behind the beat
    int microJitterMs = 0;     // +/- uniform
    int attackVarianceMs = 0;  // extra +/- uniform per note

    // Slow bar-level drift
    int driftMaxMs = 0;        // clamp
    double driftRate = 0.0;    // 0..1 random-walk step relative to driftMaxMs

    // Velocity shaping
    int velocityJitter = 0;       // +/- uniform
    double accentDownbeat = 1.0;  // beat 1
    double accentBackbeat = 1.0;  // beats 2/4

    // Determinism
    quint32 humanizeSeed = 1;

    // Structural tightening
    int clampMsStructural = 18; // tighter on chord arrivals / strong beats
    int clampMsLoose = 32;      // looser elsewhere

    // Phrase shaping (MVP):
    // Adds a tiny, deterministic arc over phrases so performances don't feel "flat".
    // This is intentionally subtle; groove templates remain the primary feel source.
    int phraseBars = 4;              // common jazz phrasing unit
    int phraseTimingMaxMs = 6;       // +/- ms added per phrase (center-weighted)
    double phraseVelocityMax = 0.10; // +/- relative multiplier (e.g. 0.10 => up to 10%)
};

struct HumanizedEvent {
    qint64 onMs = 0;
    qint64 offMs = 0;
    int velocity = 0;

    // Explainability
    QString groove_template;
    QString grid_pos;
    int timing_offset_ms = 0;
    int velocity_adjustment = 0;
    quint32 humanize_seed = 0;
};

// Deterministic expert timing with stochastic humanization (seeded).
// State is per-instrument (drift random-walk, RNG stream).
class TimingHumanizer {
public:
    TimingHumanizer() = default;
    explicit TimingHumanizer(const InstrumentGrooveProfile& p) { setProfile(p); }

    void setProfile(const InstrumentGrooveProfile& p) {
        m_profile = p;
        // Create a stable, instrument-specific RNG stream.
        const quint32 seed = (m_profile.humanizeSeed == 0u) ? 1u : m_profile.humanizeSeed;
        // Mix with a constant to reduce accidental collisions.
        m_rng.seed(quint64(seed ^ 0xA5C3'91E5u));
        m_currentBar = -1;
        m_currentPhrase = -1;
        m_driftMs = 0;
        m_phraseOffsetMs = 0;
        m_phraseVelMul = 1.0;
    }

    const InstrumentGrooveProfile& profile() const { return m_profile; }

    void setFeelTemplate(const FeelTemplate& t) { m_feel = t; }
    const FeelTemplate& feelTemplate() const { return m_feel; }

    void setGrooveTemplate(const GrooveTemplate& t) { m_grooveTemplate = t; m_hasGrooveTemplate = true; }
    bool hasGrooveTemplate() const { return m_hasGrooveTemplate; }

    void reset() {
        m_currentBar = -1;
        m_currentPhrase = -1;
        m_driftMs = 0;
        m_phraseOffsetMs = 0;
        m_phraseVelMul = 1.0;
    }

    HumanizedEvent humanizeNote(const GridPos& start,
                               const TimeSignature& ts,
                               int bpm,
                               int baseVelocity,
                               const Rational& durationWhole,
                               bool structural) {
        advanceDriftToBar(start.barIndex);

        const qint64 baseOn = GrooveGrid::posToMs(start, ts, bpm);
        const qint64 baseOff = baseOn + qMax<qint64>(1, GrooveGrid::wholeNotesToMs(durationWhole, bpm));

        // Template offset (swing/pocket).
        const int feelMs = m_hasGrooveTemplate
            ? m_grooveTemplate.offsetMsFor(start, ts, bpm)
            : m_feel.offsetMsFor(start, ts, bpm);

        // Tempo-aware tightening:
        // At higher BPM, fixed-ms offsets quickly become a large fraction of a 16th note,
        // which reads as "drunk/stumbling". Scale down profile-driven offsets and clamp them
        // relative to the beat subdivision.
        if (bpm <= 0) bpm = 120;
        const double tempoScale = qBound(0.35, 90.0 / double(bpm), 1.0); // 90bpm=1.0, 150bpm~0.60
        const double wholeMs = 240000.0 / double(bpm); // ms per whole note
        const double beatMs = wholeMs / double(qMax(1, ts.den));         // ms per beat (ts.den defines beat)
        const double sixteenthMs = beatMs / 4.0;
        const int maxOffsetMusical = qBound(6, int(llround(0.22 * sixteenthMs)), 48); // ~22% of a 16th

        // Random components: center-weighted (triangular) instead of uniform.
        // This better matches human playing: most hits are near the grid with occasional larger deviations.
        auto tri = [&](int maxAbs) -> int {
            if (maxAbs <= 0) return 0;
            // Two uniforms (0..maxAbs) summed minus maxAbs -> [-maxAbs..+maxAbs] with triangular distribution.
            const int a = int(m_rng.bounded(quint32(maxAbs + 1)));
            const int b = int(m_rng.bounded(quint32(maxAbs + 1)));
            return (a + b) - maxAbs;
        };
        const int jitterMax = int(llround(double(m_profile.microJitterMs) * tempoScale));
        const int attackMax = int(llround(double(m_profile.attackVarianceMs) * tempoScale));
        int jitter = tri(jitterMax);
        int attackVar = tri(attackMax);

        int push = int(llround(double(m_profile.pushMs) * tempoScale));
        int laidBack = int(llround(double(m_profile.laidBackMs) * tempoScale));
        int driftLocal = int(llround(double(m_driftMs) * tempoScale));

        if (structural) {
            // Tighten timing on strong musical landmarks.
            jitter = 0;
            attackVar = 0;
            push = int(llround(double(push) * 0.40));
            laidBack = int(llround(double(laidBack) * 0.40));
            driftLocal = int(llround(double(driftLocal) * 0.30));
        }

        // Phrase shaping: a tiny arc (crescendo toward mid-phrase, then relax),
        // plus a small per-phrase pocket offset. Deterministic and bar-index driven.
        int phraseOffset = int(llround(double(m_phraseOffsetMs) * tempoScale));
        double phraseVelMul = m_phraseVelMul;
        if (m_profile.phraseBars > 1) {
            const int posInPhrase = (start.barIndex >= 0) ? (start.barIndex % m_profile.phraseBars) : 0;
            const double t = double(posInPhrase) / double(qMax(1, m_profile.phraseBars - 1)); // 0..1
            const double arc = 1.0 - qAbs(2.0 * t - 1.0); // 0..1 (peaks mid phrase)
            // Keep it subtle: arc influences less than the per-phrase random multiplier.
            phraseVelMul *= (1.0 + (arc - 0.5) * m_profile.phraseVelocityMax * 0.40);
            phraseOffset += int(llround((arc - 0.5) * double(m_profile.phraseTimingMaxMs) * 0.30 * tempoScale));
        }

        int totalOffset = feelMs + laidBack - push + driftLocal + phraseOffset + jitter + attackVar;
        int clampMs = structural ? m_profile.clampMsStructural : m_profile.clampMsLoose;
        clampMs = int(llround(double(clampMs) * tempoScale));
        clampMs = qMin(clampMs, maxOffsetMusical);
        clampMs = qMax(4, clampMs);
        if (totalOffset > clampMs) totalOffset = clampMs;
        if (totalOffset < -clampMs) totalOffset = -clampMs;

        // Velocity curve: downbeat/backbeat + jitter.
        int beatInBar = 0;
        Rational withinBeat{0, 1};
        GrooveGrid::splitWithinBar(start, ts, beatInBar, withinBeat);
        const bool isBeatStart = (withinBeat.num == 0);
        double velMul = 1.0;
        // Important: only apply beat accents at the *start of the beat*.
        // Otherwise 8th-note and triplet patterns would "double/triple accent" the beat.
        if (isBeatStart && beatInBar == 0) velMul *= m_profile.accentDownbeat;
        if (isBeatStart && (beatInBar % 2) == 1) velMul *= m_profile.accentBackbeat;
        // Apply phrase dynamics after beat accents.
        velMul *= phraseVelMul;

        int vel = int(llround(double(baseVelocity) * velMul));
        int velJ = tri(m_profile.velocityJitter);
        if (structural) velJ = 0;
        vel += velJ;
        if (vel < 1) vel = 1;
        if (vel > 127) vel = 127;

        HumanizedEvent out;
        out.onMs = baseOn + totalOffset;
        out.offMs = baseOff + totalOffset;
        out.velocity = vel;
        out.groove_template = m_hasGrooveTemplate ? m_grooveTemplate.key : m_feel.key;
        out.grid_pos = GrooveGrid::toString(start, ts);
        out.timing_offset_ms = totalOffset;
        out.velocity_adjustment = vel - baseVelocity;
        out.humanize_seed = (m_profile.humanizeSeed == 0u) ? 1u : m_profile.humanizeSeed;
        return out;
    }

private:
    void advanceDriftToBar(int barIndex) {
        if (barIndex < 0) barIndex = 0;
        if (m_currentBar == -1) {
            m_currentBar = barIndex;
            m_driftMs = 0;
            // Initialize phrase state.
            m_currentPhrase = -1;
            m_phraseOffsetMs = 0;
            m_phraseVelMul = 1.0;
            // Force phrase calculation for the current bar.
            const int phraseBars = qMax(1, m_profile.phraseBars);
            const int phraseIndex = barIndex / phraseBars;
            m_currentPhrase = phraseIndex - 1;
            return;
        }
        while (m_currentBar < barIndex) {
            m_currentBar++;

            // Update per-phrase parameters when we enter a new phrase (bar-index driven).
            const int phraseBars = qMax(1, m_profile.phraseBars);
            const int phraseIndex = m_currentBar / phraseBars;
            if (phraseIndex != m_currentPhrase) {
                m_currentPhrase = phraseIndex;
                // Deterministic phrase RNG seeded by instrument seed + phrase index (independent of note count).
                const quint32 baseSeed = (m_profile.humanizeSeed == 0u) ? 1u : m_profile.humanizeSeed;
                virtuoso::util::StableRng phraseRng(quint64(baseSeed ^ 0x51ED'BEEFu) ^ quint64(quint32(phraseIndex * 1315423911u)));

                auto triLocal = [&](int maxAbs) -> int {
                    if (maxAbs <= 0) return 0;
                    const int a = int(phraseRng.bounded(quint32(maxAbs + 1)));
                    const int b = int(phraseRng.bounded(quint32(maxAbs + 1)));
                    return (a + b) - maxAbs;
                };

                const int pt = triLocal(qMax(0, m_profile.phraseTimingMaxMs));
                m_phraseOffsetMs = qBound(-m_profile.phraseTimingMaxMs, pt, m_profile.phraseTimingMaxMs);

                const double vMax = qBound(0.0, m_profile.phraseVelocityMax, 0.50);
                const double u = phraseRng.nextDouble01(); // 0..1
                m_phraseVelMul = 1.0 + ((u * 2.0 - 1.0) * vMax);
                if (m_phraseVelMul < 0.50) m_phraseVelMul = 0.50;
                if (m_phraseVelMul > 1.50) m_phraseVelMul = 1.50;
            }

            if (m_profile.driftMaxMs <= 0 || m_profile.driftRate <= 0.0) {
                m_driftMs = 0;
                continue;
            }
            const int stepMax = qMax(1, int(llround(double(m_profile.driftMaxMs) * m_profile.driftRate)));
            // Center-weighted drift steps (reduces "random walk jitteriness").
            const int a = int(m_rng.bounded(quint32(stepMax + 1)));
            const int b = int(m_rng.bounded(quint32(stepMax + 1)));
            const int delta = (a + b) - stepMax;
            m_driftMs += delta;
            if (m_driftMs > m_profile.driftMaxMs) m_driftMs = m_profile.driftMaxMs;
            if (m_driftMs < -m_profile.driftMaxMs) m_driftMs = -m_profile.driftMaxMs;
        }
    }

    InstrumentGrooveProfile m_profile;
    FeelTemplate m_feel = FeelTemplate::straight();
    bool m_hasGrooveTemplate = false;
    GrooveTemplate m_grooveTemplate{};
    virtuoso::util::StableRng m_rng;
    int m_currentBar = -1;
    int m_currentPhrase = -1;
    int m_driftMs = 0;
    int m_phraseOffsetMs = 0;
    double m_phraseVelMul = 1.0;
};

} // namespace virtuoso::groove

