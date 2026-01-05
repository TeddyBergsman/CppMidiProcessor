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
class QHBoxLayout;
class QStackedWidget;
QT_END_NAMESPACE

class NoteMonitorWidget;
class LibraryWindow;

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
    void onTimelineDataReceived(const QString& timelineJson);
    void onSectionClicked(qint64 timeMs);
    
    // Voice control slots
    void onVoiceControlToggled(bool checked);
    void onTranscriptionReceived(const QString& text, double confidence, const QStringList& detectedTriggers, const QStringList& detectedTargets);
    void onVoiceConnectionStatusChanged(bool connected);
    
    // Transpose slot
    void onTransposeToggled(bool checked);
    void openPreferences();
    void applyLegacyUiSetting(bool legacyOn);
    void openIRealHtml();

private:
    void createWidgets(const Preset& preset);
    void createLayout();
    void createConnections();
    QString formatTranscriptionWithColors(const QString& text, const QStringList& triggers, const QStringList& targets);
    void rebuildSectionMarkers();
    bool loadIRealHtmlFile(const QString& path, bool showErrors);
    
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

    // MIDI Logic handler (owned by MainWindow)
    MidiProcessor* m_midiProcessor;
    
    // Voice control
    VoiceController* m_voiceController;

    // UI Widgets
    QStackedWidget* rootStack = nullptr;
    QWidget* centralWidget;
    QVBoxLayout* mainLayout;
    std::vector<QPushButton*> programButtons;
    std::vector<std::string> programNames;
    std::map<std::string, QCheckBox*> trackCheckBoxes;
    QTextEdit* logConsole;
    QCheckBox* verboseLogCheckBox;
    NoteMonitorWidget* noteMonitorWidget = nullptr;

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
    
    // Section markers bar
    QWidget* sectionBarWidget = nullptr;
    QHBoxLayout* sectionBarLayout = nullptr;
    QVector<QPushButton*> m_sectionButtons;  // Changed to vector to track order
    
    // 4-bar window widgets
    QWidget* fourBarWidget = nullptr;
    QLabel* fourBarRangeLabel = nullptr;
    QWidget* fourBarContainer = nullptr;
    QHBoxLayout* fourBarLayout = nullptr;
    int m_barWindowSize = 4;
    int m_currentBarWindowStart = 0;
    
    // Lyrics widgets
    QWidget* lyricsWidget = nullptr;
    QLabel* currentLyricLabel = nullptr;
    QProgressBar* currentLyricProgress = nullptr;
    QLabel* nextLyricLabel = nullptr;
    
    // Timeline data structures
    struct BarMarkerUI { double bar; qint64 timeMs; };
    struct SectionMarkerUI { QString label; qint64 timeMs; double bar; };
    struct ChordEventUI { double bar; QString chord; };
    struct ProgramChangeUI { double bar; QString programName; };
    struct TransposeToggleUI { double bar; bool on; };
    struct LyricLineUI { double startBar; double endBar; QString text; };
    struct TempoChangeUI { double bar; int bpm; };
    struct TimeSignatureUI { double bar; int numerator; int denominator; };
    
    QVector<BarMarkerUI> m_barMarkers;
    QVector<SectionMarkerUI> m_sections;
    QVector<ChordEventUI> m_chords;
    QVector<ProgramChangeUI> m_programChanges;
    QVector<TransposeToggleUI> m_transposeToggles;
    QVector<LyricLineUI> m_lyrics;
    QVector<TempoChangeUI> m_tempoChanges;
    QVector<TimeSignatureUI> m_timeSignatureChanges;
    
    // Helper functions
    double timeMsToBar(qint64 ms) const;
    qint64 barToTimeMs(double bar) const;
    void updateFourBarWindow(double currentBar);
    void updateLyrics(double currentBar);
    
    // Voice control UI
    QGroupBox* voiceControlBox;
    QCheckBox* voiceControlCheckBox;
    QLabel* voiceTranscriptionLabel;
    QLabel* voiceStatusLabel;
    QTimer* voiceTranscriptionTimer;
    
    // Transpose control
    QCheckBox* transposeCheckBox;

    // Library Visualizer window (Window → Library)
    LibraryWindow* m_libraryWindow = nullptr;
};

#endif // MAINWINDOW_H