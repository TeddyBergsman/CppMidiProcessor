#include "voicecontroller.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QThread>

// VoiceController implementation
VoiceController::VoiceController(const Preset& preset, QObject *parent)
    : QObject(parent), m_preset(preset) {
    
    m_worker = new VoiceControllerWorker(preset);
    m_workerThread = new QThread(this);
    m_worker->moveToThread(m_workerThread);
    
    // Connect signals from worker to this
    connect(m_worker, &VoiceControllerWorker::transcriptionReceived,
            this, &VoiceController::transcriptionReceived);
    connect(m_worker, &VoiceControllerWorker::connectionStatusChanged,
            [this](bool connected) {
                m_connected = connected;
                emit connectionStatusChanged(connected);
            });
    connect(m_worker, &VoiceControllerWorker::errorOccurred,
            this, &VoiceController::errorOccurred);
    connect(m_worker, &VoiceControllerWorker::programCommandDetected,
            this, &VoiceController::programCommandDetected);
    connect(m_worker, &VoiceControllerWorker::trackCommandDetected,
            this, &VoiceController::trackCommandDetected);
    connect(m_worker, &VoiceControllerWorker::toggleCommandDetected,
            this, &VoiceController::toggleCommandDetected);
    
    // Thread control
    connect(m_workerThread, &QThread::started, m_worker, &VoiceControllerWorker::start);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
}

VoiceController::~VoiceController() {
    stop();
}

void VoiceController::start() {
    if (m_preset.settings.voiceControlEnabled) {
        m_workerThread->start();
    }
}

void VoiceController::stop() {
    if (m_workerThread->isRunning()) {
        m_worker->stop();
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void VoiceController::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (m_worker) {
        m_worker->setEnabled(enabled);
    }
}

// VoiceControllerWorker implementation
VoiceControllerWorker::VoiceControllerWorker(const Preset& preset, QObject *parent)
    : QObject(parent), m_preset(preset) {
    initializeNumberWords();
    
    // Load backing tracks list
    QDir backingTracksDir(m_preset.settings.backingTrackDirectory);
    QFileInfoList fileInfoList = backingTracksDir.entryInfoList(QStringList() << "*.mp3", QDir::Files);
    for (const QFileInfo& fileInfo : fileInfoList) {
        m_backingTracks.append(fileInfo.absoluteFilePath());
    }
    m_backingTracks.sort();
}

VoiceControllerWorker::~VoiceControllerWorker() {
    stop();
}

void VoiceControllerWorker::start() {
    m_running = true;
    if (!startBridgeProcess()) {
        emit errorOccurred("Failed to start voice bridge process");
    }
}

void VoiceControllerWorker::stop() {
    m_running = false;
    stopBridgeProcess();
}

void VoiceControllerWorker::setEnabled(bool enabled) {
    m_enabled = enabled;
}

bool VoiceControllerWorker::startBridgeProcess() {
    if (m_bridgeProcess) {
        stopBridgeProcess();
    }
    
    m_bridgeProcess = new QProcess(this);
    
    // Connect process signals
    connect(m_bridgeProcess, &QProcess::readyRead,
            this, &VoiceControllerWorker::onProcessReadyRead);
    connect(m_bridgeProcess, &QProcess::errorOccurred,
            this, &VoiceControllerWorker::onProcessError);
    connect(m_bridgeProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VoiceControllerWorker::onProcessFinished);
    
    // Find the Python script
    QString scriptPath;
    QStringList searchPaths;
    
    // Try various locations
    searchPaths << QCoreApplication::applicationDirPath() + "/../../../voice_bridge.py"  // From app bundle
                << QCoreApplication::applicationDirPath() + "/voice_bridge.py"           // Next to executable
                << QDir::currentPath() + "/voice_bridge.py"                             // Current directory
                << "/Users/teddybergsman/Documents/Cursor Projects/CppMidiProcessor/voice_bridge.py"; // Absolute path
    
    for (const QString& path : searchPaths) {
        QFileInfo scriptFile(path);
        if (scriptFile.exists()) {
            scriptPath = path;
            qDebug() << "VoiceController: Found voice_bridge.py at:" << scriptPath;
            break;
        }
    }
    
    if (scriptPath.isEmpty()) {
        qDebug() << "VoiceController: Searched for voice_bridge.py in:" << searchPaths;
        emit errorOccurred("Voice bridge script not found");
        return false;
    }
    
    // Start the Python bridge process
    qDebug() << "VoiceController: Starting Python bridge with command: python3" << scriptPath;
    m_bridgeProcess->start("python3", QStringList() << scriptPath);
    
    if (!m_bridgeProcess->waitForStarted(5000)) {
        emit errorOccurred("Failed to start voice bridge: " + m_bridgeProcess->errorString());
        delete m_bridgeProcess;
        m_bridgeProcess = nullptr;
        return false;
    }
    
    qDebug() << "VoiceController: Python bridge process started successfully";
    return true;
}

void VoiceControllerWorker::stopBridgeProcess() {
    if (m_bridgeProcess) {
        m_bridgeProcess->terminate();
        if (!m_bridgeProcess->waitForFinished(5000)) {
            m_bridgeProcess->kill();
        }
        delete m_bridgeProcess;
        m_bridgeProcess = nullptr;
    }
    emit connectionStatusChanged(false);
}

void VoiceControllerWorker::onProcessReadyRead() {
    if (!m_bridgeProcess) return;
    
    // Read stdout
    QByteArray data = m_bridgeProcess->readAllStandardOutput();
    m_buffer += QString::fromUtf8(data);
    
    // Also read and log stderr
    QByteArray errorData = m_bridgeProcess->readAllStandardError();
    if (!errorData.isEmpty()) {
        qDebug() << "VoiceController: Python stderr:" << QString::fromUtf8(errorData);
    }
    
    // Process complete lines
    while (m_buffer.contains('\n')) {
        int index = m_buffer.indexOf('\n');
        QString line = m_buffer.left(index);
        m_buffer = m_buffer.mid(index + 1);
        
        if (line.isEmpty()) continue;
        
        qDebug() << "VoiceController: Received line:" << line;
        
        // Parse JSON
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (doc.isObject()) {
            processIncomingMessage(doc.object());
        } else {
            qDebug() << "VoiceController: Failed to parse JSON:" << line;
        }
    }
}

void VoiceControllerWorker::onProcessError(QProcess::ProcessError error) {
    QString errorMsg;
    switch (error) {
        case QProcess::FailedToStart:
            errorMsg = "Voice bridge failed to start - check Python installation";
            break;
        case QProcess::Crashed:
            errorMsg = "Voice bridge crashed";
            break;
        case QProcess::Timedout:
            errorMsg = "Voice bridge timed out";
            break;
        default:
            errorMsg = "Voice bridge error";
    }
    emit errorOccurred(errorMsg);
    emit connectionStatusChanged(false);
}

void VoiceControllerWorker::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitStatus == QProcess::CrashExit) {
        emit errorOccurred("Voice bridge crashed");
    }
    emit connectionStatusChanged(false);
    
    // Try to restart if still running
    if (m_running) {
        QThread::msleep(1000);
        startBridgeProcess();
    }
}

void VoiceControllerWorker::processIncomingMessage(const QJsonObject& message) {
    QString type = message["type"].toString();
    
    qDebug() << "VoiceController: Processing message type:" << type;
    
    if (type == "ready") {
        QString status = message["status"].toString();
        qDebug() << "VoiceController: Ready status:" << status;
        if (status == "connected") {
            emit connectionStatusChanged(true);
        } else if (status == "listening") {
            // Ready to receive transcriptions
            qDebug() << "VoiceController: Bridge is now listening for transcriptions";
        }
    } else if (type == "transcription") {
        TranscriptionData transcription;
        transcription.text = message["text"].toString();
        transcription.confidence = message["confidence"].toDouble();
        transcription.language = message["language"].toString();
        transcription.isFinal = message["is_final"].toBool();
        
        // Process all transcriptions for real-time feedback
        if (m_enabled && !transcription.text.isEmpty()) {
            parseVoiceCommand(transcription.text, transcription.confidence);
        }
    } else if (type == "error") {
        QString error = message["error"].toString();
        emit errorOccurred(error);
    }
}

void VoiceControllerWorker::parseVoiceCommand(const QString& text, double confidence) {
    if (confidence < m_preset.settings.voiceConfidenceThreshold) {
        return;
    }
    
    QString lowerText = text.toLower().trimmed();
    QStringList detectedCommands = detectTriggerWords(lowerText);
    
    // Emit the transcription with detected commands
    emit transcriptionReceived(text, confidence, detectedCommands);
    
    // Try to parse different command types
    if (parseProgramCommand(lowerText)) {
        return;
    }
    
    if (parseTrackCommand(lowerText)) {
        return;
    }
    
    if (parseToggleCommand(lowerText)) {
        return;
    }
}

QStringList VoiceControllerWorker::detectTriggerWords(const QString& text) {
    QStringList triggers;
    
    // Program switching triggers
    if (text.contains("switch") || text.contains("switched") || text.contains("go to")) {
        triggers << "switch" << "switched" << "go to";
    }
    
    // Track control triggers
    if (text.contains("play")) {
        triggers << "play";
    }
    if (text.contains("stop") || text.contains("pause")) {
        triggers << "stop" << "pause";
    }
    
    // Toggle triggers
    if (text.contains("toggle") || text.contains("turn on") || text.contains("turn off")) {
        triggers << "toggle" << "turn on" << "turn off";
    }
    
    return triggers;
}

bool VoiceControllerWorker::parseProgramCommand(const QString& text) {
    // Look for program switching patterns
    QRegularExpression switchRe("(?:switch|change|go)\\s*(?:to)?\\s*(.+)");
    QRegularExpressionMatch match = switchRe.match(text);
    
    if (match.hasMatch()) {
        QString target = match.captured(1).trimmed();
        
        // Convert number words to digits
        QString convertedTarget = convertNumberWordsToDigits(target);
        
        // Check if it's a program number
        QRegularExpression programNumRe("program\\s*(\\d+)");
        QRegularExpressionMatch numMatch = programNumRe.match(convertedTarget);
        
        if (numMatch.hasMatch()) {
            int programNum = numMatch.captured(1).toInt();
            // Convert 1-based to 0-based index
            if (programNum > 0 && programNum <= m_preset.programs.size()) {
                emit programCommandDetected(programNum - 1);
                return true;
            }
        }
        
        // Check if it's just a number
        bool isNumber;
        int num = convertedTarget.toInt(&isNumber);
        if (isNumber && num > 0 && num <= m_preset.programs.size()) {
            emit programCommandDetected(num - 1);
            return true;
        }
        
        // Try to match by program name or tag
        int programIndex = findProgramByNameOrTag(target);
        if (programIndex >= 0) {
            emit programCommandDetected(programIndex);
            return true;
        }
    }
    
    return false;
}

bool VoiceControllerWorker::parseTrackCommand(const QString& text) {
    // Check for stop command first
    if (text.contains("stop") || (text.contains("pause") && !text.contains("play"))) {
        emit trackCommandDetected(-1, false); // -1 means stop current
        return true;
    }
    
    // Look for play patterns
    QRegularExpression playRe("play\\s+(.+)");
    QRegularExpressionMatch match = playRe.match(text);
    
    if (match.hasMatch()) {
        QString target = match.captured(1).trimmed();
        
        // Convert number words to digits
        QString convertedTarget = convertNumberWordsToDigits(target);
        
        // Check if it's a track number
        QRegularExpression trackNumRe("track\\s*(\\d+)");
        QRegularExpressionMatch numMatch = trackNumRe.match(convertedTarget);
        
        if (numMatch.hasMatch()) {
            int trackNum = numMatch.captured(1).toInt();
            // Convert 1-based to 0-based index
            if (trackNum > 0 && trackNum <= m_backingTracks.size()) {
                emit trackCommandDetected(trackNum - 1, true);
                return true;
            }
        }
        
        // Check if it's just a number
        bool isNumber;
        int num = convertedTarget.toInt(&isNumber);
        if (isNumber && num > 0 && num <= m_backingTracks.size()) {
            emit trackCommandDetected(num - 1, true);
            return true;
        }
        
        // Try fuzzy matching on track name
        QString matchedTrack = fuzzyMatchTrackName(target);
        if (!matchedTrack.isEmpty()) {
            int index = m_backingTracks.indexOf(matchedTrack);
            if (index >= 0) {
                emit trackCommandDetected(index, true);
                return true;
            }
        }
    }
    
    return false;
}

bool VoiceControllerWorker::parseToggleCommand(const QString& text) {
    // This is a placeholder for future toggle commands
    // You can implement track toggle commands here
    return false;
}

void VoiceControllerWorker::initializeNumberWords() {
    // Basic numbers
    m_numberWords["zero"] = 0; m_numberWords["one"] = 1; m_numberWords["two"] = 2;
    m_numberWords["three"] = 3; m_numberWords["four"] = 4; m_numberWords["five"] = 5;
    m_numberWords["six"] = 6; m_numberWords["seven"] = 7; m_numberWords["eight"] = 8;
    m_numberWords["nine"] = 9; m_numberWords["ten"] = 10; m_numberWords["eleven"] = 11;
    m_numberWords["twelve"] = 12; m_numberWords["thirteen"] = 13; m_numberWords["fourteen"] = 14;
    m_numberWords["fifteen"] = 15; m_numberWords["sixteen"] = 16; m_numberWords["seventeen"] = 17;
    m_numberWords["eighteen"] = 18; m_numberWords["nineteen"] = 19; m_numberWords["twenty"] = 20;
    m_numberWords["thirty"] = 30; m_numberWords["forty"] = 40; m_numberWords["fifty"] = 50;
    m_numberWords["sixty"] = 60; m_numberWords["seventy"] = 70; m_numberWords["eighty"] = 80;
    m_numberWords["ninety"] = 90; m_numberWords["hundred"] = 100;
    
    // Also add variations
    m_numberWords["first"] = 1; m_numberWords["second"] = 2; m_numberWords["third"] = 3;
    m_numberWords["fourth"] = 4; m_numberWords["fifth"] = 5; m_numberWords["sixth"] = 6;
    m_numberWords["seventh"] = 7; m_numberWords["eighth"] = 8; m_numberWords["ninth"] = 9;
    m_numberWords["tenth"] = 10;
}

QString VoiceControllerWorker::convertNumberWordsToDigits(const QString& text) {
    QString result = text;
    QStringList words = text.split(' ', Qt::SkipEmptyParts);
    
    for (int i = 0; i < words.size(); ++i) {
        QString word = words[i].toLower();
        
        // Handle compound numbers like "twenty-one"
        if (word.contains('-')) {
            QStringList parts = word.split('-');
            if (parts.size() == 2) {
                int tens = wordToNumber(parts[0]);
                int ones = wordToNumber(parts[1]);
                if (tens >= 20 && tens <= 90 && ones >= 1 && ones <= 9) {
                    result.replace(word, QString::number(tens + ones));
                    continue;
                }
            }
        }
        
        // Handle "one hundred" patterns
        if (i < words.size() - 1 && word == "one" && words[i+1] == "hundred") {
            result.replace("one hundred", "100");
            i++; // Skip "hundred"
            continue;
        }
        
        // Handle simple number words
        int num = wordToNumber(word);
        if (num >= 0) {
            result.replace(QRegularExpression("\\b" + word + "\\b"), QString::number(num));
        }
    }
    
    // Handle remaining compound numbers up to 128
    for (int n = 21; n <= 128; ++n) {
        if (n <= 99) {
            int tens = (n / 10) * 10;
            int ones = n % 10;
            if (ones > 0) {
                QString tensWord = m_numberWords.key(tens);
                QString onesWord = m_numberWords.key(ones);
                if (!tensWord.isEmpty() && !onesWord.isEmpty()) {
                    result.replace(tensWord + " " + onesWord, QString::number(n));
                }
            }
        } else if (n >= 101 && n <= 128) {
            int ones = n - 100;
            QString onesWord = m_numberWords.key(ones);
            if (!onesWord.isEmpty()) {
                result.replace("one hundred " + onesWord, QString::number(n));
            }
        }
    }
    
    return result;
}

int VoiceControllerWorker::wordToNumber(const QString& word) {
    return m_numberWords.value(word.toLower(), -1);
}

QString VoiceControllerWorker::fuzzyMatchTrackName(const QString& input) {
    QString bestMatch;
    int bestScore = 0;
    
    for (const QString& track : m_backingTracks) {
        QFileInfo fi(track);
        QString trackName = fi.baseName().toLower();
        
        // Simple fuzzy matching: count matching words
        QStringList inputWords = input.toLower().split(' ', Qt::SkipEmptyParts);
        int score = 0;
        
        for (const QString& word : inputWords) {
            if (trackName.contains(word)) {
                score += word.length(); // Longer matches score higher
            }
        }
        
        if (score > bestScore) {
            bestScore = score;
            bestMatch = track;
        }
    }
    
    // Only return a match if we have a reasonable score
    return (bestScore > 3) ? bestMatch : QString();
}

int VoiceControllerWorker::findProgramByNameOrTag(const QString& search) {
    QString searchLower = search.toLower();
    
    // First try exact name match
    for (int i = 0; i < m_preset.programs.size(); ++i) {
        if (m_preset.programs[i].name.toLower() == searchLower) {
            return i;
        }
    }
    
    // Then try partial name match
    for (int i = 0; i < m_preset.programs.size(); ++i) {
        if (m_preset.programs[i].name.toLower().contains(searchLower)) {
            return i;
        }
    }
    
    // Finally try tag match
    for (int i = 0; i < m_preset.programs.size(); ++i) {
        for (const QString& tag : m_preset.programs[i].tags) {
            if (tag.toLower() == searchLower || tag.toLower().contains(searchLower)) {
                return i;
            }
        }
    }
    
    return -1;
}
