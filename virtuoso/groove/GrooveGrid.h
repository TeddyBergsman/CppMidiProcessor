#pragma once

#include <QtGlobal>
#include <QString>

namespace virtuoso::groove {

struct TimeSignature {
    int num = 4;
    int den = 4; // 1,2,4,8,16...
};

// Minimal rational type for exact tuplet/subdivision math.
// Invariants: den > 0, fraction is normalized (gcd reduced, den positive).
struct Rational {
    qint64 num = 0;
    qint64 den = 1;

    Rational() = default;
    Rational(qint64 n, qint64 d) : num(n), den(d) { normalize(); }

    void normalize() {
        if (den == 0) { den = 1; }
        if (den < 0) { den = -den; num = -num; }
        const qint64 a = num < 0 ? -num : num;
        const qint64 b = den;
        qint64 g = gcd(a, b);
        if (g <= 0) g = 1;
        num /= g;
        den /= g;
    }

    static constexpr qint64 gcd(qint64 a, qint64 b) {
        while (b != 0) {
            qint64 t = a % b;
            a = b;
            b = t;
        }
        return a < 0 ? -a : a;
    }

    double toDouble() const { return den == 0 ? 0.0 : double(num) / double(den); }

    friend constexpr bool operator==(const Rational& a, const Rational& b) {
        return a.num * b.den == b.num * a.den;
    }
    friend constexpr bool operator!=(const Rational& a, const Rational& b) { return !(a == b); }
    friend constexpr bool operator<(const Rational& a, const Rational& b) {
        return a.num * b.den < b.num * a.den;
    }
    friend constexpr bool operator<=(const Rational& a, const Rational& b) { return (a < b) || (a == b); }
    friend constexpr bool operator>(const Rational& a, const Rational& b) { return b < a; }
    friend constexpr bool operator>=(const Rational& a, const Rational& b) { return (b < a) || (a == b); }

    friend Rational operator+(Rational a, const Rational& b) {
        a.num = a.num * b.den + b.num * a.den;
        a.den = a.den * b.den;
        a.normalize();
        return a;
    }
    friend Rational operator-(Rational a, const Rational& b) {
        a.num = a.num * b.den - b.num * a.den;
        a.den = a.den * b.den;
        a.normalize();
        return a;
    }
    friend Rational operator*(Rational a, qint64 k) {
        a.num *= k;
        a.normalize();
        return a;
    }
    friend Rational operator/(Rational a, qint64 k) {
        a.den *= k;
        a.normalize();
        return a;
    }
};

// Position inside the chart in musical units.
// - barIndex: 0-based bar
// - withinBarWhole: offset from bar start in WHOLE-NOTE units (rational).
struct GridPos {
    int barIndex = 0;
    Rational withinBarWhole{0, 1};
};

class GrooveGrid {
public:
    static Rational barDurationWhole(const TimeSignature& ts) {
        return Rational(ts.num, ts.den); // num * (1/den) whole notes
    }

    static Rational beatDurationWhole(const TimeSignature& ts) {
        return Rational(1, ts.den);
    }

    // Create a position at a bar + beat + N-tuplet subdivision within the beat.
    // Example: triplet-eighth inside beat => subdivCount=3, subdivIndex=0..2.
    static GridPos fromBarBeatTuplet(int barIndex, int beatInBar, int subdivIndex, int subdivCount, const TimeSignature& ts) {
        if (subdivCount <= 0) subdivCount = 1;
        if (subdivIndex < 0) subdivIndex = 0;
        if (subdivIndex >= subdivCount) subdivIndex = subdivCount - 1;

        const Rational beat = beatDurationWhole(ts);
        const Rational withinBeat = beat / subdivCount * subdivIndex;
        GridPos p;
        p.barIndex = qMax(0, barIndex);
        p.withinBarWhole = beat * qMax(0, beatInBar) + withinBeat;
        return p;
    }

    // Total absolute time in whole-note units since chart start.
    static Rational toAbsoluteWholeNotes(const GridPos& p, const TimeSignature& ts) {
        return barDurationWhole(ts) * p.barIndex + p.withinBarWhole;
    }

    // Convert whole-note units to milliseconds given tempo (quarter-note BPM).
    static qint64 wholeNotesToMs(const Rational& wholeNotes, int bpm) {
        // quarter note ms:
        //   beatMs = 60000 / bpm
        // whole note ms:
        //   wholeMs = 4 * beatMs = 240000 / bpm
        if (bpm <= 0) bpm = 120;
        const double wholeMs = 240000.0 / double(bpm);
        return qint64(llround(wholeNotes.toDouble() * wholeMs));
    }

    static qint64 posToMs(const GridPos& p, const TimeSignature& ts, int bpm) {
        return wholeNotesToMs(toAbsoluteWholeNotes(p, ts), bpm);
    }

    // Split a within-bar offset into (beatInBar, withinBeatWhole).
    static void splitWithinBar(const GridPos& p, const TimeSignature& ts, int& beatInBarOut, Rational& withinBeatWholeOut) {
        const Rational beat = beatDurationWhole(ts);
        // beatInBar = floor(withinBarWhole / beat)
        const qint64 scaled = p.withinBarWhole.num * beat.den;
        const qint64 div = p.withinBarWhole.den * beat.num;
        qint64 q = 0;
        if (div != 0) {
            // floor division that works for non-negative offsets (we only use >=0 here).
            q = scaled / div;
        }
        if (q < 0) q = 0;
        beatInBarOut = int(q);
        withinBeatWholeOut = p.withinBarWhole - (beat * q);
    }

    // Human-readable, stable-ish representation for explainability.
    // Format: "bar.beat@num/denWhole" (bar/beat are 1-based).
    static QString toString(const GridPos& p, const TimeSignature& ts) {
        int beatInBar = 0;
        Rational withinBeat{0, 1};
        splitWithinBar(p, ts, beatInBar, withinBeat);
        return QString("%1.%2@%3/%4w")
            .arg(p.barIndex + 1)
            .arg(beatInBar + 1)
            .arg(withinBeat.num)
            .arg(withinBeat.den);
    }
};

} // namespace virtuoso::groove

