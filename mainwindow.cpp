#include "mainwindow.h"
#include <QtWidgets>

MainWindow::MainWindow(const Preset& preset, QWidget *parent) 
    : QMainWindow(parent) {

    // MainWindow now owns MidiProcessor
    m_midiProcessor = new MidiProcessor(preset, this);

    createWidgets(preset);
    createLayout();
    createConnections();

    setWindowTitle(preset.name);
    
    // Initialize the processor after the UI is ready to receive signals
    if (!m_midiProcessor->initialize()) {
        QMessageBox::critical(this, "MIDI Error", "Could not initialize MIDI ports. Please check connections and port names in preset.xml.");
    }
}

MainWindow::~MainWindow() {
    // Qt's parent-child ownership will automatically delete m_midiProcessor
}

void MainWindow::createWidgets(const Preset& preset) {
    centralWidget = new QWidget;
    setCentralWidget(centralWidget);

    // Dynamically create program buttons from preset data
    for (const auto& program : preset.programs) {
        QPushButton* button = new QPushButton(program.name);
        button->setCheckable(true);
        programButtons.push_back(button);
    }

    // Dynamically create track checkboxes from preset data
    for (const auto& toggle : preset.toggles) {
        trackCheckBoxes[toggle.id.toStdString()] = new QCheckBox(toggle.name);
    }

    // Log Console
    logConsole = new QTextEdit;
    logConsole->setReadOnly(true);
}

void MainWindow::createLayout() {
    mainLayout = new QVBoxLayout(centralWidget);

    QGroupBox *programBox = new QGroupBox("Programs");
    QVBoxLayout *programLayout = new QVBoxLayout;
    for (auto button : programButtons) {
        programLayout->addWidget(button);
    }
    programBox->setLayout(programLayout);
    mainLayout->addWidget(programBox);

    QGroupBox *trackBox = new QGroupBox("Track Toggles");
    QVBoxLayout *trackLayout = new QVBoxLayout;
    // Iterate through the map in a defined order for consistent layout
    std::vector<std::string> sorted_keys;
    for(auto const& [key, val] : trackCheckBoxes) {
        sorted_keys.push_back(key);
    }
    std::sort(sorted_keys.begin(), sorted_keys.end());
    for(const auto& key : sorted_keys) {
        trackLayout->addWidget(trackCheckBoxes.at(key));
    }
    trackBox->setLayout(trackLayout);
    mainLayout->addWidget(trackBox);

    QGroupBox *consoleBox = new QGroupBox("Debug Console");
    QVBoxLayout *consoleLayout = new QVBoxLayout;
    consoleLayout->addWidget(logConsole);
    consoleBox->setLayout(consoleLayout);
    mainLayout->addWidget(consoleBox, 1);
}

void MainWindow::createConnections() {
    for (size_t i = 0; i < programButtons.size(); ++i) {
        connect(programButtons[i], &QPushButton::clicked, this, [this, i]() {
            m_midiProcessor->applyProgram(i);
        });
    }

    for(auto const& [key, checkbox] : trackCheckBoxes) {
        connect(checkbox, &QCheckBox::clicked, this, [this, key](){
            m_midiProcessor->toggleTrack(key);
        });
    }

    connect(m_midiProcessor, &MidiProcessor::programChanged, this, &MainWindow::updateProgramUI);
    connect(m_midiProcessor, &MidiProcessor::trackStateUpdated, this, &MainWindow::updateTrackUI);
    connect(m_midiProcessor, &MidiProcessor::logMessage, this, &MainWindow::logToConsole);
}

void MainWindow::updateProgramUI(int newProgramIndex) {
    for (size_t i = 0; i < programButtons.size(); ++i) {
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