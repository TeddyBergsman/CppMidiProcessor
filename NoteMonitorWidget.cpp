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
    root->setSpacing(20);

    auto makeSection = [&](const QString& title,
                           QLabel*& titleLbl,
                           QLabel*& noteLbl,
                           QLabel*& centsLbl) {
        QWidget* section = new QWidget(this);
        QVBoxLayout* v = new QVBoxLayout(section);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);

        titleLbl = new QLabel(title, section);
        titleLbl->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        titleLbl->setStyleSheet("QLabel { color: #aaa; font-size: 11pt; }");

        noteLbl = new QLabel("", section);
        noteLbl->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        noteLbl->setStyleSheet("QLabel { color: #ddd; font-size: 40pt; font-weight: bold; }");

        centsLbl = new QLabel("", section);
        centsLbl->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        centsLbl->setStyleSheet("QLabel { color: #888; font-size: 12pt; }");

        v->addWidget(titleLbl);
        v->addWidget(noteLbl);
        v->addWidget(centsLbl);
        section->setLayout(v);
        root->addWidget(section);
    };

    makeSection("Guitar", m_guitarTitle, m_guitarNote, m_guitarCents);

    // Insert wave visualizer between the sections
    m_wave = new WaveVisualizer(this);
    root->addWidget(m_wave);

    makeSection("Vocal", m_vocalTitle, m_vocalNote, m_vocalCents);

    // Hide initially
    m_guitarTitle->setVisible(false);
    m_guitarNote->setVisible(false);
    m_guitarCents->setVisible(false);
    m_vocalTitle->setVisible(false);
    m_vocalNote->setVisible(false);
    m_vocalCents->setVisible(false);
}

void NoteMonitorWidget::setGuitarNote(int midiNote, double cents) {
    updateSection(m_guitarTitle, m_guitarNote, m_guitarCents, midiNote, cents);
    if (midiNote >= 0 && m_wave) {
        QColor c(colorForCents(cents));
        m_wave->setGuitarColor(c);
    }
}

void NoteMonitorWidget::setVoiceNote(int midiNote, double cents) {
    updateSection(m_vocalTitle, m_vocalNote, m_vocalCents, midiNote, cents);
    if (midiNote >= 0 && m_wave) {
        QColor c(colorForCents(cents));
        m_wave->setVoiceColor(c);
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

void NoteMonitorWidget::updateSection(QLabel* titleLabel,
                                      QLabel* noteLabel,
                                      QLabel* centsLabel,
                                      int midiNote,
                                      double cents) {
    bool show = midiNote >= 0;
    titleLabel->setVisible(show);
    noteLabel->setVisible(show);
    centsLabel->setVisible(show);
    if (!show) return;

    QString color = colorForCents(cents);
    QString noteText = formatNoteName(midiNote);
    QString centsText = formatCentsText(cents);

    noteLabel->setText(noteText);
    noteLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 40pt; font-weight: bold; }").arg(color));
    centsLabel->setText(centsText);
    centsLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 12pt; }").arg(color));
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

