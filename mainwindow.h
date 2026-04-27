#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <vector>
#include <map>
#include <string>
#include "midiprocessor.h"
#include "PresetData.h"
#include "voicecontroller.h"

// Forward declare Qt classes
QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QPushButton;
class QListWidget;
class QCheckBox;
class QTextEdit;
class QGroupBox;
class QLabel;
class QTimer;
class QProgressBar;
class QHBoxLayout;
class QStackedWidget;
QT_END_NAMESPACE

class NoteMonitorWidget;
class LibraryWindow;
class GrooveLabWindow;
class VirtuosoVocabularyWindow;
class SnappingWindow;
class AudioTrackSwitchEditor;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const Preset& preset, QWidget *parent = nullptr);
    ~MainWindow();
    
    // Public method to toggle transpose checkbox (for voice control)
    void toggleTranspose();

private slots:
    void updateProgramUI(int newProgramIndex);
    void updateTrackUI(const std::string& trackId, bool newState);
    // FIX: Changed signature to match the signal's type
    void logToConsole(const QString& message); 
    void onVerboseLogToggled(bool checked);
    
    // Voice control slots
    void onVoiceControlToggled(bool checked);
    void onTranscriptionReceived(const QString& text, double confidence, const QStringList& detectedTriggers, const QStringList& detectedTargets);
    void onVoiceConnectionStatusChanged(bool connected);
    
    // Transpose slot
    void onTransposeToggled(bool checked);
    void openPreferences();
    void applyLegacyUiSetting(bool legacyOn);
    void openIRealHtml();

private:
    void createWidgets(const Preset& preset);
    void createLayout();
    void createConnections();
    QString formatTranscriptionWithColors(const QString& text, const QStringList& triggers, const QStringList& targets);
    bool loadIRealHtmlFile(const QString& path, bool showErrors);
    
protected:
    // MIDI Logic handler (owned by MainWindow)
    MidiProcessor* m_midiProcessor;
    
    // Voice control
    VoiceController* m_voiceController;

    // UI Widgets
    QStackedWidget* rootStack = nullptr;
    QWidget* centralWidget;
    QVBoxLayout* mainLayout;
    std::vector<QPushButton*> programButtons;
    std::vector<std::string> programNames;
    std::map<std::string, QCheckBox*> trackCheckBoxes;
    QTextEdit* logConsole;
    QCheckBox* verboseLogCheckBox;
    NoteMonitorWidget* noteMonitorWidget = nullptr;
    
    // Voice control UI
    QGroupBox* voiceControlBox;
    QCheckBox* voiceControlCheckBox;
    QLabel* voiceTranscriptionLabel;
    QLabel* voiceStatusLabel;
    QTimer* voiceTranscriptionTimer;
    
    // Transpose control
    QCheckBox* transposeCheckBox;

    // Library Visualizer window (Window → Library)
    LibraryWindow* m_libraryWindow = nullptr;

    // Groove Lab window (Window → Groove Lab)
    GrooveLabWindow* m_grooveLabWindow = nullptr;

    // Virtuoso vocabulary windows (Window → Virtuoso Vocabulary)
    VirtuosoVocabularyWindow* m_vocabPianoWindow = nullptr;
    VirtuosoVocabularyWindow* m_vocabBassWindow = nullptr;
    VirtuosoVocabularyWindow* m_vocabDrumsWindow = nullptr;

    // Snapping settings window (Window → Snapping)
    SnappingWindow* m_snappingWindow = nullptr;

    // Audio-track switching editor (Window → Audio Track Switch…)
    AudioTrackSwitchEditor* m_audioTrackSwitchEditor = nullptr;
    // Preset defaults captured at startup so the editor can offer "Reset".
    QList<AudioTrackMute> m_audioTrackSwitchPresetDefaults;
    int m_audioTrackSwitchPresetDefaultCC = 27;

    // --- Harmony footswitch state (live; persisted to QSettings) ---
    int m_harmonyToggleCC = 33;
    int m_harmonyRootStepCC = 34;
    int m_harmonyAccidentalStepCC = 35;
    int m_harmonyQualityStepCC = 36;
    int m_harmonyRootIdx = 6;        // default 'B' (matches editor's kRootLabels)
    int m_harmonyAccidentalIdx = 1;  // default '♭' (order is ♮ → ♭ → ♯)
    int m_harmonyQualityIdx = 0;     // default 'maj'
    bool m_harmonyEnabled = false;
    bool m_harmonyEnabledAtStartup = false;
    bool m_speakChordChanges = true;

    // Slots that respond to MidiProcessor's harmony footswitch signals.
    void onHarmonyToggleRequested(bool enabled);
    void onHarmonyRootStepRequested();
    void onHarmonyAccidentalStepRequested();
    void onHarmonyQualityStepRequested();
    // Helpers
    void applyHarmonyChordToEngine();
    void persistHarmonyChordIndices();
    QString chordSymbolText() const;   // e.g. "Bbmaj7" — for ChordSymbol parser
    QString chordSpokenText() const;   // e.g. "B flat major seven" — for `say`
    void speakChordIfEnabled();

    // Performance mode: lightweight startup with only vocal+guitar fusion/snapping
    bool m_performanceMode = true;
};

#endif // MAINWINDOW_H