#include "mainwindow.h"
#include <QtWidgets>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStackedWidget>
#include <QSettings>
#include <QMenuBar>
#include <QDialog>
#include "NoteMonitorWidget.h"
#include "ireal/HtmlPlaylistParser.h"
#include "LibraryWindow.h"
#include "GrooveLabWindow.h"
#include "VirtuosoPresetInspectorWindow.h"
#include "VirtuosoVocabularyWindow.h"

namespace {
static const char* kIRealLastHtmlPathKey = "ireal/lastHtmlPath";
}

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

    // Apply legacy UI preference (default: OFF -> show new minimal UI)
    QSettings settings;
    bool legacyOn = settings.value("ui/legacy", false).toBool();
    applyLegacyUiSetting(legacyOn);

    // Auto-load last opened iReal HTML (persisted between sessions).
    const QString lastIReal = settings.value(kIRealLastHtmlPathKey, QString()).toString();
    if (!lastIReal.isEmpty()) {
        // Do not show warnings on startup; just ignore if missing/corrupt.
        loadIRealHtmlFile(lastIReal, /*showErrors=*/false);
    }
}

MainWindow::~MainWindow() {
    // Qt's parent-child ownership will automatically delete m_midiProcessor
}

void MainWindow::createWidgets(const Preset& preset) {
    rootStack = new QStackedWidget;
    setCentralWidget(rootStack);

    // Legacy page container (existing UI)
    centralWidget = new QWidget;
    rootStack->addWidget(centralWidget);

    // Minimal note-only UI
    noteMonitorWidget = new NoteMonitorWidget(this);
    noteMonitorWidget->setMidiProcessor(m_midiProcessor);
    rootStack->addWidget(noteMonitorWidget);

    // Preferences action in a Settings menu (PreferencesRole => macOS App menu)
    QAction* preferencesAction = new QAction("Preferences…", this);
    preferencesAction->setMenuRole(QAction::PreferencesRole);
    connect(preferencesAction, &QAction::triggered, this, &MainWindow::openPreferences);
    
    // Open iReal HTML action
    QAction* openIRealAction = new QAction("Open iReal Pro HTML…", this);
    openIRealAction->setMenuRole(QAction::NoRole);
    connect(openIRealAction, &QAction::triggered, this, &MainWindow::openIRealHtml);

    if (menuBar()) {
        // Ensure there is a File menu
        QMenu* fileMenu = nullptr;
        for (QAction* a : menuBar()->actions()) {
            if (a->menu() && a->menu()->title() == "File") {
                fileMenu = a->menu();
                break;
            }
        }
        if (!fileMenu) {
            fileMenu = menuBar()->addMenu("File");
        }
        fileMenu->addAction(openIRealAction);

        QMenu* settingsMenu = nullptr;
        // Try to find an existing "Settings" menu to avoid duplicates
        for (QAction* a : menuBar()->actions()) {
            if (a->menu() && a->menu()->title() == "Settings") {
                settingsMenu = a->menu();
                break;
            }
        }
        if (!settingsMenu) {
            settingsMenu = menuBar()->addMenu("Settings");
        }
        settingsMenu->addAction(preferencesAction);

        // Window menu: access secondary windows/dialogs.
        QMenu* windowMenu = nullptr;
        for (QAction* a : menuBar()->actions()) {
            if (a->menu() && a->menu()->title() == "Window") {
                windowMenu = a->menu();
                break;
            }
        }
        if (!windowMenu) {
            windowMenu = menuBar()->addMenu("Window");
        }

        QMenu* vocabMenu = windowMenu->addMenu("Virtuoso Vocabulary");
        vocabMenu->setToolTipsVisible(true);

        QAction* vocabPiano = new QAction("Piano", this);
        vocabPiano->setMenuRole(QAction::NoRole);
        connect(vocabPiano, &QAction::triggered, this, [this]() {
            if (!m_vocabPianoWindow) {
                m_vocabPianoWindow = new VirtuosoVocabularyWindow(m_midiProcessor, VirtuosoVocabularyWindow::Instrument::Piano, this);
                m_vocabPianoWindow->setAttribute(Qt::WA_DeleteOnClose, false);
                if (noteMonitorWidget) {
                    connect(noteMonitorWidget, &NoteMonitorWidget::virtuosoLookaheadPlanJson,
                            m_vocabPianoWindow, &VirtuosoVocabularyWindow::ingestTheoryEventJson,
                            Qt::UniqueConnection);
                    connect(m_vocabPianoWindow, &VirtuosoVocabularyWindow::requestSongPreview,
                            noteMonitorWidget, &NoteMonitorWidget::requestVirtuosoLookaheadOnce,
                            Qt::UniqueConnection);
                    connect(m_vocabPianoWindow, &VirtuosoVocabularyWindow::agentEnergyMultiplierChanged,
                            noteMonitorWidget, &NoteMonitorWidget::setVirtuosoAgentEnergyMultiplier,
                            Qt::UniqueConnection);
                }
            }
            m_vocabPianoWindow->show();
            m_vocabPianoWindow->raise();
            m_vocabPianoWindow->activateWindow();
        });
        vocabMenu->addAction(vocabPiano);

        QAction* vocabBass = new QAction("Bass", this);
        vocabBass->setMenuRole(QAction::NoRole);
        connect(vocabBass, &QAction::triggered, this, [this]() {
            if (!m_vocabBassWindow) {
                m_vocabBassWindow = new VirtuosoVocabularyWindow(m_midiProcessor, VirtuosoVocabularyWindow::Instrument::Bass, this);
                m_vocabBassWindow->setAttribute(Qt::WA_DeleteOnClose, false);
                if (noteMonitorWidget) {
                    connect(noteMonitorWidget, &NoteMonitorWidget::virtuosoLookaheadPlanJson,
                            m_vocabBassWindow, &VirtuosoVocabularyWindow::ingestTheoryEventJson,
                            Qt::UniqueConnection);
                    connect(m_vocabBassWindow, &VirtuosoVocabularyWindow::requestSongPreview,
                            noteMonitorWidget, &NoteMonitorWidget::requestVirtuosoLookaheadOnce,
                            Qt::UniqueConnection);
                    connect(m_vocabBassWindow, &VirtuosoVocabularyWindow::agentEnergyMultiplierChanged,
                            noteMonitorWidget, &NoteMonitorWidget::setVirtuosoAgentEnergyMultiplier,
                            Qt::UniqueConnection);
                }
            }
            m_vocabBassWindow->show();
            m_vocabBassWindow->raise();
            m_vocabBassWindow->activateWindow();
        });
        vocabMenu->addAction(vocabBass);

        QAction* vocabDrums = new QAction("Drums", this);
        vocabDrums->setMenuRole(QAction::NoRole);
        connect(vocabDrums, &QAction::triggered, this, [this]() {
            if (!m_vocabDrumsWindow) {
                m_vocabDrumsWindow = new VirtuosoVocabularyWindow(m_midiProcessor, VirtuosoVocabularyWindow::Instrument::Drums, this);
                m_vocabDrumsWindow->setAttribute(Qt::WA_DeleteOnClose, false);
                if (noteMonitorWidget) {
                    connect(noteMonitorWidget, &NoteMonitorWidget::virtuosoLookaheadPlanJson,
                            m_vocabDrumsWindow, &VirtuosoVocabularyWindow::ingestTheoryEventJson,
                            Qt::UniqueConnection);
                    connect(m_vocabDrumsWindow, &VirtuosoVocabularyWindow::requestSongPreview,
                            noteMonitorWidget, &NoteMonitorWidget::requestVirtuosoLookaheadOnce,
                            Qt::UniqueConnection);
                    connect(m_vocabDrumsWindow, &VirtuosoVocabularyWindow::agentEnergyMultiplierChanged,
                            noteMonitorWidget, &NoteMonitorWidget::setVirtuosoAgentEnergyMultiplier,
                            Qt::UniqueConnection);
                }
            }
            m_vocabDrumsWindow->show();
            m_vocabDrumsWindow->raise();
            m_vocabDrumsWindow->activateWindow();
        });
        vocabMenu->addAction(vocabDrums);

        QAction* libraryAction = new QAction("Library", this);
        libraryAction->setMenuRole(QAction::NoRole);
        connect(libraryAction, &QAction::triggered, this, [this]() {
            if (!m_libraryWindow) {
                m_libraryWindow = new LibraryWindow(m_midiProcessor, this);
                m_libraryWindow->setAttribute(Qt::WA_DeleteOnClose, false);
                if (noteMonitorWidget) {
                    connect(noteMonitorWidget, &NoteMonitorWidget::virtuosoTheoryEventJson,
                            m_libraryWindow, &LibraryWindow::ingestTheoryEventJson,
                            Qt::UniqueConnection);
                    connect(noteMonitorWidget, &NoteMonitorWidget::virtuosoPlannedTheoryEventJson,
                            m_libraryWindow, &LibraryWindow::ingestTheoryEventJson,
                            Qt::UniqueConnection);
                }
            }
            m_libraryWindow->show();
            m_libraryWindow->raise();
            m_libraryWindow->activateWindow();
        });
        windowMenu->addAction(libraryAction);

        QAction* grooveLabAction = new QAction("Groove Lab", this);
        grooveLabAction->setMenuRole(QAction::NoRole);
        connect(grooveLabAction, &QAction::triggered, this, [this]() {
            if (!m_grooveLabWindow) {
                m_grooveLabWindow = new GrooveLabWindow(m_midiProcessor, this);
                m_grooveLabWindow->setAttribute(Qt::WA_DeleteOnClose, false);
            }
            m_grooveLabWindow->show();
            m_grooveLabWindow->raise();
            m_grooveLabWindow->activateWindow();
        });
        windowMenu->addAction(grooveLabAction);

        QAction* presetInspectorAction = new QAction("Virtuoso Preset Inspector", this);
        presetInspectorAction->setMenuRole(QAction::NoRole);
        connect(presetInspectorAction, &QAction::triggered, this, [this]() {
            // Make this independent of NoteMonitorWidget; it’s a global library inspector.
            auto* w = new VirtuosoPresetInspectorWindow(m_midiProcessor, this);
            w->setAttribute(Qt::WA_DeleteOnClose, true);
            w->show();
            w->raise();
            w->activateWindow();
        });
        windowMenu->addAction(presetInspectorAction);
    }

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
    // Avoid generic "monospace" fallback on macOS Qt.
    voiceTranscriptionLabel->setStyleSheet("QLabel { background-color: black; color: white; padding: 10px; border-radius: 5px; font-family: Menlo, Monaco; }");
    
    // Transpose checkbox
    transposeCheckBox = new QCheckBox("Transpose");
    transposeCheckBox->setChecked(false); // Default to OFF
}

void MainWindow::createLayout() {
    mainLayout = new QVBoxLayout(centralWidget);
    
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
    
    // Voice control connections
    connect(voiceControlCheckBox, &QCheckBox::toggled, this, &MainWindow::onVoiceControlToggled);
    connect(m_voiceController, &VoiceController::transcriptionReceived, this, &MainWindow::onTranscriptionReceived);
    connect(m_voiceController, &VoiceController::connectionStatusChanged, this, &MainWindow::onVoiceConnectionStatusChanged);
    connect(m_voiceController, &VoiceController::errorOccurred, this, [this](const QString& error) {
        logToConsole("Voice Control Error: " + error);
    });
    connect(m_voiceController, &VoiceController::programCommandDetected, m_midiProcessor, &MidiProcessor::applyProgram);
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

    // Wire pitch updates to minimal NoteMonitor UI
    if (noteMonitorWidget) {
        // Connect piano debug log to main console
        connect(noteMonitorWidget, &NoteMonitorWidget::pianoDebugLogMessage,
                this, &MainWindow::logToConsole,
                static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
        
        connect(m_midiProcessor, &MidiProcessor::guitarPitchUpdated,
                noteMonitorWidget, &NoteMonitorWidget::setGuitarNote,
                static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
        connect(m_midiProcessor, &MidiProcessor::voicePitchUpdated,
                noteMonitorWidget, &NoteMonitorWidget::setVoiceNote,
                static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
        connect(m_midiProcessor, &MidiProcessor::guitarHzUpdated,
                noteMonitorWidget, &NoteMonitorWidget::setGuitarHz,
                static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
        connect(m_midiProcessor, &MidiProcessor::voiceHzUpdated,
                noteMonitorWidget, &NoteMonitorWidget::setVoiceHz,
                static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
        connect(m_midiProcessor, &MidiProcessor::guitarAftertouchUpdated,
                noteMonitorWidget, &NoteMonitorWidget::setGuitarAmplitude,
                static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
        connect(m_midiProcessor, &MidiProcessor::voiceCc2Updated,
                noteMonitorWidget, &NoteMonitorWidget::setVoiceAmplitude,
                static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
        connect(m_midiProcessor, &MidiProcessor::guitarVelocityUpdated,
                noteMonitorWidget, &NoteMonitorWidget::setGuitarVelocity,
                static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
    }

    // --- Shutdown safety: stop playback engines before MIDI teardown ---
    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
            if (noteMonitorWidget) noteMonitorWidget->stopAllPlayback();
        });
    }
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

void MainWindow::openPreferences() {
    QSettings settings;
    bool legacyOn = settings.value("ui/legacy", false).toBool();

    QDialog dlg(this);
    dlg.setWindowTitle("Preferences");
    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    QCheckBox* legacyCheck = new QCheckBox("Legacy UI", &dlg);
    legacyCheck->setChecked(legacyOn);
    layout->addWidget(legacyCheck);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        bool legacy = legacyCheck->isChecked();
        settings.setValue("ui/legacy", legacy);
        applyLegacyUiSetting(legacy);
    }
}

void MainWindow::openIRealHtml() {
    QSettings settings;
    const QString lastPath = settings.value(kIRealLastHtmlPathKey, QString()).toString();
    const QString startDir = lastPath.isEmpty() ? QString() : QFileInfo(lastPath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open iReal Pro HTML Playlist",
        startDir,
        "HTML files (*.html *.htm);;All files (*)"
    );
    if (path.isEmpty()) return;

    if (loadIRealHtmlFile(path, /*showErrors=*/true)) {
        settings.setValue(kIRealLastHtmlPathKey, path);
    }
}

void MainWindow::applyLegacyUiSetting(bool legacyOn) {
    if (!rootStack) return;
    // index 0 = legacy (existing UI), index 1 = new minimal UI
    rootStack->setCurrentIndex(legacyOn ? 0 : 1);
}

bool MainWindow::loadIRealHtmlFile(const QString& path, bool showErrors) {
    if (path.trimmed().isEmpty()) return false;
    if (!QFileInfo::exists(path)) {
        if (showErrors) {
            QMessageBox::warning(this, "iReal Import", "The selected file no longer exists.");
        }
        return false;
    }

    const ireal::Playlist pl = ireal::HtmlPlaylistParser::parseFile(path);
    if (pl.songs.isEmpty()) {
        if (showErrors) {
            QMessageBox::warning(this, "iReal Import", "No iReal Pro playlist link found or playlist contained no songs.");
        }
        return false;
    }

    if (noteMonitorWidget) {
        noteMonitorWidget->setIRealPlaylist(pl);
        // Ensure the chart is visible when an iReal file is loaded.
        applyLegacyUiSetting(false);
    }
    return true;
}