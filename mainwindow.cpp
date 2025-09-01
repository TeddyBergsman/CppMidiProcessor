#include "mainwindow.h"
#include <QtWidgets>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

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
        programNames.push_back(program.name.toStdString());
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
    
    // Set dark theme for the backing track list
    backingTrackList->setStyleSheet(
        "QListWidget {"
        "    background-color: black;"
        "    color: white;"
        "    font-family: Consolas, Monaco, monospace;"
        "    font-size: 10pt;"
        "    border: 1px solid #333;"
        "}"
        "QListWidget::item {"
        "    padding: 5px;"
        "    border-bottom: 1px solid #222;"
        "}"
        "QListWidget::item:selected {"
        "    background-color: #2a2a2a;"  // Faint dark gray
        "}"
        "QListWidget::item:hover {"
        "    background-color: #1a1a1a;"
        "}"
    );
    
    // Top timeline bar controls
    transportButton = new QPushButton;
    transportButton->setEnabled(false);
    transportButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    transportButton->setIconSize(QSize(20, 20));
    transportButton->setFlat(false);
    transportButton->setFixedSize(30, 30);
    
    timelineBar = new QProgressBar;
    timelineBar->setTextVisible(false);
    timelineBar->setRange(0, 0);
    timelineBar->setValue(0);
    timelineBar->setMinimumHeight(12);
    
    timeRemainingLabel = new QLabel("--:--");
    timeRemainingLabel->setMinimumWidth(60);
    timeRemainingLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    timeRemainingLabel->setStyleSheet("QLabel { padding-right: 5px; }");
    
    // Section bar widget - overlay on timeline
    sectionBarWidget = new QWidget;
    sectionBarWidget->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    sectionBarLayout = new QHBoxLayout(sectionBarWidget);
    sectionBarLayout->setContentsMargins(0, 0, 0, 0);
    sectionBarLayout->setSpacing(0);
    
    // 4-bar window widgets
    fourBarWidget = new QWidget;
    fourBarWidget->setStyleSheet("QWidget { background-color: #2a2a2a; border-radius: 4px; padding: 0px; }");
    // Keep consistent height whether populated or empty to avoid layout popping
    fourBarWidget->setFixedHeight(80);
    
    fourBarRangeLabel = new QLabel("");  // Removed label
    fourBarRangeLabel->setVisible(false);
    
    // Container for bar cells
    fourBarContainer = new QWidget;
    fourBarContainer->setStyleSheet("QWidget { background-color: transparent; }");
    fourBarLayout = new QHBoxLayout(fourBarContainer);
    fourBarLayout->setSpacing(10);
    fourBarLayout->setContentsMargins(0, 0, 0, 0);
    
    // Lyrics widgets
    lyricsWidget = new QWidget;
    lyricsWidget->setStyleSheet("QWidget { background-color: #2a2a2a; padding: 2px; }");
    lyricsWidget->setFixedHeight(38); // Reduced height to be more compact
    
    currentLyricLabel = new QLabel("");
    currentLyricLabel->setWordWrap(true);
    currentLyricLabel->setAlignment(Qt::AlignCenter);
    currentLyricLabel->setStyleSheet("QLabel { color: white; font-size: 14pt; padding: 2px; }");
    
    nextLyricLabel = new QLabel("");
    nextLyricLabel->setWordWrap(true);
    nextLyricLabel->setAlignment(Qt::AlignCenter);
    nextLyricLabel->setStyleSheet("QLabel { color: #666; font-size: 11pt; padding: 1px; }");
    
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
    
    // Transpose checkbox
    transposeCheckBox = new QCheckBox("Transpose");
    transposeCheckBox->setChecked(false); // Default to OFF
}

void MainWindow::createLayout() {
    mainLayout = new QVBoxLayout(centralWidget);
    
    // Create a widget to hold timeline and section markers
    QWidget* timelineWidget = new QWidget;
    timelineWidget->setFixedHeight(60);
    QVBoxLayout* timelineVerticalLayout = new QVBoxLayout(timelineWidget);
    timelineVerticalLayout->setContentsMargins(0, 0, 0, 0);
    timelineVerticalLayout->setSpacing(0);
    
    // Section markers positioned above timeline
    sectionBarWidget->setFixedHeight(30);
    timelineVerticalLayout->addWidget(sectionBarWidget);
    
    // Timeline bar with transport controls
    QHBoxLayout* timelineLayout = new QHBoxLayout;
    timelineLayout->setContentsMargins(5, 0, 5, 0);
    timelineLayout->setSpacing(10);
    timelineLayout->addWidget(transportButton);
    timelineLayout->addWidget(timelineBar, 1);
    timelineLayout->addWidget(timeRemainingLabel);
    
    QWidget* timelineBarWidget = new QWidget;
    timelineBarWidget->setLayout(timelineLayout);
    timelineVerticalLayout->addWidget(timelineBarWidget);
    
    mainLayout->addWidget(timelineWidget);
    
    // 4-bar window layout
    QVBoxLayout* fourBarMainLayout = new QVBoxLayout(fourBarWidget);
    fourBarMainLayout->addWidget(fourBarContainer);
    mainLayout->addWidget(fourBarWidget);
    
    // Lyrics layout
    QVBoxLayout* lyricsLayout = new QVBoxLayout(lyricsWidget);
    lyricsLayout->setContentsMargins(5, 2, 5, 2);
    lyricsLayout->setSpacing(2);
    lyricsLayout->addWidget(currentLyricLabel);
    lyricsLayout->addWidget(nextLyricLabel);
    mainLayout->addWidget(lyricsWidget);
    
    // Create a horizontal layout for the two columns
    QHBoxLayout* columnsLayout = new QHBoxLayout;
    
    // Left column layout
    QVBoxLayout* leftColumnLayout = new QVBoxLayout;
    
    // Programs section
    QGroupBox *programBox = new QGroupBox("Programs");
    QVBoxLayout *programLayout = new QVBoxLayout;
    for (auto button : programButtons) {
        programLayout->addWidget(button);
    }
    programBox->setLayout(programLayout);
    leftColumnLayout->addWidget(programBox);

    // Track Toggles section
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
    leftColumnLayout->addWidget(trackBox);
    
    // Transpose section
    QGroupBox *transposeBox = new QGroupBox("Transpose Control");
    QVBoxLayout *transposeLayout = new QVBoxLayout;
    transposeLayout->addWidget(transposeCheckBox);
    transposeBox->setLayout(transposeLayout);
    leftColumnLayout->addWidget(transposeBox);
    
    leftColumnLayout->addStretch(1); // Add stretch to keep widgets at top

    // Right column layout
    QVBoxLayout* rightColumnLayout = new QVBoxLayout;

    // Backing track layout
    QVBoxLayout* backingTrackLayout = new QVBoxLayout;
    backingTrackLayout->addWidget(backingTrackList);
    backingTrackBox->setLayout(backingTrackLayout);
    rightColumnLayout->addWidget(backingTrackBox);
    
    // Voice control layout
    QVBoxLayout* voiceControlLayout = new QVBoxLayout;
    QHBoxLayout* voiceControlHeaderLayout = new QHBoxLayout;
    voiceControlHeaderLayout->addWidget(voiceControlCheckBox);
    voiceControlHeaderLayout->addWidget(voiceStatusLabel);
    voiceControlHeaderLayout->addStretch();
    voiceControlLayout->addLayout(voiceControlHeaderLayout);
    voiceControlLayout->addWidget(voiceTranscriptionLabel);
    voiceControlBox->setLayout(voiceControlLayout);
    rightColumnLayout->addWidget(voiceControlBox);

    // Debug Console section
    QGroupBox *consoleBox = new QGroupBox("Debug Console");
    QVBoxLayout *consoleLayout = new QVBoxLayout;
    consoleLayout->addWidget(verboseLogCheckBox);
    consoleLayout->addWidget(logConsole);
    consoleBox->setLayout(consoleLayout);
    rightColumnLayout->addWidget(consoleBox, 1);

    // Add both columns to the columns layout
    columnsLayout->addLayout(leftColumnLayout);
    columnsLayout->addLayout(rightColumnLayout);
    
    // Add the columns layout to the main layout
    mainLayout->addLayout(columnsLayout);
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
    connect(m_midiProcessor, &MidiProcessor::backingTrackPositionChanged, this, &MainWindow::onTrackPositionChanged);
    connect(m_midiProcessor, &MidiProcessor::backingTrackDurationChanged, this, &MainWindow::onTrackDurationChanged);
    connect(m_midiProcessor, &MidiProcessor::backingTrackTimelineUpdated, this, &MainWindow::onTimelineDataReceived);
    connect(transportButton, &QPushButton::clicked, this, &MainWindow::onTransportClicked);
    connect(backingTrackList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem* previous){ 
        transportButton->setEnabled(current != nullptr);
        // Only load timeline data if not currently playing
        if (current && m_currentPlaybackState != QMediaPlayer::PlayingState) {
            int index = backingTrackList->row(current);
            m_midiProcessor->loadTrackTimeline(index);
        }
    });
    
    // Make timeline clickable for seeking
    timelineBar->installEventFilter(this);
    
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
    connect(m_voiceController, &VoiceController::toggleCommandDetected, this, [this](const QString& toggleId) {
        if (toggleId.toLower() == "transpose") {
            toggleTranspose();
        }
    });
    
    // Timer to clear transcription after 5 seconds
    connect(voiceTranscriptionTimer, &QTimer::timeout, this, [this]() {
        voiceTranscriptionLabel->clear();
    });
    
    // Transpose checkbox connection
    connect(transposeCheckBox, &QCheckBox::toggled, this, &MainWindow::onTransposeToggled);

    // Inform VoiceController of current program changes so quick switch can use the active program
    connect(m_midiProcessor, &MidiProcessor::programChanged,
            m_voiceController, &VoiceController::onProgramChanged);
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
    m_currentTrackIndex = trackIndex;
    m_currentPlaybackState = state;
    
    // Reset all item backgrounds and fonts to defaults
    for (int i = 0; i < backingTrackList->count(); ++i) {
        backingTrackList->item(i)->setBackground(QBrush());
        QFont font = backingTrackList->item(i)->font();
        font.setBold(false);
        backingTrackList->item(i)->setFont(font);
    }

    if (trackIndex >= 0 && trackIndex < backingTrackList->count()) {
    if (state == QMediaPlayer::PlayingState) {
        backingTrackList->item(trackIndex)->setBackground(QColor(0, 40, 0));
            QFont font = backingTrackList->item(trackIndex)->font();
            font.setBold(true);
            backingTrackList->item(trackIndex)->setFont(font);
            backingTrackList->setCurrentRow(trackIndex);
            transportButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        
            // Reset bar window when starting a new track
            m_currentBarWindowStart = 0;
        } else if (state == QMediaPlayer::PausedState) {
            backingTrackList->item(trackIndex)->setBackground(QColor(40, 60, 0));
        QFont font = backingTrackList->item(trackIndex)->font();
        font.setBold(true);
        backingTrackList->item(trackIndex)->setFont(font);
            transportButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        } else { // Stopped
            backingTrackList->item(trackIndex)->setBackground(QBrush());
            transportButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        }
    }
    
    // Enable/disable transport button
    transportButton->setEnabled(backingTrackList->currentItem() != nullptr);
}

void MainWindow::onTransportClicked() {
    int selected = backingTrackList->currentRow();
    if (selected < 0) return;
    if (m_currentPlaybackState == QMediaPlayer::PlayingState) {
        m_midiProcessor->pauseTrack();
    } else {
        // Always load timeline data when playing to ensure sections are shown
        m_midiProcessor->loadTrackTimeline(selected);
        m_midiProcessor->playTrack(selected);
    }
}

static QString formatMsToMinSec(qint64 ms) {
    if (ms < 0) ms = 0;
    qint64 totalSeconds = ms / 1000;
    qint64 minutes = totalSeconds / 60;
    qint64 seconds = totalSeconds % 60;
    return QString::asprintf("%lld:%02lld", (long long)minutes, (long long)seconds);
}

void MainWindow::onTrackPositionChanged(qint64 positionMs) {
    m_trackPositionMs = positionMs;
    if (m_trackDurationMs > 0) {
        timelineBar->setRange(0, (int)m_trackDurationMs);
        timelineBar->setValue((int)std::min<qint64>(m_trackDurationMs, positionMs));
        qint64 remaining = m_trackDurationMs - positionMs;
        timeRemainingLabel->setText(formatMsToMinSec(remaining));
    } else {
        timelineBar->setRange(0, 0);
        timeRemainingLabel->setText("--:--");
    }
    
    // Update 4-bar window and lyrics based on current position
    double currentBar = timeMsToBar(positionMs);
    updateFourBarWindow(currentBar);
    updateLyrics(currentBar);
    
    // Check for program changes and transpose toggles
    static double lastCheckedBar = -1;
    static int lastTrackIndex = -1;
    
    // Reset when track changes
    if (m_currentTrackIndex != lastTrackIndex) {
        lastCheckedBar = -1;
        lastTrackIndex = m_currentTrackIndex;
    }
    
    if (currentBar > lastCheckedBar) {
        // Check for program changes
        for (const auto& prog : m_programChanges) {
            if (prog.bar > lastCheckedBar && prog.bar <= currentBar) {
                // Find program index by name
                for (size_t i = 0; i < programNames.size(); ++i) {
                    if (QString::fromStdString(programNames[i]) == prog.programName) {
                        m_midiProcessor->applyProgram(i);
                        break;
                    }
                }
            }
        }
        
        // Check for transpose toggles
        for (const auto& trans : m_transposeToggles) {
            if (trans.bar > lastCheckedBar && trans.bar <= currentBar) {
                m_midiProcessor->applyTranspose(trans.on ? 12 : 0);
                bool prev = transposeCheckBox->blockSignals(true);
                transposeCheckBox->setChecked(trans.on);
                transposeCheckBox->blockSignals(prev);
            }
        }
        
        lastCheckedBar = currentBar;
    }
}

void MainWindow::onTrackDurationChanged(qint64 durationMs) {
    m_trackDurationMs = durationMs;
    onTrackPositionChanged(m_trackPositionMs);
    // Ensure section markers are positioned using the correct, current duration
    rebuildSectionMarkers();
}

void MainWindow::onTimelineDataReceived(const QString& timelineJson) {
    // Clear existing data
    m_barMarkers.clear();
    m_sections.clear();
    m_chords.clear();
    m_programChanges.clear();
    m_transposeToggles.clear();
    m_lyrics.clear();
    m_tempoChanges.clear();
    m_timeSignatureChanges.clear();
    
    // Reset duration so we don't position sections using a stale duration
    m_trackDurationMs = 0;
    timelineBar->setRange(0, 0);
    timeRemainingLabel->setText("--:--");
    
    // Parse JSON
    QJsonDocument doc = QJsonDocument::fromJson(timelineJson.toUtf8());
    if (!doc.isObject()) return;
    
    QJsonObject root = doc.object();
    
    // Parse bar markers
    QJsonArray bars = root["bars"].toArray();
    for (const auto& value : bars) {
        QJsonObject obj = value.toObject();
        BarMarkerUI bar;
        bar.bar = obj["bar"].toDouble();
        bar.timeMs = (qint64)obj["timeMs"].toDouble();
        m_barMarkers.append(bar);
    }
    
    // Parse sections
    QJsonArray sections = root["sections"].toArray();
    for (const auto& value : sections) {
        QJsonObject obj = value.toObject();
        SectionMarkerUI section;
        section.label = obj["label"].toString();
        section.timeMs = (qint64)obj["timeMs"].toDouble();
        section.bar = obj["bar"].toDouble();
        m_sections.append(section);
    }
    
    // Parse chords
    QJsonArray chords = root["chords"].toArray();
    for (const auto& value : chords) {
        QJsonObject obj = value.toObject();
        ChordEventUI chord;
        chord.bar = obj["bar"].toDouble();
        chord.chord = obj["chord"].toString();
        m_chords.append(chord);
    }
    
    // Parse program changes
    QJsonArray programs = root["programs"].toArray();
    for (const auto& value : programs) {
        QJsonObject obj = value.toObject();
        ProgramChangeUI prog;
        prog.bar = obj["bar"].toDouble();
        prog.programName = obj["program"].toString();
        m_programChanges.append(prog);
    }
    
    // Parse transpose toggles
    QJsonArray transpose = root["transpose"].toArray();
    for (const auto& value : transpose) {
        QJsonObject obj = value.toObject();
        TransposeToggleUI trans;
        trans.bar = obj["bar"].toDouble();
        trans.on = obj["on"].toBool();
        m_transposeToggles.append(trans);
    }
    
    // Parse lyrics
    QJsonArray lyrics = root["lyrics"].toArray();
    for (const auto& value : lyrics) {
        QJsonObject obj = value.toObject();
        LyricLineUI lyric;
        lyric.startBar = obj["startBar"].toDouble();
        lyric.endBar = obj["endBar"].toDouble();
        lyric.text = obj["text"].toString();
        m_lyrics.append(lyric);
    }
    
    // Parse tempo changes
    QJsonArray tempos = root["tempos"].toArray();
    for (const auto& value : tempos) {
        QJsonObject obj = value.toObject();
        TempoChangeUI tempo;
        tempo.bar = obj["bar"].toDouble();
        tempo.bpm = obj["bpm"].toInt();
        m_tempoChanges.append(tempo);
    }
    
    // Parse time signature changes
    QJsonArray timeSigs = root["timeSignatures"].toArray();
    for (const auto& value : timeSigs) {
        QJsonObject obj = value.toObject();
        TimeSignatureUI ts;
        ts.bar = obj["bar"].toDouble();
        ts.numerator = obj["numerator"].toInt();
        ts.denominator = obj["denominator"].toInt();
        m_timeSignatureChanges.append(ts);
    }
    
    // Get bar window size
    if (root.contains("barWindowSize")) {
        m_barWindowSize = root["barWindowSize"].toInt();
    }
    
    // Reset the window start position
    m_currentBarWindowStart = 0;
    
    // Rebuild section markers UI
    rebuildSectionMarkers();
}

void MainWindow::rebuildSectionMarkers() {
    // Clear existing section buttons
    QLayoutItem* child;
    while ((child = sectionBarLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            delete child->widget();
        }
        delete child;
    }
    m_sectionButtons.clear();
    
    // Create a layout that positions markers relative to the timeline
    // Use fixed margins since transport button has fixed size
    sectionBarLayout->setContentsMargins(45, 0, 65, 0);
    
    // Add spacer for proper alignment
    sectionBarLayout->addStretch();
    
    // Calculate positions for section markers based on timeline
    if (m_trackDurationMs > 0 && !m_sections.isEmpty()) {
        double lastPosition = 0;
        
        for (int i = 0; i < m_sections.size(); ++i) {
            const auto& section = m_sections[i];
            double position = (double)section.timeMs / (double)m_trackDurationMs;
            
            // Add spacer proportional to distance from last marker
            if (i > 0) {
                int stretch = (int)((position - lastPosition) * 100);
                sectionBarLayout->addStretch(stretch);
            }
            
            QPushButton* sectionBtn = new QPushButton(section.label);
            sectionBtn->setFlat(true);
            sectionBtn->setFixedSize(30, 30);
            sectionBtn->setStyleSheet(
                "QPushButton {"
                "    color: white;"
                "    font-weight: bold;"
                "    font-size: 16pt;"
                "    border: none;"
                "    background-color: transparent;"
                "}"
                "QPushButton:hover {"
                "    color: #4a90e2;"
                "}"
            );
            
            // Connect click to seek
            connect(sectionBtn, &QPushButton::clicked, this, [this, timeMs = section.timeMs]() {
                onSectionClicked(timeMs);
            });
            
            // Store for position updates (by index, not label)
            m_sectionButtons.append(sectionBtn);
            
            sectionBarLayout->addWidget(sectionBtn);
            lastPosition = position;
        }
        
        // Add final spacer
        sectionBarLayout->addStretch((int)((1.0 - lastPosition) * 100));
    }
}

void MainWindow::onSectionClicked(qint64 timeMs) {
    if (m_currentPlaybackState != QMediaPlayer::StoppedState) {
        m_midiProcessor->seekToPosition(timeMs);
    }
}

double MainWindow::timeMsToBar(qint64 ms) const {
    if (m_barMarkers.isEmpty()) return 0.0;
    
    // Find the two bar markers that bracket this time
    for (int i = 1; i < m_barMarkers.size(); ++i) {
        if (ms <= m_barMarkers[i].timeMs) {
            // Interpolate between previous and current bar
            const auto& prev = m_barMarkers[i-1];
            const auto& curr = m_barMarkers[i];
            
            double fraction = (double)(ms - prev.timeMs) / (double)(curr.timeMs - prev.timeMs);
            return prev.bar + (curr.bar - prev.bar) * fraction;
        }
    }
    
    // If past the last marker, extrapolate
    if (m_barMarkers.size() >= 2) {
        const auto& prev = m_barMarkers[m_barMarkers.size()-2];
        const auto& last = m_barMarkers.last();
        
        double barsPerMs = (last.bar - prev.bar) / (double)(last.timeMs - prev.timeMs);
        return last.bar + (ms - last.timeMs) * barsPerMs;
    }
    
    return m_barMarkers.last().bar;
}

qint64 MainWindow::barToTimeMs(double bar) const {
    if (m_barMarkers.isEmpty()) return 0;
    
    // Find the two bar markers that bracket this bar
    for (int i = 1; i < m_barMarkers.size(); ++i) {
        if (bar <= m_barMarkers[i].bar) {
            // Interpolate between previous and current time
            const auto& prev = m_barMarkers[i-1];
            const auto& curr = m_barMarkers[i];
            
            double fraction = (bar - prev.bar) / (curr.bar - prev.bar);
            return prev.timeMs + (qint64)((curr.timeMs - prev.timeMs) * fraction);
        }
    }
    
    // If past the last marker, extrapolate
    if (m_barMarkers.size() >= 2) {
        const auto& prev = m_barMarkers[m_barMarkers.size()-2];
        const auto& last = m_barMarkers.last();
        
        double msPerBar = (double)(last.timeMs - prev.timeMs) / (last.bar - prev.bar);
        return last.timeMs + (qint64)((bar - last.bar) * msPerBar);
    }
    
    return m_barMarkers.last().timeMs;
}

void MainWindow::updateFourBarWindow(double currentBar) {
    // currentBar is 1-based from the XML (bar 1, 2, 3...)
    // Convert to 0-based for internal logic
    double currentBar0Based = currentBar - 1.0;
    int currentBarInt = (int)currentBar0Based;
    double barProgress = currentBar0Based - currentBarInt;
    
    // When bar completes (progress >= 0.99), shift window forward
    if (barProgress >= 0.99 || currentBarInt > m_currentBarWindowStart) {
        m_currentBarWindowStart = currentBarInt + 1;
    }
    
    // If we're before the window start, reset
    if (currentBarInt < m_currentBarWindowStart) {
        m_currentBarWindowStart = currentBarInt;
    }
    
    // Clear existing widgets
    QLayoutItem* child;
    while ((child = fourBarLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            delete child->widget();
        }
        delete child;
    }
    
    // Find which section we're in by index (using 1-based currentBar)
    int currentSectionIndex = -1;
    for (int i = 0; i < m_sections.size(); ++i) {
        if (timeMsToBar(m_sections[i].timeMs) <= currentBar) {
            currentSectionIndex = i;
        }
    }
    
    // Update section buttons
    for (int i = 0; i < m_sectionButtons.size(); ++i) {
        QPushButton* button = m_sectionButtons[i];
        if (i == currentSectionIndex) {
            button->setStyleSheet(
                "QPushButton {"
                "    color: #00ff00;"  // Green for current section
                "    font-weight: bold;"
                "    font-size: 16pt;"
                "    border: none;"
                "    background-color: transparent;"
                "}"
            );
        } else {
            button->setStyleSheet(
                "QPushButton {"
                "    color: white;"
                "    font-weight: bold;"
                "    font-size: 16pt;"
                "    border: none;"
                "    background-color: transparent;"
                "}"
                "QPushButton:hover {"
                "    color: #4a90e2;"
                "}"
            );
        }
    }
    
    // Create widgets for each bar in the window
    for (int i = 0; i < m_barWindowSize; ++i) {
        int bar = m_currentBarWindowStart + i;
        
        QWidget* barWidget = new QWidget;
        barWidget->setMinimumSize(150, 70);
        
        // Create inner content widget
        QWidget* contentWidget = new QWidget(barWidget);
        contentWidget->setStyleSheet("QWidget { background-color: transparent; }");
        
        QVBoxLayout* barLayout = new QVBoxLayout(contentWidget);
        barLayout->setContentsMargins(8, 5, 8, 3);
        barLayout->setSpacing(2);
        
        // Find chord for this bar
        QString chord;
        for (const auto& c : m_chords) {
            if (c.bar <= bar + 1) {
                chord = c.chord;
            }
        }
        
        // Main chord label
        if (!chord.isEmpty()) {
            QLabel* chordLabel = new QLabel(chord);
            chordLabel->setAlignment(Qt::AlignCenter);
            chordLabel->setStyleSheet("QLabel { color: white; font-size: 14pt; font-weight: bold; }");
            barLayout->addWidget(chordLabel);
        }
        
        // Additional info (program changes, transpose)
        QString additionalInfo;
        
        // Check for program change at this bar (convert 0-based bar to 1-based for comparison)
        for (const auto& p : m_programChanges) {
            if (p.bar >= bar + 1 && p.bar < bar + 2) {
                if (!additionalInfo.isEmpty()) additionalInfo += "\n";
                additionalInfo += p.programName;
            }
        }
        
        // Check for transpose toggle at this bar (convert 0-based bar to 1-based for comparison)
        for (const auto& t : m_transposeToggles) {
            if (t.bar >= bar + 1 && t.bar < bar + 2) {
                if (!additionalInfo.isEmpty()) additionalInfo += "\n";
                additionalInfo += QString("Transpose (%1)").arg(t.on ? "12" : "0");
            }
        }
        
        if (!additionalInfo.isEmpty()) {
            QLabel* infoLabel = new QLabel(additionalInfo);
            infoLabel->setAlignment(Qt::AlignCenter);
            infoLabel->setStyleSheet("QLabel { color: white; font-size: 10pt; }");
            barLayout->addWidget(infoLabel);
        }
        
        barLayout->addStretch();
        
        // Style the bar based on whether it's the current bar
        if (i == 0 && bar == currentBarInt) {
            // Current bar with progress visualization
            // Use the 0-based bar progress we already calculated
            int progressWidth = (int)(barWidget->minimumWidth() * barProgress);
            
            // Use gradient background to show progress
            barWidget->setStyleSheet(QString(
                "QWidget { "
                "    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
                "        stop:0 #1a3a5a, "
                "        stop:%1 #1a3a5a, "
                "        stop:%2 #000000); "
                "}"
            ).arg(barProgress - 0.001).arg(barProgress));
            
            // Position and size content widget
            contentWidget->setGeometry(0, 0, barWidget->minimumWidth(), barWidget->minimumHeight());
        } else {
            // Normal bar
            barWidget->setStyleSheet("QWidget { background-color: black; }");
            contentWidget->setGeometry(0, 0, barWidget->minimumWidth(), barWidget->minimumHeight());
        }
        
        fourBarLayout->addWidget(barWidget, 1);
    }
}

void MainWindow::updateLyrics(double currentBar) {
    // Find current lyric
    LyricLineUI* currentLyric = nullptr;
    LyricLineUI* nextLyric = nullptr;
    LyricLineUI* previousLyric = nullptr;
    
    for (int i = 0; i < m_lyrics.size(); ++i) {
        if (currentBar >= m_lyrics[i].startBar && currentBar < m_lyrics[i].endBar) {
            currentLyric = &m_lyrics[i];
            if (i + 1 < m_lyrics.size()) {
                nextLyric = &m_lyrics[i + 1];
            }
            break;
        } else if (currentBar >= m_lyrics[i].endBar) {
            previousLyric = &m_lyrics[i];
        }
    }
    
    if (currentLyric) {
        currentLyricLabel->setText(currentLyric->text);
        currentLyricLabel->setStyleSheet("QLabel { color: white; font-size: 14pt; padding: 2px; }");
    } else {
        // Check if there's a next lyric coming soon
        bool keepPreviousLyric = false;
        if (previousLyric) {
            // Find the next lyric after the previous one
            for (const auto& lyric : m_lyrics) {
                if (lyric.startBar > previousLyric->endBar) {
                    double barsUntilNextLyric = lyric.startBar - currentBar;
                    if (barsUntilNextLyric <= 2) {  // Keep showing previous if next is within 2 bars
                        keepPreviousLyric = true;
                    }
                    break;
                }
            }
        }
        
        if (keepPreviousLyric && previousLyric) {
            // Keep showing the previous lyric
            currentLyricLabel->setText(previousLyric->text);
            currentLyricLabel->setStyleSheet("QLabel { color: #888; font-size: 14pt; padding: 2px; }");
        } else if (!m_lyrics.isEmpty()) {
            // Check for count-in only if we're not keeping previous lyric
            bool foundUpcoming = false;
            for (const auto& lyric : m_lyrics) {
                if (lyric.startBar > currentBar) {
                    double barsUntilLyrics = lyric.startBar - currentBar;
                    if (barsUntilLyrics <= 4) {
                        // Calculate quarter notes until lyrics
                        int currentTempo = 120; // Default
                        for (const auto& tempo : m_tempoChanges) {
                            if (tempo.bar <= currentBar) {
                                currentTempo = tempo.bpm;
                            }
                        }
                        
                        int beatsPerBar = 4; // Default to 4/4
                        for (const auto& ts : m_timeSignatureChanges) {
                            if (ts.bar <= currentBar) {
                                beatsPerBar = ts.numerator;
                            }
                        }
                        
                        double fractionalBars = barsUntilLyrics;
                        int quarterNotesRemaining = (int)(fractionalBars * beatsPerBar);
                        
                        currentLyricLabel->setText(QString("Lyrics in %1...").arg(quarterNotesRemaining));
                        currentLyricLabel->setStyleSheet("QLabel { color: #666; font-size: 14pt; padding: 2px; }");
                        foundUpcoming = true;
                    } else {
                        currentLyricLabel->setText("");
                        currentLyricLabel->setStyleSheet("QLabel { color: #666; font-size: 14pt; padding: 2px; }");
                    }
                    break;
                }
            }
            
            if (!foundUpcoming) {
                currentLyricLabel->setText("");
            }
        } else {
            currentLyricLabel->setText("No lyrics");
            currentLyricLabel->setStyleSheet("QLabel { color: #666; font-size: 14pt; padding: 2px; }");
        }
    }
    
    // Always try to show the next upcoming lyric
    if (!nextLyric) {
        // Find the next lyric that comes after the current position
        for (const auto& lyric : m_lyrics) {
            if (lyric.startBar > currentBar) {
                nextLyricLabel->setText(lyric.text);
                nextLyricLabel->setVisible(true);
                break;
            }
        }
    } else {
        nextLyricLabel->setText(nextLyric->text);
        nextLyricLabel->setVisible(true);
    }
    
    // Only hide if there really are no more lyrics
    if (nextLyricLabel->text().isEmpty()) {
        nextLyricLabel->setVisible(false);
    }
}

void MainWindow::onVoiceControlToggled(bool checked) {
    m_voiceController->setEnabled(checked);
    m_midiProcessor->setVoiceControlEnabled(checked);
    
    if (checked && !m_voiceController->isConnected()) {
        m_voiceController->start();
    }
}

void MainWindow::onTranscriptionReceived(const QString& text, double confidence, const QStringList& detectedTriggers, const QStringList& detectedTargets) {
    // Log the transcription to debug console
    QString logMsg = QString("Voice: \"%1\" (confidence: %2)").arg(text).arg(confidence, 0, 'f', 2);
    if (!detectedTriggers.isEmpty() || !detectedTargets.isEmpty()) {
        logMsg += " - Triggers: " + detectedTriggers.join(", ");
        logMsg += " - Targets: " + detectedTargets.join(", ");
    }
    logToConsole(logMsg);
    
    QString formattedText = formatTranscriptionWithColors(text, detectedTriggers, detectedTargets);
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

void MainWindow::onTransposeToggled(bool checked) {
    // The checkbox state has changed, so we need to apply transpose
    // When OFF->ON: we transpose up (existing notes play higher)
    // When ON->OFF: we transpose down (back to normal)
    int transposeAmount = checked ? 12 : 0;
    m_midiProcessor->applyTranspose(transposeAmount);
    logToConsole(QString("Transpose %1: notes will play %2")
        .arg(checked ? "ON" : "OFF")
        .arg(checked ? "one octave higher" : "at normal pitch"));
}

void MainWindow::toggleTranspose() {
    // Toggle the checkbox which will trigger onTransposeToggled
    transposeCheckBox->setChecked(!transposeCheckBox->isChecked());
}

QString MainWindow::formatTranscriptionWithColors(const QString& text, const QStringList& triggers, const QStringList& targets) {
    QString formattedText = text;
    
    // Escape HTML special characters
    formattedText = formattedText.toHtmlEscaped();
    
    // Make trigger words bold with yellow color
    for (const QString& trigger : triggers) {
        QString pattern = "\\b" + QRegularExpression::escape(trigger) + "\\b";
        QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        formattedText.replace(re, "<b style='color: yellow;'>" + trigger + "</b>");
    }
    
    // Make target words bold with green color
    for (const QString& target : targets) {
        QString pattern = "\\b" + QRegularExpression::escape(target) + "\\b";
        QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        // Only replace if not already formatted (to avoid overwriting yellow triggers)
        QString replacement = "<b style='color: #00ff00;'>" + target + "</b>";
        QRegularExpressionMatchIterator it = re.globalMatch(formattedText);
        QList<QPair<int, int>> positions;
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            int start = match.capturedStart();
            int end = match.capturedEnd();
            // Check if this position is already formatted
            QString before = formattedText.mid(qMax(0, start - 4), 4);
            if (!before.contains("<b ")) {
                positions.append(qMakePair(start, end));
            }
        }
        // Replace from end to start to maintain positions
        for (int i = positions.size() - 1; i >= 0; --i) {
            int start = positions[i].first;
            int end = positions[i].second;
            QString word = formattedText.mid(start, end - start);
            formattedText.replace(start, end - start, "<b style='color: #00ff00;'>" + word + "</b>");
        }
    }
    
    return formattedText;
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj == timelineBar && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton && m_trackDurationMs > 0) {
            // Calculate position from click
            int width = timelineBar->width();
            int clickX = mouseEvent->position().x();
            double fraction = (double)clickX / (double)width;
            qint64 newPosition = (qint64)(fraction * m_trackDurationMs);
            
            if (m_currentPlaybackState != QMediaPlayer::StoppedState) {
                m_midiProcessor->seekToPosition(newPosition);
            }
        }
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}