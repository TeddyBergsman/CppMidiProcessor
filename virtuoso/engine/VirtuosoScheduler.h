#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <array>

#include "virtuoso/engine/VirtuosoClock.h"

namespace virtuoso::engine {

// Minimal real-time scheduler (single-shot wakeup + min-heap).
// This is infrastructure only: no legacy musician logic.
class VirtuosoScheduler : public QObject {
    Q_OBJECT
public:
    enum class Kind {
        NoteOn,
        NoteOff,
        AllNotesOff,
        CC,
        TheoryEventJson,
    };

    struct ScheduledEvent {
        qint64 dueMs = 0; // absolute, in clock elapsed ms
        Kind kind = Kind::NoteOn;
        int channel = 1; // 1..16

        // Note fields
        int note = 0;
        int velocity = 0;

        // CC fields
        int cc = 0;
        int ccValue = 0;

        // JSON explainability payload
        QString theoryJson;
    };

    explicit VirtuosoScheduler(VirtuosoClock* clock, QObject* parent = nullptr);

    void clear();
    bool isEmpty() const { return m_heap.isEmpty(); }

    void schedule(const ScheduledEvent& ev);

    // Hard stop: immediately emits NoteOff for any notes that are currently on (tracked internally),
    // then emits AllNotesOff per channel as a safety net, and clears the queue.
    // This does NOT depend on the clock running.
    void panicSilence();

signals:
    void noteOn(int channel, int note, int velocity);
    void noteOff(int channel, int note);
    void allNotesOff(int channel);
    void cc(int channel, int cc, int value);
    void theoryEventJson(const QString& json);

private slots:
    void onDispatch();

private:
    VirtuosoClock* m_clock = nullptr; // not owned
    QVector<ScheduledEvent> m_heap;   // min-heap by dueMs
    QTimer m_dispatchTimer;

    // Track active notes that have actually been emitted as NOTE_ON and not yet NOTE_OFF.
    // [channel-1][note] => on/off
    std::array<std::array<bool, 128>, 16> m_active{};
};

} // namespace virtuoso::engine

