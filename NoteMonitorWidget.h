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

    void updateSection(QLabel* titleLabel,
                       QLabel* noteLabel,
                       QLabel* centsLabel,
                       int midiNote,
                       double cents);

    // Guitar section
    QLabel* m_guitarTitle = nullptr;
    QLabel* m_guitarNote = nullptr;
    QLabel* m_guitarCents = nullptr;

    // Vocal section
    QLabel* m_vocalTitle = nullptr;
    QLabel* m_vocalNote = nullptr;
    QLabel* m_vocalCents = nullptr;

    // Wave visualizer in between
    WaveVisualizer* m_wave = nullptr;
};

#endif // NOTEMONITORWIDGET_H

