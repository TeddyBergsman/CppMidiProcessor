#include "virtuoso/engine/VirtuosoScheduler.h"

#include <algorithm>

namespace virtuoso::engine {

namespace {
static bool heapLess(const VirtuosoScheduler::ScheduledEvent& a,
                     const VirtuosoScheduler::ScheduledEvent& b) {
    return a.dueMs > b.dueMs; // reversed for min-heap behavior
}
} // namespace

VirtuosoScheduler::VirtuosoScheduler(VirtuosoClock* clock, QObject* parent)
    : QObject(parent), m_clock(clock) {
    m_dispatchTimer.setSingleShot(true);
    // Critical for microtiming: Qt defaults can be coarse; request precise timer behavior.
    m_dispatchTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_dispatchTimer, &QTimer::timeout, this, &VirtuosoScheduler::onDispatch);
    for (auto& row : m_active) row.fill(false);
}

void VirtuosoScheduler::clear() {
    m_heap.clear();
    m_dispatchTimer.stop();
}

void VirtuosoScheduler::panicSilence() {
    // Stop any pending dispatches first.
    m_dispatchTimer.stop();

    // Emit explicit NOTE_OFF for any active notes (critical for looped articulations in samplers).
    for (int ch = 1; ch <= 16; ++ch) {
        bool any = false;
        for (int n = 0; n < 128; ++n) {
            if (!m_active[ch - 1][n]) continue;
            any = true;
            m_active[ch - 1][n] = false;
            emit noteOff(ch, n);
        }
        if (any) {
            // Safety net: also emit AllNotesOff (CC123/CC120 downstream).
            emit allNotesOff(ch);
        }
    }

    m_heap.clear();
}

void VirtuosoScheduler::schedule(const ScheduledEvent& ev) {
    const bool wasEmpty = m_heap.isEmpty();
    m_heap.push_back(ev);
    std::push_heap(m_heap.begin(), m_heap.end(), heapLess);

    if (!m_clock || !m_clock->isRunning()) return;

    const qint64 now = m_clock->elapsedMs();
    if (wasEmpty || ev.dueMs <= m_heap.front().dueMs) {
        const int delay = int(std::max<qint64>(0, m_heap.front().dueMs - now));
        if (!m_dispatchTimer.isActive() || delay < m_dispatchTimer.remainingTime()) {
            m_dispatchTimer.start(delay);
        }
    }
}

void VirtuosoScheduler::onDispatch() {
    if (!m_clock || !m_clock->isRunning()) return;
    const qint64 now = m_clock->elapsedMs();

    while (!m_heap.isEmpty()) {
        const ScheduledEvent& top = m_heap.front();
        if (top.dueMs > now) break;

        ScheduledEvent ev = top;
        std::pop_heap(m_heap.begin(), m_heap.end(), heapLess);
        m_heap.pop_back();

        switch (ev.kind) {
        case Kind::NoteOn:
            if (ev.channel >= 1 && ev.channel <= 16 && ev.note >= 0 && ev.note <= 127) {
                m_active[ev.channel - 1][ev.note] = true;
            }
            emit noteOn(ev.channel, ev.note, ev.velocity);
            break;
        case Kind::NoteOff:
            if (ev.channel >= 1 && ev.channel <= 16 && ev.note >= 0 && ev.note <= 127) {
                m_active[ev.channel - 1][ev.note] = false;
            }
            emit noteOff(ev.channel, ev.note);
            break;
        case Kind::AllNotesOff:
            if (ev.channel >= 1 && ev.channel <= 16) {
                m_active[ev.channel - 1].fill(false);
            }
            emit allNotesOff(ev.channel);
            break;
        case Kind::CC: emit cc(ev.channel, ev.cc, ev.ccValue); break;
        case Kind::TheoryEventJson:
            if (!ev.theoryJson.isEmpty()) emit theoryEventJson(ev.theoryJson);
            break;
        }
    }

    if (!m_heap.isEmpty()) {
        const qint64 nextDue = m_heap.front().dueMs;
        const int delay = int(std::max<qint64>(0, nextDue - now));
        m_dispatchTimer.start(delay);
    }
}

} // namespace virtuoso::engine

