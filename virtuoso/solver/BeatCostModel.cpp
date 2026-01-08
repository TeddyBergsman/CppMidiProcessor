#include "virtuoso/solver/BeatCostModel.h"

#include <QtGlobal>

namespace virtuoso::solver {
namespace {

static int thirdIntervalForQuality(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::Minor:
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished: return 3;
        case music::ChordQuality::Sus2: return 2;
        case music::ChordQuality::Sus4: return 5;
        default: return 4;
    }
}

static int fifthIntervalForQuality(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished: return 6;
        case music::ChordQuality::Augmented: return 8;
        default: return 7;
    }
}

static int seventhIntervalFor(const music::ChordSymbol& c) {
    if (c.seventh == music::SeventhQuality::Major7) return 11;
    if (c.seventh == music::SeventhQuality::Dim7) return 9;
    if (c.seventh == music::SeventhQuality::Minor7) return 10;
    if (c.extension >= 7) return 10;
    return -1;
}

static int clampMidi(int m) { return (m < 0) ? 0 : (m > 127 ? 127 : m); }

} // namespace

QString CostBreakdown::shortTag(const CostWeights& w) const {
    const double t = total(w);
    return QString("cost=%1 h=%2 vl=%3 r=%4 i=%5")
        .arg(t, 0, 'f', 2)
        .arg(harmonicStability, 0, 'f', 2)
        .arg(voiceLeadingDistance, 0, 'f', 2)
        .arg(rhythmicInterest, 0, 'f', 2)
        .arg(interactionFactor, 0, 'f', 2);
}

CostWeights weightsFromWeightsV2(const virtuoso::control::PerformanceWeightsV2& w2) {
    const double hr = qBound(0.0, w2.creativity, 1.0);
    // Rhythmism is more meaningful when density is non-zero.
    const double rc = qBound(0.0, w2.rhythm * (0.70 + 0.60 * qBound(0.0, w2.density, 1.0)), 1.0);
    const double it = qBound(0.0, w2.interactivity, 1.0);

    CostWeights w;
    // Higher harmonicRisk means we tolerate "outside" more (lower harmony penalty weight).
    w.harmony = 1.30 - 0.85 * hr;         // 1.30..0.45
    // Voice leading always matters; interaction raises tolerance for larger moves a bit.
    w.voiceLeading = 0.95 - 0.20 * it;     // 0.95..0.75
    // Higher rhythmicComplexity means we tolerate more syncopation (lower penalty weight).
    w.rhythm = 1.25 - 0.85 * rc;          // 1.25..0.40
    // Higher interaction means we tolerate/encourage more density changes; but still penalize conflict.
    w.interaction = 1.00 + 0.40 * it;      // 1.00..1.40
    return w;
}

QSet<int> allowedPitchClassesForChord(const music::ChordSymbol& c) {
    QSet<int> pcs;
    const int root = (c.rootPc >= 0) ? c.rootPc : 0;
    const int bass = (c.bassPc >= 0) ? c.bassPc : root;

    auto pc = [&](int semi) -> int { return (root + semi + 1200) % 12; };
    auto applyAlter = [&](int degree, int basePc) -> int {
        for (const auto& a : c.alterations) {
            if (a.degree != degree) continue;
            return (basePc + a.delta + 1200) % 12;
        }
        return basePc;
    };

    // Always allow bass/root.
    pcs.insert((bass + 12) % 12);
    pcs.insert((root + 12) % 12);

    // Core chord tones + common extensions.
    const int pc3 = pc(thirdIntervalForQuality(c.quality));
    const int pc5 = applyAlter(5, pc(fifthIntervalForQuality(c.quality)));
    pcs.insert(pc3);
    pcs.insert(pc5);

    const int sev = seventhIntervalFor(c);
    if (sev >= 0) pcs.insert(pc(sev));

    // Extensions (if present/likely): 9/11/13 + alterations.
    pcs.insert(applyAlter(9, pc(14)));
    pcs.insert(applyAlter(11, pc(17)));
    pcs.insert(applyAlter(13, pc(21)));

    if (c.noChord) pcs.clear();
    return pcs;
}

double harmonicOutsidePenalty01(const QVector<virtuoso::engine::AgentIntentNote>& notes,
                                const music::ChordSymbol& chord) {
    if (notes.isEmpty()) return 0.0;
    const auto pcs = allowedPitchClassesForChord(chord);
    if (pcs.isEmpty()) return 0.0;
    int outside = 0;
    for (const auto& n : notes) {
        const int pc = clampMidi(n.note) % 12;
        if (!pcs.contains(pc)) outside++;
    }
    return double(outside) / double(qMax(1, notes.size()));
}

double rhythmicInterestPenalty01(const QVector<virtuoso::engine::AgentIntentNote>& notes,
                                 const virtuoso::groove::TimeSignature& ts) {
    if (notes.isEmpty()) return 0.0;
    int offbeat = 0;
    int sync = 0;
    for (const auto& n : notes) {
        int beatInBar = 0;
        virtuoso::groove::Rational within{0, 1};
        virtuoso::groove::GrooveGrid::splitWithinBar(n.startPos, ts, beatInBar, within);
        if (within.num != 0) offbeat++;
        if ((beatInBar % 2) == 1) sync++;
    }
    const double off01 = double(offbeat) / double(qMax(1, notes.size()));
    const double sync01 = double(sync) / double(qMax(1, notes.size()));
    // Penalize too much offbeat/sync relative to "ballad default". This is still a penalty model.
    return 0.70 * off01 + 0.30 * sync01;
}

double voiceLeadingPenalty(const QVector<virtuoso::engine::AgentIntentNote>& notes,
                           int prevCenterMidi) {
    if (notes.isEmpty()) return 0.0;
    qint64 sum = 0;
    for (const auto& n : notes) sum += clampMidi(n.note);
    const int mean = int(llround(double(sum) / double(qMax(1, notes.size()))));
    return double(qAbs(mean - clampMidi(prevCenterMidi))) / 12.0;
}

} // namespace virtuoso::solver

