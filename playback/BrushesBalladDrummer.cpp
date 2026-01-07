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
    const int phraseBars = (ctx.phraseBars > 0) ? ctx.phraseBars : (m_p.phraseBars > 0 ? m_p.phraseBars : 4);
    const int barInPhrase = (phraseBars > 0) ? (bar % phraseBars) : 0;
    const bool phraseStart = (phraseBars > 0) ? (barInPhrase == 0) : (bar == 0);
    const bool phraseEnd = (phraseBars > 0) ? (barInPhrase == (phraseBars - 1)) : false;
    const bool phraseEndBar = ctx.phraseEndBar || phraseEnd;
    const bool phraseSetupBar = (phraseBars > 1) ? (barInPhrase == (phraseBars - 2)) : false;
    const double cadence01 = qBound(0.0, ctx.cadence01, 1.0);
    const bool shouldRetriggerLoop = (m_p.loopRetriggerBars > 0) ? ((bar % m_p.loopRetriggerBars) == 0) : phraseStart;

    const auto gp = GrooveGrid::fromBarBeatTuplet(bar, beat, 0, 1, ts);
    const double e = qBound(0.0, ctx.energy, 1.0);
    const double gb = qBound(-1.0, ctx.gestureBias, 1.0);
    const bool allowRide = ctx.allowRide;
    const bool allowPhraseGestures = ctx.allowPhraseGestures;

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
    // Start ride/cymbal texture earlier as energy rises.
    // Build: ride on backbeats (2&4). Climax: ride every beat + optional upbeats.
    if (allowRide && e >= 0.42) {
        const bool backbeatOnly = (e < 0.72);
        const bool doRideThisBeat = backbeatOnly ? isBackbeat : true;
        if (doRideThisBeat) {
            AgentIntentNote ride;
            ride.agent = "Drums";
            ride.channel = m_p.channel;
            ride.note = m_p.noteRideHit;
            ride.baseVelocity = qBound(1, 20 + int(llround(32.0 * e)), 127);
            ride.startPos = gp;
            ride.durationWhole = Rational(1, 16);
            ride.structural = true;
            ride.logic_tag = backbeatOnly ? "Drums:RideBackbeat" : "Drums:RidePulse";
            out.push_back(ride);

            if (!backbeatOnly && e >= 0.80) {
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
    if (allowRide && ctx.intensityPeak && beat == 0) {
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
    if (allowPhraseGestures && allowRide && phraseEndBar && beat == (qMax(1, ts.num) - 1)) {
        const quint32 s = mixSeed(ctx.determinismSeed, quint32(bar * 23 + 909));
        const double p = unitRand01(s);
        const double pSwish = qBound(0.0, m_p.phraseEndSwishProb + 0.35 * cadence01, 1.0);
        if (p < pSwish) {
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

    // --- 4b) Cadence pickup: soft brush short on the and-of-4 into the next bar. ---
    // This is a key "session drummer" marker: a tiny pickup, not a fill.
    if (allowPhraseGestures && phraseEndBar && cadence01 >= 0.55 && beat == (qMax(1, ts.num) - 1) && !ctx.intensityPeak) {
        const quint32 s = mixSeed(ctx.determinismSeed, quint32(bar * 29 + 0xCADEu));
        const double p = unitRand01(s);
        const double want = qBound(0.0, 0.10 + 0.55 * cadence01 + 0.20 * e, 0.85);
        if (p < want) {
            AgentIntentNote pk;
            pk.agent = "Drums";
            pk.channel = m_p.channel;
            pk.note = m_p.noteBrushShort;
            pk.baseVelocity = qBound(1, 18 + int(llround(14.0 * e)) + int(llround(10.0 * cadence01)), 127);
            pk.startPos = GrooveGrid::fromBarBeatTuplet(bar, beat, /*sub*/1, /*count*/2, ts); // and-of-last-beat
            pk.durationWhole = Rational(1, 16);
            pk.structural = true;
            pk.logic_tag = "Drums:CadencePickupBrush";
            out.push_back(pk);
        }
    }

    // --- 4c) Cadence orchestration: occasional ride hit on the last beat (more air / shimmer). ---
    if (allowPhraseGestures && allowRide && phraseEndBar && cadence01 >= 0.70 && beat == (qMax(1, ts.num) - 1)) {
        const quint32 s = mixSeed(ctx.determinismSeed, quint32(bar * 37 + 0xBEEFu));
        const double p = unitRand01(s);
        const double want = qBound(0.0, 0.08 + 0.30 * cadence01, 0.50);
        if (p < want) {
            AgentIntentNote rh;
            rh.agent = "Drums";
            rh.channel = m_p.channel;
            rh.note = m_p.noteRideHit;
            rh.baseVelocity = qBound(1, 22 + int(llround(26.0 * e)), 127);
            rh.startPos = gp;
            rh.durationWhole = Rational(1, 16);
            rh.structural = true;
            rh.logic_tag = "Drums:CadenceRideHit";
            out.push_back(rh);
        }
    }

    // --- 5) Phrase setup swell (bar before phrase end): set up the cadence.
    // This is a subtle "session drummer" move: a soft ride swish or brush short pickup on the last beat
    // of the setup bar, to make the phrase end feel prepared rather than random.
    if (allowPhraseGestures && phraseSetupBar && beat == (qMax(1, ts.num) - 1) && cadence01 >= 0.35) {
        const quint32 s = mixSeed(ctx.determinismSeed, quint32(bar * 41 + 0x5157u));
        const double p = unitRand01(s);
        const double want = qBound(0.0, 0.10 + 0.35 * cadence01 + 0.20 * e + 0.18 * gb, 0.75);
        if (p < want) {
            const bool doSwish = allowRide && (unitRand01(mixSeed(s, 0x5315u)) < qBound(0.0, 0.35 + 0.45 * e + 0.20 * gb, 0.92));
            if (doSwish) {
                AgentIntentNote sw;
                sw.agent = "Drums";
                sw.channel = m_p.channel;
                sw.note = m_p.noteRideSwish;
                sw.baseVelocity = qBound(1, 18 + int(llround(18.0 * e)) + int(llround(10.0 * cadence01)), 127);
                sw.startPos = gp;
                // Hold into the downbeat a bit (reads as swell), but keep it short.
                const int holdMs = qBound(420, msForBars(bpm, ts, 1) / 3, 1200);
                sw.durationWhole = durationWholeFromHoldMs(holdMs, bpm);
                sw.structural = true;
                sw.logic_tag = "Drums:PhraseSetupSwish";
                out.push_back(sw);
            } else {
                // Brush pickup gesture: two 16ths on the last beat (and-of-4 + a).
                for (int sub = 1; sub <= 3; sub += 2) {
                    AgentIntentNote pk;
                    pk.agent = "Drums";
                    pk.channel = m_p.channel;
                    pk.note = m_p.noteBrushShort;
                    pk.baseVelocity = qBound(1, 16 + int(llround(16.0 * e)) + int(llround(8.0 * cadence01)), 127);
                    pk.startPos = GrooveGrid::fromBarBeatTuplet(bar, beat, /*sub*/sub, /*count*/4, ts);
                    pk.durationWhole = Rational(1, 32);
                    pk.structural = true;
                    pk.logic_tag = "Drums:PhraseSetupBrushPickup";
                    out.push_back(pk);
                }
            }
        }
    }

    // --- 6) Phrase end flourish (strong cadence): a tiny fill, not a "drum fill".
    // When cadence is very strong and energy allows, add a short three-note gesture on the last beat
    // (brush short -> snare swish -> ride hit). This is deliberately sparse and deterministic.
    if (allowPhraseGestures && phraseEndBar && cadence01 >= 0.85 && beat == (qMax(1, ts.num) - 1) && e >= 0.35 && !ctx.intensityPeak) {
        const quint32 s = mixSeed(ctx.determinismSeed, quint32(bar * 43 + 0xF11Eu));
        const double p = unitRand01(s);
        const double want = qBound(0.0, 0.12 + 0.35 * cadence01 + 0.15 * e + 0.20 * gb, 0.70);
        if (p < want) {
            struct Hit { int sub; int count; int note; int vel; const char* tag; };
            const Hit hits[] = {
                {1, 4, m_p.noteBrushShort, qBound(1, 16 + int(llround(14.0 * e)), 127), "Drums:CadenceFlourishBrush"},
                {2, 4, m_p.noteSnareSwish, qBound(1, 20 + int(llround(20.0 * e)), 127), "Drums:CadenceFlourishSnare"},
                {3, 4, allowRide ? m_p.noteRideHit : m_p.noteBrushShort, qBound(1, 18 + int(llround(24.0 * e)), 127), "Drums:CadenceFlourishRide"},
            };
            for (const auto& h : hits) {
                AgentIntentNote n;
                n.agent = "Drums";
                n.channel = m_p.channel;
                n.note = h.note;
                n.baseVelocity = h.vel;
                n.startPos = GrooveGrid::fromBarBeatTuplet(bar, beat, h.sub, h.count, ts);
                n.durationWhole = Rational(1, 32);
                n.structural = true;
                n.logic_tag = h.tag;
                out.push_back(n);
            }
        }
    }

    Q_UNUSED(phraseStart);
    return out;
}

} // namespace playback

