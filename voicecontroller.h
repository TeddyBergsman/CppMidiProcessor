#ifndef VOICECONTROLLER_H
#define VOICECONTROLLER_H

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <atomic>
#include "PresetData.h"

class VoiceControllerWorker;

class VoiceController : public QObject {
    Q_OBJECT

public:
    explicit VoiceController(const Preset& preset, QObject *parent = nullptr);
    ~VoiceController();

    void start();
    void stop();
    bool isConnected() const { return m_connected; }
    void setEnabled(bool enabled);

public slots:
    void onProgramChanged(int programIndex);

signals:
    void transcriptionReceived(const QString& text, double confidence, const QStringList& detectedTriggers, const QStringList& detectedTargets);
    void connectionStatusChanged(bool connected);
    void errorOccurred(const QString& error);
    void programCommandDetected(int programIndex);
    void trackCommandDetected(int trackIndex, bool play); // play=true for play, false for stop
    void toggleCommandDetected(const QString& toggleId);

private:
    const Preset& m_preset;
    VoiceControllerWorker* m_worker;
    QThread* m_workerThread;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_enabled{true};
};

// Worker class that runs in separate thread
class VoiceControllerWorker : public QObject {
    Q_OBJECT

public:
    explicit VoiceControllerWorker(const Preset& preset, QObject *parent = nullptr);
    ~VoiceControllerWorker();

public slots:
    void start();
    void stop();
    void setEnabled(bool enabled);
    void onProgramChanged(int programIndex);

signals:
    void transcriptionReceived(const QString& text, double confidence, const QStringList& detectedTriggers, const QStringList& detectedTargets);
    void connectionStatusChanged(bool connected);
    void errorOccurred(const QString& error);
    void programCommandDetected(int programIndex);
    void trackCommandDetected(int trackIndex, bool play);
    void toggleCommandDetected(const QString& toggleId);

private slots:
    void onProcessReadyRead();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    struct TranscriptionData {
        QString text;
        double confidence;
        QString language;
        bool isFinal;
    };

    // Process management
    bool startBridgeProcess();
    void stopBridgeProcess();
    void processIncomingMessage(const QJsonObject& message);
    
    // Command parsing
    void parseVoiceCommand(const QString& text, double confidence);
    void detectTriggerWords(const QString& text, QStringList& triggers, QStringList& targets);
    bool parseQuickSwitchCommand(const QString& text);
    bool parseProgramCommand(const QString& text);
    bool parseTrackCommand(const QString& text);
    bool parseToggleCommand(const QString& text);
    
    // Number word conversion
    QString convertNumberWordsToDigits(const QString& text);
    int wordToNumber(const QString& word);
    QString fuzzyMatchTrackName(const QString& input);
    int findProgramByNameOrTag(const QString& search);

    const Preset& m_preset;
    QProcess* m_bridgeProcess = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_enabled{true};
    std::atomic<int> m_currentProgramIndex{-1};
    QStringList m_backingTracks; // Will be populated from directory
    QString m_buffer; // Buffer for incomplete JSON lines
    
    // Number word mappings
    QMap<QString, int> m_numberWords;
    void initializeNumberWords();
};

#endif // VOICECONTROLLER_H
