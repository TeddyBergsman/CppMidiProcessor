#include "virtuoso/theory/GrooveEngine.h"

#include <QRandomGenerator>
#include <QtGlobal>

#include <algorithm>

namespace virtuoso::theory {

QVector<GrooveTemplate> GrooveEngine::builtins() {
    QVector<GrooveTemplate> out;

    auto add = [&](QString key, QString name, int order, double swing, int pocketMs, int humanizeMs, QStringList tags) {
        GrooveTemplate g;
        g.key = std::move(key);
        g.name = std::move(name);
        g.order = order;
        g.swing = swing;
        g.pocketMs = pocketMs;
        g.humanizeMs = humanizeMs;
        g.tags = std::move(tags);
        out.push_back(std::move(g));
    };

    add("straight", "Straight", 0, 0.50, 0, 0, {"grid"});
    add("swing_66", "Swing (66%)", 10, 0.666, 0, 0, {"swing"});
    add("laidback_12", "Laid back (+12ms offbeats)", 20, 0.50, 12, 0, {"feel"});
    add("swing_66_laidback", "Swing (66%) + laid back", 30, 0.666, 10, 0, {"swing","feel"});
    add("humanize_light", "Humanize (±8ms)", 40, 0.50, 0, 8, {"humanize"});

    std::sort(out.begin(), out.end(), [](const GrooveTemplate& a, const GrooveTemplate& b) {
        if (a.order != b.order) return a.order < b.order;
        return a.name < b.name;
    });
    return out;
}

QVector<int> GrooveEngine::scheduleDueMs(int steps,
                                        int baseStepMs,
                                        int stepsPerBeat,
                                        const GrooveTemplate& tpl,
                                        quint32 seed) {
    QVector<int> due;
    if (steps <= 0) return due;
    baseStepMs = qMax(1, baseStepMs);
    stepsPerBeat = qMax(1, stepsPerBeat);

    due.resize(steps);
    QRandomGenerator rng(seed);

    const int beatMs = baseStepMs * stepsPerBeat;
    const double swingRatio = std::clamp(tpl.swing, 0.50, 0.90);
    const int swingDelay8thMs = (stepsPerBeat == 2)
        ? int(std::round((swingRatio - 0.50) * double(beatMs)))
        : 0;
    const int swingDelay16thMs = (stepsPerBeat == 4)
        ? int(std::round((swingRatio - 0.50) * double(beatMs / 2)))
        : 0;

    for (int i = 0; i < steps; ++i) {
        int t = i * baseStepMs;

        // 8th swing: delay the offbeat within each beat.
        if (stepsPerBeat == 2) {
            const int inBeat = i % 2;
            if (inBeat == 1) t += swingDelay8thMs;
        }
        // 16th swing: treat each beat as two 8th-pairs (0-1 and 2-3), and delay the 2nd note in each pair.
        if (stepsPerBeat == 4) {
            const int inBeat = i % 4;
            if (inBeat == 1 || inBeat == 3) t += swingDelay16thMs;
        }

        // Pocket: shift offbeats.
        if ((i % stepsPerBeat) != 0) t += tpl.pocketMs;

        // Humanize jitter.
        if (tpl.humanizeMs > 0) {
            const int r = int(rng.bounded(tpl.humanizeMs * 2 + 1)) - tpl.humanizeMs;
            t += r;
        }

        due[i] = t;
    }

    // Enforce monotonic schedule (avoid reordering due to negative pocket/humanize).
    for (int i = 1; i < due.size(); ++i) {
        due[i] = std::max(due[i], due[i - 1] + 1);
    }
    // Ensure start is non-negative.
    due[0] = std::max(0, due[0]);
    return due;
}

} // namespace virtuoso::theory

