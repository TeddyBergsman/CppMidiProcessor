#include "NoteMonitorWidget.h"
#include "WaveVisualizer.h"
#include "PitchMonitorWidget.h"
#include "PitchColor.h"
#include "chart/SongChartWidget.h"
#include "chart/IRealProgressionParser.h"
#include "ireal/IRealTypes.h"
#include "playback/VirtuosoBalladMvpPlaybackEngine.h"
#include "midiprocessor.h"
#include "virtuoso/groove/GrooveRegistry.h"
#include <QtWidgets>
#include <cmath>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QCryptographicHash>
#include <QSettings>
#include <QJsonDocument>
#include <QEasingCurve>
#include <QImage>

namespace {
static QString normalizeKeyCenter(const QString& s) {
    return s.trimmed().toLower();
}

static QString stripDefaultSuffix(QString s) {
    s = s.trimmed();
    static const QString suffix = " (*)";
    if (s.endsWith(suffix)) {
        s.chop(suffix.size());
        s = s.trimmed();
    }
    return s;
}

static QString shortKeyLabelFromKeyCenter(const QString& keyCenter) {
    // "Bb major" -> "Bb", "G minor" -> "G-"
    const QString trimmed = keyCenter.trimmed();
    if (trimmed.isEmpty()) return {};
    const QString lower = trimmed.toLower();
    const bool isMinor = lower.contains("minor");
    const QString tonic = trimmed.split(' ', Qt::SkipEmptyParts).value(0);
    if (tonic.isEmpty()) return {};
    return isMinor ? (tonic + "-") : tonic;
}

static QString keyCenterFromShortLabel(const QString& shortLabel) {
    // "Bb" -> "Bb major", "G-" -> "G minor"
    QString s = shortLabel.trimmed();
    if (s.isEmpty()) return {};
    const bool isMinor = s.endsWith('-');
    if (isMinor) s.chop(1);
    s = s.trimmed();
    if (s.isEmpty()) return {};
    return s + (isMinor ? " minor" : " major");
}

static QStringList orderedMajorKeyCenters() {
    // Requested ordering: C, Db, D, Eb, E, F, Gb, G, Ab, A, Bb, B
    return {
        "C major",
        "Db major",
        "D major",
        "Eb major",
        "E major",
        "F major",
        "Gb major",
        "G major",
        "Ab major",
        "A major",
        "Bb major",
        "B major",
    };
}

static QStringList orderedMinorKeyCenters() {
    // Requested ordering: A, Bb, C, C# ... (chromatic from A with preferred spellings)
    return {
        "A minor",
        "Bb minor",
        "B minor",
        "C minor",
        "C# minor",
        "D minor",
        "Eb minor",
        "E minor",
        "F minor",
        "F# minor",
        "G minor",
        "Ab minor",
    };
}

static QStringList keyCentersForMode(bool isMinor) {
    return isMinor ? orderedMinorKeyCenters() : orderedMajorKeyCenters();
}

static QIcon makeWhiteIcon(const QIcon& src, const QSize& logicalSize, qreal dpr) {
    auto tint = [&](const QPixmap& pm) -> QPixmap {
        if (pm.isNull()) return pm;
        QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const int w = img.width();
        const int h = img.height();
        for (int y = 0; y < h; ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int a = qAlpha(row[x]);
                row[x] = qRgba(255, 255, 255, a);
            }
        }
        QPixmap out = QPixmap::fromImage(img);
        out.setDevicePixelRatio(pm.devicePixelRatio());
        return out;
    };

    const QSize pxSize = QSize(int(std::round(logicalSize.width() * dpr)),
                               int(std::round(logicalSize.height() * dpr)));

    QIcon out;
    const QPixmap normal = src.pixmap(pxSize, QIcon::Normal, QIcon::Off);
    const QPixmap disabled = src.pixmap(pxSize, QIcon::Disabled, QIcon::Off);
    out.addPixmap(tint(normal), QIcon::Normal, QIcon::Off);
    out.addPixmap(tint(disabled), QIcon::Disabled, QIcon::Off);
    return out;
}

static void setVirtuosoTransportButtonUi(QPushButton* b, QStyle* style, bool isPlaying) {
    if (!b) return;
    if (!style) return;
    // Slightly smaller stop icon looks better in a compact square button.
    const QSize playSz(18, 18);
    const QSize stopSz(14, 14);
    b->setIconSize(isPlaying ? stopSz : playSz);
    const QStyle::StandardPixmap sp = isPlaying ? QStyle::SP_MediaStop : QStyle::SP_MediaPlay;
    const QIcon base = style->standardIcon(sp, /*opt=*/nullptr, /*widget=*/b);
    b->setIcon(makeWhiteIcon(base, b->iconSize(), b->devicePixelRatioF()));
    b->setToolTip(isPlaying ? "Stop" : "Play");
    // Keep it screen-reader friendly (macOS VoiceOver, etc.)
    b->setAccessibleName(isPlaying ? "Stop" : "Play");
}

static void updateComboPopupToShowAllItems(QComboBox* combo) {
    if (!combo) return;
    const int n = combo->count();
    if (n <= 0) return;

    combo->setMaxVisibleItems(n);
    if (auto* v = combo->view()) {
        // Try to eliminate scrolling by sizing the popup to fit all items.
        const int rowH = std::max(18, v->sizeHintForRow(0));
        const int frame = 6;
        v->setMinimumHeight(rowH * n + frame);
        v->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
}

static void populateKeyCombo(QComboBox* combo,
                             bool isMinorSong,
                             const QString& detectedDefaultKeyCenter,
                             const QString& selectedKeyCenter) {
    if (!combo) return;
    const QString defNorm = normalizeKeyCenter(detectedDefaultKeyCenter);

    const bool prevSignals = combo->blockSignals(true);
    combo->clear();

    const QStringList keys = keyCentersForMode(isMinorSong);
    for (const QString& k : keys) {
        QString label = shortKeyLabelFromKeyCenter(k);
        if (!defNorm.isEmpty() && normalizeKeyCenter(k) == defNorm) {
            label += " (*)";
        }
        combo->addItem(label);
        combo->setItemData(combo->count() - 1, k, Qt::UserRole); // store canonical key (no suffix)
    }

    auto findIndexByValue = [&](const QString& value) -> int {
        for (int i = 0; i < combo->count(); ++i) {
            if (normalizeKeyCenter(combo->itemData(i, Qt::UserRole).toString()) == normalizeKeyCenter(value)) {
                return i;
            }
        }
        return -1;
    };

    int idx = findIndexByValue(selectedKeyCenter);
    if (idx < 0 && !selectedKeyCenter.isEmpty()) {
        // If the detected/overridden key isn't in our list (rare), prepend it (still mode-consistent).
        QString label = selectedKeyCenter;
        if (!defNorm.isEmpty() && normalizeKeyCenter(selectedKeyCenter) == defNorm) {
            label += " (*)";
        }
        combo->insertItem(0, label);
        combo->setItemData(0, selectedKeyCenter, Qt::UserRole);
        idx = 0;
    }
    if (idx >= 0) combo->setCurrentIndex(idx);

    // Keep the closed combo label clean (no "(default)").
    if (combo->isEditable() && combo->lineEdit()) {
        const QString value = combo->currentData(Qt::UserRole).toString();
        const QString shortLabel = value.isEmpty()
            ? stripDefaultSuffix(combo->currentText())
            : shortKeyLabelFromKeyCenter(value);
        combo->lineEdit()->setText(shortLabel);
    }

    updateComboPopupToShowAllItems(combo);
    combo->blockSignals(prevSignals);
}

static int pitchClassFromSpelling(const QString& letter, const QString& accidental) {
    if (letter.isEmpty()) return -1;
    const QChar c = letter[0].toUpper();
    int pc = -1;
    switch (c.unicode()) {
        case 'C': pc = 0; break;
        case 'D': pc = 2; break;
        case 'E': pc = 4; break;
        case 'F': pc = 5; break;
        case 'G': pc = 7; break;
        case 'A': pc = 9; break;
        case 'B': pc = 11; break;
        default: return -1;
    }
    if (!accidental.isEmpty()) {
        const QChar a = accidental[0];
        if (a == QChar('#') || a == QChar(0x266F)) pc += 1;      // # / ♯
        else if (a == QChar('b') || a == QChar(0x266D)) pc -= 1; // b / ♭
    }
    pc %= 12;
    if (pc < 0) pc += 12;
    return pc;
}

static QString canonicalKeyNameFromPitchClass(int pc, bool isMinor) {
    pc %= 12;
    if (pc < 0) pc += 12;
    // Match dropdown spellings.
    static const QString majorNames[12] = {
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
    };
    static const QString minorNames[12] = {
        "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
    };
    return isMinor ? minorNames[pc] : majorNames[pc];
}

static QString keyFieldToKeyCenter(const QString& keyField) {
    // iReal song key field examples: "Eb", "F#", "G-" (minor).
    QString k = keyField.trimmed();
    if (k.isEmpty()) return {};

    const bool isMinor = k.endsWith('-');
    if (isMinor) k.chop(1);
    if (k.isEmpty()) return {};

    const QString letter = k.left(1);
    QString acc;
    if (k.size() >= 2) {
        const QChar a = k[1];
        if (a == QChar('#') || a == QChar('b') || a == QChar(0x266F) || a == QChar(0x266D)) {
            acc = k.mid(1, 1);
        }
    }
    const int pc = pitchClassFromSpelling(letter, acc);
    if (pc < 0) {
        // Fallback to raw spelling if unexpected.
        return k + (isMinor ? " minor" : " major");
    }
    const QString canon = canonicalKeyNameFromPitchClass(pc, isMinor);
    return canon + (isMinor ? " minor" : " major");
}

static int pitchClassFromKeyCenter(const QString& keyCenter, bool* isMinorOut = nullptr) {
    const QString trimmed = keyCenter.trimmed();
    if (trimmed.isEmpty()) return -1;
    const QString lower = trimmed.toLower();
    const bool isMinor = lower.contains("minor");
    if (isMinorOut) *isMinorOut = isMinor;

    const QString token = trimmed.split(' ', Qt::SkipEmptyParts).value(0);
    if (token.isEmpty()) return -1;
    const QString letter = token.left(1);
    QString acc;
    if (token.size() >= 2) {
        const QChar a = token[1];
        if (a == QChar('#') || a == QChar('b') || a == QChar(0x266F) || a == QChar(0x266D)) {
            acc = token.mid(1, 1);
        }
    }
    return pitchClassFromSpelling(letter, acc);
}

static bool preferFlatsForKeyCenter(const QString& keyCenter) {
    const QString k = keyCenter.toLower();
    // Flat keys: F, Bb, Eb, Ab, Db, Gb, Cb (+ relative minors)
    static const QStringList flatKeys = {
        "f major","bb major","b♭ major","eb major","e♭ major","ab major","a♭ major","db major","d♭ major","gb major","g♭ major","cb major","c♭ major",
        "d minor","g minor","c minor","f minor","bb minor","b♭ minor","eb minor","e♭ minor","ab minor","a♭ minor"
    };
    for (const QString& fk : flatKeys) {
        if (k == fk) return true;
    }
    // Heuristic: any 'b'/'♭' in the tonic implies flat spelling.
    if (k.contains("b ") || k.contains(QChar(0x266D))) return true;
    return false;
}

static QString noteNameFromPitchClass(int pc, bool preferFlats) {
    const QChar sharp(0x266F);
    const QChar flat(0x266D);
    pc %= 12;
    if (pc < 0) pc += 12;
    static const QString sharpNames[12] = {
        "C", "C" + QString(sharp), "D", "D" + QString(sharp), "E", "F",
        "F" + QString(sharp), "G", "G" + QString(sharp), "A", "A" + QString(sharp), "B"
    };
    static const QString flatNames[12] = {
        "C", "D" + QString(flat), "D", "E" + QString(flat), "E", "F",
        "G" + QString(flat), "G", "A" + QString(flat), "A", "B" + QString(flat), "B"
    };
    return preferFlats ? flatNames[pc] : sharpNames[pc];
}

static QString transposeChordText(const QString& chordText, int semitoneDelta, bool preferFlats) {
    QString t = chordText.trimmed();
    if (t.isEmpty()) return chordText;
    if (t == "x") return chordText;

    // Split slash chords
    QString main = t;
    QString bass;
    const int slash = t.indexOf('/');
    if (slash >= 0) {
        main = t.left(slash);
        bass = t.mid(slash + 1);
    }

    // Extract parenthetical alternatives, e.g. Ao7(Bb7sus)
    QString paren;
    const int lp = main.indexOf('(');
    const int rp = main.lastIndexOf(')');
    if (lp >= 0 && rp > lp) {
        paren = main.mid(lp, rp - lp + 1);
        main = main.left(lp);
    }

    // Parse main root letter + accidental
    QString root;
    QString accidental;
    QString rest;
    if (!main.isEmpty() && main[0].isLetter()) {
        root = main.left(1);
        int pos = 1;
        if (pos < main.size() && (main[pos] == QChar(0x266D) || main[pos] == QChar(0x266F) || main[pos] == QChar('b') || main[pos] == QChar('#'))) {
            accidental = main.mid(pos, 1);
            pos += 1;
        }
        rest = main.mid(pos);
    } else {
        // Unknown format; leave unchanged
        return chordText;
    }

    const int pc = pitchClassFromSpelling(root, accidental);
    if (pc < 0) return chordText;
    const QString newRoot = noteNameFromPitchClass(pc + semitoneDelta, preferFlats);

    // Bass note (if present)
    QString newBass = bass;
    if (!bass.isEmpty() && bass[0].isLetter()) {
        QString bRoot = bass.left(1);
        QString bAcc;
        int pos = 1;
        if (pos < bass.size() && (bass[pos] == QChar(0x266D) || bass[pos] == QChar(0x266F) || bass[pos] == QChar('b') || bass[pos] == QChar('#'))) {
            bAcc = bass.mid(pos, 1);
        }
        const int bpc = pitchClassFromSpelling(bRoot, bAcc);
        if (bpc >= 0) {
            // Preserve any trailing characters after accidental (rare) by slicing remainder.
            QString bRemainder;
            if (!bAcc.isEmpty()) bRemainder = bass.mid(2);
            else bRemainder = bass.mid(1);
            newBass = noteNameFromPitchClass(bpc + semitoneDelta, preferFlats) + bRemainder;
        }
    }

    QString out = newRoot + rest + paren;
    if (!bass.isEmpty()) out += "/" + newBass;
    return out;
}

static chart::ChartModel transposeChartModel(const chart::ChartModel& in, int semitoneDelta, bool preferFlats) {
    if (semitoneDelta % 12 == 0) return in;
    chart::ChartModel out = in;
    for (auto& line : out.lines) {
        for (auto& bar : line.bars) {
            for (auto& cell : bar.cells) {
                if (!cell.chord.isEmpty()) {
                    cell.chord = transposeChordText(cell.chord, semitoneDelta, preferFlats);
                }
            }
        }
    }
    return out;
}

static QString songStableId(const ireal::Song& song) {
    // Stable across sessions and resistant to duplicate titles by including progression.
    const QString key = song.title + "|" + song.composer + "|" + song.style + "|" + song.key + "|" + song.progression;
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString::fromUtf8(hash);
}

static QString overrideGroupForSongId(const QString& songId) {
    return "ireal/songOverrides/" + songId;
}
} // namespace

NoteMonitorWidget::NoteMonitorWidget(QWidget* parent)
    : QWidget(parent) {
    // Black background for entire minimal UI
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    // --- iReal chart container (top half) ---
    m_chartContainer = new QWidget(this);
    m_chartContainer->setAutoFillBackground(false);
    QVBoxLayout* chartLayout = new QVBoxLayout(m_chartContainer);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->setSpacing(6);

    QWidget* chartHeader = new QWidget(m_chartContainer);
    QHBoxLayout* headerLayout = new QHBoxLayout(chartHeader);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    m_songCombo = new QComboBox(chartHeader);
    m_songCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_songCombo->setEnabled(false);
    m_songCombo->setStyleSheet("QComboBox { background-color: #111; color: #eee; padding: 4px; }");

    m_keyCombo = new QComboBox(chartHeader);
    m_keyCombo->setEnabled(false);
    m_keyCombo->setFixedWidth(76);
    m_keyCombo->setStyleSheet("QComboBox { background-color: #111; color: #eee; padding: 4px; }");
    // On macOS, the native combo popup ignores custom delegates.
    // We instead encode "(default)" into the popup item text, while keeping the closed label clean
    // by using an editable+read-only line edit showing the canonical key value.
    m_keyCombo->setEditable(true);
    if (m_keyCombo->lineEdit()) {
        m_keyCombo->lineEdit()->setReadOnly(true);
        m_keyCombo->lineEdit()->setStyleSheet("QLineEdit { background: transparent; border: none; color: #eee; padding: 0px; }");
    }
    populateKeyCombo(m_keyCombo, /*isMinorSong=*/false, /*default=*/{}, /*selected=*/"C major");

    // Virtuoso MVP preset selector + play button
    m_virtuosoPresetCombo = new QComboBox(chartHeader);
    m_virtuosoPresetCombo->setEnabled(false);
    m_virtuosoPresetCombo->setFixedWidth(260);
    m_virtuosoPresetCombo->setStyleSheet("QComboBox { background-color: #111; color: #eee; padding: 4px; }");

    m_virtuosoPlayButton = new QPushButton(chartHeader);
    m_virtuosoPlayButton->setEnabled(false);
    m_virtuosoPlayButton->setFixedSize(30, 30);
    m_virtuosoPlayButton->setIconSize(QSize(18, 18));
    m_virtuosoPlayButton->setFocusPolicy(Qt::StrongFocus);
    setVirtuosoTransportButtonUi(m_virtuosoPlayButton, style(), /*isPlaying=*/false);

    // Virtuoso debug toggle (shows theory stream + live intent/vibe HUD)
    m_virtuosoDebugToggle = new QCheckBox("Debug", chartHeader);
    m_virtuosoDebugToggle->setChecked(false);
    m_virtuosoDebugToggle->setStyleSheet("QCheckBox { color: #bbb; }");

    m_tempoSpin = new QSpinBox(chartHeader);
    m_tempoSpin->setRange(30, 300);
    m_tempoSpin->setValue(120);
    m_tempoSpin->setSuffix(" bpm");
    m_tempoSpin->setEnabled(false);
    m_tempoSpin->setFixedWidth(84);

    m_repeatsSpin = new QSpinBox(chartHeader);
    m_repeatsSpin->setRange(1, 16);
    m_repeatsSpin->setValue(3);
    m_repeatsSpin->setSuffix("x");
    m_repeatsSpin->setToolTip("Repeats");
    m_repeatsSpin->setEnabled(false);
    m_repeatsSpin->setFixedWidth(44);

    headerLayout->addWidget(m_songCombo, 1);
    headerLayout->addWidget(m_keyCombo, 0);
    headerLayout->addWidget(m_tempoSpin, 0);
    headerLayout->addWidget(m_repeatsSpin, 0);

    headerLayout->addWidget(m_virtuosoPresetCombo, 0);
    headerLayout->addWidget(m_virtuosoPlayButton, 0);
    headerLayout->addWidget(m_virtuosoDebugToggle, 0);
    chartHeader->setLayout(headerLayout);

    m_chartWidget = new chart::SongChartWidget(m_chartContainer);
    m_chartWidget->setMinimumHeight(180);

    chartLayout->addWidget(chartHeader);
    chartLayout->addWidget(m_chartWidget, 1);
    
    m_chartContainer->setLayout(chartLayout);

    // Virtuoso MVP playback engine (drives highlighting + new VirtuosoEngine groove pipeline)
    // The engine now shows its own popup dialog during pre-planning
    m_virtuosoPlayback = new playback::VirtuosoBalladMvpPlaybackEngine(this);
    connect(m_virtuosoPlayback, &playback::VirtuosoBalladMvpPlaybackEngine::currentCellChanged,
            m_chartWidget, &chart::SongChartWidget::setCurrentCellIndex);

    // --- Virtuoso debug panel (hidden by default) ---
    QGroupBox* virtuosoDbg = new QGroupBox("Virtuoso Debug (Theory Stream + Listening)", m_chartContainer);
    virtuosoDbg->setVisible(false);
    {
        QVBoxLayout* dv = new QVBoxLayout(virtuosoDbg);
        dv->setContentsMargins(8, 8, 8, 8);
        dv->setSpacing(6);

        m_virtuosoHud = new QLabel("Vibe=-  energy=-  intents=-", virtuosoDbg);
        m_virtuosoHud->setWordWrap(true);
        m_virtuosoHud->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_virtuosoHud->setStyleSheet("QLabel { color: #ddd; font-family: Menlo, monospace; font-size: 10pt; }");

        QVBoxLayout* controlsV = new QVBoxLayout();
        controlsV->setContentsMargins(0, 0, 0, 0);
        controlsV->setSpacing(6);

        // --- Energy ---
        {
            QHBoxLayout* row = new QHBoxLayout();
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(8);

            m_virtuosoEnergyAuto = new QCheckBox("Auto", virtuosoDbg);
            m_virtuosoEnergyAuto->setChecked(true);
            m_virtuosoEnergyAuto->setToolTip("When enabled, Energy follows the listening/vibe engine automatically.");

            m_virtuosoEnergySlider = new QSlider(Qt::Horizontal, virtuosoDbg);
            m_virtuosoEnergySlider->setRange(0, 100); // 0..1
            m_virtuosoEnergySlider->setValue(25);
            m_virtuosoEnergySlider->setEnabled(false); // disabled while Auto is on
            m_virtuosoEnergySlider->setToolTip("Global Energy (0..1). When Auto is off, this directly drives all agents.");

            row->addWidget(new QLabel("Energy:", virtuosoDbg));
            row->addWidget(m_virtuosoEnergyAuto, 0);
            row->addWidget(m_virtuosoEnergySlider, 1);
            controlsV->addLayout(row);
        }

        m_virtuosoTheoryLog = new QTextEdit(virtuosoDbg);
        m_virtuosoTheoryLog->setReadOnly(true);
        m_virtuosoTheoryLog->setMinimumHeight(160);
        m_virtuosoTheoryLog->setStyleSheet("QTextEdit { background: #0b0b0b; color: #ddd; font-family: Menlo, monospace; font-size: 9pt; }");

        // --- Debug Isolation Controls ---
        {
            QHBoxLayout* debugRow = new QHBoxLayout();
            debugRow->setContentsMargins(0, 6, 0, 0);
            debugRow->setSpacing(12);

            m_debugMuteLH = new QCheckBox("LH", virtuosoDbg);
            m_debugMuteLH->setChecked(true);
            m_debugMuteLH->setToolTip("Play left hand (uncheck to mute LH for debugging)");
            m_debugMuteLH->setStyleSheet("QCheckBox { color: #aaa; }");

            m_debugMuteRH = new QCheckBox("RH", virtuosoDbg);
            m_debugMuteRH->setChecked(true);
            m_debugMuteRH->setToolTip("Play right hand (uncheck to mute RH for debugging)");
            m_debugMuteRH->setStyleSheet("QCheckBox { color: #aaa; }");

            m_debugVerbose = new QCheckBox("Verbose", virtuosoDbg);
            m_debugVerbose->setChecked(false); // Default OFF for cleaner view
            m_debugVerbose->setToolTip("Verbose logging (check for detailed data dumps)");
            m_debugVerbose->setStyleSheet("QCheckBox { color: #aaa; }");

            debugRow->addWidget(new QLabel("Piano:", virtuosoDbg));
            debugRow->addWidget(m_debugMuteLH);
            debugRow->addWidget(m_debugMuteRH);
            debugRow->addSpacing(20);
            debugRow->addWidget(m_debugVerbose);
            debugRow->addStretch(1);

            controlsV->addLayout(debugRow);
        }

        dv->addWidget(m_virtuosoHud);
        dv->addLayout(controlsV);
        dv->addWidget(m_virtuosoTheoryLog, 1);
    }
    chartLayout->addWidget(virtuosoDbg);

    connect(m_virtuosoDebugToggle, &QCheckBox::toggled, this, [virtuosoDbg](bool on) {
        virtuosoDbg->setVisible(on);
    });

    connect(m_virtuosoPlayback, &playback::VirtuosoBalladMvpPlaybackEngine::debugStatus,
            this, [this](const QString& s) {
                if (m_virtuosoHud) m_virtuosoHud->setText(s);
            });
    
    // Piano debug log - forward to main console and theory log for comprehensive debugging
    // Filter based on verbose mode
    connect(m_virtuosoPlayback, &playback::VirtuosoBalladMvpPlaybackEngine::pianoDebugLog,
            this, [this](const QString& s) {
                // Check verbose mode - if off, only show simple summaries (emoji prefixed)
                const bool isVerbose = m_debugVerbose && m_debugVerbose->isChecked();
                const bool isSimpleSummary = s.startsWith("🎹") || s.startsWith("🎸") || s.startsWith("🥁");
                
                // Skip non-summary messages when verbose is off
                if (!isVerbose && !isSimpleSummary) {
                    return;
                }
                
                // Emit to theory log for visibility
                if (m_virtuosoTheoryLog) {
                    m_virtuosoTheoryLog->append(s);
                }
                // Also forward to main MidiProcessor log
                emit pianoDebugLogMessage(s);
            });
    
    connect(m_virtuosoPlayback, &playback::VirtuosoBalladMvpPlaybackEngine::theoryEventJson,
            this, [this](const QString& json) {
                // Debug: log that we received a theory event
                static int fwdCount = 0;
                if (fwdCount++ % 50 == 0) {
                    qDebug() << "NoteMonitorWidget: Forwarding theoryEventJson #" << fwdCount;
                }
                
                // Always forward to external listeners (LibraryWindow, etc.)
                emit virtuosoTheoryEventJson(json);
                
                // Only show JSON dumps in verbose mode (UI performance)
                const bool isVerbose = m_debugVerbose && m_debugVerbose->isChecked();
                if (!isVerbose) return;
                
                if (!m_virtuosoTheoryLog) return;
                QString line = json;
                const QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
                if (!d.isNull()) line = QString::fromUtf8(d.toJson(QJsonDocument::Compact));
                m_virtuosoTheoryLog->append(line);
            });

    // Planned stream (immediate upon scheduling): used for 4-bar lookahead UIs.
    connect(m_virtuosoPlayback, &playback::VirtuosoBalladMvpPlaybackEngine::plannedTheoryEventJson,
            this, [this](const QString& json) {
                emit virtuosoPlannedTheoryEventJson(json);
            });

    // Lookahead plan stream (JSON array of next 4 bars).
    connect(m_virtuosoPlayback, &playback::VirtuosoBalladMvpPlaybackEngine::lookaheadPlanJson,
            this, [this](const QString& json) {
                emit virtuosoLookaheadPlanJson(json);
            });

    connect(m_virtuosoEnergyAuto, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_virtuosoPlayback || !m_virtuosoEnergySlider) return;
        m_virtuosoEnergySlider->setEnabled(!on);
        m_virtuosoPlayback->setDebugEnergyAuto(on);
        if (!on) {
            m_virtuosoPlayback->setDebugEnergy(double(m_virtuosoEnergySlider->value()) / 100.0);
        }
    });
    connect(m_virtuosoEnergySlider, &QSlider::valueChanged, this, [this](int v) {
        if (!m_virtuosoPlayback || !m_virtuosoEnergyAuto) return;
        if (m_virtuosoEnergyAuto->isChecked()) return;
        m_virtuosoPlayback->setDebugEnergy(double(v) / 100.0);
    });

    // Debug isolation: LH/RH mute and verbose logging
    connect(m_debugMuteLH, &QCheckBox::toggled, this, [this](bool on) {
        if (m_virtuosoPlayback) m_virtuosoPlayback->setDebugMutePianoLH(!on); // Checked = play, unchecked = mute
    });
    connect(m_debugMuteRH, &QCheckBox::toggled, this, [this](bool on) {
        if (m_virtuosoPlayback) m_virtuosoPlayback->setDebugMutePianoRH(!on);
    });
    connect(m_debugVerbose, &QCheckBox::toggled, this, [this](bool on) {
        if (m_virtuosoPlayback) m_virtuosoPlayback->setDebugVerbose(on);
    });

    connect(m_virtuosoPlayback, &playback::VirtuosoBalladMvpPlaybackEngine::debugEnergy,
            this, [this](double e01, bool isAuto) {
                if (!m_virtuosoEnergySlider || !m_virtuosoEnergyAuto) return;
                const int target = qBound(0, int(llround(e01 * 100.0)), 100);

                m_virtuosoEnergyAuto->blockSignals(true);
                m_virtuosoEnergyAuto->setChecked(isAuto);
                m_virtuosoEnergyAuto->blockSignals(false);

                m_virtuosoEnergySlider->setEnabled(!isAuto);

                // Smoothly animate the slider when in Auto mode so it doesn't jump in discrete steps.
                if (isAuto) {
                    if (!m_virtuosoEnergyAnim) {
                        m_virtuosoEnergyAnim = new QPropertyAnimation(m_virtuosoEnergySlider, "value", this);
                        m_virtuosoEnergyAnim->setDuration(280);
                        m_virtuosoEnergyAnim->setEasingCurve(QEasingCurve::InOutQuad);
                    }
                    m_virtuosoEnergySlider->blockSignals(true);
                    m_virtuosoEnergyAnim->stop();
                    m_virtuosoEnergyAnim->setStartValue(m_virtuosoEnergySlider->value());
                    m_virtuosoEnergyAnim->setEndValue(target);
                    m_virtuosoEnergyAnim->start();
                    m_virtuosoEnergySlider->blockSignals(false);
                } else {
                    const bool prev = m_virtuosoEnergySlider->blockSignals(true);
                    m_virtuosoEnergySlider->setValue(target);
                    m_virtuosoEnergySlider->blockSignals(prev);
                }
            });

    // (Weights v2 sliders removed - all parameters now derived from Energy)

    auto makeSection = [&](const QString& title,
                           QLabel*& titleLbl,
                           QLabel*& letterLbl,
                           QLabel*& accidentalLbl,
                           QLabel*& octaveLbl,
                           QLabel*& centsLbl) -> QWidget* {
        QWidget* section = new QWidget(this);
        // Make section background transparent for trail effect
        section->setAttribute(Qt::WA_TranslucentBackground, true);
        section->setAutoFillBackground(false);
        QVBoxLayout* v = new QVBoxLayout(section);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);

        titleLbl = nullptr; // titles removed

        QWidget* noteRow = new QWidget(section);
        QHBoxLayout* h = new QHBoxLayout(noteRow);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);

        letterLbl = new QLabel("", noteRow);
        accidentalLbl = new QLabel("", noteRow);
        octaveLbl = new QLabel("", noteRow);

        letterLbl->setAlignment(Qt::AlignCenter);
        accidentalLbl->setAlignment(Qt::AlignCenter);
        octaveLbl->setAlignment(Qt::AlignCenter);

        // Fixed positions to avoid jumping, bring closer together
        letterLbl->setFixedWidth(38);
        accidentalLbl->setFixedWidth(18);
        octaveLbl->setFixedWidth(20);

        letterLbl->setStyleSheet("QLabel { color: #ddd; font-size: 40pt; font-weight: bold; }");
        accidentalLbl->setStyleSheet("QLabel { color: #ddd; font-size: 28pt; font-weight: bold; }");
        octaveLbl->setStyleSheet("QLabel { color: #bbb; font-size: 18pt; font-weight: normal; }");

        h->addStretch(1);
        h->addWidget(letterLbl);
        h->addWidget(accidentalLbl);
        h->addWidget(octaveLbl);
        h->addStretch(1);
        noteRow->setLayout(h);

        // Bottom-align note row within fixed-height section
        v->addStretch(1);
        v->addWidget(noteRow);

        centsLbl = new QLabel("", section);
        centsLbl->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        centsLbl->setStyleSheet("QLabel { color: #888; font-size: 12pt; }");

        // Do not add title or local cents label (cents will be shown under freq labels)
        section->setLayout(v);
        return section;
    };

    m_guitarSection = makeSection("Guitar", m_guitarTitle, m_guitarLetter, m_guitarAccidental, m_guitarOctave, m_guitarCents);
    m_guitarSection->setFixedHeight(60);

    // Insert wave visualizer between the sections
    m_wave = new WaveVisualizer(this);

    m_vocalSection = makeSection("Vocal", m_vocalTitle, m_vocalLetter, m_vocalAccidental, m_vocalOctave, m_vocalCents);
    m_vocalSection->setFixedHeight(60);
    // Make vocal section background fully transparent for trail effect
    m_vocalSection->setAttribute(Qt::WA_TranslucentBackground, true);
    m_vocalSection->setAutoFillBackground(false);
    // 70% opacity for vocal section
    {
        auto* eff = new QGraphicsOpacityEffect(m_vocalSection);
        eff->setOpacity(0.7);
        m_vocalSection->setGraphicsEffect(eff);
    }

    // Top row: left (guitar, centered), right (vocal, right aligned), both bottom-aligned over waves
    // Notes overlay (no layout); reparent sections into overlay for absolute positioning
    m_notesOverlay = new QWidget(this);
    m_notesOverlay->setFixedHeight(60);
    m_notesOverlay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_notesOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_guitarSection->setParent(m_notesOverlay);
    m_vocalSection->setParent(m_notesOverlay);
    
    // Trail layer (behind vocal section for fading ghosts)
    m_trailLayer = new QWidget(m_notesOverlay);
    m_trailLayer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_trailLayer->setAttribute(Qt::WA_TranslucentBackground, true);
    m_trailLayer->setAutoFillBackground(false);
    m_trailLayer->setGeometry(0, 0, m_notesOverlay->width(), m_notesOverlay->height());
    m_trailLayer->show(); // Explicitly show the trail layer
    m_trailLayer->lower(); // Place behind vocal section
    m_vocalSection->raise(); // Ensure vocal section stays on top

    // Overlay the note visualization on top of the wave visualizer.
    QWidget* waveBlock = new QWidget(this);
    QGridLayout* blockLayout = new QGridLayout(waveBlock);
    blockLayout->setContentsMargins(0, 0, 0, 0);
    blockLayout->setSpacing(0);
    blockLayout->addWidget(m_wave, 0, 0);
    // Overlay the note visualization on top of the wavelength visualizer.
    // Use a tiny visual bias upward (text baselines make perfect centering feel low).
    QWidget* notesContainer = new QWidget(waveBlock);
    notesContainer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    notesContainer->setAutoFillBackground(false);
    QVBoxLayout* notesLayout = new QVBoxLayout(notesContainer);
    notesLayout->setContentsMargins(0, 0, 0, 0);
    notesLayout->setSpacing(0);
    notesLayout->addStretch(8);                // slightly less space above
    // Do NOT horizontally center via alignment here; it would shrink the overlay to its sizeHint,
    // causing the note visualization to be clipped. Let it expand to the full wave width.
    notesLayout->addWidget(m_notesOverlay, 0);
    notesLayout->addStretch(12);               // slightly more space below
    notesContainer->setLayout(notesLayout);

    blockLayout->addWidget(notesContainer, 0, 0);
    waveBlock->setLayout(blockLayout);
    notesContainer->raise();

    // Layout goal:
    // - Wave section (with notes overlay) visually centered vertically
    // - Pitch monitor uses the remaining space below that (typically < 50% of window)
    // Put chart in the top half; keep wave + pitch monitor below.
    root->addWidget(m_chartContainer, 1);
    root->addWidget(waveBlock, 0);

    m_pitchMonitor = new PitchMonitorWidget(this);
    m_pitchMonitor->setMinimumHeight(140);
    root->addWidget(m_pitchMonitor, 1);

    // Hide initially (keep section height fixed)
    m_guitarLetter->setVisible(false);
    m_guitarAccidental->setVisible(false);
    m_guitarOctave->setVisible(false);
    if (m_guitarCents) m_guitarCents->setVisible(false);
    m_vocalLetter->setVisible(false);
    m_vocalAccidental->setVisible(false);
    m_vocalOctave->setVisible(false);
    if (m_vocalCents) m_vocalCents->setVisible(false);

    // Initial positioning
    repositionNotes();

    // --- chart UI connections ---
    connect(m_songCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        loadSongAtIndex(idx);
    });

    connect(m_keyCombo, &QComboBox::currentIndexChanged, this, [this](int /*idx*/) {
        if (!m_keyCombo) return;
        QString sel = m_keyCombo->currentData(Qt::UserRole).toString().trimmed();
        if (sel.isEmpty()) {
            const QString shortLabel = stripDefaultSuffix(m_keyCombo->currentText());
            sel = keyCenterFromShortLabel(shortLabel);
        }
        if (m_keyCombo->isEditable() && m_keyCombo->lineEdit()) {
            m_keyCombo->lineEdit()->setText(shortKeyLabelFromKeyCenter(sel));
        }
        if (!sel.isEmpty()) setKeyCenter(sel);

        // Persist per-song key override.
        if (!m_isApplyingSongState && !m_currentSongId.isEmpty()) {
            QSettings s;
            s.setValue(overrideGroupForSongId(m_currentSongId) + "/keyCenter", sel);
        }

        // Transpose chart relative to detected song key.
        if (m_hasBaseChartModel && !m_detectedSongKeyCenter.isEmpty()) {
            const int srcPc = pitchClassFromKeyCenter(m_detectedSongKeyCenter);
            const int dstPc = pitchClassFromKeyCenter(sel);
            if (srcPc >= 0 && dstPc >= 0) {
                const int delta = (dstPc - srcPc + 12) % 12;
                const bool flats = preferFlatsForKeyCenter(sel);
                const chart::ChartModel m = transposeChartModel(m_baseChartModel, delta, flats);
                m_chartWidget->setChartModel(m);
                if (m_virtuosoPlayback) m_virtuosoPlayback->setChartModel(m);
            }
        }
    });

    connect(m_tempoSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int bpm) {
        if (m_virtuosoPlayback) m_virtuosoPlayback->setTempoBpm(bpm);
        if (m_pitchMonitor) m_pitchMonitor->setBpm(bpm);
        if (!m_isApplyingSongState && !m_currentSongId.isEmpty()) {
            QSettings s;
            s.setValue(overrideGroupForSongId(m_currentSongId) + "/tempoBpm", bpm);
        }
    });

    connect(m_repeatsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int reps) {
        if (m_virtuosoPlayback) m_virtuosoPlayback->setRepeats(reps);
        if (!m_isApplyingSongState && !m_currentSongId.isEmpty()) {
            QSettings s;
            s.setValue(overrideGroupForSongId(m_currentSongId) + "/repeats", reps);
        }
    });

    connect(m_virtuosoPresetCombo, &QComboBox::currentIndexChanged, this, [this](int /*idx*/) {
        if (!m_virtuosoPlayback || !m_virtuosoPresetCombo) return;
        const QString key = m_virtuosoPresetCombo->currentData(Qt::UserRole).toString();
        if (!key.isEmpty()) {
            m_virtuosoPlayback->setStylePresetKey(key);
            // Persist per-song preset choice.
            if (!m_isApplyingSongState && !m_currentSongId.isEmpty()) {
                QSettings s;
                s.setValue(overrideGroupForSongId(m_currentSongId) + "/virtuosoPresetKey", key);
            }
        }
    });

    connect(m_virtuosoPlayButton, &QPushButton::clicked, this, [this]() {
        if (!m_virtuosoPlayback) return;
        if (m_virtuosoPlayback->isPlaying()) {
            m_virtuosoPlayback->stop();
            setVirtuosoTransportButtonUi(m_virtuosoPlayButton, style(), /*isPlaying=*/false);
        } else {
            m_virtuosoPlayback->play();
            setVirtuosoTransportButtonUi(m_virtuosoPlayButton, style(), /*isPlaying=*/true);
        }
    });
}

void NoteMonitorWidget::setMidiProcessor(MidiProcessor* processor) {
    m_midiProcessor = processor;
    if (!m_midiProcessor) return;

    if (m_virtuosoPlayback) {
        m_virtuosoPlayback->setMidiProcessor(m_midiProcessor);
    }
}

void NoteMonitorWidget::loadSongAtIndex(int idx) {
    if (!m_playlist || idx < 0 || idx >= m_playlist->songs.size()) return;

    // Stop playback when switching songs.
    if (m_virtuosoPlayback) m_virtuosoPlayback->stop();
    setVirtuosoTransportButtonUi(m_virtuosoPlayButton, style(), /*isPlaying=*/false);

    const auto& song = m_playlist->songs[idx];
    m_currentSongId = songStableId(song);
    m_detectedSongKeyCenter = keyFieldToKeyCenter(song.key);
    m_baseChartModel = chart::parseIRealProgression(song.progression);
    m_hasBaseChartModel = true;

    // Key center from the song metadata (iReal HTML).
    QSettings settings;
    const QString group = overrideGroupForSongId(m_currentSongId);
    const QString overriddenKeyCenter = settings.value(group + "/keyCenter", QString()).toString();
    const QString selectedKeyCenter = overriddenKeyCenter.isEmpty() ? m_detectedSongKeyCenter : overriddenKeyCenter;

    bool isMinorSong = false;
    (void)pitchClassFromKeyCenter(m_detectedSongKeyCenter, &isMinorSong);
    if (m_keyCombo) {
        m_isApplyingSongState = true;
        populateKeyCombo(m_keyCombo, isMinorSong, m_detectedSongKeyCenter, selectedKeyCenter);
        m_isApplyingSongState = false;
    }
    if (!selectedKeyCenter.isEmpty()) setKeyCenter(selectedKeyCenter);

    // Apply transposition (or identity) to the chart model.
    {
        const int srcPc = pitchClassFromKeyCenter(m_detectedSongKeyCenter);
        const int dstPc = pitchClassFromKeyCenter(selectedKeyCenter);
        const int delta = (srcPc >= 0 && dstPc >= 0) ? ((dstPc - srcPc + 12) % 12) : 0;
        const bool flats = preferFlatsForKeyCenter(selectedKeyCenter);
        const chart::ChartModel m = transposeChartModel(m_baseChartModel, delta, flats);
        m_chartWidget->setChartModel(m);
        if (m_virtuosoPlayback) m_virtuosoPlayback->setChartModel(m);
    }

    // Tempo preference: song tempo if present, else current spin.
    int bpm = song.actualTempoBpm > 0 ? song.actualTempoBpm : m_tempoSpin->value();
    const int overriddenTempo = settings.value(group + "/tempoBpm", 0).toInt();
    if (overriddenTempo > 0) bpm = overriddenTempo;
    m_tempoSpin->blockSignals(true);
    m_tempoSpin->setValue(bpm);
    m_tempoSpin->blockSignals(false);

    if (m_virtuosoPlayback) m_virtuosoPlayback->setTempoBpm(bpm);
    if (m_pitchMonitor) m_pitchMonitor->setBpm(bpm);

    // Repeats preference: song metadata if present, else default 3; overridable per-song.
    int reps = song.actualRepeats > 0 ? song.actualRepeats : 3;
    const int overriddenReps = settings.value(group + "/repeats", 0).toInt();
    if (overriddenReps > 0) reps = overriddenReps;
    if (m_repeatsSpin) {
        m_repeatsSpin->blockSignals(true);
        m_repeatsSpin->setValue(reps);
        m_repeatsSpin->blockSignals(false);
    }
    if (m_virtuosoPlayback) m_virtuosoPlayback->setRepeats(reps);

    if (m_virtuosoPlayButton) m_virtuosoPlayButton->setEnabled(true);
    if (m_virtuosoPresetCombo) m_virtuosoPresetCombo->setEnabled(true);

    // Populate Virtuoso preset dropdown (jazz-only for now; filter to ballad/brushes first).
    if (m_virtuosoPresetCombo) {
        const bool prevSig = m_virtuosoPresetCombo->blockSignals(true);
        m_virtuosoPresetCombo->clear();
        const auto reg = virtuoso::groove::GrooveRegistry::builtins();
        const auto presets = reg.allStylePresets();
        int sel = -1;
        QString desiredKey;
        {
            QSettings s;
            // Prefer per-song selection, fallback to Evans.
            desiredKey = s.value(overrideGroupForSongId(m_currentSongId) + "/virtuosoPresetKey",
                                 QString("jazz_brushes_ballad_60_evans")).toString();
        }
        for (int i = 0; i < presets.size(); ++i) {
            const auto* p = presets[i];
            if (!p) continue;
            // MVP focus: show ballad + brushes presets first.
            if (!p->key.contains("ballad", Qt::CaseInsensitive) && !p->key.contains("brush", Qt::CaseInsensitive)) continue;
            m_virtuosoPresetCombo->addItem(p->name, p->key);
            const int row = m_virtuosoPresetCombo->count() - 1;
            m_virtuosoPresetCombo->setItemData(row, p->key, Qt::UserRole);
            if (p->key == desiredKey) sel = row;
        }
        if (m_virtuosoPresetCombo->count() == 0) {
            // Fallback: show everything.
            for (const auto* p : presets) {
                if (!p) continue;
                m_virtuosoPresetCombo->addItem(p->name, p->key);
                const int row = m_virtuosoPresetCombo->count() - 1;
                m_virtuosoPresetCombo->setItemData(row, p->key, Qt::UserRole);
                if (p->key == desiredKey) sel = row;
            }
        }
        if (sel >= 0) m_virtuosoPresetCombo->setCurrentIndex(sel);
        m_virtuosoPresetCombo->blockSignals(prevSig);
        if (m_virtuosoPlayback) {
            const QString key = m_virtuosoPresetCombo->currentData(Qt::UserRole).toString();
            if (!key.isEmpty()) m_virtuosoPlayback->setStylePresetKey(key);
        }
    }

    // Persist last selected song across sessions.
    if (!m_isApplyingSongState) {
        QSettings s;
        s.setValue("ui/lastSongId", m_currentSongId);
    }
}

void NoteMonitorWidget::setIRealPlaylist(const ireal::Playlist& playlist) {
    // Replace stored playlist
    delete m_playlist;
    m_playlist = new ireal::Playlist(playlist);

    // Prevent mid-population index signals from toggling Play state.
    const bool prev = m_songCombo->blockSignals(true);
    m_songCombo->clear();
    for (const auto& s : m_playlist->songs) {
        m_songCombo->addItem(s.title);
    }
    m_songCombo->blockSignals(prev);

    const bool hasSongs = !m_playlist->songs.isEmpty();
    m_songCombo->setEnabled(hasSongs);
    m_tempoSpin->setEnabled(hasSongs);
    if (m_repeatsSpin) m_repeatsSpin->setEnabled(hasSongs);
    if (m_keyCombo) m_keyCombo->setEnabled(hasSongs);
    if (m_virtuosoPresetCombo) m_virtuosoPresetCombo->setEnabled(hasSongs);
    if (m_virtuosoPlayButton) m_virtuosoPlayButton->setEnabled(hasSongs);

    if (!hasSongs) {
        if (m_chartWidget) m_chartWidget->clear();
        if (m_virtuosoPlayback) m_virtuosoPlayback->stop();
        setVirtuosoTransportButtonUi(m_virtuosoPlayButton, style(), /*isPlaying=*/false);
        return;
    }

    // Restore last selected song if possible; else fall back to first.
    int targetIdx = 0;
    {
        QSettings s;
        const QString lastId = s.value("ui/lastSongId", QString()).toString();
        if (!lastId.isEmpty()) {
            for (int i = 0; i < m_playlist->songs.size(); ++i) {
                if (songStableId(m_playlist->songs[i]) == lastId) { targetIdx = i; break; }
            }
        }
    }

    // Force-load selected song so Play is enabled immediately (even on startup auto-load).
    const bool prev2 = m_songCombo->blockSignals(true);
    const int maxIdx = std::max(0, int(m_playlist->songs.size()) - 1);
    m_songCombo->setCurrentIndex(std::max(0, std::min(targetIdx, maxIdx)));
    m_songCombo->blockSignals(prev2);
    loadSongAtIndex(m_songCombo->currentIndex());
}

NoteMonitorWidget::~NoteMonitorWidget() {
    delete m_playlist;
    m_playlist = nullptr;
}

void NoteMonitorWidget::requestVirtuosoLookaheadOnce() {
    if (!m_virtuosoPlayback) return;
    m_virtuosoPlayback->emitLookaheadPlanOnce();
}

void NoteMonitorWidget::setVirtuosoAgentEnergyMultiplier(const QString& agent, double mult01to2) {
    if (!m_virtuosoPlayback) return;
    m_virtuosoPlayback->setAgentEnergyMultiplier(agent, mult01to2);
    // Refresh lookahead immediately so UIs update even if transport is stopped.
    m_virtuosoPlayback->emitLookaheadPlanOnce();
}

void NoteMonitorWidget::setTheoryEventsEnabled(bool enabled) {
    if (!m_virtuosoPlayback) {
        qWarning() << "NoteMonitorWidget::setTheoryEventsEnabled: m_virtuosoPlayback is NULL!";
        return;
    }
    if (!m_virtuosoPlayback->engine()) {
        qWarning() << "NoteMonitorWidget::setTheoryEventsEnabled: engine() is NULL!";
        return;
    }
    // Enable theory JSON emission in the engine (required for LibraryWindow live-follow)
    m_virtuosoPlayback->engine()->setEmitTheoryJson(enabled);
    qDebug() << "NoteMonitorWidget: Theory events" << (enabled ? "ENABLED" : "DISABLED")
             << "emitTheoryJson now:" << m_virtuosoPlayback->engine()->emitTheoryJson();
}

void NoteMonitorWidget::stopAllPlayback() {
    if (m_virtuosoPlayback && m_virtuosoPlayback->isPlaying()) {
        m_virtuosoPlayback->stop();
        setVirtuosoTransportButtonUi(m_virtuosoPlayButton, style(), /*isPlaying=*/false);
    }
}

void NoteMonitorWidget::setGuitarNote(int midiNote, double cents) {
    updateNoteUISection(m_guitarTitle, m_guitarLetter, m_guitarAccidental, m_guitarOctave, m_guitarCents, midiNote, cents);
    m_lastGuitarNote = midiNote;
    if (midiNote >= 0 && m_wave) {
        QColor c(pitchColorForCents(cents));
        m_wave->setGuitarColor(c);
        m_wave->setGuitarCentsText(formatCentsText(cents));
    }
    if (m_pitchMonitor) {
        m_pitchMonitor->pushGuitar(midiNote, cents);
    }
    repositionNotes();
}

void NoteMonitorWidget::setVoiceNote(int midiNote, double cents) {
    updateNoteUISection(m_vocalTitle, m_vocalLetter, m_vocalAccidental, m_vocalOctave, m_vocalCents, midiNote, cents);
    m_lastVoiceNote = midiNote;
    m_lastVoiceCents = cents;
    if (midiNote >= 0 && m_wave) {
        QColor c(pitchColorForCents(cents));
        m_wave->setVoiceColor(c);
        m_wave->setVoiceCentsText(formatCentsText(cents));
    }
    if (m_pitchMonitor) {
        m_pitchMonitor->pushVocal(midiNote, cents);
    }
    repositionNotes();
}

void NoteMonitorWidget::setGuitarHz(double hz) {
    if (m_wave) m_wave->setGuitarHz(hz);
}

void NoteMonitorWidget::setVoiceHz(double hz) {
    if (m_wave) m_wave->setVoiceHz(hz);
}

void NoteMonitorWidget::setGuitarAmplitude(int aftertouch) {
    if (m_wave) m_wave->setGuitarAmplitude(aftertouch);
}

void NoteMonitorWidget::setVoiceAmplitude(int cc2) {
    if (m_wave) m_wave->setVoiceAmplitude(cc2);
    if (m_pitchMonitor) m_pitchMonitor->setVoiceAmplitude(cc2);
}

void NoteMonitorWidget::setGuitarVelocity(int velocity) {
    if (m_wave) m_wave->setGuitarVelocity(velocity);
    if (m_pitchMonitor) m_pitchMonitor->setGuitarVelocity(velocity);
}

void NoteMonitorWidget::updateNoteUISection(QLabel* titleLabel,
                                      QLabel* letterLbl,
                                      QLabel* accidentalLbl,
                                      QLabel* octaveLbl,
                                      QLabel* centsLabel,
                                      int midiNote,
                                      double cents) {
    bool show = midiNote >= 0;
    if (titleLabel) titleLabel->setVisible(show);
    letterLbl->setVisible(show);
    accidentalLbl->setVisible(show);
    octaveLbl->setVisible(show);
    centsLabel->setVisible(show);
    if (!show) return;

    QString color = pitchColorForCents(cents);
    updateNoteParts(letterLbl, accidentalLbl, octaveLbl, midiNote, cents);
    QString letterStyle = QString("QLabel { color: %1; font-size: 40pt; font-weight: bold; }").arg(color);
    QString accidentalStyle = QString("QLabel { color: %1; font-size: 28pt; font-weight: bold; }").arg(color);
    QString octaveStyle = QString("QLabel { color: %1; font-size: 18pt; font-weight: normal; }").arg(color);
    letterLbl->setStyleSheet(letterStyle);
    accidentalLbl->setStyleSheet(accidentalStyle);
    octaveLbl->setStyleSheet(octaveStyle);
}

QString NoteMonitorWidget::formatNoteName(int midiNote) {
    if (midiNote < 0) return "";
    static const char* sharps[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* flats[]  = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    int pc = midiNote % 12;
    int octave = midiNote / 12 - 1;
    // Natural notes don't need enharmonic pair
    bool isAccidental = (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
    if (isAccidental) {
        return QString("%1%2/%3%4")
            .arg(sharps[pc]).arg(octave)
            .arg(flats[pc]).arg(octave);
    }
    return QString("%1%2").arg(sharps[pc]).arg(octave);
}

QString NoteMonitorWidget::formatCentsText(double cents) {
    int rounded = static_cast<int>(std::round(cents));
    if (rounded == 0) return "0 cents";
    if (rounded > 0) return QString("+%1 cents").arg(rounded);
    return QString("%1 cents").arg(rounded);
}

void NoteMonitorWidget::setKeyCenter(const QString& keyCenter) {
    m_keyCenter = keyCenter;
    if (m_pitchMonitor) {
        m_pitchMonitor->setKeyCenter(keyCenter);
    }
    if (m_chartWidget) {
        m_chartWidget->setKeyCenter(keyCenter);
    }
}

bool NoteMonitorWidget::preferFlats() const {
    QString k = m_keyCenter.toLower();
    // Flat keys: F, Bb, Eb, Ab, Db, Gb, Cb
    static const QStringList flatKeys = {
        "f major","bb major","b♭ major","eb major","e♭ major","ab major","a♭ major","db major","d♭ major","gb major","g♭ major","cb major","c♭ major"
    };
    for (const QString& fk : flatKeys) {
        if (k == fk) return true;
    }
    // If contains 'b' or '♭' before 'major', prefer flats
    if (k.contains("b major") || k.contains(QChar(0x266D))) return true;
    return false;
}

void NoteMonitorWidget::chooseSpellingForKey(int midiNote, QChar& letterOut, QChar& accidentalOut, int& octaveOut) const {
    if (midiNote < 0) { letterOut = QChar(' '); accidentalOut = QChar(' '); octaveOut = 0; return; }
    int pc = midiNote % 12;
    octaveOut = midiNote / 12 - 1;
    static const char* lettersSharp[] = {"C","C","D","D","E","F","F","G","G","A","A","B"};
    static const QChar accSharp[] = {QChar(),QChar(0x266F),QChar(),QChar(0x266F),QChar(),QChar(),QChar(0x266F),QChar(),QChar(0x266F),QChar(),QChar(0x266F),QChar()};
    static const char* lettersFlat[]  = {"C","D","D","E","E","F","G","G","A","A","B","B"};
    static const QChar accFlat[]  = {QChar(),QChar(0x266D),QChar(),QChar(0x266D),QChar(),QChar(),QChar(0x266D),QChar(),QChar(0x266D),QChar(),QChar(0x266D),QChar()};
    bool useFlat = preferFlats();
    if (useFlat) {
        letterOut = QChar(lettersFlat[pc][0]);
        accidentalOut = accFlat[pc].isNull() ? QChar() : accFlat[pc];
    } else {
        letterOut = QChar(lettersSharp[pc][0]);
        accidentalOut = accSharp[pc].isNull() ? QChar() : accSharp[pc];
    }
}

void NoteMonitorWidget::updateNoteParts(QLabel* letterLbl, QLabel* accidentalLbl, QLabel* octaveLbl,
                                        int midiNote, double cents) {
    QChar letter, accidental;
    int octave = 0;
    chooseSpellingForKey(midiNote, letter, accidental, octave);
    letterLbl->setText(QString(letter));
    octaveLbl->setText(QString::number(octave));
    // Spelled accidental always visible if present
    accidentalLbl->setText(accidental.isNull() ? QString("") : QString(accidental));
}

void NoteMonitorWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!m_notesOverlay) return;
    // Ensure overlay matches wave width
    m_notesOverlay->setMinimumWidth(m_wave ? m_wave->width() : width());
    if (m_trailLayer) {
        m_trailLayer->setGeometry(0, 0, m_notesOverlay->width(), m_notesOverlay->height());
    }
    repositionNotes();
}

void NoteMonitorWidget::repositionNotes() {
    if (!m_notesOverlay || !m_guitarSection || !m_vocalSection) return;
    int W = m_notesOverlay->width();
    int H = m_notesOverlay->height();
    if (W <= 0 || H <= 0) return;

    // Ensure sections have proper size
    m_guitarSection->adjustSize();
    m_vocalSection->adjustSize();
    int gW = m_guitarSection->sizeHint().width();
    int vW = m_vocalSection->sizeHint().width();
    int gH = m_guitarSection->height();
    int vH = m_vocalSection->height();

    // Center guitar
    int gCenterX = W / 2;
    int gLeft = gCenterX - gW / 2;
    int gTop = H - gH;
    m_guitarSection->setGeometry(gLeft, gTop, gW, gH);

    // If no guitar note (or no vocal yet), center the vocal section horizontally
    if (m_lastVoiceNote < 0 || m_lastGuitarNote < 0) {
        int vCenterXInit = W / 2;
        int vLeftInit = vCenterXInit - vW / 2;
        QRect oldGeo = m_vocalSection->geometry();
        QRect newGeo(vLeftInit, H - vH, vW, vH);
        if (oldGeo.isValid() && std::abs(oldGeo.x() - newGeo.x()) >= 1) {
            addVocalTrailSnapshot(oldGeo);
        }
        m_vocalSection->setGeometry(newGeo);
        m_lastVocalX = vLeftInit;
        return;
    }

    // Pitch-class delta in semitones ignoring octaves
    auto norm = [](int x)->int { int r = x % 12; if (r < 0) r += 12; return r; };
    int pcG = norm(m_lastGuitarNote);
    int pcV = norm(m_lastVoiceNote);
    int semi = pcV - pcG;
    if (semi > 6) semi -= 12;
    if (semi < -6) semi += 12;

    // Total delta cents relative to guitar perfect pitch (clamp to [-100, 100]).
    // Determine direction by absolute note difference (incl. octaves) so higher notes never appear left of guitar.
    int noteDiff = m_lastVoiceNote - m_lastGuitarNote; // signed semitones (incl. octaves)
    double totalCents;
    if (pcG == pcV) {
        // Same pitch class (possibly different octaves) -> use cents only (overlap around 0)
        totalCents = m_lastVoiceCents;
    } else if (std::abs(noteDiff) >= 12) {
        // Different pitch class and at least one octave away -> snap to extreme side by octave sign
        totalCents = (noteDiff > 0) ? 100.0 : -100.0;
    } else {
        // Within an octave but different pitch class: base on wrapped semitone delta +/- cents,
        // but make sure direction matches absolute noteDiff sign so higher never goes left.
        totalCents = semi * 100.0 + m_lastVoiceCents;
        if (noteDiff > 0 && totalCents < 0) totalCents = -totalCents;
        if (noteDiff < 0 && totalCents > 0) totalCents = -totalCents;
    }
    if (totalCents > 100.0) totalCents = 100.0;
    if (totalCents < -100.0) totalCents = -100.0;

    // Compute max center offset when edges just touch (0 overlap)
    int edgeCenterOffset = (gW + vW) / 2;
    int vCenterX = gCenterX + static_cast<int>(std::round((totalCents / 100.0) * edgeCenterOffset));
    int vLeft = vCenterX - vW / 2;
    int vTop = H - vH;
    
    // Create trail snapshot if position changed horizontally
    QRect oldGeo = m_vocalSection->geometry();
    QRect newGeo(vLeft, vTop, vW, vH);
    
    // Only create trail if:
    // 1. We have valid geometry
    // 2. Vocal note is actually being displayed (has content)
    // 3. Vocal note pitch class matches guitar note pitch class (same note, ignoring octave)
    // 4. Position actually changed by at least 1 pixel
    // 5. Old position wasn't at origin (0,0) which indicates initial positioning
    // Note: pcG and pcV are already calculated above for positioning logic
    bool samePitchClass = (pcG == pcV);
    bool shouldCreateTrail = oldGeo.isValid() && 
                             oldGeo.width() > 0 && 
                             oldGeo.height() > 0 &&
                             m_lastVoiceNote >= 0 && // Vocal note is active
                             m_lastGuitarNote >= 0 && // Guitar note is active
                             samePitchClass && // Same pitch class (ignoring octave)
                             std::abs(oldGeo.x() - newGeo.x()) >= 1 &&
                             (oldGeo.x() != 0 || oldGeo.y() != 0); // Not at initial position
    
    if (shouldCreateTrail) {
        // Capture snapshot BEFORE moving the widget
        addVocalTrailSnapshot(oldGeo);
    }
    
    m_vocalSection->setGeometry(newGeo);
    m_lastVocalX = vLeft;
    
    // Ensure proper z-ordering: guitar section at bottom, trail layer in middle, vocal section on top
    if (m_guitarSection) m_guitarSection->lower();
    if (m_trailLayer) {
        m_trailLayer->raise(); // Above guitar
        m_trailLayer->lower(); // But below vocal
    }
    m_vocalSection->raise(); // Always on top
}

void NoteMonitorWidget::addVocalTrailSnapshot(const QRect& oldGeo) {
    if (!m_trailLayer || !m_vocalSection || oldGeo.width() <= 0 || oldGeo.height() <= 0) return;
    
    // Ensure trail layer is properly sized
    if (m_trailLayer->width() != m_notesOverlay->width() || 
        m_trailLayer->height() != m_notesOverlay->height()) {
        m_trailLayer->setGeometry(0, 0, m_notesOverlay->width(), m_notesOverlay->height());
    }
    
    // Cap number of ghosts to avoid performance issues
    const auto labels = m_trailLayer->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly);
    if (labels.size() >= m_trailMaxGhosts) {
        // Remove oldest (first in list)
        QLabel* oldest = labels.first();
        if (oldest) {
            oldest->deleteLater();
        }
    }
    
    // Temporarily restore full opacity for snapshot (if opacity effect exists)
    // This ensures the trail ghost has full detail before fading
    QGraphicsOpacityEffect* opacityEff = qobject_cast<QGraphicsOpacityEffect*>(m_vocalSection->graphicsEffect());
    double oldOpacity = 0.7;
    if (opacityEff) {
        oldOpacity = opacityEff->opacity();
        opacityEff->setOpacity(1.0);
    }
    
    // Ensure widget is visible and updated before grabbing
    m_vocalSection->setVisible(true);
    m_vocalSection->update();
    
    // Grab snapshot of vocal section at its current position (which is still oldGeo)
    // Widget hasn't moved yet when this is called
    // Use grab() without arguments to capture the entire widget
    QPixmap pm = m_vocalSection->grab();
    
    // Restore original opacity immediately
    if (opacityEff) {
        opacityEff->setOpacity(oldOpacity);
    }
    
    // Skip if pixmap is empty or invalid
    if (pm.isNull() || pm.width() <= 0 || pm.height() <= 0) {
        return;
    }
    
    // Create ghost label with snapshot at old position
    QLabel* ghost = new QLabel(m_trailLayer);
    ghost->setPixmap(pm);
    ghost->setGeometry(oldGeo);
    ghost->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    ghost->setAttribute(Qt::WA_TranslucentBackground, true);
    ghost->setAutoFillBackground(false);
    ghost->setScaledContents(false); // Don't scale, use exact pixmap
    ghost->show();
    
    // Force update of trail layer to ensure ghost is visible
    m_trailLayer->update();
    m_trailLayer->repaint();
    
    // Apply opacity effect for fading
    // Start at full opacity for maximum visibility, then fade
    auto* ghostEff = new QGraphicsOpacityEffect(ghost);
    ghostEff->setOpacity(1.0); // Start at full opacity for maximum trail visibility
    ghost->setGraphicsEffect(ghostEff);
    
    // Animate fade-out over 2500ms (longer for better trail visibility)
    QPropertyAnimation* anim = new QPropertyAnimation(ghostEff, "opacity", ghost);
    anim->setDuration(2500);
    anim->setStartValue(0.1);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::OutQuad); // Smooth fade
    connect(anim, &QPropertyAnimation::finished, ghost, &QObject::deleteLater);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

