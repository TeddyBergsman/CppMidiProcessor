#pragma once

#include <QtGlobal>
#include <QRandomGenerator>
#include <QString>

#include "virtuoso/groove/FeelTemplate.h"
#include "virtuoso/groove/GrooveGrid.h"
#include "virtuoso/groove/GrooveTemplate.h"

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
        m_rng.seed(seed ^ 0xA5C3'91E5u);
        m_currentBar = -1;
        m_driftMs = 0;
    }

    const InstrumentGrooveProfile& profile() const { return m_profile; }

    void setFeelTemplate(const FeelTemplate& t) { m_feel = t; }
    const FeelTemplate& feelTemplate() const { return m_feel; }

    void setGrooveTemplate(const GrooveTemplate& t) { m_grooveTemplate = t; m_hasGrooveTemplate = true; }
    bool hasGrooveTemplate() const { return m_hasGrooveTemplate; }

    void reset() {
        m_currentBar = -1;
        m_driftMs = 0;
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

        // Random components (uniform for MVP; gaussian can be added later).
        int jitter = (m_profile.microJitterMs > 0)
            ? (int(m_rng.bounded(m_profile.microJitterMs * 2 + 1)) - m_profile.microJitterMs)
            : 0;
        int attackVar = (m_profile.attackVarianceMs > 0)
            ? (int(m_rng.bounded(m_profile.attackVarianceMs * 2 + 1)) - m_profile.attackVarianceMs)
            : 0;

        int push = m_profile.pushMs;
        int laidBack = m_profile.laidBackMs;
        int driftLocal = m_driftMs;

        if (structural) {
            // Tighten timing on strong musical landmarks.
            jitter = 0;
            attackVar = 0;
            push = int(llround(double(push) * 0.40));
            laidBack = int(llround(double(laidBack) * 0.40));
            driftLocal = int(llround(double(driftLocal) * 0.30));
        }

        int totalOffset = feelMs + laidBack - push + driftLocal + jitter + attackVar;
        const int clampMs = structural ? m_profile.clampMsStructural : m_profile.clampMsLoose;
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

        int vel = int(llround(double(baseVelocity) * velMul));
        int velJ = (m_profile.velocityJitter > 0)
            ? (int(m_rng.bounded(m_profile.velocityJitter * 2 + 1)) - m_profile.velocityJitter)
            : 0;
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
            return;
        }
        while (m_currentBar < barIndex) {
            m_currentBar++;
            if (m_profile.driftMaxMs <= 0 || m_profile.driftRate <= 0.0) {
                m_driftMs = 0;
                continue;
            }
            const int stepMax = qMax(1, int(llround(double(m_profile.driftMaxMs) * m_profile.driftRate)));
            const int delta = int(m_rng.bounded(stepMax * 2 + 1)) - stepMax;
            m_driftMs += delta;
            if (m_driftMs > m_profile.driftMaxMs) m_driftMs = m_profile.driftMaxMs;
            if (m_driftMs < -m_profile.driftMaxMs) m_driftMs = -m_profile.driftMaxMs;
        }
    }

    InstrumentGrooveProfile m_profile;
    FeelTemplate m_feel = FeelTemplate::straight();
    bool m_hasGrooveTemplate = false;
    GrooveTemplate m_grooveTemplate{};
    QRandomGenerator m_rng;
    int m_currentBar = -1;
    int m_driftMs = 0;
};

} // namespace virtuoso::groove

