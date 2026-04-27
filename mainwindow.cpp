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
#include "playback/VirtuosoBalladMvpPlaybackEngine.h"
#include "GrooveLabWindow.h"
#include "VirtuosoPresetInspectorWindow.h"
#include "VirtuosoVocabularyWindow.h"
#include "SnappingWindow.h"
#include "AudioTrackSwitchEditor.h"
#include "playback/ScaleSnapProcessor.h"
#include "music/ChordSymbol.h"
#include <QProcess>

namespace {
static const char* kIRealLastHtmlPathKey = "ireal/lastHtmlPath";
}

MainWindow::MainWindow(const Preset& preset, QWidget *parent) 
    : QMainWindow(parent) {

    // MainWindow now owns MidiProcessor
    m_midiProcessor = new MidiProcessor(preset, this);

    // Capture the preset-defined audio-track-switch map so the editor's
    // "Reset to Preset Defaults" button can always restore it.
    m_audioTrackSwitchPresetDefaults = preset.settings.audioTrackMutes;
    m_audioTrackSwitchPresetDefaultCC = preset.settings.audioTrackSwitchCC;

    // Apply any persisted audio-track-switch overrides from QSettings. Edits
    // made in the in-app editor survive restarts via this mechanism (the
    // preset.xml is baked into the binary and not writable at runtime).
    {
        QSettings s;
        const bool hasPersisted = s.contains("audioTrackSwitch/cc");
        const int count = s.beginReadArray("audioTrackSwitch/entries");
        QList<AudioTrackMute> entries;
        entries.reserve(count);
        for (int i = 0; i < count; ++i) {
            s.setArrayIndex(i);
            AudioTrackMute e;
            e.name = s.value("name").toString();
            e.switchValue = s.value("switchValue").toInt();
            e.muteCC = s.value("muteCC").toInt();
            entries.append(e);
        }
        s.endArray();
        if (hasPersisted) {
            const int cc = s.value("audioTrackSwitch/cc",
                                   preset.settings.audioTrackSwitchCC).toInt();
            m_midiProcessor->setAudioTrackSwitch(cc, entries);
        }
    }

    // Initialize voice controller
    m_voiceController = new VoiceController(preset, this);
    
    // Initialize transcription timer
    voiceTranscriptionTimer = new QTimer(this);
    voiceTranscriptionTimer->setSingleShot(true);

    // Read performance mode setting BEFORE creating widgets (they depend on it)
    QSettings settings;
    m_performanceMode = settings.value("app/performanceMode", true).toBool();

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
    bool legacyOn = settings.value("ui/legacy", false).toBool();
    applyLegacyUiSetting(legacyOn);

    // --- Harmony footswitch state: load from QSettings, then apply live ---
    m_harmonyToggleCC          = settings.value("harmony/toggleCC", 33).toInt();
    m_harmonyRootStepCC        = settings.value("harmony/rootStepCC", 34).toInt();
    m_harmonyAccidentalStepCC  = settings.value("harmony/accidentalStepCC", 35).toInt();
    m_harmonyQualityStepCC     = settings.value("harmony/qualityStepCC", 36).toInt();
    m_harmonyRootIdx           = settings.value("harmony/rootIndex", 6).toInt();        // B
    m_harmonyAccidentalIdx     = settings.value("harmony/accidentalIndex", 1).toInt();  // ♭ (new order: ♮/♭/♯)
    // One-time migration: the accidental dropdown order changed from
    // ♮(0)/♯(1)/♭(2) to ♮(0)/♭(1)/♯(2). Swap 1↔2 on existing saves so the
    // user keeps the chord they configured under the old order.
    if (!settings.contains("harmony/accidentalSchemaV2")) {
        if (m_harmonyAccidentalIdx == 1)      m_harmonyAccidentalIdx = 2; // was ♯
        else if (m_harmonyAccidentalIdx == 2) m_harmonyAccidentalIdx = 1; // was ♭
        settings.setValue("harmony/accidentalIndex", m_harmonyAccidentalIdx);
        settings.setValue("harmony/accidentalSchemaV2", true);
    }
    m_harmonyQualityIdx        = settings.value("harmony/qualityIndex", 0).toInt();     // maj
    m_harmonyEnabledAtStartup  = settings.value("harmony/enabledAtStartup", false).toBool();
    m_speakChordChanges        = settings.value("harmony/speakChanges", true).toBool();
    m_harmonyEnabled           = m_harmonyEnabledAtStartup;

    // Push CC numbers to MidiProcessor and wire its signals back to us.
    m_midiProcessor->setHarmonyCCs(m_harmonyToggleCC, m_harmonyRootStepCC,
                                   m_harmonyAccidentalStepCC, m_harmonyQualityStepCC);
    // Seed the worker's toggle bookkeeping so the first CC33 press flips
    // *from* whatever state we just loaded (not from a default of false).
    m_midiProcessor->setHarmonyToggleStateForFlip(m_harmonyEnabled);
    connect(m_midiProcessor, &MidiProcessor::harmonyToggleRequested,
            this, &MainWindow::onHarmonyToggleRequested);
    connect(m_midiProcessor, &MidiProcessor::harmonyRootStepRequested,
            this, &MainWindow::onHarmonyRootStepRequested);
    connect(m_midiProcessor, &MidiProcessor::harmonyAccidentalStepRequested,
            this, &MainWindow::onHarmonyAccidentalStepRequested);
    connect(m_midiProcessor, &MidiProcessor::harmonyQualityStepRequested,
            this, &MainWindow::onHarmonyQualityStepRequested);

    // Push initial chord + enabled state to the standalone ScaleSnapProcessor.
    // Done after createWidgets() so noteMonitorWidget exists.
    applyHarmonyChordToEngine();
    if (noteMonitorWidget && noteMonitorWidget->scaleSnapProcessor()) {
        // Eagerly construct SnappingWindow (hidden) so its persisted voice
        // configs get pushed to the standalone ScaleSnapProcessor at launch.
        // Without this, voice configs only apply after the user opens the
        // Snapping window — which means the master harmony toggle would
        // silently do nothing the first time you use it after a fresh start.
        if (m_performanceMode && !m_snappingWindow) {
            m_snappingWindow = new SnappingWindow(this);
            m_snappingWindow->setAttribute(Qt::WA_DeleteOnClose, false);
            m_snappingWindow->setScaleSnapProcessor(noteMonitorWidget->scaleSnapProcessor());
            // Don't show — the user opens it explicitly via Window → Snapping.
        }

        noteMonitorWidget->scaleSnapProcessor()->setHarmonyEnabled(m_harmonyEnabled);
    }

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
    noteMonitorWidget = new NoteMonitorWidget(m_performanceMode, this);
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
                
                // Connect DIRECTLY to the playback engine (more reliable than forwarding through NoteMonitorWidget)
                if (noteMonitorWidget && noteMonitorWidget->virtuosoPlayback()) {
                    auto* engine = noteMonitorWidget->virtuosoPlayback();
                    qDebug() << "MainWindow: Connecting LibraryWindow DIRECTLY to VirtuosoBalladMvpPlaybackEngine";
                    
                    connect(engine, &playback::VirtuosoBalladMvpPlaybackEngine::theoryEventJson,
                            m_libraryWindow, &LibraryWindow::ingestTheoryEventJson,
                            Qt::UniqueConnection);
                    connect(engine, &playback::VirtuosoBalladMvpPlaybackEngine::plannedTheoryEventJson,
                            m_libraryWindow, &LibraryWindow::ingestTheoryEventJson,
                            Qt::UniqueConnection);
                } else {
                    qWarning() << "MainWindow: Could not connect LibraryWindow - noteMonitorWidget or virtuosoPlayback is null";
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
            // Make this independent of NoteMonitorWidget; it's a global library inspector.
            auto* w = new VirtuosoPresetInspectorWindow(m_midiProcessor, this);
            w->setAttribute(Qt::WA_DeleteOnClose, true);
            w->show();
            w->raise();
            w->activateWindow();
        });
        windowMenu->addAction(presetInspectorAction);

        QAction* snappingAction = new QAction("Snapping", this);
        snappingAction->setMenuRole(QAction::NoRole);
        connect(snappingAction, &QAction::triggered, this, [this]() {
            if (!m_snappingWindow) {
                m_snappingWindow = new SnappingWindow(this);
                m_snappingWindow->setAttribute(Qt::WA_DeleteOnClose, false);
                if (m_performanceMode) {
                    // Performance mode: connect directly to ScaleSnapProcessor
                    if (noteMonitorWidget && noteMonitorWidget->scaleSnapProcessor()) {
                        m_snappingWindow->setScaleSnapProcessor(noteMonitorWidget->scaleSnapProcessor());
                    }
                } else if (noteMonitorWidget && noteMonitorWidget->virtuosoPlayback()) {
                    m_snappingWindow->setPlaybackEngine(noteMonitorWidget->virtuosoPlayback());
                }
            }
            m_snappingWindow->show();
            m_snappingWindow->raise();
            m_snappingWindow->activateWindow();
        });
        windowMenu->addAction(snappingAction);

        // Audio Track Switch editor (radio-button mute map for the Ampero CC 27 fan-out).
        QAction* audioSwitchAction = new QAction("Audio Track Switch…", this);
        audioSwitchAction->setMenuRole(QAction::NoRole);
        connect(audioSwitchAction, &QAction::triggered, this, [this]() {
            if (!m_audioTrackSwitchEditor) {
                HarmonyEditorState hs;
                hs.toggleCC = m_harmonyToggleCC;
                hs.rootStepCC = m_harmonyRootStepCC;
                hs.accidentalStepCC = m_harmonyAccidentalStepCC;
                hs.qualityStepCC = m_harmonyQualityStepCC;
                hs.rootIndex = m_harmonyRootIdx;
                hs.accidentalIndex = m_harmonyAccidentalIdx;
                hs.qualityIndex = m_harmonyQualityIdx;
                hs.enabledOnStartup = m_harmonyEnabledAtStartup;
                hs.speakChanges = m_speakChordChanges;

                m_audioTrackSwitchEditor = new AudioTrackSwitchEditor(
                    m_midiProcessor->audioTrackSwitchCC(),
                    m_midiProcessor->audioTrackMutes(),
                    m_audioTrackSwitchPresetDefaults,
                    m_audioTrackSwitchPresetDefaultCC,
                    hs,
                    this);
                m_audioTrackSwitchEditor->setLiveHarmonyEnabled(m_harmonyEnabled);
                if (auto* snap = noteMonitorWidget ? noteMonitorWidget->scaleSnapProcessor() : nullptr) {
                    const bool preferFlats = (m_harmonyAccidentalIdx == 1);
                    m_audioTrackSwitchEditor->setLiveScaleSummary(snap->currentScaleSummary(preferFlats));
                }
                m_audioTrackSwitchEditor->setAttribute(Qt::WA_DeleteOnClose, false);

                connect(m_audioTrackSwitchEditor, &AudioTrackSwitchEditor::mapChanged,
                        this, [this](int cc, const QList<AudioTrackMute>& entries) {
                    // Push live into the processor.
                    m_midiProcessor->setAudioTrackSwitch(cc, entries);
                    // Persist to QSettings so edits survive restarts.
                    QSettings s;
                    s.setValue("audioTrackSwitch/cc", cc);
                    s.beginWriteArray("audioTrackSwitch/entries", entries.size());
                    for (int i = 0; i < entries.size(); ++i) {
                        s.setArrayIndex(i);
                        s.setValue("name", entries[i].name);
                        s.setValue("switchValue", entries[i].switchValue);
                        s.setValue("muteCC", entries[i].muteCC);
                    }
                    s.endArray();
                });

                // Harmony editor → MainWindow wiring.
                connect(m_audioTrackSwitchEditor, &AudioTrackSwitchEditor::harmonyCCsChanged,
                        this, [this](int t, int r, int a, int q) {
                    m_harmonyToggleCC = t; m_harmonyRootStepCC = r;
                    m_harmonyAccidentalStepCC = a; m_harmonyQualityStepCC = q;
                    m_midiProcessor->setHarmonyCCs(t, r, a, q);
                    QSettings s;
                    s.setValue("harmony/toggleCC", t);
                    s.setValue("harmony/rootStepCC", r);
                    s.setValue("harmony/accidentalStepCC", a);
                    s.setValue("harmony/qualityStepCC", q);
                });
                connect(m_audioTrackSwitchEditor, &AudioTrackSwitchEditor::defaultChordChanged,
                        this, [this](int rootIdx, int accIdx, int qualIdx) {
                    m_harmonyRootIdx = rootIdx;
                    m_harmonyAccidentalIdx = accIdx;
                    m_harmonyQualityIdx = qualIdx;
                    persistHarmonyChordIndices();
                    applyHarmonyChordToEngine();
                    // Don't speak when the user clicks dropdowns themselves —
                    // only footswitch presses speak. Keeps mouse use silent.
                });
                connect(m_audioTrackSwitchEditor, &AudioTrackSwitchEditor::enabledOnStartupChanged,
                        this, [this](bool checked) {
                    m_harmonyEnabledAtStartup = checked;
                    QSettings().setValue("harmony/enabledAtStartup", checked);
                });
                connect(m_audioTrackSwitchEditor, &AudioTrackSwitchEditor::speakChangesChanged,
                        this, [this](bool checked) {
                    m_speakChordChanges = checked;
                    QSettings().setValue("harmony/speakChanges", checked);
                });
            }
            m_audioTrackSwitchEditor->show();
            m_audioTrackSwitchEditor->raise();
            m_audioTrackSwitchEditor->activateWindow();
        });
        windowMenu->addAction(audioSwitchAction);

        // Gray out menus not available in Performance Mode
        if (m_performanceMode) {
            libraryAction->setEnabled(false);
            libraryAction->setToolTip("Not available in Performance Mode");
            vocabMenu->setEnabled(false);
            vocabMenu->setToolTip("Not available in Performance Mode");
            grooveLabAction->setEnabled(false);
            grooveLabAction->setToolTip("Not available in Performance Mode");
            presetInspectorAction->setEnabled(false);
            presetInspectorAction->setToolTip("Not available in Performance Mode");
        }
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
    bool perfModeOn = settings.value("app/performanceMode", true).toBool();

    QDialog dlg(this);
    dlg.setWindowTitle("Preferences");
    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    QCheckBox* legacyCheck = new QCheckBox("Legacy UI", &dlg);
    legacyCheck->setChecked(legacyOn);
    layout->addWidget(legacyCheck);

    QCheckBox* perfModeCheck = new QCheckBox("Performance Mode (requires restart)", &dlg);
    perfModeCheck->setChecked(perfModeOn);
    perfModeCheck->setToolTip("When enabled, the app starts with only vocal+guitar MIDI fusion and snapping.\nVirtuoso musician subsystem is not loaded for faster startup.");
    layout->addWidget(perfModeCheck);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        bool legacy = legacyCheck->isChecked();
        settings.setValue("ui/legacy", legacy);
        applyLegacyUiSetting(legacy);

        bool newPerfMode = perfModeCheck->isChecked();
        if (newPerfMode != perfModeOn) {
            settings.setValue("app/performanceMode", newPerfMode);
            QMessageBox::information(this, "Performance Mode",
                "Performance Mode has been " + QString(newPerfMode ? "enabled" : "disabled") + ".\n"
                "Please restart the application for this change to take effect.");
        }
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

// ============================================================================
// Harmony footswitch handling
// ============================================================================
//
// Each step CC advances one component. Pressing the root-step also resets the
// accidental to natural and the quality to maj — the user's preference, so
// they always land on a clean fresh-major chord and tune from there. Speech
// only fires on actual footswitch presses (not editor dropdown clicks).

namespace {
// Roots in the same order as the editor's kRootLabels (C, D, E, F, G, A, B).
// Pitch-class numbers are 0..11 with C=0.
const int kRootPcs[7] = { 0, 2, 4, 5, 7, 9, 11 };
// ChordSymbol parser is happy with ASCII root letters + 'b' / '#'.
const char  kRootChars[7]   = { 'C','D','E','F','G','A','B' };
const QString kRootSpoken[7] = { "C","D","E","F","G","A","B" };
// Accidental order: 0=natural, 1=flat, 2=sharp (matches editor's kAccidentals).
const QString kAccidentalParser[3] = { "", "b", "#" };
const QString kAccidentalSpoken[3] = { "", " flat", " sharp" };
// Quality strings in the same order as the editor's kQualityLabels:
//   maj, min, dim, aug, sus2, sus4, maj7, m7, 7, m7♭5
const QString kQualityParser[10] = {
    "", "m", "dim", "aug", "sus2", "sus4", "maj7", "m7", "7", "m7b5"
};
const QString kQualitySpoken[10] = {
    " major", " minor", " diminished", " augmented",
    " suss two", " suss four", " major seven", " minor seven", " seven",
    " minor seven flat five"
};
}

QString MainWindow::chordSymbolText() const {
    QString out;
    out += QChar(kRootChars[m_harmonyRootIdx]);
    out += kAccidentalParser[m_harmonyAccidentalIdx];
    out += kQualityParser[m_harmonyQualityIdx];
    return out;
}

QString MainWindow::chordSpokenText() const {
    return kRootSpoken[m_harmonyRootIdx]
         + kAccidentalSpoken[m_harmonyAccidentalIdx]
         + kQualitySpoken[m_harmonyQualityIdx];
}

void MainWindow::applyHarmonyChordToEngine() {
    if (!noteMonitorWidget || !noteMonitorWidget->scaleSnapProcessor()) return;
    const QString text = chordSymbolText();
    music::ChordSymbol chord;
    const bool parsed = music::parseChordSymbol(text, chord);
    if (parsed) {
        noteMonitorWidget->scaleSnapProcessor()->setDefaultHarmonyChord(chord);
    }
    const QString line = QString("Harmony chord: \"%1\" parsed=%2 rootPc=%3 quality=%4 "
                                 "(idx root=%5 acc=%6 quality=%7)")
                             .arg(text)
                             .arg(parsed ? "Y" : "N")
                             .arg(chord.rootPc)
                             .arg(static_cast<int>(chord.quality))
                             .arg(m_harmonyRootIdx)
                             .arg(m_harmonyAccidentalIdx)
                             .arg(m_harmonyQualityIdx);
    logToConsole(line);
    if (m_midiProcessor) m_midiProcessor->pushLog(line);
    if (m_audioTrackSwitchEditor) {
        m_audioTrackSwitchEditor->setLiveChord(m_harmonyRootIdx,
                                               m_harmonyAccidentalIdx,
                                               m_harmonyQualityIdx);
        // Show the scale that's actually conforming the harmony output —
        // useful for visually verifying that footswitch chord changes
        // produce the expected scale (e.g. B♭ minor → B♭ Aeolian, not
        // dorian).
        if (auto* snap = noteMonitorWidget ? noteMonitorWidget->scaleSnapProcessor() : nullptr) {
            // Prefer flats only when the chord uses a flat accidental.
            const bool preferFlats = (m_harmonyAccidentalIdx == 1);
            m_audioTrackSwitchEditor->setLiveScaleSummary(snap->currentScaleSummary(preferFlats));
        }
    }
}

void MainWindow::persistHarmonyChordIndices() {
    QSettings s;
    s.setValue("harmony/rootIndex", m_harmonyRootIdx);
    s.setValue("harmony/accidentalIndex", m_harmonyAccidentalIdx);
    s.setValue("harmony/qualityIndex", m_harmonyQualityIdx);
}

void MainWindow::speakChordIfEnabled() {
    if (!m_speakChordChanges) return;
    // 220 wpm — faster than default (~175) for performance feedback.
    QProcess::startDetached("say",
        QStringList() << "-r" << "220" << chordSpokenText());
}

void MainWindow::onHarmonyToggleRequested(bool enabled) {
    m_harmonyEnabled = enabled;
    auto* snap = noteMonitorWidget ? noteMonitorWidget->scaleSnapProcessor() : nullptr;
    if (snap) {
        snap->setHarmonyEnabled(enabled);
        // Echo every harmony decision into the console while toggle is on,
        // so we can see exactly which gate is killing output.
        snap->setHarmonyDebug(enabled);
    }
    if (m_audioTrackSwitchEditor) {
        m_audioTrackSwitchEditor->setLiveHarmonyEnabled(enabled);
    }
    logToConsole(QString("Harmony toggle (footswitch): %1")
                     .arg(enabled ? "ON" : "OFF"));

    // Diagnostic: turning the master switch ON has no audible effect unless
    // at least one voice is configured in the Snapping window. After the
    // setHarmonyEnabled(true) call above, isMultiVoiceModeActive() returns
    // true iff any voice has a non-Off mode. If false here, the silence is
    // because nothing is configured to play.
    if (enabled && snap && !snap->isMultiVoiceModeActive()) {
        logToConsole("WARNING: harmonies enabled but no voices configured. "
                     "Open Window → Snapping → Multi-Voice Harmony and set "
                     "at least one voice's Mode to something other than Off.");
    }
}

void MainWindow::onHarmonyRootStepRequested() {
    m_harmonyRootIdx = (m_harmonyRootIdx + 1) % 7;
    // Per spec: root change resets accidental and quality to defaults.
    m_harmonyAccidentalIdx = 0;  // ♮
    m_harmonyQualityIdx = 0;     // maj
    persistHarmonyChordIndices();
    applyHarmonyChordToEngine();
    speakChordIfEnabled();
}

void MainWindow::onHarmonyAccidentalStepRequested() {
    m_harmonyAccidentalIdx = (m_harmonyAccidentalIdx + 1) % 3;
    persistHarmonyChordIndices();
    applyHarmonyChordToEngine();
    speakChordIfEnabled();
}

void MainWindow::onHarmonyQualityStepRequested() {
    m_harmonyQualityIdx = (m_harmonyQualityIdx + 1) % 10;
    persistHarmonyChordIndices();
    applyHarmonyChordToEngine();
    speakChordIfEnabled();
}