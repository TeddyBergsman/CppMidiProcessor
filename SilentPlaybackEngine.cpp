#include "SilentPlaybackEngine.h"

#include <QtGlobal>
#include <QHash>

namespace playback {

namespace {
static QVector<const chart::Bar*> flattenBars(const chart::ChartModel& model) {
    QVector<const chart::Bar*> bars;
    for (const auto& line : model.lines) {
        for (const auto& bar : line.bars) {
            bars.push_back(&bar);
        }
    }
    return bars;
}

static QVector<int> buildPlaybackSequence(const chart::ChartModel& model) {
    const QVector<const chart::Bar*> bars = flattenBars(model);
    const int nBars = bars.size();
    QVector<int> seq;
    if (nBars <= 0) return seq;
    seq.reserve(nBars * 4);

    // Locate Fine (if any) and Segno (best-effort; parser doesn't special-case it today).
    int fineBar = -1;
    int segnoBar = -1;
    for (int i = 0; i < nBars; ++i) {
        const QString ann = bars[i]->annotation.trimmed();
        if (fineBar < 0 && ann.compare("Fine", Qt::CaseInsensitive) == 0) fineBar = i;
        if (segnoBar < 0 && ann.contains("Segno", Qt::CaseInsensitive)) segnoBar = i;
    }

    // Parse footer jump instruction (best-effort).
    const QString footer = model.footerText.trimmed();
    const bool wantsJump = footer.startsWith("D.C.", Qt::CaseInsensitive) || footer.startsWith("D.S.", Qt::CaseInsensitive);
    const bool jumpIsDS = footer.startsWith("D.S.", Qt::CaseInsensitive);
    const bool alFine = footer.contains("al Fine", Qt::CaseInsensitive);
    const int jumpTarget = jumpIsDS ? (segnoBar >= 0 ? segnoBar : 0) : 0;

    // Precompute repeat regions using { ... }.
    QVector<int> repeatStartStack;
    QHash<int, int> startToEnd;
    QHash<int, int> endToStart;
    repeatStartStack.reserve(8);
    for (int i = 0; i < nBars; ++i) {
        const QString l = bars[i]->barlineLeft;
        const QString r = bars[i]->barlineRight;
        if (l.contains('{')) {
            repeatStartStack.push_back(i);
        }
        if (r.contains('}')) {
            int start = 0;
            if (!repeatStartStack.isEmpty()) {
                start = repeatStartStack.takeLast();
            }
            startToEnd.insert(start, i);
            endToStart.insert(i, start);
        }
    }

    // Precompute ending segments: startIdx -> endIdx for each Nn.
    QHash<int, int> endingStartToEnd;
    for (int i = 0; i < nBars; ++i) {
        const int n = bars[i]->endingStart;
        if (n <= 0) continue;
        int end = i;
        for (int j = i; j < nBars; ++j) {
            if (bars[j]->endingEnd == n) { end = j; break; }
        }
        endingStartToEnd.insert(i, end);
    }

    // Compute how many passes each repeat should take: max ending number inside region (>=2), else 2.
    QHash<int, int> repeatEndToPasses;
    for (auto it = startToEnd.constBegin(); it != startToEnd.constEnd(); ++it) {
        const int start = it.key();
        const int end = it.value();
        int maxEnding = 0;
        for (int i = start; i <= end && i < nBars; ++i) {
            maxEnding = std::max(maxEnding, bars[i]->endingStart);
            maxEnding = std::max(maxEnding, bars[i]->endingEnd);
        }
        repeatEndToPasses.insert(end, std::max(2, maxEnding));
    }

    // Playback simulation over bars, expanding repeats/endings and optional D.C./D.S. al Fine.
    struct RepeatCtx { int start = 0; int end = -1; int pass = 1; int passes = 2; };
    QVector<RepeatCtx> stack;
    stack.reserve(4);

    bool jumped = false;
    int pc = 0;
    int guardSteps = 0;
    const int guardMax = 20000; // safety against malformed charts

    auto currentPass = [&]() -> int { return stack.isEmpty() ? 1 : stack.last().pass; };

    while (pc < nBars && guardSteps++ < guardMax) {
        // If we're in a repeat, push context when entering its start.
        if (startToEnd.contains(pc)) {
            const int end = startToEnd.value(pc);
            bool already = false;
            if (!stack.isEmpty() && stack.last().start == pc && stack.last().end == end) already = true;
            if (!already) {
                RepeatCtx ctx;
                ctx.start = pc;
                ctx.end = end;
                ctx.pass = 1;
                ctx.passes = repeatEndToPasses.value(end, 2);
                stack.push_back(ctx);
            }
        }

        // Ending skip logic: if this bar begins an ending segment not meant for this pass, skip to its end+1.
        if (!stack.isEmpty()) {
            const int n = bars[pc]->endingStart;
            if (n > 0 && n != currentPass()) {
                const int end = endingStartToEnd.value(pc, pc);
                pc = end + 1;
                continue;
            }
        }

        // Emit 4 cells for this bar.
        for (int c = 0; c < 4; ++c) {
            seq.push_back(pc * 4 + c);
        }

        // If we've jumped and are doing "al Fine", stop when we hit the Fine bar.
        if (jumped && alFine && fineBar >= 0 && pc == fineBar) {
            break;
        }

        // At repeat end: loop or exit repeat.
        if (!stack.isEmpty() && pc == stack.last().end) {
            if (stack.last().pass < stack.last().passes) {
                stack.last().pass += 1;
                pc = stack.last().start;
                continue;
            }
            stack.removeLast();
            pc += 1;
            continue;
        }

        pc += 1;
        if (pc >= nBars) {
            // End-of-chart: apply D.C./D.S. (best-effort).
            if (wantsJump && !jumped) {
                jumped = true;
                pc = jumpTarget;
                continue;
            }
            break;
        }
    }

    return seq;
}
} // namespace

SilentPlaybackEngine::SilentPlaybackEngine(QObject* parent)
    : QObject(parent) {
    m_tickTimer.setInterval(25);
    connect(&m_tickTimer, &QTimer::timeout, this, &SilentPlaybackEngine::onTick);
}

void SilentPlaybackEngine::setTempoBpm(int bpm) {
    m_bpm = qBound(30, bpm, 300);
}

void SilentPlaybackEngine::setTotalCells(int totalCells) {
    m_totalCells = qMax(0, totalCells);
}

void SilentPlaybackEngine::setRepeats(int repeats) {
    m_repeats = qMax(1, repeats);
}

void SilentPlaybackEngine::setChartModel(const chart::ChartModel& model) {
    m_sequence = buildPlaybackSequence(model);
    // Keep legacy fallback in sync.
    m_totalCells = flattenBars(model).size() * 4;
}

void SilentPlaybackEngine::play() {
    const int seqLen = !m_sequence.isEmpty() ? m_sequence.size() : m_totalCells;
    if (seqLen <= 0) return;
    m_playing = true;
    m_clock.restart();
    m_tickTimer.start();
    emit currentCellChanged(!m_sequence.isEmpty() ? m_sequence.first() : 0);
}

void SilentPlaybackEngine::stop() {
    if (!m_playing) return;
    m_playing = false;
    m_tickTimer.stop();
    emit currentCellChanged(-1);
}

void SilentPlaybackEngine::onTick() {
    const int seqLen = !m_sequence.isEmpty() ? m_sequence.size() : m_totalCells;
    if (!m_playing || seqLen <= 0) return;

    // One cell per beat (quarter note) in v1.
    const double beatMs = 60000.0 / double(m_bpm);
    const qint64 elapsedMs = m_clock.elapsed();
    const int step = int(elapsedMs / beatMs);

    const int total = seqLen * qMax(1, m_repeats);
    if (step >= total) {
        stop();
        return;
    }
    if (!m_sequence.isEmpty()) {
        emit currentCellChanged(m_sequence[step % seqLen]);
    } else {
        emit currentCellChanged(step % seqLen);
    }
}

} // namespace playback

