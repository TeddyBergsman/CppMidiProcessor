#include "playback/BrushesBalladDrummer.h"

#include <QtGlobal>
#include <QtMath>

namespace playback {
using virtuoso::engine::AgentIntentNote;
using virtuoso::groove::GrooveGrid;
using virtuoso::groove::Rational;

double BrushesBalladDrummer::unitRand01(quint32 x) {
    // Deterministic 0..1 from integer.
    // Keep it stable and fast: map to 24-bit mantissa.
    const quint32 v = (x ^ 0x9E37'79B9u) & 0x00FF'FFFFu;
    return double(v) / double(0x0100'0000u);
}

quint32 BrushesBalladDrummer::mixSeed(quint32 a, quint32 b) {
    // Simple reversible-ish mixing (not cryptographic).
    quint32 x = a ^ (b + 0x9E37'79B9u + (a << 6) + (a >> 2));
    x ^= (x << 13);
    x ^= (x >> 17);
    x ^= (x << 5);
    return x;
}

Rational BrushesBalladDrummer::durationWholeFromHoldMs(int holdMs, int bpm) {
    if (holdMs <= 0) return Rational(1, 16);
    if (bpm <= 0) bpm = 120;
    return Rational(qint64(holdMs) * qint64(bpm), qint64(240000));
}

int BrushesBalladDrummer::msForBars(int bpm, const virtuoso::groove::TimeSignature& ts, int bars) {
    if (bpm <= 0) bpm = 120;
    const int num = (ts.num > 0) ? ts.num : 4;
    const int den = (ts.den > 0) ? ts.den : 4;
    const double quarterMs = 60000.0 / double(bpm);
    const double beatMs = quarterMs * (4.0 / double(den));
    const double barMs = beatMs * double(num);
    return int(llround(qMax(0.0, barMs * double(qMax(1, bars)))));
}

QVector<AgentIntentNote> BrushesBalladDrummer::planBeat(const Context& ctx) const {
    QVector<AgentIntentNote> out;

    const int bpm = qMax(30, ctx.bpm);
    virtuoso::groove::TimeSignature ts = ctx.ts;
    if (ts.num <= 0) ts.num = 4;
    if (ts.den <= 0) ts.den = 4;

    const int bar = qMax(0, ctx.playbackBarIndex);
    const int beat = qMax(0, ctx.beatInBar);
    const bool phraseStart = (m_p.phraseBars > 0) ? ((bar % m_p.phraseBars) == 0) : (bar == 0);
    const bool phraseEnd = (m_p.phraseBars > 0) ? ((bar % m_p.phraseBars) == (m_p.phraseBars - 1)) : false;
    const bool shouldRetriggerLoop = (m_p.loopRetriggerBars > 0) ? ((bar % m_p.loopRetriggerBars) == 0) : phraseStart;

    const auto gp = GrooveGrid::fromBarBeatTuplet(bar, beat, 0, 1, ts);
    const double e = qBound(0.0, ctx.energy, 1.0);

    // --- 1) Feather kick on beat 1 (beatInBar==0). ---
    // Keep probability very low; make it slightly more likely on structural beats.
    if (beat == 0) {
        const quint32 s = mixSeed(ctx.determinismSeed, quint32(bar * 17 + beat * 3 + 101));
        const double p = unitRand01(s);
        const double energyBoost = (0.65 + 0.70 * qBound(0.0, ctx.energy, 1.0)); // 0.65..1.35
        const double kickProb = qBound(0.0, m_p.kickProbOnBeat1 * (ctx.structural ? 1.20 : 1.0) * energyBoost, 1.0);
        if (p < kickProb) {
            AgentIntentNote k;
            k.agent = "Drums";
            k.channel = m_p.channel;
            k.note = m_p.noteKick;
            const int vel = m_p.velKick + (ctx.structural ? 4 : 0) + int(llround(6.0 * qBound(0.0, ctx.energy, 1.0)));
            k.baseVelocity = qBound(1, vel, 127);
            k.startPos = gp;
            k.durationWhole = Rational(1, 16);
            k.structural = ctx.structural;
            k.logic_tag = "Drums:FeatherKick";
            out.push_back(k);
        }
    }

    // --- 2) Continuous brush texture (looping stir). ---
    // Retrigger only on phrase starts (once per N bars). Hold long enough to reach loop body.
    if (beat == 0 && shouldRetriggerLoop) {
        const quint32 s = mixSeed(ctx.determinismSeed, quint32(bar * 31 + 777));
        const double pick = unitRand01(s);
        const int note = (pick < 0.70) ? m_p.noteBrushLoopA : m_p.noteBrushLoopB;

        const int holdBarsMs = msForBars(bpm, ts, qMax(1, m_p.loopHoldBars));
        const int holdMs = qMax(m_p.minLoopHoldMs, holdBarsMs);

        AgentIntentNote n;
        n.agent = "Drums";
        n.channel = m_p.channel;
        n.note = note;
        n.baseVelocity = qBound(1, m_p.velLoop, 127);
        n.startPos = gp;
        n.durationWhole = durationWholeFromHoldMs(holdMs, bpm);
        n.structural = true;
        n.logic_tag = "Drums:BrushStirLoop";
        out.push_back(n);
    }

    // --- 3) Swish accents on 2&4 (beats 2 and 4 in 4/4). ---
    // In odd meters, still treat every other beat as a "backbeat-ish" landmark.
    const bool isBackbeat = (beat % 2) == 1;
    if (isBackbeat) {
        const quint32 s = mixSeed(ctx.determinismSeed, quint32(bar * 19 + beat * 7 + 202));
        const double p = unitRand01(s);
        const double swishProb = qBound(0.0, m_p.swishProbOn2And4 * (0.80 + 0.50 * e), 1.0);
        if (p < swishProb) {
            const double altp = unitRand01(mixSeed(s, 0xB00Bu));
            const bool useAlt = (altp < qBound(0.0, m_p.swishAltShortProb, 1.0));

            AgentIntentNote sw;
            sw.agent = "Drums";
            sw.channel = m_p.channel;
            sw.note = useAlt ? m_p.noteBrushShort : m_p.noteSnareSwish;
            sw.baseVelocity = qBound(1, m_p.velSwish + int(llround(8.0 * e)), 127);
            sw.startPos = gp;
            sw.durationWhole = Rational(1, 16);
            sw.structural = true;
            sw.logic_tag = useAlt ? "Drums:BrushSwishShort" : "Drums:SnareSwish";
            out.push_back(sw);
        }
    }

    // --- 3c) Vibe support: ride pattern (audible Build/Climax). ---
    // Build: ride on backbeats (2&4). Climax: ride every beat + optional upbeats.
    if (e >= 0.60) {
        const bool backbeatOnly = (e < 0.80);
        const bool doRideThisBeat = backbeatOnly ? isBackbeat : true;
        if (doRideThisBeat) {
            AgentIntentNote ride;
            ride.agent = "Drums";
            ride.channel = m_p.channel;
            ride.note = m_p.noteRideHit;
            ride.baseVelocity = qBound(1, 24 + int(llround(34.0 * e)), 127);
            ride.startPos = gp;
            ride.durationWhole = Rational(1, 16);
            ride.structural = true;
            ride.logic_tag = backbeatOnly ? "Drums:RideBackbeat" : "Drums:RidePulse";
            out.push_back(ride);

            if (!backbeatOnly && e >= 0.88) {
                AgentIntentNote up = ride;
                up.startPos = GrooveGrid::fromBarBeatTuplet(bar, beat, /*sub*/1, /*count*/2, ts);
                up.baseVelocity = qBound(1, up.baseVelocity - 10, 127);
                up.logic_tag = "Drums:RidePulseUpbeat";
                out.push_back(up);
            }
        }
    }

    // --- 3b) Intensity support: brief ride swish on beat 1 when user is peaking. ---
    // This is a *support texture*, not a full pattern switch.
    if (ctx.intensityPeak && beat == 0) {
        AgentIntentNote r;
        r.agent = "Drums";
        r.channel = m_p.channel;
        r.note = m_p.noteRideSwish;
        r.baseVelocity = qBound(1, 22 + int(llround(16.0 * e)), 127);
        r.startPos = gp;
        r.durationWhole = Rational(1, 8);
        r.structural = true;
        r.logic_tag = "Drums:IntensitySupportRide";
        out.push_back(r);
    }

    // --- 4) Phrase-end swish (longer ride swish / sweep). ---
    // Small probability on the last beat of the phrase to create a subtle phrase marker.
    if (phraseEnd && beat == (qMax(1, ts.num) - 1)) {
        const quint32 s = mixSeed(ctx.determinismSeed, quint32(bar * 23 + 909));
        const double p = unitRand01(s);
        if (p < qBound(0.0, m_p.phraseEndSwishProb, 1.0)) {
            const int holdMs = qMax(800, qMin(2000, msForBars(bpm, ts, 1) / 2));
            AgentIntentNote sw;
            sw.agent = "Drums";
            sw.channel = m_p.channel;
            sw.note = m_p.noteRideSwish;
            sw.baseVelocity = qBound(1, m_p.velPhraseEnd, 127);
            sw.startPos = gp;
            sw.durationWhole = durationWholeFromHoldMs(holdMs, bpm);
            sw.structural = true;
            sw.logic_tag = "Drums:PhraseEndSwish";
            out.push_back(sw);
        }
    }

    Q_UNUSED(phraseStart);
    return out;
}

} // namespace playback

