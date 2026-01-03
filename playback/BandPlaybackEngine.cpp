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
    m_timingRng.seed(1u);
    m_driftMs = 0;
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
    m_barSections = buildBarSectionsFrom(m_model);
    m_lastChord = music::ChordSymbol{};
    m_hasLastChord = false;
    m_lastBassMidi = -1;
    m_lastStep = -1;
    m_lastEmittedCell = -1;
    m_lastBarIndex = -1;
    m_scheduledNoteOnsInBar.clear();
    m_driftMs = 0;
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
    m_driftMs = 0;
    m_lastPlayheadStep = -1;
    m_nextScheduledStep = 0;
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
    m_scheduledNoteOnsInBar.clear();
    m_driftMs = 0;
    m_lastPlayheadStep = -1;
    m_nextScheduledStep = 0;
    m_bass.reset();

    emit currentCellChanged(-1);
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
    if (!m_bassProfile.enabled) return;

    // Lookahead scheduling window to avoid "late notes" caused by tick quantization.
    constexpr int kLookaheadMs = 180;
    const int scheduleUntil = int((elapsedMs + kLookaheadMs) / beatMs);
    const int maxStepToSchedule = std::min(total - 1, scheduleUntil);

    // Helpers
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
            if (m_bassProfile.driftMaxMs > 0 && m_bassProfile.driftRate > 0.0) {
                const int stepMax = std::max(1, int(std::round(double(m_bassProfile.driftMaxMs) * m_bassProfile.driftRate)));
                const int delta = int(m_timingRng.bounded(stepMax * 2 + 1)) - stepMax;
                m_driftMs += delta;
                if (m_driftMs > m_bassProfile.driftMaxMs) m_driftMs = m_bassProfile.driftMaxMs;
                if (m_driftMs < -m_bassProfile.driftMaxMs) m_driftMs = -m_bassProfile.driftMaxMs;
            } else {
                m_driftMs = 0;
            }
        }

        music::ChordSymbol cur;
        bool isNewChord = false;
        const bool haveCur = chordForCellIndex(cellIndex, cur, isNewChord);
        if (!haveCur || cur.noChord) {
            // Silence bass on N.C. (or missing harmony) at the moment it occurs.
            // Use an on-beat timing (no jitter) so this feels intentional and tight.
            const int delay = std::max(0, int(std::llround(beatStartMs - double(elapsedMs))));
            if (m_lastBassMidi >= 0) {
                const int prev = m_lastBassMidi;
                schedule(delay, [this, prev]() { emit bassNoteOff(m_bassProfile.midiChannel, prev); });
                m_lastBassMidi = -1;
            }
            schedule(delay, [this]() { emit bassAllNotesOff(m_bassProfile.midiChannel); });
            continue;
        }

        music::ChordSymbol next;
        const bool haveNext = chordForNextCellIndex(cellIndex, next);
        const music::ChordSymbol* nextPtr = haveNext ? &next : &cur;

        // Beat context for evolving performance.
        music::BassBeatContext ctx;
        ctx.barIndex = barIndex;
        ctx.beatInBar = beatInBar;
        ctx.isNewBar = (beatInBar == 0);
        ctx.isNewChord = isNewChord;
        ctx.songPass = (seqLen > 0) ? (step / seqLen) : 0;
        ctx.totalPasses = std::max(1, m_repeats);
        ctx.phraseLengthBars = std::max(1, m_bassProfile.phraseLengthBars);
        const QString sec = (barIndex >= 0 && barIndex < m_barSections.size()) ? m_barSections[barIndex] : QString();
        ctx.sectionHash = (quint32)qHash(sec);
        if (ctx.isNewBar) {
            const QString prevSec = (barIndex - 1 >= 0 && barIndex - 1 < m_barSections.size()) ? m_barSections[barIndex - 1] : QString();
            ctx.isSectionChange = (sec != prevSec) && (!sec.isEmpty() || !prevSec.isEmpty());
            int count = 0;
            for (int b = barIndex; b >= 0; --b) {
                const QString s = (b >= 0 && b < m_barSections.size()) ? m_barSections[b] : QString();
                if (s != sec) break;
                count++;
            }
            ctx.barInSection = std::max(0, count - 1);
            ctx.isPhraseEnd = (((ctx.barInSection + 1) % ctx.phraseLengthBars) == 0);
        }

        // Generate events for this beat (possibly multiple per beat).
        QVector<music::BassEvent> ev = m_bass.nextBeat(ctx, &cur, nextPtr);
        if (ev.isEmpty()) continue;
        std::sort(ev.begin(), ev.end(), [](const music::BassEvent& a, const music::BassEvent& b) {
            return a.offsetBeats < b.offsetBeats;
        });

        // Human timing for this scheduled beat (stable per-song).
        const int jitter = (m_bassProfile.microJitterMs > 0)
            ? (int(m_timingRng.bounded(m_bassProfile.microJitterMs * 2 + 1)) - m_bassProfile.microJitterMs)
            : 0;
        const int attackVar = (m_bassProfile.attackVarianceMs > 0)
            ? (int(m_timingRng.bounded(m_bassProfile.attackVarianceMs * 2 + 1)) - m_bassProfile.attackVarianceMs)
            : 0;
        const int push = m_bassProfile.pushMs;
        const int laidBack = m_bassProfile.laidBackMs;

        auto calcBaseOffsetMs = [&](double offsetBeats) -> int {
            int swingMsLocal = 0;
            const double frac = offsetBeats - std::floor(offsetBeats);
            const bool isUpbeat8th = std::fabs(frac - 0.5) < 0.001;
            if (isUpbeat8th) {
                const double ratio = std::max(1.2, std::min(4.0, m_bassProfile.swingRatio));
                const double deltaFrac = (ratio / (ratio + 1.0)) - 0.5;
                swingMsLocal = int(std::round(beatMs * deltaFrac * m_bassProfile.swingAmount));
            }
            int base = laidBack - push + jitter + attackVar + m_driftMs + swingMsLocal;
            // Safety clamp: even with wild settings, don't allow "mistake-level" timing errors.
            base = std::max(-80, std::min(80, base));
            return base;
        };

        constexpr int kBassMusicalOctaveShift = 12;

        // Schedule note-ons/offs (and optional explainability log).
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

            // Per-bar sanity cap (avoid runaway density) without pre-silencing future bars.
            const int curCount = m_scheduledNoteOnsInBar.value(barIndex, 0);
            if (e.role == music::BassEvent::Role::MusicalNote && curCount >= 24) {
                continue;
            }

            if (e.role == music::BassEvent::Role::MusicalNote && !e.allowOverlap) {
                if (m_lastBassMidi >= 0 && m_lastBassMidi != note) {
                    const int prev = m_lastBassMidi;
                    schedule(std::max(0, delayOn - 1), [this, prev]() { emit bassNoteOff(m_bassProfile.midiChannel, prev); });
                }
            }

            // Note-on (and optional explainability) uses a single timer to minimize overhead.
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
            }

            schedule(delayOn, [this, note, vel, logOn, logLine]() {
                if (logOn && !logLine.isEmpty()) emit bassLogLine(logLine);
                emit bassNoteOn(m_bassProfile.midiChannel, note, vel);
            });
            schedule(delayOn + lenMs, [this, note]() { emit bassNoteOff(m_bassProfile.midiChannel, note); });

            if (e.role == music::BassEvent::Role::MusicalNote) {
                m_lastBassMidi = note;
                m_scheduledNoteOnsInBar.insert(barIndex, curCount + 1);
            }
        }
    }
}

} // namespace playback

