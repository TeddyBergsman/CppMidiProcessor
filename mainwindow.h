#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <vector>
#include <map>
#include <string>
#include "midiprocessor.h"
#include "PresetData.h"
#include "voicecontroller.h"

// Forward declare Qt classes
QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QPushButton;
class QListWidget;
class QCheckBox;
class QTextEdit;
class QGroupBox;
class QLabel;
class QTimer;
class QProgressBar;
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const Preset& preset, QWidget *parent = nullptr);
    ~MainWindow();
    
    // Public method to toggle transpose checkbox (for voice control)
    void toggleTranspose();

private slots:
    void updateProgramUI(int newProgramIndex);
    void updateTrackUI(const std::string& trackId, bool newState);
    // FIX: Changed signature to match the signal's type
    void logToConsole(const QString& message); 
    void onVerboseLogToggled(bool checked);
    void onBackingTracksLoaded(const QStringList& tracks);
    void onBackingTrackStateChanged(int trackIndex, QMediaPlayer::PlaybackState state);
    void onTransportClicked();
    void onTrackPositionChanged(qint64 positionMs);
    void onTrackDurationChanged(qint64 durationMs);
    
    // Voice control slots
    void onVoiceControlToggled(bool checked);
    void onTranscriptionReceived(const QString& text, double confidence, const QStringList& detectedTriggers, const QStringList& detectedTargets);
    void onVoiceConnectionStatusChanged(bool connected);
    
    // Transpose slot
    void onTransposeToggled(bool checked);

private:
    void createWidgets(const Preset& preset);
    void createLayout();
    void createConnections();
    QString formatTranscriptionWithColors(const QString& text, const QStringList& triggers, const QStringList& targets);

    // MIDI Logic handler (owned by MainWindow)
    MidiProcessor* m_midiProcessor;
    
    // Voice control
    VoiceController* m_voiceController;

    // UI Widgets
    QWidget* centralWidget;
    QVBoxLayout* mainLayout;
    std::vector<QPushButton*> programButtons;
    std::map<std::string, QCheckBox*> trackCheckBoxes;
    QTextEdit* logConsole;
    QCheckBox* verboseLogCheckBox;

    // NEW: Backing track UI
    QGroupBox* backingTrackBox;
    QListWidget* backingTrackList;
    // Top timeline bar
    QPushButton* transportButton;
    QProgressBar* timelineBar;
    QLabel* timeRemainingLabel;
    qint64 m_trackDurationMs = 0;
    qint64 m_trackPositionMs = 0;
    int m_currentTrackIndex = -1;
    QMediaPlayer::PlaybackState m_currentPlaybackState = QMediaPlayer::StoppedState;
    
    // Voice control UI
    QGroupBox* voiceControlBox;
    QCheckBox* voiceControlCheckBox;
    QLabel* voiceTranscriptionLabel;
    QLabel* voiceStatusLabel;
    QTimer* voiceTranscriptionTimer;
    
    // Transpose control
    QCheckBox* transposeCheckBox;
};

#endif // MAINWINDOW_H