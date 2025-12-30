#ifndef NOTEMONITORWIDGET_H
#define NOTEMONITORWIDGET_H

#include <QWidget>
class WaveVisualizer;

class QLabel;
class QVBoxLayout;

class NoteMonitorWidget : public QWidget {
    Q_OBJECT
public:
    explicit NoteMonitorWidget(QWidget* parent = nullptr);
    void setKeyCenter(const QString& keyCenter);

public slots:
    void setGuitarNote(int midiNote, double cents);
    void setVoiceNote(int midiNote, double cents);
    void setGuitarHz(double hz);
    void setVoiceHz(double hz);
    void setGuitarAmplitude(int aftertouch);
    void setVoiceAmplitude(int cc2);
    void setGuitarVelocity(int velocity);

private:
    static QString formatNoteName(int midiNote);
    static QString formatCentsText(double cents);
    static QString colorForCents(double cents);
    void updateNoteParts(QLabel* letterLbl, QLabel* accidentalLbl, QLabel* octaveLbl,
                         int midiNote, double cents);
    void chooseSpellingForKey(int midiNote, QChar& letterOut, QChar& accidentalOut, int& octaveOut) const;
    bool preferFlats() const;

    void updateNoteUISection(QLabel* titleLabel,
                       QLabel* letterLbl,
                       QLabel* accidentalLbl,
                       QLabel* octaveLbl,
                       QLabel* centsLabel,
                       int midiNote,
                       double cents);

    // Guitar section
    QLabel* m_guitarTitle = nullptr;
    QLabel* m_guitarLetter = nullptr;
    QLabel* m_guitarAccidental = nullptr;
    QLabel* m_guitarOctave = nullptr;
    QLabel* m_guitarCents = nullptr;

    // Vocal section
    QLabel* m_vocalTitle = nullptr;
    QLabel* m_vocalLetter = nullptr;
    QLabel* m_vocalAccidental = nullptr;
    QLabel* m_vocalOctave = nullptr;
    QLabel* m_vocalCents = nullptr;

    // Wave visualizer in between
    WaveVisualizer* m_wave = nullptr;

    // Key center (for enharmonic spelling)
    QString m_keyCenter = "Eb major";
};

#endif // NOTEMONITORWIDGET_H

