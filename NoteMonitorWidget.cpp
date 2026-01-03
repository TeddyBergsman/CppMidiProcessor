#include "NoteMonitorWidget.h"
#include "WaveVisualizer.h"
#include "PitchMonitorWidget.h"
#include "PitchColor.h"
#include "chart/SongChartWidget.h"
#include "chart/IRealProgressionParser.h"
#include "ireal/IRealTypes.h"
#include "SilentPlaybackEngine.h"
#include <QtWidgets>
#include <cmath>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>

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

    m_playButton = new QPushButton("Play", chartHeader);
    m_playButton->setEnabled(false);
    m_playButton->setFixedWidth(70);

    m_tempoSpin = new QSpinBox(chartHeader);
    m_tempoSpin->setRange(30, 300);
    m_tempoSpin->setValue(120);
    m_tempoSpin->setSuffix(" bpm");
    m_tempoSpin->setEnabled(false);
    m_tempoSpin->setFixedWidth(110);

    headerLayout->addWidget(m_songCombo, 1);
    headerLayout->addWidget(m_tempoSpin, 0);
    headerLayout->addWidget(m_playButton, 0);
    chartHeader->setLayout(headerLayout);

    m_chartWidget = new chart::SongChartWidget(m_chartContainer);
    m_chartWidget->setMinimumHeight(180);

    chartLayout->addWidget(chartHeader);
    chartLayout->addWidget(m_chartWidget, 1);
    m_chartContainer->setLayout(chartLayout);

    // Silent playback engine
    m_playback = new playback::SilentPlaybackEngine(this);
    connect(m_playback, &playback::SilentPlaybackEngine::currentCellChanged,
            m_chartWidget, &chart::SongChartWidget::setCurrentCellIndex);

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

    connect(m_tempoSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int bpm) {
        if (m_playback) m_playback->setTempoBpm(bpm);
    });

    connect(m_playButton, &QPushButton::clicked, this, [this]() {
        if (!m_playback) return;
        if (m_playback->isPlaying()) {
            m_playback->stop();
            m_playButton->setText("Play");
        } else {
            m_playback->play();
            m_playButton->setText("Stop");
        }
    });
}

void NoteMonitorWidget::loadSongAtIndex(int idx) {
    if (!m_playlist || idx < 0 || idx >= m_playlist->songs.size()) return;

    // Stop playback when switching songs.
    if (m_playback) m_playback->stop();
    if (m_playButton) m_playButton->setText("Play");

    const auto& song = m_playlist->songs[idx];
    chart::ChartModel model = chart::parseIRealProgression(song.progression);
    m_chartWidget->setChartModel(model);

    // Tempo preference: song tempo if present, else current spin.
    int bpm = song.actualTempoBpm > 0 ? song.actualTempoBpm : m_tempoSpin->value();
    m_tempoSpin->blockSignals(true);
    m_tempoSpin->setValue(bpm);
    m_tempoSpin->blockSignals(false);

    m_playback->setTempoBpm(bpm);
    int totalBars = 0;
    for (const auto& line : model.lines) totalBars += line.bars.size();
    m_playback->setTotalCells(totalBars * 4);

    m_playButton->setEnabled(true);
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
    m_playButton->setEnabled(false);

    if (!hasSongs) {
        if (m_chartWidget) m_chartWidget->clear();
        if (m_playback) m_playback->stop();
        m_playButton->setText("Play");
        return;
    }

    // Force-load first song so Play is enabled immediately (even on startup auto-load).
    m_songCombo->setCurrentIndex(0);
    loadSongAtIndex(0);
}

NoteMonitorWidget::~NoteMonitorWidget() {
    delete m_playlist;
    m_playlist = nullptr;
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

void NoteMonitorWidget::setPitchMonitorBpm(int bpm) {
    if (m_pitchMonitor) {
        m_pitchMonitor->setBpm(bpm);
    }
}

void NoteMonitorWidget::setKeyCenter(const QString& keyCenter) {
    m_keyCenter = keyCenter;
    if (m_pitchMonitor) {
        m_pitchMonitor->setKeyCenter(keyCenter);
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

