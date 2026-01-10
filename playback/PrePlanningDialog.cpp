#include "playback/PrePlanningDialog.h"

#include <QGraphicsDropShadowEffect>
#include <QApplication>
#include <QCloseEvent>
#include <QScreen>

namespace playback {

PrePlanningDialog::PrePlanningDialog(QWidget* parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFixedSize(420, 200);
    
    setupUI();
    
    // Timer to update elapsed time display
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &PrePlanningDialog::updateElapsedTime);
}

void PrePlanningDialog::setupUI() {
    // Main container with rounded corners and dark theme
    QWidget* container = new QWidget(this);
    container->setObjectName("prePlanContainer");
    container->setStyleSheet(R"(
        #prePlanContainer {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #1a1a2e, stop:0.5 #16213e, stop:1 #0f0f1a);
            border: 1px solid #3a3a5a;
            border-radius: 16px;
        }
    )");
    
    // Add subtle shadow
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(container);
    shadow->setBlurRadius(30);
    shadow->setColor(QColor(0, 0, 0, 180));
    shadow->setOffset(0, 8);
    container->setGraphicsEffect(shadow);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->addWidget(container);
    
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);
    
    // Title (song name)
    m_titleLabel = new QLabel("Preparing Performance", container);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(R"(
        QLabel {
            color: #e8e8ff;
            font-size: 16pt;
            font-weight: 600;
            font-family: 'SF Pro Display', 'Helvetica Neue', sans-serif;
        }
    )");
    layout->addWidget(m_titleLabel);
    
    // Phase indicator
    m_phaseLabel = new QLabel("Phase 1/2: Analyzing Harmony", container);
    m_phaseLabel->setAlignment(Qt::AlignCenter);
    m_phaseLabel->setStyleSheet(R"(
        QLabel {
            color: #7a9fff;
            font-size: 11pt;
            font-weight: 500;
        }
    )");
    layout->addWidget(m_phaseLabel);
    
    layout->addSpacing(8);
    
    // Overall progress bar
    m_overallProgress = new QProgressBar(container);
    m_overallProgress->setRange(0, 100);
    m_overallProgress->setValue(0);
    m_overallProgress->setTextVisible(false);
    m_overallProgress->setFixedHeight(8);
    m_overallProgress->setStyleSheet(R"(
        QProgressBar {
            background: #1a1a3a;
            border: none;
            border-radius: 4px;
        }
        QProgressBar::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #4a6cf7, stop:0.5 #7a5af8, stop:1 #a855f7);
            border-radius: 4px;
        }
    )");
    layout->addWidget(m_overallProgress);
    
    // Phase-specific progress bar (smaller)
    m_phaseProgress = new QProgressBar(container);
    m_phaseProgress->setRange(0, 100);
    m_phaseProgress->setValue(0);
    m_phaseProgress->setTextVisible(false);
    m_phaseProgress->setFixedHeight(4);
    m_phaseProgress->setStyleSheet(R"(
        QProgressBar {
            background: #1a1a3a;
            border: none;
            border-radius: 2px;
        }
        QProgressBar::chunk {
            background: #5a7aff;
            border-radius: 2px;
        }
    )");
    layout->addWidget(m_phaseProgress);
    
    layout->addSpacing(6);
    
    // Status text
    m_statusLabel = new QLabel("Initializing...", container);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(R"(
        QLabel {
            color: #8a8aaa;
            font-size: 10pt;
        }
    )");
    layout->addWidget(m_statusLabel);
    
    // Elapsed time
    m_elapsedLabel = new QLabel("0.0s", container);
    m_elapsedLabel->setAlignment(Qt::AlignCenter);
    m_elapsedLabel->setStyleSheet(R"(
        QLabel {
            color: #5a5a7a;
            font-size: 9pt;
        }
    )");
    layout->addWidget(m_elapsedLabel);
}

void PrePlanningDialog::start(const QString& songTitle) {
    m_completed = false;
    m_currentPhase = -1;  // Not started
    
    m_titleLabel->setText(songTitle.isEmpty() ? "Preparing Performance" : songTitle);
    m_phaseLabel->setText("Initializing...");
    m_statusLabel->setText("Preparing virtual musicians...");
    m_overallProgress->setValue(0);
    m_phaseProgress->setValue(0);
    
    m_elapsedTimer.start();
    m_updateTimer->start(100); // Update elapsed time every 100ms
    
    // Show first, then center (geometry not valid until shown)
    show();
    raise();
    
    // Center on screen
    if (QScreen* screen = QApplication::primaryScreen()) {
        QRect screenGeom = screen->availableGeometry();
        move(screenGeom.center().x() - width() / 2, 
             screenGeom.center().y() - height() / 2);
    }
    
    QApplication::processEvents();
}

void PrePlanningDialog::updateProgress(int phase, double progress01, const QString& statusText) {
    if (m_completed) return;
    
    // Update phase label when phase changes
    if (phase != m_currentPhase) {
        m_currentPhase = phase;
        if (phase == 0) {
            m_phaseLabel->setText("Phase 1/2: Analyzing Harmony");
        } else if (phase == 1) {
            m_phaseLabel->setText("Phase 2/2: Generating Performances");
        }
    }
    
    // Phase progress (within current phase) - shown on the smaller bar
    m_phaseProgress->setValue(qBound(0, int(progress01 * 100), 100));
    
    // Overall progress: Phase 1 = 0-40%, Phase 2 = 40-100%
    double overallProgress = 0.0;
    if (phase == 0) {
        // Context building: 0-40%
        overallProgress = progress01 * 0.40;
    } else if (phase == 1) {
        // Branch building: 40-100%
        // Since branches run in parallel, progress01 represents the progress of any branch
        // which approximates overall Phase 2 progress
        overallProgress = 0.40 + (progress01 * 0.60);
    }
    
    // Only update overall progress if it's increasing (avoid jumping backwards with parallel threads)
    int newOverall = qBound(0, int(overallProgress * 100), 100);
    if (newOverall > m_overallProgress->value()) {
        m_overallProgress->setValue(newOverall);
    }
    
    m_statusLabel->setText(statusText);
    
    QApplication::processEvents();
}

void PrePlanningDialog::complete() {
    m_completed = true;
    m_updateTimer->stop();
    
    m_overallProgress->setValue(100);
    m_phaseProgress->setValue(100);
    m_phaseLabel->setText("Complete!");
    m_statusLabel->setText("Ready to perform");
    
    // Update elapsed time one final time
    updateElapsedTime();
    
    QApplication::processEvents();
    
    // Auto-close after a brief moment
    QTimer::singleShot(400, this, [this]() {
        accept();
    });
}

void PrePlanningDialog::updateElapsedTime() {
    if (!m_elapsedTimer.isValid()) return;
    
    double elapsed = m_elapsedTimer.elapsed() / 1000.0;
    m_elapsedLabel->setText(QString("%1s").arg(elapsed, 0, 'f', 1));
}

void PrePlanningDialog::closeEvent(QCloseEvent* event) {
    if (!m_completed) {
        // User closed dialog before completion - emit cancelled
        emit cancelled();
    }
    event->accept();
}

} // namespace playback
