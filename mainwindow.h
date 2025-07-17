#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <vector>
#include <map>
#include "midiprocessor.h"

// Forward declare Qt classes to reduce compile times
QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QPushButton;
class QCheckBox;
class QTextEdit;
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Slots are functions that can be connected to signals.
    void updateProgramUI(int newProgramIndex);
    void updateTrackUI(const std::string& trackId, bool newState);
    void logToConsole(const std::string& message);

private:
    void createWidgets();
    void createLayout();
    void createConnections();

    // MIDI Logic handler
    MidiProcessor* midiProcessor;

    // UI Widgets
    QWidget* centralWidget;
    QVBoxLayout* mainLayout;
    std::vector<QPushButton*> programButtons;
    std::map<std::string, QCheckBox*> trackCheckBoxes;
    QTextEdit* logConsole;
};

#endif // MAINWINDOW_H