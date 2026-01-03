#include "SilentPlaybackEngine.h"

#include <QtGlobal>

namespace playback {

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

void SilentPlaybackEngine::play() {
    if (m_totalCells <= 0) return;
    m_playing = true;
    m_clock.restart();
    m_tickTimer.start();
    emit currentCellChanged(0);
}

void SilentPlaybackEngine::stop() {
    if (!m_playing) return;
    m_playing = false;
    m_tickTimer.stop();
    emit currentCellChanged(-1);
}

void SilentPlaybackEngine::onTick() {
    if (!m_playing || m_totalCells <= 0) return;

    // One cell per beat (quarter note) in v1.
    const double beatMs = 60000.0 / double(m_bpm);
    const qint64 elapsedMs = m_clock.elapsed();
    const int cell = int(elapsedMs / beatMs);

    const int total = m_totalCells * qMax(1, m_repeats);
    if (cell >= total) {
        stop();
        return;
    }
    emit currentCellChanged(cell % m_totalCells);
}

} // namespace playback

