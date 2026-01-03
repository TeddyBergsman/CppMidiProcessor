#include "playback/BandPlaybackEngine.h"

#include "music/ChordSymbol.h"

#include <QtGlobal>
#include <QHash>
#include <functional>
#include <cmath>

namespace playback {
namespace {

static QVector<const chart::Bar*> flattenBarsFrom(const chart::ChartModel& model) {
    QVector<const chart::Bar*> bars;
    for (const auto& line : model.lines) {
        for (const auto& bar : line.bars) {
            bars.push_back(&bar);
        }
    }
    return bars;
}

static QVector<int> buildPlaybackSequenceFrom(const chart::ChartModel& model) {
    // Copied (intentionally) from SilentPlaybackEngine to keep the same repeat/ending behavior.
    const QVector<const chart::Bar*> bars = flattenBarsFrom(model);
    const int nBars = bars.size();
    QVector<int> seq;
    if (nBars <= 0) return seq;
    seq.reserve(nBars * 4);

    int fineBar = -1;
    int segnoBar = -1;
    for (int i = 0; i < nBars; ++i) {
        const QString ann = bars[i]->annotation.trimmed();
        if (fineBar < 0 && ann.compare("Fine", Qt::CaseInsensitive) == 0) fineBar = i;
        if (segnoBar < 0 && ann.contains("Segno", Qt::CaseInsensitive)) segnoBar = i;
    }

    const QString footer = model.footerText.trimmed();
    const bool wantsJump = footer.startsWith("D.C.", Qt::CaseInsensitive) || footer.startsWith("D.S.", Qt::CaseInsensitive);
    const bool jumpIsDS = footer.startsWith("D.S.", Qt::CaseInsensitive);
    const bool alFine = footer.contains("al Fine", Qt::CaseInsensitive);
    const int jumpTarget = jumpIsDS ? (segnoBar >= 0 ? segnoBar : 0) : 0;

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

    struct RepeatCtx { int start = 0; int end = -1; int pass = 1; int passes = 2; };
    QVector<RepeatCtx> stack;
    stack.reserve(4);

    bool jumped = false;
    int pc = 0;
    int guardSteps = 0;
    const int guardMax = 20000;

    auto currentPass = [&]() -> int { return stack.isEmpty() ? 1 : stack.last().pass; };

    while (pc < nBars && guardSteps++ < guardMax) {
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

        if (!stack.isEmpty()) {
            const int n = bars[pc]->endingStart;
            if (n > 0 && n != currentPass()) {
                const int end = endingStartToEnd.value(pc, pc);
                pc = end + 1;
                continue;
            }
        }

        for (int c = 0; c < 4; ++c) {
            seq.push_back(pc * 4 + c);
        }

        if (jumped && alFine && fineBar >= 0 && pc == fineBar) {
            break;
        }

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

BandPlaybackEngine::BandPlaybackEngine(QObject* parent)
    : QObject(parent) {
    m_tickTimer.setInterval(25);
    connect(&m_tickTimer, &QTimer::timeout, this, &BandPlaybackEngine::onTick);
    m_timingRng.seed(1u);
}

void BandPlaybackEngine::setTempoBpm(int bpm) {
    m_bpm = qBound(30, bpm, 300);
}

void BandPlaybackEngine::setRepeats(int repeats) {
    m_repeats = qMax(1, repeats);
}

void BandPlaybackEngine::setBassProfile(const music::BassProfile& p) {
    const bool wasEnabled = m_bassProfile.enabled;
    const int oldCh = m_bassProfile.midiChannel;
    m_bassProfile = p;
    m_bass.setProfile(m_bassProfile);
    // Stable timing randomness per-song.
    quint32 seed = (m_bassProfile.humanizeSeed == 0) ? 1u : m_bassProfile.humanizeSeed;
    m_timingRng.seed(seed ^ 0x9E3779B9u);

    // If bass was disabled or channel changed during playback, hard-stop pending events + silence.
    if (m_playing && (wasEnabled && !m_bassProfile.enabled)) {
        for (QTimer* t : m_pendingTimers) {
            if (!t) continue;
            t->stop();
            t->deleteLater();
        }
        m_pendingTimers.clear();
        if (m_lastBassMidi >= 0) emit bassNoteOff(oldCh, m_lastBassMidi);
        emit bassAllNotesOff(oldCh);
        m_lastBassMidi = -1;
    } else if (m_playing && wasEnabled && m_bassProfile.enabled && oldCh != m_bassProfile.midiChannel) {
        // Channel change: stop old channel to avoid stuck notes.
        for (QTimer* t : m_pendingTimers) {
            if (!t) continue;
            t->stop();
            t->deleteLater();
        }
        m_pendingTimers.clear();
        if (m_lastBassMidi >= 0) emit bassNoteOff(oldCh, m_lastBassMidi);
        emit bassAllNotesOff(oldCh);
        m_lastBassMidi = -1;
    }
}

void BandPlaybackEngine::setChartModel(const chart::ChartModel& model) {
    m_model = model;
    m_sequence = buildPlaybackSequenceFrom(m_model);
    m_lastChord = music::ChordSymbol{};
    m_hasLastChord = false;
    m_lastBassMidi = -1;
    m_lastStep = -1;
    m_lastEmittedCell = -1;
    m_lastBarIndex = -1;
    m_noteOnsInBar = 0;
    // Clear any scheduled events.
    for (QTimer* t : m_pendingTimers) {
        if (!t) continue;
        t->stop();
        t->deleteLater();
    }
    m_pendingTimers.clear();
    m_bass.reset();
}

QVector<const chart::Bar*> BandPlaybackEngine::flattenBars() const {
    return flattenBarsFrom(m_model);
}

QVector<int> BandPlaybackEngine::buildPlaybackSequence() const {
    return buildPlaybackSequenceFrom(m_model);
}

const chart::Cell* BandPlaybackEngine::cellForFlattenedIndex(int cellIndex) const {
    if (cellIndex < 0) return nullptr;
    const int barIndex = cellIndex / 4;
    const int cellInBar = cellIndex % 4;
    const auto bars = flattenBars();
    if (barIndex < 0 || barIndex >= bars.size()) return nullptr;
    const chart::Bar* b = bars[barIndex];
    if (!b) return nullptr;
    if (cellInBar < 0 || cellInBar >= b->cells.size()) return nullptr;
    return &b->cells[cellInBar];
}

bool BandPlaybackEngine::chordForCellIndex(int cellIndex, music::ChordSymbol& outChord, bool& isNewChord) {
    isNewChord = false;
    const chart::Cell* cell = cellForFlattenedIndex(cellIndex);
    if (!cell) return false;
    const QString txt = cell->chord.trimmed();
    if (txt.isEmpty()) {
        if (m_hasLastChord) { outChord = m_lastChord; return true; }
        return false;
    }

    music::ChordSymbol parsed;
    if (!music::parseChordSymbol(txt, parsed)) {
        if (m_hasLastChord) { outChord = m_lastChord; return true; }
        return false;
    }

    // Placeholder repeats the previous chord.
    if (parsed.placeholder) {
        if (m_hasLastChord) { outChord = m_lastChord; return true; }
        return false;
    }
    if (parsed.noChord) {
        // Treat N.C. as silence: no chord to walk against.
        outChord = parsed;
        isNewChord = true;
        m_lastChord = parsed;
        m_hasLastChord = true;
        return true;
    }

    outChord = parsed;
    isNewChord = true;
    m_lastChord = parsed;
    m_hasLastChord = true;
    return true;
}

bool BandPlaybackEngine::chordForNextCellIndex(int cellIndex, music::ChordSymbol& outChord) {
    // Next harmonic target: find the next non-empty chord token at or after the next cell.
    const int seqLen = !m_sequence.isEmpty() ? m_sequence.size() : 0;
    if (seqLen <= 0) return false;

    // Find current position in sequence (best-effort; linear scan is fine at this scale).
    int pos = -1;
    for (int i = 0; i < seqLen; ++i) {
        if (m_sequence[i] == cellIndex) { pos = i; break; }
    }
    if (pos < 0) return false;

    for (int k = 1; k <= 16; ++k) {
        const int nextIdx = m_sequence[(pos + k) % seqLen];
        const chart::Cell* cell = cellForFlattenedIndex(nextIdx);
        if (!cell) continue;
        const QString txt = cell->chord.trimmed();
        if (txt.isEmpty()) continue;
        music::ChordSymbol parsed;
        if (!music::parseChordSymbol(txt, parsed)) continue;
        if (parsed.placeholder) continue;
        outChord = parsed;
        return true;
    }
    return false;
}

void BandPlaybackEngine::play() {
    const int seqLen = !m_sequence.isEmpty() ? m_sequence.size() : 0;
    if (seqLen <= 0) return;
    m_playing = true;
    m_clock.restart();
    m_lastStep = -1;
    m_lastEmittedCell = -1;
    m_lastBarIndex = -1;
    m_noteOnsInBar = 0;
    m_tickTimer.start();
    // Let onTick emit the first cell (and first note) exactly once.
}

void BandPlaybackEngine::stop() {
    if (!m_playing) return;
    m_playing = false;
    m_tickTimer.stop();

    // Cancel pending scheduled note events.
    for (QTimer* t : m_pendingTimers) {
        if (!t) continue;
        t->stop();
        t->deleteLater();
    }
    m_pendingTimers.clear();

    // Release any held bass note and send all-notes-off.
    if (m_bassProfile.enabled) {
        if (m_lastBassMidi >= 0) emit bassNoteOff(m_bassProfile.midiChannel, m_lastBassMidi);
        emit bassAllNotesOff(m_bassProfile.midiChannel);
    }
    m_lastBassMidi = -1;
    m_hasLastChord = false;
    m_lastStep = -1;
    m_lastEmittedCell = -1;
    m_lastBarIndex = -1;
    m_noteOnsInBar = 0;
    m_bass.reset();

    emit currentCellChanged(-1);
}

void BandPlaybackEngine::onTick() {
    const int seqLen = !m_sequence.isEmpty() ? m_sequence.size() : 0;
    if (!m_playing || seqLen <= 0) return;

    const double beatMs = 60000.0 / double(m_bpm);
    const qint64 elapsedMs = m_clock.elapsed();
    const int step = int(elapsedMs / beatMs);

    const int total = seqLen * qMax(1, m_repeats);
    if (step >= total) {
        stop();
        return;
    }

    // The timer ticks every 25ms; guard against re-triggering the same beat step repeatedly.
    if (step == m_lastStep) {
        return;
    }
    m_lastStep = step;

    const int cellIndex = m_sequence[step % seqLen];
    if (cellIndex != m_lastEmittedCell) {
        m_lastEmittedCell = cellIndex;
        emit currentCellChanged(cellIndex);
    }

    // Bass generation: one note per beat, using the chord implied by this cell.
    if (!m_bassProfile.enabled) return;

    // Simple sanity: ensure we don't emit an impossible number of bass notes per bar.
    const int barIndex = cellIndex / 4;
    if (barIndex != m_lastBarIndex) {
        m_lastBarIndex = barIndex;
        m_noteOnsInBar = 0;
    }

    const int beatInBar = cellIndex % 4;

    music::ChordSymbol cur;
    bool isNewChord = false;
    const bool haveCur = chordForCellIndex(cellIndex, cur, isNewChord);
    if (!haveCur || cur.noChord) {
        // Silence bass if no harmony.
        if (m_lastBassMidi >= 0) {
            emit bassNoteOff(m_bassProfile.midiChannel, m_lastBassMidi);
            m_lastBassMidi = -1;
        }
        return;
    }

    music::ChordSymbol next;
    bool haveNext = chordForNextCellIndex(cellIndex, next);
    const music::ChordSymbol* nextPtr = haveNext ? &next : &cur;

    const auto decision = m_bass.nextNote(beatInBar, &cur, nextPtr);
    if (decision.midiNote < 0 || decision.velocity <= 0) return;

    // Human timing: schedule note events within the beat.
    const double beatStartMs = double(step) * beatMs;
    const int jitter = (m_bassProfile.microJitterMs > 0)
        ? (int(m_timingRng.bounded(m_bassProfile.microJitterMs * 2 + 1)) - m_bassProfile.microJitterMs)
        : 0;
    const int push = m_bassProfile.pushMs;
    const int laidBack = m_bassProfile.laidBackMs;

    // Minimal swing feel on off-beats (subtle; true swing comes later with subdivisions).
    const int offbeat = (beatInBar % 2 == 1) ? 1 : 0;
    const int swingMs = offbeat ? int(std::round(6.0 * m_bassProfile.swingAmount)) : 0;

    const int timingOffsetMs = laidBack - push + jitter + swingMs;
    int delayOn = int(std::round(beatStartMs + double(timingOffsetMs) - double(elapsedMs)));
    if (delayOn < 0) delayOn = 0;

    int noteLen = m_bassProfile.noteLengthMs;
    if (noteLen <= 0) {
        noteLen = int(std::round(beatMs * m_bassProfile.gatePct));
    }
    noteLen = std::max(30, std::min(2000, noteLen));
    const int delayOff = delayOn + noteLen;

    auto schedule = [&](int delay, std::function<void()> fn) {
        if (delay < 0) delay = 0;
        QTimer* t = new QTimer(this);
        t->setSingleShot(true);
        m_pendingTimers.push_back(t);
        connect(t, &QTimer::timeout, this, [this, t, fn]() {
            fn();
            m_pendingTimers.removeOne(t);
            t->deleteLater();
        });
        t->start(delay);
    };

    // Ensure monophonic behavior: schedule previous note off at (or before) this note-on.
    if (m_lastBassMidi >= 0) {
        const int prev = m_lastBassMidi;
        schedule(std::max(0, delayOn - 1), [this, prev]() {
            emit bassNoteOff(m_bassProfile.midiChannel, prev);
        });
    }

    const int note = decision.midiNote;
    const int vel = decision.velocity;
    schedule(delayOn, [this, note, vel]() {
        emit bassNoteOn(m_bassProfile.midiChannel, note, vel);
    });
    schedule(delayOff, [this, note]() {
        emit bassNoteOff(m_bassProfile.midiChannel, note);
    });

    m_lastBassMidi = note;
    m_noteOnsInBar += 1;
    if (m_noteOnsInBar > 6) {
        qWarning("Bass sanity: too many notes in bar %d (count=%d). Silencing bass.",
                 m_lastBarIndex, m_noteOnsInBar);
        // Fail safe: cancel pending events and send all-notes-off for bass channel.
        for (QTimer* t : m_pendingTimers) {
            if (!t) continue;
            t->stop();
            t->deleteLater();
        }
        m_pendingTimers.clear();
        emit bassAllNotesOff(m_bassProfile.midiChannel);
        m_lastBassMidi = -1;
    }
}

} // namespace playback

