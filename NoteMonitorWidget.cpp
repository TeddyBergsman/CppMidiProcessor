#include "NoteMonitorWidget.h"
#include <QtWidgets>
#include <cmath>

NoteMonitorWidget::NoteMonitorWidget(QWidget* parent)
    : QWidget(parent) {
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
}

void NoteMonitorWidget::setVoiceNote(int midiNote, double cents) {
    updateSection(m_vocalTitle, m_vocalNote, m_vocalCents, midiNote, cents);
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
    double a = std::fabs(cents);
    if (a <= 3.0) return "#00ff00";      // green
    if (a <= 10.0) return "#ffa500";     // orange
    return "#ff4444";                    // red
}

