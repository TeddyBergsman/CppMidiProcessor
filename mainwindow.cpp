#include "mainwindow.h"
#include <QtWidgets>
#include "midiprocessor.h"


MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    midiProcessor = new MidiProcessor(this);

    createWidgets();
    createLayout();
    createConnections();

    setWindowTitle("C++ MIDI Processor");
    
    // Start the MIDI processing
    if (!midiProcessor->initialize()) {
        // If it fails, show an error message
        QMessageBox::critical(this, "MIDI Error", "Could not initialize MIDI ports. Please check connections and port names in midiprocessor.cpp.");
    }
}

MainWindow::~MainWindow() {
    // No need to delete child widgets, Qt handles it.
}

void MainWindow::createWidgets() {
    centralWidget = new QWidget;
    setCentralWidget(centralWidget);

    // Program Buttons
    for (const auto& config : programConfigs) {
        programButtons.push_back(new QPushButton(QString::fromStdString(config.name)));
        programButtons.back()->setCheckable(true); // Make the button checkable
    }

    // Track Checkboxes
    trackCheckBoxes["track1"] = new QCheckBox("Track 1: Chromatic Guitar");
    trackCheckBoxes["track2"] = new QCheckBox("Track 2: Guitar");
    trackCheckBoxes["track3"] = new QCheckBox("Track 3: Voice");

    // Log Console
    logConsole = new QTextEdit;
    logConsole->setReadOnly(true);
}

void MainWindow::createLayout() {
    mainLayout = new QVBoxLayout(centralWidget);

    // Programs Box
    QGroupBox *programBox = new QGroupBox("Programs");
    QVBoxLayout *programLayout = new QVBoxLayout;
    for (auto button : programButtons) {
        programLayout->addWidget(button);
    }
    programBox->setLayout(programLayout);
    mainLayout->addWidget(programBox);

    // Tracks Box
    QGroupBox *trackBox = new QGroupBox("Track Toggles");
    QVBoxLayout *trackLayout = new QVBoxLayout;
    for(auto const& [key, val] : trackCheckBoxes) {
        trackLayout->addWidget(val);
    }
    trackBox->setLayout(trackLayout);
    mainLayout->addWidget(trackBox);

    // Console Box
    QGroupBox *consoleBox = new QGroupBox("Debug Console");
    QVBoxLayout *consoleLayout = new QVBoxLayout;
    consoleLayout->addWidget(logConsole);
    consoleBox->setLayout(consoleLayout);
    mainLayout->addWidget(consoleBox, 1); // The '1' makes it stretch
}

void MainWindow::createConnections() {
    // Connect GUI actions to MIDI processor slots
    for (int i = 0; i < programButtons.size(); ++i) {
        connect(programButtons[i], &QPushButton::clicked, this, [this, i]() {
            midiProcessor->applyProgram(i);
        });
    }

    for(auto const& [key, checkbox] : trackCheckBoxes) {
        connect(checkbox, &QCheckBox::clicked, this, [this, key](){
            midiProcessor->toggleTrack(key);
        });
    }

    // Connect MIDI processor signals to GUI update slots
    connect(midiProcessor, &MidiProcessor::programChanged, this, &MainWindow::updateProgramUI);
    connect(midiProcessor, &MidiProcessor::trackStateUpdated, this, &MainWindow::updateTrackUI);
    connect(midiProcessor, &MidiProcessor::logMessage, this, &MainWindow::logToConsole);
}

// --- UI Update Slots ---
void MainWindow::updateProgramUI(int newProgramIndex) {
    for (int i = 0; i < programButtons.size(); ++i) {
        programButtons[i]->setChecked(i == newProgramIndex);
    }
}

void MainWindow::updateTrackUI(const std::string& trackId, bool newState) {
    if (trackCheckBoxes.count(trackId)) {
        trackCheckBoxes[trackId]->setChecked(newState);
    }
}

void MainWindow::logToConsole(const std::string& message) {
    logConsole->append(QString::fromStdString(message));
}