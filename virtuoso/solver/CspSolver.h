#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace virtuoso::solver {

// Explicit "CSP-style" selection:
// - generate finite candidates
// - evaluate feasibility + cost
// - choose minimum-cost feasible candidate
//
// This is intentionally lightweight and deterministic; it is *not* a search tree (yet).
struct DecisionTrace {
    int chosenIndex = -1;
    QString chosenId;
    double chosenCost = 0.0;
    QStringList chosenReasons;
};

template <typename T>
struct Candidate {
    QString id;
    T value{};
};

struct EvalResult {
    bool ok = true;
    double cost = 0.0;      // lower is better
    QStringList reasons;    // explainable
};

class CspSolver final {
public:
    template <typename T, typename EvalFn>
    static int chooseMinCost(const QVector<Candidate<T>>& cands, EvalFn eval, DecisionTrace* trace = nullptr) {
        int bestIdx = -1;
        double bestCost = 0.0;
        QStringList bestReasons;

        for (int i = 0; i < cands.size(); ++i) {
            const auto& c = cands[i];
            const EvalResult r = eval(c);
            if (!r.ok) continue;
            if (bestIdx < 0 || r.cost < bestCost) {
                bestIdx = i;
                bestCost = r.cost;
                bestReasons = r.reasons;
            }
        }

        if (trace) {
            trace->chosenIndex = bestIdx;
            trace->chosenId = (bestIdx >= 0) ? cands[bestIdx].id : QString();
            trace->chosenCost = bestCost;
            trace->chosenReasons = bestReasons;
        }
        return bestIdx;
    }
};

} // namespace virtuoso::solver

