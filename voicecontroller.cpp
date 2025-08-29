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
#include <QProcess>
#include <QProcessEnvironment>
#include <algorithm>

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
    qDebug() << "VoiceController: Loading tracks from:" << m_preset.settings.backingTrackDirectory;
    QFileInfoList fileInfoList = backingTracksDir.entryInfoList(QStringList() << "*.mp3", QDir::Files);
    for (const QFileInfo& fileInfo : fileInfoList) {
        m_backingTracks.append(fileInfo.absoluteFilePath());
    }
    m_backingTracks.sort();
    qDebug() << "VoiceController: Loaded" << m_backingTracks.size() << "tracks";
    if (m_backingTracks.size() > 0) {
        for (const QString& track : m_backingTracks) {
            QFileInfo fi(track);
            QString fullName = fi.fileName();
            QString displayName = fullName.left(fullName.lastIndexOf('.'));
            qDebug() << "  Track:" << displayName;
        }
    }
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
                << QCoreApplication::applicationDirPath() + "/../Resources/voice_bridge.py" // Inside bundle Resources
                << QCoreApplication::applicationDirPath() + "/voice_bridge.py"           // Next to executable
                << QDir::currentPath() + "/voice_bridge.py"                             // Current directory
                << "/Users/teddybergsman/Documents/Cursor Projects/CppMidiProcessor/voice_bridge.py"; // Absolute path
    
    for (const QString& path : searchPaths) {
        QFileInfo scriptFile(path);
        if (scriptFile.exists()) {
            scriptPath = path;
            break;
        }
    }
    
    if (scriptPath.isEmpty()) {
        emit errorOccurred("Voice bridge script not found");
        return false;
    }
    
    // Prepare environment and choose Python interpreter explicitly for Finder launches
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PATH", "/opt/homebrew/bin:/usr/local/bin:" + env.value("PATH"));
    env.insert("PYTHONUNBUFFERED", "1");
    m_bridgeProcess->setProcessEnvironment(env);
    m_bridgeProcess->setWorkingDirectory(QFileInfo(scriptPath).absolutePath());

    // Choose a Python interpreter that can import rt_stt
    QString pythonExe;
    QString envPython = QString::fromUtf8(qgetenv("RTSTT_PYTHON"));
    QStringList pythonCandidates;
    if (!envPython.isEmpty()) pythonCandidates << envPython;
    pythonCandidates << "python3" << "/opt/homebrew/bin/python3" << "/usr/local/bin/python3" << "python";

    auto canImportRtStt = [&](const QString& exe) -> bool {
        if (exe.startsWith("/") && !QFileInfo::exists(exe)) return false;
        QProcess checkProc;
        checkProc.setProcessEnvironment(env);
        int code = checkProc.execute(exe, QStringList() << "-c" << "import rt_stt");
        return code == 0;
    };

    for (const QString& cand : pythonCandidates) {
        if (canImportRtStt(cand)) { pythonExe = cand; break; }
    }
    if (pythonExe.isEmpty()) {
        // Fallback to python3 even if import test failed; surface a clear error to UI
        pythonExe = "python3";
        emit errorOccurred("rt_stt Python package not found in any interpreter (tried: " + pythonCandidates.join(", ") + ")");
    }

    // Start the Python bridge process
    m_bridgeProcess->start(pythonExe, QStringList() << scriptPath);
    
    if (!m_bridgeProcess->waitForStarted(5000)) {
        emit errorOccurred("Failed to start voice bridge: " + m_bridgeProcess->errorString());
        delete m_bridgeProcess;
        m_bridgeProcess = nullptr;
        return false;
    }
    
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
    
    // Also read and log stderr for debugging
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
        
        // Parse JSON
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (doc.isObject()) {
            processIncomingMessage(doc.object());
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
    
    if (type == "ready") {
        QString status = message["status"].toString();
        if (status == "connected") {
            emit connectionStatusChanged(true);
        } else if (status == "listening") {
            // Ready to receive transcriptions
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
    
    // Remove punctuation from the end
    QString cleanText = text.trimmed();
    while (!cleanText.isEmpty() && (cleanText.endsWith('.') || cleanText.endsWith(',') || 
           cleanText.endsWith('!') || cleanText.endsWith('?') || cleanText.endsWith(';') ||
           cleanText.endsWith(':'))) {
        cleanText.chop(1);
    }
    // Normalize commas inside the sentence so phrases like "quick, switch" still match
    cleanText.remove(',');
    
    QString lowerText = cleanText.toLower();
    QStringList detectedTriggers, detectedTargets;
    detectTriggerWords(lowerText, detectedTriggers, detectedTargets);
    
    // Emit the transcription with detected triggers and targets
    emit transcriptionReceived(text, confidence, detectedTriggers, detectedTargets);
    
    // Try to parse different command types
    qDebug() << "VoiceController: Attempting to parse command:" << lowerText;
    
    // Try quick switch first since it's more specific
    if (parseQuickSwitchCommand(lowerText)) {
        qDebug() << "VoiceController: Matched as quick switch command";
        return;
    }
    
    if (parseProgramCommand(lowerText)) {
        qDebug() << "VoiceController: Matched as program command";
        return;
    }
    
    if (parseTrackCommand(lowerText)) {
        qDebug() << "VoiceController: Matched as track command";
        return;
    }
    
    if (parseToggleCommand(lowerText)) {
        qDebug() << "VoiceController: Matched as toggle command";
        return;
    }
    
    qDebug() << "VoiceController: No command matched";
}

bool VoiceControllerWorker::parseQuickSwitchCommand(const QString& text) {
    // Check if the command is "quick switch"
    if (text.contains("quick switch")) {
        // Find the current program
        for (int i = 0; i < m_preset.programs.size(); ++i) {
            const Program& program = m_preset.programs[i];
            if (!program.quickSwitch.isEmpty()) {
                // Try to find the target program by name
                int targetIndex = findProgramByNameOrTag(program.quickSwitch);
                if (targetIndex >= 0) {
                    emit programCommandDetected(targetIndex);
                    return true;
                }
            }
        }
    }
    return false;
}

void VoiceControllerWorker::detectTriggerWords(const QString& text, QStringList& triggers, QStringList& targets) {
    QString lowerText = text.toLower();
    
    // Program switching triggers (yellow)
    QStringList switchTriggers = {"switch", "switched", "change", "changed", "go to", "go", "quick switch"};
    for (const QString& trigger : switchTriggers) {
        if (lowerText.contains(trigger)) {
            triggers << trigger;
        }
    }
    
    // Track control triggers (yellow)
    if (lowerText.contains("play")) {
        triggers << "play";
    }
    if (lowerText.contains("stop") || lowerText.contains("pause")) {
        if (lowerText.contains("stop")) triggers << "stop";
        if (lowerText.contains("pause")) triggers << "pause";
    }
    
    // Toggle triggers (yellow)
    if (lowerText.contains("toggle") || lowerText.contains("turn on") || lowerText.contains("turn off")) {
        if (lowerText.contains("toggle")) triggers << "toggle";
        if (lowerText.contains("turn on")) triggers << "turn on";
        if (lowerText.contains("turn off")) triggers << "turn off";
    }
    
    // Program names and tags (green targets)
    for (const auto& program : m_preset.programs) {
        if (lowerText.contains(program.name.toLower())) {
            targets << program.name.toLower();
        }
        for (const QString& tag : program.tags) {
            if (lowerText.contains(tag.toLower())) {
                targets << tag.toLower();
            }
        }
    }
    
    // Numbers and "program"/"track" keywords (green targets)
    // Include all number words
    QStringList numberWords;
    for (auto it = m_numberWords.begin(); it != m_numberWords.end(); ++it) {
        numberWords << it.key();
    }
    QString numberPattern = numberWords.join("|");
    
    QRegularExpression numRe("\\b(program\\s*\\d+|track\\s*\\d+|\\d+|" + numberPattern + ")\\b");
    QRegularExpressionMatchIterator matchIt = numRe.globalMatch(lowerText);
    while (matchIt.hasNext()) {
        QRegularExpressionMatch match = matchIt.next();
        targets << match.captured(0);
    }
    
    // Track names if "play" was mentioned (green targets)
    if (lowerText.contains("play")) {
        // Find what track would be matched by fuzzy search
        QRegularExpression playRe("play(?:\\s+play)*\\s*(?:the|a)?\\s*(.+)");
        QRegularExpressionMatch match = playRe.match(lowerText);
        
        if (match.hasMatch()) {
            QString trackQuery = match.captured(1).trimmed();
            QString matchedTrack = fuzzyMatchTrackName(trackQuery);
            
            if (!matchedTrack.isEmpty()) {
                QFileInfo fi(matchedTrack);
                QString fullName = fi.fileName();
                QString trackName = fullName.left(fullName.lastIndexOf('.')).toLower();
                
                // Remove leading numbers and punctuation
                QRegularExpression leadingNum("^\\d+\\.\\s*");
                trackName.remove(leadingNum);
                
                // Add individual words from the matched track name
                QStringList trackWords = trackName.split(QRegularExpression("[\\s_.-]+"), Qt::SkipEmptyParts);
                for (const QString& word : trackWords) {
                    if (lowerText.contains(word) && word.length() > 2) {
                        targets << word;
                    }
                }
            }
        }
    }
    
    // Remove duplicates
    triggers.removeDuplicates();
    targets.removeDuplicates();
}

bool VoiceControllerWorker::parseProgramCommand(const QString& text) {
    // More flexible pattern that handles multiple triggers and various connecting words
    // This will match things like "switch switch to trumpet" or "go saxophone" etc.
    QRegularExpression switchRe("(?:switch|switched|change|changed|go|going)(?:\\s+(?:switch|switched|change|changed|go|going))*\\s*(?:to\\s+the|to\\s+a|to)?\\s*(.+)");
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
    
    // More flexible play pattern that handles multiple "play" words and optional connecting words
    QRegularExpression playRe("play(?:\\s+play)*\\s*(?:the|a)?\\s*(.+)");
    QRegularExpressionMatch match = playRe.match(text);
    
    if (match.hasMatch()) {
        QString target = match.captured(1).trimmed();
        qDebug() << "VoiceController: parseTrackCommand - target:" << target;
        qDebug() << "VoiceController: Available tracks:" << m_backingTracks;
        
        // Convert number words to digits
        QString convertedTarget = convertNumberWordsToDigits(target);
        
        // Check if it's a track number
        QRegularExpression trackNumRe("track\\s*(\\d+)");
        QRegularExpressionMatch numMatch = trackNumRe.match(convertedTarget);
        
        if (numMatch.hasMatch()) {
            int trackNum = numMatch.captured(1).toInt();
            // Convert 1-based to 0-based index
            if (trackNum > 0 && trackNum <= m_backingTracks.size()) {
                qDebug() << "VoiceController: Detected track number:" << trackNum;
                emit trackCommandDetected(trackNum - 1, true);
                return true;
            }
        }
        
        // Check if it's just a number
        bool isNumber;
        int num = convertedTarget.toInt(&isNumber);
        if (isNumber && num > 0 && num <= m_backingTracks.size()) {
            qDebug() << "VoiceController: Detected number:" << num;
            emit trackCommandDetected(num - 1, true);
            return true;
        }
        
        // Try fuzzy matching on track name
        QString matchedTrack = fuzzyMatchTrackName(target);
        qDebug() << "VoiceController: Fuzzy match result:" << matchedTrack;
        if (!matchedTrack.isEmpty()) {
            int index = m_backingTracks.indexOf(matchedTrack);
            if (index >= 0) {
                qDebug() << "VoiceController: Playing track at index:" << index << "path:" << matchedTrack;
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
    
    // Generate compound numbers from 21 to 99
    QStringList tens = {"twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
    QStringList ones = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
    
    for (int i = 0; i < tens.size(); ++i) {
        for (int j = 0; j < ones.size(); ++j) {
            int value = (i + 2) * 10 + (j + 1);
            QString word = tens[i] + "-" + ones[j];
            m_numberWords[word] = value;
            // Also without hyphen
            m_numberWords[tens[i] + " " + ones[j]] = value;
        }
    }
    
    // Numbers from 100 to 128
    m_numberWords["one hundred"] = 100;
    m_numberWords["hundred one"] = 101;
    m_numberWords["one hundred one"] = 101;
    m_numberWords["hundred two"] = 102;
    m_numberWords["one hundred two"] = 102;
    m_numberWords["hundred three"] = 103;
    m_numberWords["one hundred three"] = 103;
    m_numberWords["hundred four"] = 104;
    m_numberWords["one hundred four"] = 104;
    m_numberWords["hundred five"] = 105;
    m_numberWords["one hundred five"] = 105;
    m_numberWords["hundred six"] = 106;
    m_numberWords["one hundred six"] = 106;
    m_numberWords["hundred seven"] = 107;
    m_numberWords["one hundred seven"] = 107;
    m_numberWords["hundred eight"] = 108;
    m_numberWords["one hundred eight"] = 108;
    m_numberWords["hundred nine"] = 109;
    m_numberWords["one hundred nine"] = 109;
    m_numberWords["hundred ten"] = 110;
    m_numberWords["one hundred ten"] = 110;
    m_numberWords["hundred eleven"] = 111;
    m_numberWords["one hundred eleven"] = 111;
    m_numberWords["hundred twelve"] = 112;
    m_numberWords["one hundred twelve"] = 112;
    m_numberWords["hundred thirteen"] = 113;
    m_numberWords["one hundred thirteen"] = 113;
    m_numberWords["hundred fourteen"] = 114;
    m_numberWords["one hundred fourteen"] = 114;
    m_numberWords["hundred fifteen"] = 115;
    m_numberWords["one hundred fifteen"] = 115;
    m_numberWords["hundred sixteen"] = 116;
    m_numberWords["one hundred sixteen"] = 116;
    m_numberWords["hundred seventeen"] = 117;
    m_numberWords["one hundred seventeen"] = 117;
    m_numberWords["hundred eighteen"] = 118;
    m_numberWords["one hundred eighteen"] = 118;
    m_numberWords["hundred nineteen"] = 119;
    m_numberWords["one hundred nineteen"] = 119;
    m_numberWords["hundred twenty"] = 120;
    m_numberWords["one hundred twenty"] = 120;
    
    // Generate 121-128
    for (int i = 1; i <= 8; ++i) {
        QString word1 = "hundred twenty-" + ones[i-1];
        QString word2 = "one hundred twenty-" + ones[i-1];
        QString word3 = "hundred twenty " + ones[i-1];
        QString word4 = "one hundred twenty " + ones[i-1];
        m_numberWords[word1] = 120 + i;
        m_numberWords[word2] = 120 + i;
        m_numberWords[word3] = 120 + i;
        m_numberWords[word4] = 120 + i;
    }
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
    
    qDebug() << "VoiceController: fuzzyMatchTrackName - input:" << input;
    
    for (const QString& track : m_backingTracks) {
        QFileInfo fi(track);
        QString fullName = fi.fileName(); // Get full filename including extension
        QString trackName = fullName.left(fullName.lastIndexOf('.')).toLower(); // Remove extension
        
        // Remove leading numbers and punctuation (e.g., "1. " from "1. My Funny Valentine")
        QString cleanTrackName = trackName;
        QRegularExpression leadingNum("^\\d+\\.\\s*");
        cleanTrackName.remove(leadingNum);
        
        // Split both input and track name into words
        QStringList inputWords = input.toLower().split(QRegularExpression("[\\s_.-]+"), Qt::SkipEmptyParts);
        QStringList trackWords = cleanTrackName.split(QRegularExpression("[\\s_.-]+"), Qt::SkipEmptyParts);
        
        qDebug() << "  Comparing with track:" << trackName << "clean:" << cleanTrackName;
        qDebug() << "  Input words:" << inputWords;
        qDebug() << "  Track words:" << trackWords;
        
        int score = 0;
        
        // Check if all input words appear in the track name
        bool allWordsFound = true;
        for (const QString& inputWord : inputWords) {
            bool wordFound = false;
            for (const QString& trackWord : trackWords) {
                if (trackWord.startsWith(inputWord) || trackWord == inputWord) {
                    wordFound = true;
                    score += inputWord.length() * 2; // Bonus for exact/prefix match
                    break;
                } else if (trackWord.contains(inputWord) && inputWord.length() > 2) {
                    wordFound = true;
                    score += inputWord.length(); // Lower score for partial match
                }
            }
            if (!wordFound) {
                allWordsFound = false;
            }
        }
        
        // Bonus if all words were found
        if (allWordsFound && inputWords.size() > 0) {
            score += 10;
        }
        
        // Bonus for matching word order
        QString inputJoined = inputWords.join(" ");
        QString trackJoined = trackWords.join(" ");
        if (trackJoined.contains(inputJoined)) {
            score += 20;
        }
        
        qDebug() << "  Score:" << score << "allWordsFound:" << allWordsFound;
        
        if (score > bestScore) {
            bestScore = score;
            bestMatch = track;
        }
    }
    
    qDebug() << "VoiceController: Best match score:" << bestScore << "track:" << bestMatch;
    
    // Only return a match if we have a reasonable score
    return (bestScore > 5) ? bestMatch : QString();
}

int VoiceControllerWorker::findProgramByNameOrTag(const QString& search) {
    QString searchLower = search.toLower().trimmed();
    
    // Remove common filler words that might appear after the trigger
    QStringList fillerWords = {"the", "a", "an", "to"};
    QStringList searchWords = searchLower.split(' ', Qt::SkipEmptyParts);
    searchWords.erase(std::remove_if(searchWords.begin(), searchWords.end(),
        [&fillerWords](const QString& word) { 
            return fillerWords.contains(word); 
        }), searchWords.end());
    QString cleanSearch = searchWords.join(' ');
    
    // First try exact name match
    for (int i = 0; i < m_preset.programs.size(); ++i) {
        if (m_preset.programs[i].name.toLower() == cleanSearch) {
            return i;
        }
    }
    
    // Try exact tag match
    for (int i = 0; i < m_preset.programs.size(); ++i) {
        for (const QString& tag : m_preset.programs[i].tags) {
            if (tag.toLower() == cleanSearch) {
                return i;
            }
        }
    }
    
    // Then try if any search word matches a program name
    for (const QString& word : searchWords) {
        for (int i = 0; i < m_preset.programs.size(); ++i) {
            if (m_preset.programs[i].name.toLower() == word) {
                return i;
            }
        }
    }
    
    // Try if any search word matches a tag
    for (const QString& word : searchWords) {
        for (int i = 0; i < m_preset.programs.size(); ++i) {
            for (const QString& tag : m_preset.programs[i].tags) {
                if (tag.toLower() == word) {
                    return i;
                }
            }
        }
    }
    
    // Finally try partial matches
    for (int i = 0; i < m_preset.programs.size(); ++i) {
        if (m_preset.programs[i].name.toLower().contains(cleanSearch)) {
            return i;
        }
        for (const QString& tag : m_preset.programs[i].tags) {
            if (tag.toLower().contains(cleanSearch)) {
                return i;
            }
        }
    }
    
    return -1;
}
