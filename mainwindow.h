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
    void logToConsole(const std::string& message);

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
};

#endif // MAINWINDOW_H