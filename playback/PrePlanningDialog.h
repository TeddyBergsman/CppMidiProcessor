#pragma once

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>
#include <QElapsedTimer>

namespace playback {

/**
 * A sleek popup dialog shown during pre-planning of the song.
 * Displays progress for both phases:
 *   - Phase 1: Building harmonic context (single-threaded)
 *   - Phase 2: Building energy branches (parallel)
 */
class PrePlanningDialog : public QDialog {
    Q_OBJECT
public:
    explicit PrePlanningDialog(QWidget* parent = nullptr);
    
    // Start showing the dialog with the song title
    void start(const QString& songTitle);
    
    // Update progress (called from engine's progress callback)
    // phase: 0 = context, 1 = branches, 2 = complete
    // progress01: 0.0 to 1.0 within current phase
    // statusText: current operation description
    void updateProgress(int phase, double progress01, const QString& statusText);
    
    // Mark as complete and auto-close after a short delay
    void complete();
    
signals:
    void cancelled();
    
protected:
    void closeEvent(QCloseEvent* event) override;
    
private:
    void setupUI();
    void updateElapsedTime();
    
    QLabel* m_titleLabel = nullptr;
    QLabel* m_phaseLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_elapsedLabel = nullptr;
    QProgressBar* m_overallProgress = nullptr;
    QProgressBar* m_phaseProgress = nullptr;
    
    QElapsedTimer m_elapsedTimer;
    QTimer* m_updateTimer = nullptr;
    
    int m_currentPhase = 0;
    bool m_completed = false;
};

} // namespace playback
