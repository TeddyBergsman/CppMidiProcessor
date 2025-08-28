#include "mainwindow.h"
#include <QtWidgets>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <QTimer>

MainWindow::MainWindow(const Preset& preset, QWidget *parent) 
    : QMainWindow(parent) {

    // MainWindow now owns MidiProcessor
    m_midiProcessor = new MidiProcessor(preset, this);
    
    // Initialize voice controller
    m_voiceController = new VoiceController(preset, this);
    
    // Initialize transcription timer
    voiceTranscriptionTimer = new QTimer(this);
    voiceTranscriptionTimer->setSingleShot(true);

    createWidgets(preset);
    createLayout();
    createConnections();

    setWindowTitle(preset.name);
    
    // Initialize the processor after the UI is ready to receive signals
    if (!m_midiProcessor->initialize()) {
        QMessageBox::critical(this, "MIDI Error", "Could not initialize MIDI ports. Please check connections and port names in preset.xml.");
    }
    
    // Start voice controller if enabled
    if (preset.settings.voiceControlEnabled) {
        m_voiceController->start();
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
    
    // Voice Control Widgets
    voiceControlBox = new QGroupBox("Voice Control");
    voiceControlCheckBox = new QCheckBox("Enable Voice Control");
    voiceControlCheckBox->setChecked(preset.settings.voiceControlEnabled);
    
    voiceStatusLabel = new QLabel("Status: Disconnected");
    voiceStatusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
    
    voiceTranscriptionLabel = new QLabel("");
    voiceTranscriptionLabel->setWordWrap(true);
    voiceTranscriptionLabel->setMinimumHeight(50);
    voiceTranscriptionLabel->setTextFormat(Qt::RichText);
    voiceTranscriptionLabel->setStyleSheet("QLabel { background-color: black; color: white; padding: 10px; border-radius: 5px; font-family: monospace; }");
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
    
    // Voice control layout
    QVBoxLayout* voiceControlLayout = new QVBoxLayout;
    QHBoxLayout* voiceControlHeaderLayout = new QHBoxLayout;
    voiceControlHeaderLayout->addWidget(voiceControlCheckBox);
    voiceControlHeaderLayout->addWidget(voiceStatusLabel);
    voiceControlHeaderLayout->addStretch();
    voiceControlLayout->addLayout(voiceControlHeaderLayout);
    voiceControlLayout->addWidget(voiceTranscriptionLabel);
    voiceControlBox->setLayout(voiceControlLayout);
    mainLayout->addWidget(voiceControlBox);

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
    
    // Voice control connections
    connect(voiceControlCheckBox, &QCheckBox::toggled, this, &MainWindow::onVoiceControlToggled);
    connect(m_voiceController, &VoiceController::transcriptionReceived, this, &MainWindow::onTranscriptionReceived);
    connect(m_voiceController, &VoiceController::connectionStatusChanged, this, &MainWindow::onVoiceConnectionStatusChanged);
    connect(m_voiceController, &VoiceController::errorOccurred, this, [this](const QString& error) {
        logToConsole("Voice Control Error: " + error);
    });
    connect(m_voiceController, &VoiceController::programCommandDetected, m_midiProcessor, &MidiProcessor::applyProgram);
    connect(m_voiceController, &VoiceController::trackCommandDetected, this, [this](int trackIndex, bool play) {
        if (play && trackIndex >= 0) {
            m_midiProcessor->playTrack(trackIndex);
        } else if (!play) {
            m_midiProcessor->pauseTrack();
        }
    });
    
    // Timer to clear transcription after 5 seconds
    connect(voiceTranscriptionTimer, &QTimer::timeout, this, [this]() {
        voiceTranscriptionLabel->clear();
    });
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

void MainWindow::onVoiceControlToggled(bool checked) {
    m_voiceController->setEnabled(checked);
    m_midiProcessor->setVoiceControlEnabled(checked);
    
    if (checked && !m_voiceController->isConnected()) {
        m_voiceController->start();
    }
}

void MainWindow::onTranscriptionReceived(const QString& text, double confidence, const QStringList& detectedCommands) {
    // Log the transcription to debug console
    QString logMsg = QString("Voice: \"%1\" (confidence: %2)").arg(text).arg(confidence, 0, 'f', 2);
    if (!detectedCommands.isEmpty()) {
        logMsg += " - Commands: " + detectedCommands.join(", ");
    }
    logToConsole(logMsg);
    
    QString formattedText = formatTranscriptionWithBoldTriggers(text, detectedCommands);
    voiceTranscriptionLabel->setText(formattedText);
    
    // Restart the timer to clear after 5 seconds
    voiceTranscriptionTimer->stop();
    voiceTranscriptionTimer->start(5000);
}

void MainWindow::onVoiceConnectionStatusChanged(bool connected) {
    if (connected) {
        voiceStatusLabel->setText("Status: Connected");
        voiceStatusLabel->setStyleSheet("QLabel { color: green; font-weight: bold; }");
    } else {
        voiceStatusLabel->setText("Status: Disconnected");
        voiceStatusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
    }
}

QString MainWindow::formatTranscriptionWithBoldTriggers(const QString& text, const QStringList& triggers) {
    QString formattedText = text;
    
    // Escape HTML special characters
    formattedText = formattedText.toHtmlEscaped();
    
    // Make trigger words bold with yellow color
    for (const QString& trigger : triggers) {
        QString pattern = "\\b" + QRegularExpression::escape(trigger) + "\\b";
        QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        formattedText.replace(re, "<b style='color: yellow;'>" + trigger + "</b>");
    }
    
    return formattedText;
}