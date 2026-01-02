#ifndef PITCHMONITORWIDGET_H
#define PITCHMONITORWIDGET_H

#include <QWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>

class PitchMonitorWidget : public QWidget {
    Q_OBJECT
public:
    explicit PitchMonitorWidget(QWidget* parent = nullptr);

public slots:
    void setBpm(int bpm);
    void setKeyCenter(const QString& keyCenter);
    void pushGuitar(int midiNote, double cents);
    void pushVocal(int midiNote, double cents);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Sample {
        double tSec = 0.0;   // seconds since start
        int midiNote = -1;   // -1 => inactive/gap
        double cents = 0.0;  // [-50, 50]
    };

    void pushSample(QVector<Sample>& stream, int midiNote, double cents,
                    double& lastAppendSec, int& lastMidi, double& lastCents);

    void tick();
    void pruneOldSamples();
    void updateVerticalTargetForNote(int midiNote);

    double nowSec() const;
    double pxPerSecond() const;
    double midiToY(double midi) const;

    QString formatNoteShort(int midiNote) const;
    bool preferFlats() const;
    int keyRootPitchClass() const;
    bool isPitchClassInKeyMajorScale(int pitchClass) const;

    // Time / animation
    QElapsedTimer m_clock;
    QTimer* m_timer = nullptr;

    // Preferences
    int m_bpm = 120;
    int m_pxPerBeat = 60; // tune visual density
    double m_minAppendIntervalSec = 1.0 / 90.0; // throttle sample density
    double m_maxHistorySec = 12.0;              // cap history window for performance
    QString m_keyCenter = "Eb major";

    // Data
    QVector<Sample> m_guitar;
    QVector<Sample> m_vocal;

    // Sampling state
    double m_lastGuitarAppendSec = -1.0;
    double m_lastVocalAppendSec = -1.0;
    int m_lastGuitarMidi = -2;
    int m_lastVocalMidi = -2;
    double m_lastGuitarCents = 0.0;
    double m_lastVocalCents = 0.0;

    // Vertical viewport (in MIDI notes)
    double m_centerMidi = 60.0;
    double m_targetCenterMidi = 60.0;
    double m_visibleSemis = 24.0;
    double m_recenterMarginSemis = 2.0;
};

#endif // PITCHMONITORWIDGET_H

