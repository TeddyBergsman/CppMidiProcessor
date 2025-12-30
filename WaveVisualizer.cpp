#include "WaveVisualizer.h"
#include <QtWidgets>
#include <QTimer>
#include <QElapsedTimer>
#include <cmath>

// ---- WaveCanvas ----

WaveCanvas::WaveCanvas(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Decay timer (stopped by default)
    m_decayTimer = new QTimer(this);
    m_decayTimer->setTimerType(Qt::PreciseTimer);
    m_decayTimer->setInterval(16);
    connect(m_decayTimer, &QTimer::timeout, this, [this]() {
        if (m_guitarDecayAmp <= 0.0) {
            m_decayTimer->stop();
            return;
        }
        qint64 ms = m_decayElapsed.elapsed();
        m_decayElapsed.restart();
        if (ms <= 0) return;
        double dt = ms * 0.001;
        double tau = (m_guitarTauSec > 0.05) ? m_guitarTauSec : 0.05;
        m_guitarDecayAmp *= std::exp(-dt / tau);
        if (m_guitarDecayAmp < 0.005) {
            m_guitarDecayAmp = 0.0;
            m_decayTimer->stop();
        }
        update();
    });
}

void WaveCanvas::setGuitarHz(double hz) {
    m_guitarHz = hz;
    update();
}

void WaveCanvas::setVoiceHz(double hz) {
    m_voiceHz = hz;
    update();
}

void WaveCanvas::setGuitarAmplitude(int aftertouch01to127) {
    // Ignored by design: both waves use voice CC2 amplitude
}

void WaveCanvas::setVoiceAmplitude(int cc201to127) {
    int v = std::max(0, std::min(127, cc201to127));
    m_amp = v / 127.0;
    update();
}

void WaveCanvas::setGuitarVelocity(int velocity01to127) {
    int v = std::max(0, std::min(127, velocity01to127));
    m_guitarVelocityAmp = v / 127.0;
    // Initialize decaying amplitude from velocity
    m_guitarDecayAmp = m_guitarVelocityAmp;
    // Map velocity to decay time constant (0.3s .. 1.6s)
    double vn = m_guitarVelocityAmp; // 0..1
    m_guitarTauSec = 0.3 + 1.3 * vn;
    m_decayElapsed.restart();
    if (!m_decayTimer->isActive()) {
        m_decayTimer->start();
    }
    update();
}

void WaveCanvas::setGuitarColor(const QColor& color) {
    m_guitarColor = color;
    update();
}

void WaveCanvas::setVoiceColor(const QColor& color) {
    m_voiceColor = color;
    update();
}

void WaveCanvas::ensureBuffers(int width) {
    if (m_pointsG.size() != width) {
        m_pointsG.resize(width);
        m_pointsG2.resize(width);
        m_pointsV.resize(width);
    }
}

void WaveCanvas::resizeEvent(QResizeEvent* event) {
    ensureBuffers(event->size().width());
    QWidget::resizeEvent(event);
}

void WaveCanvas::paintEvent(QPaintEvent* /*event*/) {
    const int w = width();
    const int h = height();
    if (w <= 2 || h <= 2) return;
    ensureBuffers(w);

    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Fixed time window (seconds) controlling horizontal density
    const double T = 0.015; // 15 ms
    const double centerY = h * 0.5;
    const double maxAmpPx = h * 0.45; // prevent clipping

    // Precompute 2π
    const double twoPi = 6.283185307179586;

    // Prepare pens from dynamic pitch colors:
    QColor guitarPrimary = m_guitarColor; guitarPrimary.setAlphaF(0.5);
    QColor guitarDecay   = m_guitarColor; guitarDecay.setAlphaF(0.25);
    QColor voicePrimary  = m_voiceColor;  voicePrimary.setAlphaF(0.5);
    QPen penG(guitarPrimary);
    QPen penGDecay(guitarDecay);
    QPen penV(voicePrimary);
    penG.setWidth(2);
    penGDecay.setWidth(2);
    penV.setWidth(2);
    penV.setStyle(Qt::DotLine); // dotted voice wave to stand out

    // Compute guitar points if active
    if (m_guitarHz > 1.0) {
        const double cyclesAcross = m_guitarHz * T;
        // When voice amp is present, draw two guitar waves: voice-driven and decay-driven
        if (m_amp > 0.0) {
            // Voice-driven guitar wave
            const double ampVoicePx = maxAmpPx * m_amp;
            if (ampVoicePx > 0.5) {
                for (int x = 0; x < w; ++x) {
                    double xn = static_cast<double>(x) / static_cast<double>(w - 1);
                    double xnf = 1.0 - xn; // flip horizontally so wave goes right -> left
                    double phase = -twoPi * (cyclesAcross * xnf);
                    double y = centerY - ampVoicePx * std::sin(phase);
                    m_pointsG[x] = QPointF(x + 0.5, y);
                }
                p.setPen(penG);
                p.drawPolyline(m_pointsG.constData(), w);
            }
            // Decay-driven guitar wave
            const double ampDecayPx = maxAmpPx * m_guitarDecayAmp;
            if (ampDecayPx > 0.5) {
                for (int x = 0; x < w; ++x) {
                    double xn = static_cast<double>(x) / static_cast<double>(w - 1);
                    double xnf = 1.0 - xn; // flip horizontally so wave goes right -> left
                    double phase = -twoPi * (cyclesAcross * xnf);
                    double y = centerY - ampDecayPx * std::sin(phase);
                    m_pointsG2[x] = QPointF(x + 0.5, y);
                }
                p.setPen(penGDecay);
                p.drawPolyline(m_pointsG2.constData(), w);
            }
        } else {
            // No voice amp -> single decay-driven guitar wave
            const double ampDecayPx = maxAmpPx * m_guitarDecayAmp;
            if (ampDecayPx > 0.5) {
                for (int x = 0; x < w; ++x) {
                    double xn = static_cast<double>(x) / static_cast<double>(w - 1);
                    double xnf = 1.0 - xn; // flip horizontally so wave goes right -> left
                    double phase = -twoPi * (cyclesAcross * xnf);
                    double y = centerY - ampDecayPx * std::sin(phase);
                    m_pointsG[x] = QPointF(x + 0.5, y);
                }
                p.setPen(penGDecay);
                p.drawPolyline(m_pointsG.constData(), w);
            }
        }
    }

    // Compute voice points if active
    if (m_voiceHz > 1.0 && m_amp > 0.0) {
        const double cyclesAcross = m_voiceHz * T;
        const double ampPx = maxAmpPx * m_amp;
        for (int x = 0; x < w; ++x) {
            double xn = static_cast<double>(x) / static_cast<double>(w - 1);
            double xnf = 1.0 - xn; // flip horizontally so wave goes right -> left
            double phase = -twoPi * (cyclesAcross * xnf);
            double y = centerY - ampPx * std::sin(phase);
            m_pointsV[x] = QPointF(x + 0.5, y);
        }
        p.setPen(penV);
        p.drawPolyline(m_pointsV.constData(), w);
    }
}

// ---- WaveVisualizer ----

WaveVisualizer::WaveVisualizer(QWidget* parent)
    : QWidget(parent) {
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // Left side (guitar): Hz over cents
    QVBoxLayout* leftBox = new QVBoxLayout();
    leftBox->setContentsMargins(0, 0, 0, 0);
    leftBox->setSpacing(0);
    QWidget* leftWidget = new QWidget(this);
    leftWidget->setLayout(leftBox);

    m_leftHz = new QLabel("", leftWidget);
    m_leftHz->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_leftHz->setStyleSheet("QLabel { color: rgb(0,255,255); font-size: 12pt; }");
    m_leftHz->setFixedWidth(56);
    m_leftHz->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    m_leftCents = new QLabel("", leftWidget);
    m_leftCents->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_leftCents->setStyleSheet("QLabel { color: rgb(0,255,255); font-size: 10pt; }");
    m_leftCents->setFixedWidth(56);
    m_leftCents->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    leftBox->addStretch(1);
    leftBox->addWidget(m_leftHz);
    leftBox->addWidget(m_leftCents);
    leftBox->addStretch(1);

    m_canvas = new WaveCanvas(this);

    // Right side (voice): Hz over cents
    QVBoxLayout* rightBox = new QVBoxLayout();
    rightBox->setContentsMargins(0, 0, 0, 0);
    rightBox->setSpacing(0);
    QWidget* rightWidget = new QWidget(this);
    rightWidget->setLayout(rightBox);

    m_rightHz = new QLabel("", rightWidget);
    m_rightHz->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_rightHz->setStyleSheet("QLabel { color: rgb(255,0,255); font-size: 12pt; }");
    m_rightHz->setFixedWidth(56);
    m_rightHz->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    m_rightCents = new QLabel("", rightWidget);
    m_rightCents->setAlignment(Qt::AlignRight | Qt::AlignTop);
    m_rightCents->setStyleSheet("QLabel { color: rgb(255,0,255); font-size: 10pt; }");
    m_rightCents->setFixedWidth(56);
    m_rightCents->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    rightBox->addStretch(1);
    rightBox->addWidget(m_rightHz);
    rightBox->addWidget(m_rightCents);
    rightBox->addStretch(1);

    layout->addWidget(leftWidget);
    layout->addWidget(m_canvas, 1);
    layout->addWidget(rightWidget);
    setLayout(layout);
}

void WaveVisualizer::setGuitarHz(double hz) {
    m_canvas->setGuitarHz(hz);
    if (hz > 1.0) {
        m_leftHz->setText(QString::number(static_cast<int>(std::round(hz))) + " Hz");
    } else {
        m_leftHz->setText(QString());
        m_leftCents->setText(QString());
    }
}

void WaveVisualizer::setVoiceHz(double hz) {
    m_canvas->setVoiceHz(hz);
    if (hz > 1.0) {
        m_rightHz->setText(QString::number(static_cast<int>(std::round(hz))) + " Hz");
    } else {
        m_rightHz->setText(QString());
        m_rightCents->setText(QString());
    }
}

void WaveVisualizer::setGuitarAmplitude(int val) {
    m_canvas->setGuitarAmplitude(val);
}

void WaveVisualizer::setVoiceAmplitude(int val) {
    m_canvas->setVoiceAmplitude(val);
}

void WaveVisualizer::setGuitarVelocity(int val) {
    m_canvas->setGuitarVelocity(val);
}

void WaveVisualizer::setGuitarColor(const QColor& color) {
    m_canvas->setGuitarColor(color);
    if (m_leftHz) {
        QString style = QString("QLabel { color: rgb(%1,%2,%3); font-size: 12pt; }")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue());
        m_leftHz->setStyleSheet(style);
        QString styleSmall = QString("QLabel { color: rgb(%1,%2,%3); font-size: 10pt; }")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue());
        if (m_leftCents) m_leftCents->setStyleSheet(styleSmall);
    }
}

void WaveVisualizer::setVoiceColor(const QColor& color) {
    m_canvas->setVoiceColor(color);
    if (m_rightHz) {
        QString style = QString("QLabel { color: rgb(%1,%2,%3); font-size: 12pt; }")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue());
        m_rightHz->setStyleSheet(style);
        QString styleSmall = QString("QLabel { color: rgb(%1,%2,%3); font-size: 10pt; }")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue());
        if (m_rightCents) m_rightCents->setStyleSheet(styleSmall);
    }
}

void WaveVisualizer::setGuitarCentsText(const QString& text) {
    if (m_leftCents) m_leftCents->setText(text);
}

void WaveVisualizer::setVoiceCentsText(const QString& text) {
    if (m_rightCents) m_rightCents->setText(text);
}

