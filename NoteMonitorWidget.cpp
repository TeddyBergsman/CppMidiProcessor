#include "NoteMonitorWidget.h"
#include "WaveVisualizer.h"
#include <QtWidgets>
#include <cmath>

NoteMonitorWidget::NoteMonitorWidget(QWidget* parent)
    : QWidget(parent) {
    // Black background for entire minimal UI
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(0);

    auto makeSection = [&](const QString& title,
                           QLabel*& titleLbl,
                           QLabel*& letterLbl,
                           QLabel*& accidentalLbl,
                           QLabel*& octaveLbl,
                           QLabel*& centsLbl) -> QWidget* {
        QWidget* section = new QWidget(this);
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

    QWidget* guitarSection = makeSection("Guitar", m_guitarTitle, m_guitarLetter, m_guitarAccidental, m_guitarOctave, m_guitarCents);
    guitarSection->setFixedHeight(60);

    // Insert wave visualizer between the sections
    m_wave = new WaveVisualizer(this);

    QWidget* vocalSection = makeSection("Vocal", m_vocalTitle, m_vocalLetter, m_vocalAccidental, m_vocalOctave, m_vocalCents);
    vocalSection->setFixedHeight(60);

    // Top row: left guitar note section, right vocal note section (bottom-aligned over waves)
    QWidget* topRow = new QWidget(this);
    QHBoxLayout* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);
    topLayout->addWidget(guitarSection, 0, Qt::AlignLeft | Qt::AlignBottom);
    topLayout->addStretch(1);
    topLayout->addWidget(vocalSection, 0, Qt::AlignRight | Qt::AlignBottom);
    topRow->setLayout(topLayout);

    // Block that holds notes above the waves; this whole block will be vertically centered
    QWidget* waveBlock = new QWidget(this);
    QVBoxLayout* blockLayout = new QVBoxLayout(waveBlock);
    blockLayout->setContentsMargins(0, 0, 0, 0);
    blockLayout->setSpacing(0);
    blockLayout->addWidget(topRow, 0, Qt::AlignBottom);
    blockLayout->addWidget(m_wave, 0);
    waveBlock->setLayout(blockLayout);

    // Center the combined block (notes + waves) vertically in the window
    root->addStretch(1);
    root->addWidget(waveBlock, 0, Qt::AlignVCenter);
    root->addStretch(1);

    // Hide initially (keep section height fixed)
    m_guitarLetter->setVisible(false);
    m_guitarAccidental->setVisible(false);
    m_guitarOctave->setVisible(false);
    if (m_guitarCents) m_guitarCents->setVisible(false);
    m_vocalLetter->setVisible(false);
    m_vocalAccidental->setVisible(false);
    m_vocalOctave->setVisible(false);
    if (m_vocalCents) m_vocalCents->setVisible(false);
}

void NoteMonitorWidget::setGuitarNote(int midiNote, double cents) {
    updateNoteUISection(m_guitarTitle, m_guitarLetter, m_guitarAccidental, m_guitarOctave, m_guitarCents, midiNote, cents);
    if (midiNote >= 0 && m_wave) {
        QColor c(colorForCents(cents));
        m_wave->setGuitarColor(c);
        m_wave->setGuitarCentsText(formatCentsText(cents));
    }
}

void NoteMonitorWidget::setVoiceNote(int midiNote, double cents) {
    updateNoteUISection(m_vocalTitle, m_vocalLetter, m_vocalAccidental, m_vocalOctave, m_vocalCents, midiNote, cents);
    if (midiNote >= 0 && m_wave) {
        QColor c(colorForCents(cents));
        m_wave->setVoiceColor(c);
        m_wave->setVoiceCentsText(formatCentsText(cents));
    }
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
}

void NoteMonitorWidget::setGuitarVelocity(int velocity) {
    if (m_wave) m_wave->setGuitarVelocity(velocity);
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

    QString color = colorForCents(cents);
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

QString NoteMonitorWidget::colorForCents(double cents) {
    // Continuous gradient:
    // - Flat (cents < 0): green (#00ff00) -> blue (#0000ff) -> red (#ff0000) as |cents| grows to 50
    // - Sharp (cents > 0): green (#00ff00) -> red (#ff0000) as cents grows to 50 (yellow in-between)
    double t = std::min(1.0, std::fabs(cents) / 50.0);
    int r = 0, g = 0, b = 0;
    if (cents < 0) {
        // green -> light blue (#00ccff) -> red
        const int midR = 0, midG = 204, midB = 255;
        if (t <= 0.5) {
            double u = t / 0.5; // 0..1
            r = 0;
            g = static_cast<int>(std::round(255.0 * (1.0 - u) + midG * u));
            b = static_cast<int>(std::round(0.0 * (1.0 - u) + midB * u));
        } else {
            double u = (t - 0.5) / 0.5; // 0..1
            r = static_cast<int>(std::round(midR * (1.0 - u) + 255.0 * u));
            g = static_cast<int>(std::round(midG * (1.0 - u) + 0.0 * u));
            b = static_cast<int>(std::round(midB * (1.0 - u) + 0.0 * u));
        }
    } else if (cents > 0) {
        // green -> vibrant orange (#ff9900) -> red
        const int midR = 255, midG = 153, midB = 0;
        if (t <= 0.5) {
            double u = t / 0.5; // 0..1
            r = static_cast<int>(std::round(0.0 * (1.0 - u) + midR * u));
            g = static_cast<int>(std::round(255.0 * (1.0 - u) + midG * u));
            b = static_cast<int>(std::round(0.0 * (1.0 - u) + midB * u));
        } else {
            double u = (t - 0.5) / 0.5; // 0..1
            r = static_cast<int>(std::round(midR * (1.0 - u) + 255.0 * u));
            g = static_cast<int>(std::round(midG * (1.0 - u) + 0.0 * u));
            b = static_cast<int>(std::round(midB * (1.0 - u) + 0.0 * u));
        }
    } else {
        // perfect
        r = 0; g = 255; b = 0;
    }
    r = std::max(0, std::min(255, r));
    g = std::max(0, std::min(255, g));
    b = std::max(0, std::min(255, b));
    return QString("#%1%2%3")
        .arg(r, 2, 16, QLatin1Char('0'))
        .arg(g, 2, 16, QLatin1Char('0'))
        .arg(b, 2, 16, QLatin1Char('0'));
}

void NoteMonitorWidget::setKeyCenter(const QString& keyCenter) {
    m_keyCenter = keyCenter;
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

