#ifndef WAVEVISUALIZER_H
#define WAVEVISUALIZER_H

#include <QWidget>
#include <QVector>
#include <QElapsedTimer>
#include <QColor>
#include <QString>

class QLabel;
class QTimer;

class WaveCanvas : public QWidget {
    Q_OBJECT
public:
    explicit WaveCanvas(QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(400, 120); }

public slots:
    void setGuitarHz(double hz);
    void setVoiceHz(double hz);
    void setGuitarAmplitude(int aftertouch01to127);
    void setVoiceAmplitude(int cc201to127);
    void setGuitarVelocity(int velocity01to127);
    void setGuitarColor(const QColor& color);
    void setVoiceColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void ensureBuffers(int width);

    // State for rendering
    double m_guitarHz = 0.0;
    double m_voiceHz = 0.0;
    double m_amp = 0.0;        // 0..1, shared (voice CC2)
    double m_guitarVelocityAmp = 0.0; // 0..1, fallback when no voice amp
    double m_guitarDecayAmp = 0.0;    // 0..1, decaying amplitude
    double m_guitarTauSec = 0.8;      // decay time constant (sec), derived from velocity
    QElapsedTimer m_decayElapsed;
    QTimer* m_decayTimer = nullptr;

    // Colors (without alpha)
    QColor m_guitarColor = QColor(0, 255, 0);
    QColor m_voiceColor = QColor(0, 255, 0);

    // Reusable point buffers
    QVector<QPointF> m_pointsG;
    QVector<QPointF> m_pointsG2;
    QVector<QPointF> m_pointsV;
};

class WaveVisualizer : public QWidget {
    Q_OBJECT
public:
    explicit WaveVisualizer(QWidget* parent = nullptr);

public slots:
    void setGuitarHz(double hz);
    void setVoiceHz(double hz);
    void setGuitarAmplitude(int val);
    void setVoiceAmplitude(int val);
    void setGuitarVelocity(int val);
    void setGuitarColor(const QColor& color);
    void setVoiceColor(const QColor& color);
    void setGuitarCentsText(const QString& text);
    void setVoiceCentsText(const QString& text);

private:
    QLabel* m_leftHz = nullptr;     // Guitar
    QLabel* m_leftCents = nullptr;  // Guitar cents
    QLabel* m_rightHz = nullptr;    // Voice
    QLabel* m_rightCents = nullptr; // Voice cents
    WaveCanvas* m_canvas = nullptr;
};

#endif // WAVEVISUALIZER_H

