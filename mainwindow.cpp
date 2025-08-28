#include "mainwindow.h"
#include <QtWidgets>
#include <QFileInfo>
#include <algorithm>

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
        QCheckBox* checkbox = new QCheckBox(toggle.name);
        // FIX: Initialize checkboxes to checked to match the processor's initial state.
        checkbox->setChecked(true); 
        trackCheckBoxes[toggle.id.toStdString()] = checkbox;
    }

    // Log Console
    logConsole = new QTextEdit;
    logConsole->setReadOnly(true);

    // Verbose Log Checkbox
    verboseLogCheckBox = new QCheckBox("Verbose Pitch-Bend Logging");
    verboseLogCheckBox->setChecked(false);

    // NEW: Backing Track Widgets
    backingTrackBox = new QGroupBox("Backing Tracks");
    backingTrackList = new QListWidget;
    playButton = new QPushButton("Play");
    pauseButton = new QPushButton("Pause");
    playButton->setEnabled(false);
    pauseButton->setEnabled(false);
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

    // NEW: Backing track layout
    QVBoxLayout* backingTrackLayout = new QVBoxLayout;
    QHBoxLayout* backingTrackButtonsLayout = new QHBoxLayout;
    backingTrackButtonsLayout->addWidget(playButton);
    backingTrackButtonsLayout->addWidget(pauseButton);
    backingTrackLayout->addWidget(backingTrackList);
    backingTrackLayout->addLayout(backingTrackButtonsLayout);
    backingTrackBox->setLayout(backingTrackLayout);
    mainLayout->addWidget(backingTrackBox);

    QGroupBox *consoleBox = new QGroupBox("Debug Console");
    QVBoxLayout *consoleLayout = new QVBoxLayout;
    consoleLayout->addWidget(verboseLogCheckBox);
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
        // FIX: Use C++14 init-capture to be fully C++17 compliant and remove warning.
        connect(checkbox, &QCheckBox::clicked, this, [this, trackId = key](){
            m_midiProcessor->toggleTrack(trackId);
        });
    }

    // Connect processor signals to UI slots
    connect(m_midiProcessor, &MidiProcessor::programChanged, this, &MainWindow::updateProgramUI);
    connect(m_midiProcessor, &MidiProcessor::trackStateUpdated, this, &MainWindow::updateTrackUI);
    
    // FIX: This connection now works because the signal and slot signatures match.
    connect(m_midiProcessor, &MidiProcessor::logMessage, this, &MainWindow::logToConsole);
    
    connect(verboseLogCheckBox, &QCheckBox::toggled, this, &MainWindow::onVerboseLogToggled);

    // NEW: Connect backing track signals and slots
    connect(m_midiProcessor, &MidiProcessor::backingTracksLoaded, this, &MainWindow::onBackingTracksLoaded);
    connect(m_midiProcessor, &MidiProcessor::backingTrackStateChanged, this, &MainWindow::onBackingTrackStateChanged);
    connect(playButton, &QPushButton::clicked, this, &MainWindow::onPlayClicked);
    connect(pauseButton, &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    connect(backingTrackList, &QListWidget::currentItemChanged, this, [this](){ playButton->setEnabled(true); });
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

// FIX: Signature updated and body simplified.
void MainWindow::logToConsole(const QString& message) {
    logConsole->append(message);
}

void MainWindow::onVerboseLogToggled(bool checked) {
    m_midiProcessor->setVerbose(checked);
}

void MainWindow::onBackingTracksLoaded(const QStringList& tracks) {
    backingTrackList->clear();
    for (const QString& fullPath : tracks) {
        backingTrackList->addItem(QFileInfo(fullPath).fileName());
    }
}

void MainWindow::onBackingTrackStateChanged(int trackIndex, QMediaPlayer::PlaybackState state) {
    // Reset all item backgrounds
    for (int i = 0; i < backingTrackList->count(); ++i) {
        backingTrackList->item(i)->setBackground(Qt::white);
    }

    if (trackIndex < 0 || trackIndex >= backingTrackList->count()) return;

    if (state == QMediaPlayer::PlayingState) {
        backingTrackList->item(trackIndex)->setBackground(Qt::green);
        playButton->setEnabled(false);
        pauseButton->setEnabled(true);
    } else if (state == QMediaPlayer::PausedState) {
        backingTrackList->item(trackIndex)->setBackground(Qt::yellow);
        playButton->setEnabled(true);
        pauseButton->setEnabled(false);
    } else { // StoppedState
        backingTrackList->item(trackIndex)->setBackground(Qt::white);
        playButton->setEnabled(backingTrackList->currentItem() != nullptr);
        pauseButton->setEnabled(false);
    }
}

void MainWindow::onPlayClicked() {
    m_midiProcessor->playTrack(backingTrackList->currentRow());
}

void MainWindow::onPauseClicked() {
    m_midiProcessor->pauseTrack();
}