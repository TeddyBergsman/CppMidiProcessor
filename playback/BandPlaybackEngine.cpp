#include "playback/BandPlaybackEngine.h"

#include "music/ChordSymbol.h"

#include <QtGlobal>
#include <QHash>
#include <cmath>
#include <algorithm>

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

static QVector<QString> buildBarSectionsFrom(const chart::ChartModel& model) {
    QVector<QString> sections;
    sections.reserve(256);
    QString current;
    for (const auto& line : model.lines) {
        if (!line.sectionLabel.trimmed().isEmpty()) {
            current = line.sectionLabel.trimmed();
        }
        for (int i = 0; i < line.bars.size(); ++i) {
            sections.push_back(current);
        }
    }
    return sections;
}

} // namespace

BandPlaybackEngine::BandPlaybackEngine(QObject* parent)
    : QObject(parent) {
    // Higher tick rate + lookahead scheduling yields much tighter timing than
    // "generate exactly on the beat" (which is quantized by the tick interval).
    m_tickTimer.setInterval(10);
    connect(&m_tickTimer, &QTimer::timeout, this, &BandPlaybackEngine::onTick);

    m_dispatchTimer.setSingleShot(true);
    connect(&m_dispatchTimer, &QTimer::timeout, this, &BandPlaybackEngine::onDispatch);

    m_timingRng.seed(1u);
    m_driftMs = 0;
    m_pianoTimingRng.seed(2u);
    m_pianoDriftMs = 0;

    m_bassProfile = music::defaultBassProfile();
    m_bass.setProfile(m_bassProfile);
    m_pianoProfile = music::defaultPianoProfile();
    m_piano.setProfile(m_pianoProfile);
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
        m_eventHeap.clear();
        m_dispatchTimer.stop();
        if (m_lastBassMidi >= 0) emit bassNoteOff(oldCh, m_lastBassMidi);
        emit bassAllNotesOff(oldCh);
        m_lastBassMidi = -1;
    } else if (m_playing && wasEnabled && m_bassProfile.enabled && oldCh != m_bassProfile.midiChannel) {
        // Channel change: stop old channel to avoid stuck notes.
        m_eventHeap.clear();
        m_dispatchTimer.stop();
        if (m_lastBassMidi >= 0) emit bassNoteOff(oldCh, m_lastBassMidi);
        emit bassAllNotesOff(oldCh);
        m_lastBassMidi = -1;
    }
}

void BandPlaybackEngine::setPianoProfile(const music::PianoProfile& p) {
    const bool wasEnabled = m_pianoProfile.enabled;
    const int oldCh = m_pianoProfile.midiChannel;
    m_pianoProfile = p;
    m_piano.setProfile(m_pianoProfile);

    // Stable timing randomness per-song (separate from bass).
    quint32 seed = (m_pianoProfile.humanizeSeed == 0) ? 1u : m_pianoProfile.humanizeSeed;
    m_pianoTimingRng.seed(seed ^ 0x7F4A7C15u);

    // If piano was disabled or channel changed during playback, hard-stop pending events + silence.
    if (m_playing && (wasEnabled && !m_pianoProfile.enabled)) {
        m_eventHeap.clear();
        m_dispatchTimer.stop();
        emit pianoCC(oldCh, 64, 0);
        emit pianoAllNotesOff(oldCh);
        m_scheduledPianoNoteOnsInBar.clear();
    } else if (m_playing && wasEnabled && m_pianoProfile.enabled && oldCh != m_pianoProfile.midiChannel) {
        m_eventHeap.clear();
        m_dispatchTimer.stop();
        emit pianoCC(oldCh, 64, 0);
        emit pianoAllNotesOff(oldCh);
        m_scheduledPianoNoteOnsInBar.clear();
    }
}

void BandPlaybackEngine::setChartModel(const chart::ChartModel& model) {
    m_model = model;
    m_sequence = buildPlaybackSequenceFrom(m_model);
    m_barSections = buildBarSectionsFrom(m_model);
    m_lastChord = music::ChordSymbol{};
    m_hasLastChord = false;
    m_lastBassMidi = -1;
    m_lastStep = -1;
    m_lastEmittedCell = -1;
    m_lastBarIndex = -1;
    m_scheduledNoteOnsInBar.clear();
    m_scheduledPianoNoteOnsInBar.clear();
    m_driftMs = 0;
    m_pianoDriftMs = 0;
    // Clear any scheduled events.
    m_eventHeap.clear();
    m_dispatchTimer.stop();
    m_bass.reset();
    m_piano.reset();
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
    // Only treat as a "new chord" if it actually changes harmony (some charts repeat chord tokens each beat).
    auto sameHarmony = [](const music::ChordSymbol& a, const music::ChordSymbol& b) -> bool {
        if (a.noChord || b.noChord) return false;
        if (a.placeholder || b.placeholder) return false;
        if (a.rootPc != b.rootPc) return false;
        if (a.bassPc != b.bassPc) return false;
        if (a.quality != b.quality) return false;
        if (a.seventh != b.seventh) return false;
        if (a.extension != b.extension) return false;
        if (a.alt != b.alt) return false;
        if (a.alterations.size() != b.alterations.size()) return false;
        for (int i = 0; i < a.alterations.size(); ++i) {
            const auto& x = a.alterations[i];
            const auto& y = b.alterations[i];
            if (x.degree != y.degree || x.delta != y.delta || x.add != y.add) return false;
        }
        return true;
    };
    isNewChord = !(m_hasLastChord && sameHarmony(parsed, m_lastChord));
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
    m_scheduledNoteOnsInBar.clear();
    m_scheduledPianoNoteOnsInBar.clear();
    m_driftMs = 0;
    m_pianoDriftMs = 0;
    m_lastPlayheadStep = -1;
    m_nextScheduledStep = 0;
    m_eventHeap.clear();
    m_dispatchTimer.stop();
    m_tickTimer.start();
    // Let onTick emit the first cell (and first note) exactly once.

    // Defensive: clear sustain at playback start to avoid "stuck pedal" from previous sessions/synth state.
    if (m_pianoProfile.enabled) emit pianoCC(m_pianoProfile.midiChannel, 64, 0);
}

void BandPlaybackEngine::stop() {
    if (!m_playing) return;
    m_playing = false;
    m_tickTimer.stop();

    // Cancel pending scheduled note events.
    m_eventHeap.clear();
    m_dispatchTimer.stop();

    // Release any held bass note and send all-notes-off.
    if (m_bassProfile.enabled) {
        if (m_lastBassMidi >= 0) emit bassNoteOff(m_bassProfile.midiChannel, m_lastBassMidi);
        emit bassAllNotesOff(m_bassProfile.midiChannel);
    }
    if (m_pianoProfile.enabled) {
        emit pianoCC(m_pianoProfile.midiChannel, 64, 0);
        emit pianoAllNotesOff(m_pianoProfile.midiChannel);
    }
    m_lastBassMidi = -1;
    m_hasLastChord = false;
    m_lastStep = -1;
    m_lastEmittedCell = -1;
    m_lastBarIndex = -1;
    m_scheduledNoteOnsInBar.clear();
    m_scheduledPianoNoteOnsInBar.clear();
    m_driftMs = 0;
    m_pianoDriftMs = 0;
    m_lastPlayheadStep = -1;
    m_nextScheduledStep = 0;
    m_bass.reset();
    m_piano.reset();

    emit currentCellChanged(-1);
}

void BandPlaybackEngine::onDispatch() {
    if (!m_playing) return;
    const qint64 now = m_clock.elapsed();

    auto heapLess = [](const PendingEvent& a, const PendingEvent& b) {
        return a.dueMs > b.dueMs; // reversed for min-heap behavior with std::push_heap
    };

    // Execute all due events.
    while (!m_eventHeap.isEmpty()) {
        const PendingEvent& top = m_eventHeap.front();
        if (top.dueMs > now) break;

        PendingEvent ev = top;
        std::pop_heap(m_eventHeap.begin(), m_eventHeap.end(), heapLess);
        m_eventHeap.pop_back();

        switch (ev.kind) {
        case PendingKind::NoteOn:
            if (ev.instrument == Instrument::Bass) {
                if (ev.emitLog && !ev.logLine.isEmpty()) emit bassLogLine(ev.logLine);
                emit bassNoteOn(ev.channel, ev.note, ev.velocity);
            } else {
                if (ev.emitLog && !ev.logLine.isEmpty()) emit pianoLogLine(ev.logLine);
                emit pianoNoteOn(ev.channel, ev.note, ev.velocity);
            }
            break;
        case PendingKind::NoteOff:
            if (ev.instrument == Instrument::Bass) emit bassNoteOff(ev.channel, ev.note);
            else emit pianoNoteOff(ev.channel, ev.note);
            break;
        case PendingKind::AllNotesOff:
            if (ev.instrument == Instrument::Bass) emit bassAllNotesOff(ev.channel);
            else emit pianoAllNotesOff(ev.channel);
            break;
        case PendingKind::CC:
            if (ev.instrument == Instrument::Piano) {
                if (ev.emitLog && !ev.logLine.isEmpty()) emit pianoLogLine(ev.logLine);
                emit pianoCC(ev.channel, ev.cc, ev.ccValue);
            }
            break;
        }
    }

    // Arm next wakeup.
    if (!m_eventHeap.isEmpty()) {
        const qint64 nextDue = m_eventHeap.front().dueMs;
        const int delay = int(std::max<qint64>(0, nextDue - now));
        m_dispatchTimer.start(delay);
    }
}

void BandPlaybackEngine::onTick() {
    const int seqLen = !m_sequence.isEmpty() ? m_sequence.size() : 0;
    if (!m_playing || seqLen <= 0) return;

    const double beatMs = 60000.0 / double(m_bpm);
    const qint64 elapsedMs = m_clock.elapsed();
    const int stepNow = int(elapsedMs / beatMs);

    const int total = seqLen * qMax(1, m_repeats);
    if (stepNow >= total) {
        stop();
        return;
    }

    // Update playhead once per beat-step.
    if (stepNow != m_lastPlayheadStep) {
        m_lastPlayheadStep = stepNow;
        const int cellIndex = m_sequence[stepNow % seqLen];
        if (cellIndex != m_lastEmittedCell) {
            m_lastEmittedCell = cellIndex;
            emit currentCellChanged(cellIndex);
        }
    }

    // If bass disabled, we still update the playhead but skip scheduling.
    const bool bassOn = m_bassProfile.enabled;
    const bool pianoOn = m_pianoProfile.enabled;
    if (!bassOn && !pianoOn) return;

    // Lookahead scheduling window to avoid "late notes" caused by tick quantization.
    constexpr int kLookaheadMs = 180;
    const int scheduleUntil = int((elapsedMs + kLookaheadMs) / beatMs);
    const int maxStepToSchedule = std::min(total - 1, scheduleUntil);

    auto heapLess = [](const PendingEvent& a, const PendingEvent& b) {
        return a.dueMs > b.dueMs; // reversed for min-heap
    };

    auto scheduleEvent = [&](qint64 dueAbsMs,
                             Instrument instrument,
                             PendingKind kind,
                             int channel,
                             int note,
                             int velocity,
                             int cc,
                             int ccValue,
                             bool emitLog,
                             const QString& logLine) {
        PendingEvent ev;
        ev.dueMs = dueAbsMs;
        ev.instrument = instrument;
        ev.kind = kind;
        ev.channel = channel;
        ev.note = note;
        ev.velocity = velocity;
        ev.cc = cc;
        ev.ccValue = ccValue;
        ev.emitLog = emitLog;
        ev.logLine = logLine;

        const bool wasEmpty = m_eventHeap.isEmpty();
        m_eventHeap.push_back(std::move(ev));
        std::push_heap(m_eventHeap.begin(), m_eventHeap.end(), heapLess);

        // If this is the earliest event, re-arm dispatcher.
        const qint64 now = m_clock.elapsed();
        if (wasEmpty || dueAbsMs <= m_eventHeap.front().dueMs) {
            const int delay = int(std::max<qint64>(0, m_eventHeap.front().dueMs - now));
            if (!m_dispatchTimer.isActive() || delay < m_dispatchTimer.remainingTime()) {
                m_dispatchTimer.start(delay);
            }
        }
    };

    auto midiName = [](int midi) -> QString {
        static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        const int pc = (midi % 12 + 12) % 12;
        const int oct = midi / 12 - 1;
        return QString("%1%2").arg(names[pc]).arg(oct);
    };

    // Schedule beats in order up to the lookahead horizon.
    while (m_nextScheduledStep <= maxStepToSchedule) {
        const int step = m_nextScheduledStep;
        m_nextScheduledStep++;

        const int cellIndex = m_sequence[step % seqLen];
        const int barIndex = cellIndex / 4;
        const int beatInBar = cellIndex % 4;
        const double beatStartMs = double(step) * beatMs;

        // Update slow timing drift once per bar (random-walk) at the moment we schedule beat 1 of the bar.
        if (beatInBar == 0 && barIndex != m_lastBarIndex) {
            m_lastBarIndex = barIndex;
            if (bassOn && m_bassProfile.driftMaxMs > 0 && m_bassProfile.driftRate > 0.0) {
                const int stepMax = std::max(1, int(std::round(double(m_bassProfile.driftMaxMs) * m_bassProfile.driftRate)));
                const int delta = int(m_timingRng.bounded(stepMax * 2 + 1)) - stepMax;
                m_driftMs += delta;
                if (m_driftMs > m_bassProfile.driftMaxMs) m_driftMs = m_bassProfile.driftMaxMs;
                if (m_driftMs < -m_bassProfile.driftMaxMs) m_driftMs = -m_bassProfile.driftMaxMs;
            } else {
                m_driftMs = 0;
            }
            if (pianoOn && m_pianoProfile.driftMaxMs > 0 && m_pianoProfile.driftRate > 0.0) {
                const int stepMax = std::max(1, int(std::round(double(m_pianoProfile.driftMaxMs) * m_pianoProfile.driftRate)));
                const int delta = int(m_pianoTimingRng.bounded(stepMax * 2 + 1)) - stepMax;
                m_pianoDriftMs += delta;
                if (m_pianoDriftMs > m_pianoProfile.driftMaxMs) m_pianoDriftMs = m_pianoProfile.driftMaxMs;
                if (m_pianoDriftMs < -m_pianoProfile.driftMaxMs) m_pianoDriftMs = -m_pianoProfile.driftMaxMs;
            } else {
                m_pianoDriftMs = 0;
            }
        }

        music::ChordSymbol cur;
        bool isNewChord = false;
        const bool haveCur = chordForCellIndex(cellIndex, cur, isNewChord);
        if (!haveCur || cur.noChord) {
            // Silence bass on N.C. (or missing harmony) at the moment it occurs.
            // Use an on-beat timing (no jitter) so this feels intentional and tight.
            const int delay = std::max(0, int(std::llround(beatStartMs - double(elapsedMs))));
            const qint64 due = elapsedMs + delay;
            if (bassOn) {
                if (m_lastBassMidi >= 0) {
                    const int prev = m_lastBassMidi;
                    scheduleEvent(due, Instrument::Bass, PendingKind::NoteOff, m_bassProfile.midiChannel, prev, 0, 0, 0, false, {});
                    m_lastBassMidi = -1;
                }
                scheduleEvent(due, Instrument::Bass, PendingKind::AllNotesOff, m_bassProfile.midiChannel, 0, 0, 0, 0, false, {});
            }
            if (pianoOn) {
                scheduleEvent(due, Instrument::Piano, PendingKind::CC, m_pianoProfile.midiChannel, 0, 0, 64, 0, false, {});
                scheduleEvent(due, Instrument::Piano, PendingKind::AllNotesOff, m_pianoProfile.midiChannel, 0, 0, 0, 0, false, {});
            }
            continue;
        }

        music::ChordSymbol next;
        const bool haveNext = chordForNextCellIndex(cellIndex, next);
        const music::ChordSymbol* nextPtr = haveNext ? &next : &cur;

        // Build a small beat-aligned harmonic lookahead without mutating the engine's chord-tracking state.
        // This enables multi-beat phrase planning in the bass generator.
        auto parseCellChordNoState = [&](int anyCellIndex, const music::ChordSymbol& fallback, bool* outIsExplicit = nullptr) -> music::ChordSymbol {
            if (outIsExplicit) *outIsExplicit = false;
            const chart::Cell* c = cellForFlattenedIndex(anyCellIndex);
            if (!c) return fallback;
            const QString t = c->chord.trimmed();
            if (t.isEmpty()) return fallback;
            music::ChordSymbol parsed;
            if (!music::parseChordSymbol(t, parsed)) return fallback;
            if (parsed.placeholder) return fallback;
            if (outIsExplicit) *outIsExplicit = true;
            return parsed;
        };

        // Shared beat context signals.
        const bool isNewBar = (beatInBar == 0);
        const QString sec = (barIndex >= 0 && barIndex < m_barSections.size()) ? m_barSections[barIndex] : QString();
        const QString prevSec = (barIndex - 1 >= 0 && barIndex - 1 < m_barSections.size()) ? m_barSections[barIndex - 1] : QString();
        const bool isSectionChange = isNewBar && ((sec != prevSec) && (!sec.isEmpty() || !prevSec.isEmpty()));
        int barInSection = 0;
        if (isNewBar) {
            int count = 0;
            for (int b = barIndex; b >= 0; --b) {
                const QString s = (b >= 0 && b < m_barSections.size()) ? m_barSections[b] : QString();
                if (s != sec) break;
                count++;
            }
            barInSection = std::max(0, count - 1);
        }
        const quint32 sectionHash = (quint32)qHash(sec);
        const int songPass = (seqLen > 0) ? (step / seqLen) : 0;
        const int totalPasses = std::max(1, m_repeats);

        // Lookahead chords: current beat + next 7 beats (2 bars).
        // Use local "last-known" fallback while scanning.
        QVector<music::ChordSymbol> lookahead;
        lookahead.reserve(8);
        music::ChordSymbol laLast = cur;
        lookahead.push_back(cur);
        for (int o = 1; o < 8; ++o) {
            const int step2 = step + o;
            if (step2 >= total) break;
            const int cell2 = m_sequence[step2 % seqLen];
            music::ChordSymbol parsed = parseCellChordNoState(cell2, laLast);
            laLast = parsed;
            lookahead.push_back(parsed);
        }

        const bool strongBeat = (beatInBar == 0 || beatInBar == 2);
        const bool structural = strongBeat || isNewChord;

        // ---- Bass scheduling ----
        if (bassOn) {
            music::BassBeatContext ctx;
            ctx.barIndex = barIndex;
            ctx.beatInBar = beatInBar;
            ctx.tempoBpm = m_bpm;
            ctx.isNewBar = isNewBar;
            ctx.isNewChord = isNewChord;
            ctx.songPass = songPass;
            ctx.totalPasses = totalPasses;
            ctx.phraseLengthBars = std::max(1, m_bassProfile.phraseLengthBars);
            ctx.sectionHash = sectionHash;
            if (ctx.isNewBar) {
                ctx.isSectionChange = isSectionChange;
                ctx.barInSection = barInSection;
                ctx.isPhraseEnd = (((ctx.barInSection + 1) % ctx.phraseLengthBars) == 0);
            }
            ctx.lookaheadChords = lookahead;

            QVector<music::BassEvent> ev = m_bass.nextBeat(ctx, &cur, nextPtr);
            if (!ev.isEmpty()) {
                std::sort(ev.begin(), ev.end(), [](const music::BassEvent& a, const music::BassEvent& b) {
                    return a.offsetBeats < b.offsetBeats;
                });

                int jitter = (m_bassProfile.microJitterMs > 0)
                    ? (int(m_timingRng.bounded(m_bassProfile.microJitterMs * 2 + 1)) - m_bassProfile.microJitterMs)
                    : 0;
                int attackVar = (m_bassProfile.attackVarianceMs > 0)
                    ? (int(m_timingRng.bounded(m_bassProfile.attackVarianceMs * 2 + 1)) - m_bassProfile.attackVarianceMs)
                    : 0;
                int push = m_bassProfile.pushMs;
                int laidBack = m_bassProfile.laidBackMs;
                int driftLocal = m_driftMs;

                if (structural) {
                    jitter = 0;
                    attackVar = 0;
                    push = int(std::llround(double(push) * 0.35));
                    laidBack = int(std::llround(double(laidBack) * 0.35));
                    driftLocal = int(std::llround(double(driftLocal) * 0.30));
                }

                auto calcBaseOffsetMs = [&](double offsetBeats) -> int {
                    int swingMsLocal = 0;
                    const double frac = offsetBeats - std::floor(offsetBeats);
                    const bool isUpbeat8th = std::fabs(frac - 0.5) < 0.001;
                    if (isUpbeat8th) {
                        const double ratio = std::max(1.2, std::min(4.0, m_bassProfile.swingRatio));
                        const double deltaFrac = (ratio / (ratio + 1.0)) - 0.5;
                        swingMsLocal = int(std::round(beatMs * deltaFrac * m_bassProfile.swingAmount));
                    }
                    int base = laidBack - push + jitter + attackVar + driftLocal + swingMsLocal;
                    const int clampMs = structural ? 16 : 28;
                    base = std::max(-clampMs, std::min(clampMs, base));
                    return base;
                };

                constexpr int kBassMusicalOctaveShift = 12;

                for (int i = 0; i < ev.size(); ++i) {
                    const auto& e = ev[i];
                    if (e.rest) continue;
                    if (e.midiNote < 0 || e.velocity <= 0) continue;

                    const double offset = std::max(0.0, std::min(0.95, e.offsetBeats));
                    const double tOnMs = beatStartMs + offset * beatMs;
                    int delayOn = int(std::round(tOnMs + double(calcBaseOffsetMs(offset)) - double(elapsedMs)));
                    if (delayOn < 0) delayOn = 0;
                    if (e.role == music::BassEvent::Role::KeySwitch) {
                        delayOn = std::max(0, delayOn - 12);
                    }

                    int lenMs = 0;
                    if (e.lengthBeats > 0.0) {
                        lenMs = int(std::round(beatMs * e.lengthBeats));
                    } else if (m_bassProfile.noteLengthMs > 0) {
                        lenMs = m_bassProfile.noteLengthMs;
                    } else {
                        lenMs = int(std::round(beatMs * m_bassProfile.gatePct));
                    }
                    if (e.ghost) {
                        lenMs = int(std::round(std::max(20.0, beatMs * m_bassProfile.ghostGatePct)));
                    }
                    lenMs = std::max(20, std::min(8000, lenMs));

                    if (e.role == music::BassEvent::Role::MusicalNote && !e.allowOverlap) {
                        if (i + 1 < ev.size()) {
                            const auto& n = ev[i + 1];
                            if (n.role == music::BassEvent::Role::MusicalNote) {
                                const double nextOffset = std::max(0.0, std::min(0.95, n.offsetBeats));
                                const double nextOnMs = beatStartMs + nextOffset * beatMs;
                                int nextDelayOn = int(std::round(nextOnMs + double(calcBaseOffsetMs(nextOffset)) - double(elapsedMs)));
                                if (nextDelayOn < 0) nextDelayOn = 0;
                                lenMs = std::min(lenMs, std::max(10, nextDelayOn - delayOn - 1));
                            }
                        }
                    }

                    int note = e.midiNote;
                    if (e.role == music::BassEvent::Role::MusicalNote) {
                        note += kBassMusicalOctaveShift;
                    }
                    note = std::max(0, std::min(127, note));
                    const int vel = std::max(1, std::min(127, e.velocity));

                    const int curCount = m_scheduledNoteOnsInBar.value(barIndex, 0);
                    if (e.role == music::BassEvent::Role::MusicalNote && curCount >= 24) {
                        continue;
                    }

                    if (e.role == music::BassEvent::Role::MusicalNote && !e.allowOverlap) {
                        if (m_lastBassMidi >= 0 && m_lastBassMidi != note) {
                            const int prev = m_lastBassMidi;
                            const int d = std::max(0, delayOn - 1);
                            scheduleEvent(elapsedMs + d, Instrument::Bass, PendingKind::NoteOff, m_bassProfile.midiChannel, prev, 0, 0, 0, false, {});
                        }
                    }

                    const bool logOn = m_bassProfile.reasoningLogEnabled;
                    QString logLine;
                    if (logOn) {
                        const QString chordText = !cur.originalText.trimmed().isEmpty()
                            ? cur.originalText.trimmed()
                            : QString("pc%1").arg(cur.rootPc);
                        QString kind;
                        switch (e.role) {
                        case music::BassEvent::Role::KeySwitch: kind = "Keyswitch"; break;
                        case music::BassEvent::Role::FxSound: kind = "FX"; break;
                        case music::BassEvent::Role::MusicalNote: default: kind = "Note"; break;
                        }
                        const QString fn = e.function.trimmed().isEmpty() ? QString("—") : e.function.trimmed();
                        const QString why = e.reasoning.trimmed().isEmpty() ? QString("—") : e.reasoning.trimmed();
                        const int humanizeMs = calcBaseOffsetMs(offset);
                        const int gridOffsetMs = int(std::llround(offset * beatMs));
                        const int totalOffsetMs = gridOffsetMs + humanizeMs;
                        logLine = QString("[bar %1 beat %2] %3  %4 (%5) vel=%6  function=%7  chord=%8  why: %9")
                                      .arg(barIndex + 1)
                                      .arg(beatInBar + 1)
                                      .arg(kind)
                                      .arg(midiName(note))
                                      .arg(note)
                                      .arg(vel)
                                      .arg(fn)
                                      .arg(chordText)
                                      .arg(why);
                        logLine += QString("  timing: grid=%1ms humanize=%2ms total=%3ms (delayOn=%4ms len=%5ms)")
                                       .arg(gridOffsetMs)
                                       .arg(humanizeMs)
                                       .arg(totalOffsetMs)
                                       .arg(delayOn)
                                       .arg(lenMs);
                    }

                    scheduleEvent(elapsedMs + delayOn, Instrument::Bass, PendingKind::NoteOn, m_bassProfile.midiChannel, note, vel, 0, 0, logOn, logLine);
                    scheduleEvent(elapsedMs + delayOn + lenMs, Instrument::Bass, PendingKind::NoteOff, m_bassProfile.midiChannel, note, 0, 0, 0, false, {});

                    if (e.role == music::BassEvent::Role::MusicalNote) {
                        m_lastBassMidi = note;
                        m_scheduledNoteOnsInBar.insert(barIndex, curCount + 1);
                    }
                }
            }
        }

        // ---- Piano scheduling ----
        if (pianoOn) {
            music::PianoBeatContext pctx;
            pctx.barIndex = barIndex;
            pctx.beatInBar = beatInBar;
            pctx.tempoBpm = m_bpm;
            pctx.isNewBar = isNewBar;
            pctx.isNewChord = isNewChord;
            pctx.songPass = songPass;
            pctx.totalPasses = totalPasses;
            pctx.phraseLengthBars = std::max(1, m_pianoProfile.phraseLengthBars);
            pctx.sectionHash = sectionHash;
            if (pctx.isNewBar) {
                pctx.isSectionChange = isSectionChange;
                pctx.barInSection = barInSection;
                pctx.isPhraseEnd = (((pctx.barInSection + 1) % pctx.phraseLengthBars) == 0);
            }
            pctx.lookaheadChords = lookahead;

            QVector<music::PianoEvent> pev = m_piano.nextBeat(pctx, &cur, nextPtr);
            if (!pev.isEmpty()) {
                // Piano human timing: slightly looser than bass, still tight on chord arrivals.
                int jitter = (m_pianoProfile.microJitterMs > 0)
                    ? (int(m_pianoTimingRng.bounded(m_pianoProfile.microJitterMs * 2 + 1)) - m_pianoProfile.microJitterMs)
                    : 0;
                int push = m_pianoProfile.pushMs;
                int laidBack = m_pianoProfile.laidBackMs;
                int driftLocal = m_pianoDriftMs;
                if (structural) {
                    jitter = 0;
                    push = int(std::llround(double(push) * 0.40));
                    laidBack = int(std::llround(double(laidBack) * 0.40));
                    driftLocal = int(std::llround(double(driftLocal) * 0.30));
                }

                // Piano feel: swing the upbeat 8th slightly (even in ballads, subtly).
                // This gives "jazz time" without needing a separate piano swing UI yet.
                auto calcBaseOffsetMs = [&](double offsetBeats) -> int {
                    const double frac = offsetBeats - std::floor(offsetBeats);
                    const bool isUpbeat8th = std::fabs(frac - 0.5) < 0.001;
                    const double ratio = (m_pianoProfile.feelStyle == music::PianoFeelStyle::Ballad) ? 2.15 : 2.2;
                    const double amount = (m_pianoProfile.feelStyle == music::PianoFeelStyle::Ballad) ? 0.35 : 0.55;
                    const double deltaFrac = (ratio / (ratio + 1.0)) - 0.5;
                    const int swingMsLocal = isUpbeat8th ? int(std::round(beatMs * deltaFrac * amount)) : 0;

                    int base = laidBack - push + jitter + driftLocal + swingMsLocal;
                    const int clampMs = structural ? 18 : 32;
                    base = std::max(-clampMs, std::min(clampMs, base));
                    return base;
                };

                for (const auto& e : pev) {
                    const double offset = std::max(0.0, std::min(0.95, e.offsetBeats));
                    const double tOnMs = beatStartMs + offset * beatMs;
                    int delayOn = int(std::round(tOnMs + double(calcBaseOffsetMs(offset)) - double(elapsedMs)));
                    if (delayOn < 0) delayOn = 0;

                    if (e.kind == music::PianoEvent::Kind::CC) {
                        bool emitLog = false;
                        QString logLine;
                        if (m_pianoProfile.reasoningLogEnabled) {
                            const QString chordText = !cur.originalText.trimmed().isEmpty()
                                ? cur.originalText.trimmed()
                                : QString("pc%1").arg(cur.rootPc);
                            const QString fn = e.function.trimmed().isEmpty() ? QString("—") : e.function.trimmed();
                            const QString why = e.reasoning.trimmed().isEmpty() ? QString("—") : e.reasoning.trimmed();
                            const int humanizeMs = calcBaseOffsetMs(offset);
                            const int gridOffsetMs = int(std::llround(offset * beatMs));
                            const int totalOffsetMs = gridOffsetMs + humanizeMs;
                            emitLog = true;
                            logLine = QString("[bar %1 beat %2] Piano  chord=%3  CC%4=%5  function=%6  why: %7")
                                          .arg(barIndex + 1)
                                          .arg(beatInBar + 1)
                                          .arg(chordText)
                                          .arg(e.cc)
                                          .arg(e.ccValue)
                                          .arg(fn)
                                          .arg(why);
                            logLine += QString("  timing: grid=%1ms humanize=%2ms total=%3ms (delayOn=%4ms)")
                                           .arg(gridOffsetMs)
                                           .arg(humanizeMs)
                                           .arg(totalOffsetMs)
                                           .arg(delayOn);
                        }
                        scheduleEvent(elapsedMs + delayOn, Instrument::Piano, PendingKind::CC,
                                      m_pianoProfile.midiChannel, 0, 0, e.cc, e.ccValue, emitLog, logLine);
                        continue;
                    }

                    if (e.midiNote < 0 || e.velocity <= 0) continue;
                    const int note = std::max(0, std::min(127, e.midiNote));
                    const int vel = std::max(1, std::min(127, e.velocity));
                    int lenMs = 0;
                    if (e.lengthBeats > 0.0) lenMs = int(std::round(beatMs * e.lengthBeats));
                    else lenMs = int(std::round(beatMs * (m_pianoProfile.feelStyle == music::PianoFeelStyle::Ballad ? 0.92 : 0.78)));
                    lenMs = std::max(30, std::min(8000, lenMs));

                    const int curCount = m_scheduledPianoNoteOnsInBar.value(barIndex, 0);
                    if (curCount >= 48) continue;

                    // Reasoning log: emit one line per chord-hit (per offset group), not per note.
                    const bool logOn = m_pianoProfile.reasoningLogEnabled;
                    bool emitLog = false;
                    QString logLine;
                    if (logOn) {
                        // Build a chord-hit summary for this beat+offset on the fly:
                        // We only attach it to the first NoteOn scheduled for a given offset.
                        static QHash<QString, bool> emittedThisTick;
                        if (beatInBar == 0 && offset < 1e-6) emittedThisTick.clear();

                        const QString chordText = !cur.originalText.trimmed().isEmpty()
                            ? cur.originalText.trimmed()
                            : QString("pc%1").arg(cur.rootPc);
                        const int offKey = int(std::llround(offset * 1000.0));
                        const QString key = QString("%1|b%2|bt%3|off%4")
                                                .arg(barIndex)
                                                .arg(barIndex)
                                                .arg(beatInBar)
                                                .arg(offKey);
                        if (!emittedThisTick.value(key, false)) {
                            emittedThisTick.insert(key, true);
                            emitLog = true;

                            // Collect notes for this same offset (from pev).
                            QVector<int> notes;
                            notes.reserve(12);
                            QVector<QString> names;
                            for (const auto& e2 : pev) {
                                if (e2.kind != music::PianoEvent::Kind::Note) continue;
                                const double o2 = std::max(0.0, std::min(0.95, e2.offsetBeats));
                                if (std::abs(o2 - offset) > 1e-6) continue;
                                if (e2.midiNote < 0 || e2.velocity <= 0) continue;
                                const int n2 = std::max(0, std::min(127, e2.midiNote));
                                if (!notes.contains(n2)) notes.push_back(n2);
                            }
                            std::sort(notes.begin(), notes.end());
                            for (int n2 : notes) names.push_back(QString("%1(%2)").arg(midiName(n2)).arg(n2));

                            const QString fn = e.function.trimmed().isEmpty() ? QString("—") : e.function.trimmed();
                            const QString why = e.reasoning.trimmed().isEmpty() ? QString("—") : e.reasoning.trimmed();
                            const int humanizeMs = calcBaseOffsetMs(offset);
                            const int gridOffsetMs = int(std::llround(offset * beatMs));
                            const int totalOffsetMs = gridOffsetMs + humanizeMs;

                            logLine = QString("[bar %1 beat %2] Piano  chord=%3  notes=[%4]  function=%5  why: %6")
                                          .arg(barIndex + 1)
                                          .arg(beatInBar + 1)
                                          .arg(chordText)
                                          .arg(names.join(", "))
                                          .arg(fn)
                                          .arg(why);
                            logLine += QString("  timing: grid=%1ms humanize=%2ms total=%3ms (delayOn=%4ms len=%5ms)")
                                           .arg(gridOffsetMs)
                                           .arg(humanizeMs)
                                           .arg(totalOffsetMs)
                                           .arg(delayOn)
                                           .arg(lenMs);
                        }
                    }

                    scheduleEvent(elapsedMs + delayOn, Instrument::Piano, PendingKind::NoteOn,
                                  m_pianoProfile.midiChannel, note, vel, 0, 0, emitLog, logLine);
                    scheduleEvent(elapsedMs + delayOn + lenMs, Instrument::Piano, PendingKind::NoteOff,
                                  m_pianoProfile.midiChannel, note, 0, 0, 0, false, {});
                    m_scheduledPianoNoteOnsInBar.insert(barIndex, curCount + 1);
                }
            }
        }
    }
}

} // namespace playback

