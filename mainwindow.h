#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <vector>
#include <map>
#include <string>
#include "midiprocessor.h"
#include "PresetData.h"

// Forward declare Qt classes
QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QPushButton;
class QListWidget;
class QCheckBox;
class QTextEdit;
class QGroupBox;
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const Preset& preset, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void updateProgramUI(int newProgramIndex);
    void updateTrackUI(const std::string& trackId, bool newState);
    // FIX: Changed signature to match the signal's type
    void logToConsole(const QString& message); 
    void onVerboseLogToggled(bool checked);
    void onBackingTracksLoaded(const QStringList& tracks);
    void onBackingTrackStateChanged(int trackIndex, QMediaPlayer::PlaybackState state);
    void onPlayClicked();
    void onPauseClicked();

private:
    void createWidgets(const Preset& preset);
    void createLayout();
    void createConnections();

    // MIDI Logic handler (owned by MainWindow)
    MidiProcessor* m_midiProcessor;

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
    QPushButton* playButton;
    QPushButton* pauseButton;
};

#endif // MAINWINDOW_H