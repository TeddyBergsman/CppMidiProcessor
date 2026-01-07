#include "virtuoso/constraints/BassDriver.h"

#include <QtGlobal>
#include <limits>

namespace virtuoso::constraints {
namespace {

struct Fingering {
    int stringIndex = -1; // 0..3 (E,A,D,G)
    int fret = -1;        // 0..maxFret
    double cost = 0.0;
};

} // namespace

FeasibilityResult BassDriver::evaluateFeasibility(const PerformanceState& state,
                                                  const CandidateGesture& candidate) const {
    FeasibilityResult r;

    if (candidate.midiNotes.isEmpty()) {
        r.ok = true;
        r.reasons.push_back("OK: empty gesture");
        return r;
    }

    // Standard 4-string bass open strings: E1(40), A1(45), D2(50), G2(55)
    const int open[4] = {40, 45, 50, 55};

    const int lastFret = state.ints.value("lastFret", -1);
    const int lastString = state.ints.value("lastString", -1);

    auto fingeringsForNote = [&](int note) -> QVector<Fingering> {
        QVector<Fingering> out;
        out.reserve(4);
        for (int s = 0; s < 4; ++s) {
            const int fret = note - open[s];
            if (fret < 0 || fret > m_c.maxFret) continue;
            Fingering f;
            f.stringIndex = s;
            f.fret = fret;
            f.cost = 0.0;
            // Prefer open strings.
            if (fret == 0) f.cost -= m_c.openStringBonus;
            // Slight preference for lower strings (thicker tone) when ambiguous.
            f.cost += 0.02 * double(s);
            out.push_back(f);
        }
        return out;
    };

    // Stage 1.5+: allow multi-note gestures (interpreted as a short sequential phrase).
    const QVector<int>& notes = candidate.midiNotes;
    if (notes.size() == 1) {
        const int note = notes[0];
        const auto opts = fingeringsForNote(note);
        if (opts.isEmpty()) {
            r.ok = false;
            r.reasons.push_back(QString("FAIL: note %1 not playable on 4-string bass within maxFret=%2")
                                    .arg(note)
                                    .arg(m_c.maxFret));
            return r;
        }

        Fingering best;
        best.cost = std::numeric_limits<double>::infinity();
        for (auto f : opts) {
            if (lastFret >= 0) f.cost += 0.10 * double(qAbs(f.fret - lastFret));
            if (lastString >= 0) f.cost += 0.12 * double(qAbs(f.stringIndex - lastString));
            if (lastFret >= 0 && qAbs(f.fret - lastFret) > m_c.maxFretShiftPerNote) continue;
            if (lastString >= 0 && qAbs(f.stringIndex - lastString) > m_c.maxStringJumpPerNote) continue;
            if (f.cost < best.cost) best = f;
        }
        if (best.stringIndex < 0) {
            r.ok = false;
            r.reasons.push_back(QString("FAIL: transition exceeds shift constraints (maxFretShiftPerNote=%1 maxStringJumpPerNote=%2)")
                                    .arg(m_c.maxFretShiftPerNote)
                                    .arg(m_c.maxStringJumpPerNote));
            return r;
        }
        r.ok = true;
        r.cost = best.cost;
        r.reasons.push_back(QString("OK: note=%1 string=%2 fret=%3 cost=%4")
                                .arg(note)
                                .arg(best.stringIndex)
                                .arg(best.fret)
                                .arg(best.cost, 0, 'f', 3));
        r.stateUpdates.insert("lastFret", best.fret);
        r.stateUpdates.insert("lastString", best.stringIndex);
        return r;
    }

    // DP over short phrase.
    struct Node { Fingering f; double bestCost = std::numeric_limits<double>::infinity(); int prevIdx = -1; };
    QVector<QVector<Node>> layers;
    layers.reserve(notes.size());

    for (int i = 0; i < notes.size(); ++i) {
        QVector<Node> layer;
        const auto opts = fingeringsForNote(notes[i]);
        for (const auto& f : opts) layer.push_back(Node{f, std::numeric_limits<double>::infinity(), -1});
        layers.push_back(layer);
    }

    if (layers.isEmpty() || layers.first().isEmpty()) {
        r.ok = false;
        r.reasons.push_back("FAIL: no feasible fingering options");
        return r;
    }

    // Initialize from state.
    for (int j = 0; j < layers[0].size(); ++j) {
        double cst = layers[0][j].f.cost;
        if (lastFret >= 0) cst += 0.10 * double(qAbs(layers[0][j].f.fret - lastFret));
        if (lastString >= 0) cst += 0.12 * double(qAbs(layers[0][j].f.stringIndex - lastString));
        if (lastFret >= 0 && qAbs(layers[0][j].f.fret - lastFret) > m_c.maxFretShiftPerNote) continue;
        if (lastString >= 0 && qAbs(layers[0][j].f.stringIndex - lastString) > m_c.maxStringJumpPerNote) continue;
        layers[0][j].bestCost = cst;
        layers[0][j].prevIdx = -1;
    }

    // Transitions.
    for (int i = 1; i < layers.size(); ++i) {
        for (int j = 0; j < layers[i].size(); ++j) {
            auto& cur = layers[i][j];
            for (int k = 0; k < layers[i - 1].size(); ++k) {
                const auto& prev = layers[i - 1][k];
                if (!std::isfinite(prev.bestCost)) continue;

                const int df = qAbs(cur.f.fret - prev.f.fret);
                const int ds = qAbs(cur.f.stringIndex - prev.f.stringIndex);

                if (df > m_c.maxFretShiftPerNote) continue;
                if (ds > m_c.maxStringJumpPerNote) continue;

                double trans = 0.10 * double(df) + 0.12 * double(ds);

                // Legato/slide preference when staying on same string.
                if (ds == 0) {
                    if (df <= m_c.maxLegatoFretDelta) trans -= 0.18; // reward legato
                    else if (df <= m_c.maxSlideFretDelta) trans += 0.25 + 0.05 * double(df); // slide cost
                    else continue; // too far to slide in one gesture
                }

                const double cand = prev.bestCost + cur.f.cost + trans;
                if (cand < cur.bestCost) {
                    cur.bestCost = cand;
                    cur.prevIdx = k;
                }
            }
        }
    }

    // Best end state.
    int bestJ = -1;
    double bestCost = std::numeric_limits<double>::infinity();
    const auto& lastLayer = layers.last();
    for (int j = 0; j < lastLayer.size(); ++j) {
        if (lastLayer[j].bestCost < bestCost) { bestCost = lastLayer[j].bestCost; bestJ = j; }
    }

    if (bestJ < 0 || !std::isfinite(bestCost)) {
        r.ok = false;
        r.reasons.push_back("FAIL: no feasible fingering path under shift/legato constraints");
        return r;
    }

    // Reconstruct and emit per-note OK lines (ending with the final fingering).
    QVector<Fingering> path;
    path.resize(notes.size());
    int j = bestJ;
    for (int i = notes.size() - 1; i >= 0; --i) {
        path[i] = layers[i][j].f;
        j = layers[i][j].prevIdx;
    }

    r.ok = true;
    r.cost = bestCost;
    for (int i = 0; i < notes.size(); ++i) {
        r.reasons.push_back(QString("OK: note=%1 string=%2 fret=%3 cost=%4")
                                .arg(notes[i])
                                .arg(path[i].stringIndex)
                                .arg(path[i].fret)
                                .arg(path[i].cost, 0, 'f', 3));
    }
    r.reasons.push_back(QString("OK: gesture notes=%1 totalCost=%2").arg(notes.size()).arg(bestCost, 0, 'f', 3));
    if (!path.isEmpty()) {
        r.stateUpdates.insert("lastFret", path.last().fret);
        r.stateUpdates.insert("lastString", path.last().stringIndex);
    }
    return r;
}

} // namespace virtuoso::constraints

