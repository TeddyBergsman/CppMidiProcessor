#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>

namespace playback {

// Silent playhead driving chord/cell highlighting.
// v1: advances one grid cell per beat at the current tempo.
class SilentPlaybackEngine : public QObject {
    Q_OBJECT
public:
    explicit SilentPlaybackEngine(QObject* parent = nullptr);

    void setTempoBpm(int bpm);
    void setTotalCells(int totalCells);
    void setRepeats(int repeats);

    bool isPlaying() const { return m_playing; }

public slots:
    void play();
    void stop();

signals:
    void currentCellChanged(int cellIndex);

private slots:
    void onTick();

private:
    int m_bpm = 120;
    int m_totalCells = 0;
    int m_repeats = 3;

    bool m_playing = false;
    QElapsedTimer m_clock;
    QTimer m_tickTimer;
};

} // namespace playback

