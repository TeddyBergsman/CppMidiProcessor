#pragma once

#include <QElapsedTimer>

namespace virtuoso::engine {

// Internal clock authority for Virtuoso (Stage 1: QElapsedTimer, sample-agnostic).
class VirtuosoClock {
public:
    void start() { m_timer.restart(); m_running = true; }
    void stop() { m_running = false; }
    bool isRunning() const { return m_running; }
    qint64 elapsedMs() const { return m_running ? m_timer.elapsed() : 0; }

private:
    bool m_running = false;
    QElapsedTimer m_timer;
};

} // namespace virtuoso::engine

