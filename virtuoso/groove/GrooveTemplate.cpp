#include "virtuoso/groove/GrooveTemplate.h"

#include <QtGlobal>

namespace virtuoso::groove {

static Rational normalizeWithinBeat(const GridPos& pos, const TimeSignature& ts) {
    int beatInBar = 0;
    Rational withinBeatWhole{0, 1};
    GrooveGrid::splitWithinBar(pos, ts, beatInBar, withinBeatWhole);
    const Rational beatWhole = GrooveGrid::beatDurationWhole(ts);
    // withinBeatNormalized = withinBeat / beat
    return Rational(withinBeatWhole.num * beatWhole.den, withinBeatWhole.den * beatWhole.num);
}

static double beatMs(const TimeSignature& ts, int bpm) {
    if (bpm <= 0) bpm = 120;
    const double quarterMs = 60000.0 / double(bpm);
    return quarterMs * (4.0 / double(ts.den));
}

int GrooveTemplate::offsetMsFor(const GridPos& pos, const TimeSignature& ts, int bpm) const {
    if (amount <= 0.0) return 0;
    const Rational w = normalizeWithinBeat(pos, ts);
    for (const auto& p : offsetMap) {
        if (p.withinBeat != w) continue;
        double ms = 0.0;
        if (p.unit == OffsetUnit::Ms) {
            ms = p.value;
        } else {
            ms = p.value * beatMs(ts, bpm);
        }
        return int(llround(ms * amount));
    }
    return 0;
}

} // namespace virtuoso::groove

