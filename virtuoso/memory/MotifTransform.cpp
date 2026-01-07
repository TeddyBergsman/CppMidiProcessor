#include "virtuoso/memory/MotifTransform.h"

#include <QtGlobal>
#include <algorithm>

namespace virtuoso::memory {

PitchMotifTransformResult transformPitchMotif(const QVector<int>& basePcs, int resolvePc, quint32 modeSeed) {
    PitchMotifTransformResult out;
    if (basePcs.isEmpty()) return out;
    const int n = basePcs.size();
    const int basePc = (basePcs.first() % 12 + 12) % 12;

    // Convert to small signed intervals around the first pitch class (keeps contour stable-ish).
    QVector<int> iv;
    iv.reserve(n);
    for (int ppc : basePcs) {
        const int pc = (ppc % 12 + 12) % 12;
        int d = pc - basePc;
        while (d > 6) d -= 12;
        while (d < -6) d += 12;
        iv.push_back(d);
    }

    const int mode = int(modeSeed % 5u);
    PitchMotifTransform kind = PitchMotifTransform::Repeat;
    QVector<int> tiv = iv;
    bool displace = false;

    if (mode == 1) { kind = PitchMotifTransform::Sequence; }
    else if (mode == 2) { kind = PitchMotifTransform::Invert; for (int& x : tiv) x = -x; }
    else if (mode == 3) { kind = PitchMotifTransform::Retrograde; std::reverse(tiv.begin(), tiv.end()); }
    else if (mode == 4) { kind = PitchMotifTransform::RhythmicDisplace; displace = true; }

    QVector<int> motifPcs;
    motifPcs.reserve(tiv.size());
    for (int x : tiv) motifPcs.push_back((basePc + x + 1200) % 12);

    // Sequence: transpose so the final note resolves to resolvePc.
    if (kind == PitchMotifTransform::Sequence && !motifPcs.isEmpty()) {
        const int tgt = (resolvePc % 12 + 12) % 12;
        const int lastPc = motifPcs.last();
        const int tr = (tgt - lastPc + 1200) % 12;
        for (int& ppc : motifPcs) ppc = (ppc + tr + 1200) % 12;
    }

    out.pcs = motifPcs;
    out.kind = kind;
    out.displaceRhythm = displace;
    switch (kind) {
        case PitchMotifTransform::Sequence: out.tag = "mem:sequence"; break;
        case PitchMotifTransform::Invert: out.tag = "mem:invert"; break;
        case PitchMotifTransform::Retrograde: out.tag = "mem:retro"; break;
        case PitchMotifTransform::RhythmicDisplace: out.tag = "mem:displace"; break;
        default: out.tag = "mem:repeat"; break;
    }
    return out;
}

} // namespace virtuoso::memory

