#ifndef NOTEMONITORWIDGET_H
#define NOTEMONITORWIDGET_H

#include <QWidget>
class WaveVisualizer;

class QLabel;
class QVBoxLayout;
class QComboBox;
class QPushButton;
class QSpinBox;

namespace ireal { struct Playlist; }
namespace chart { class SongChartWidget; }
namespace playback { class SilentPlaybackEngine; }

class NoteMonitorWidget : public QWidget {
    Q_OBJECT
public:
    explicit NoteMonitorWidget(QWidget* parent = nullptr);
    ~NoteMonitorWidget() override;
    void setKeyCenter(const QString& keyCenter);
    void setPitchMonitorBpm(int bpm);
    void setIRealPlaylist(const ireal::Playlist& playlist);

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
    void repositionNotes();
    void addVocalTrailSnapshot(const QRect& oldGeo);

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
    class PitchMonitorWidget* m_pitchMonitor = nullptr;

    // Key center (for enharmonic spelling)
    QString m_keyCenter = "Eb major";

    // Absolute-position overlay above waves
    QWidget* m_notesOverlay = nullptr;
    QWidget* m_guitarSection = nullptr;
    QWidget* m_vocalSection = nullptr;
    QWidget* m_trailLayer = nullptr; // Layer for fading trail ghosts

    // iReal chart UI (top half)
    QWidget* m_chartContainer = nullptr;
    chart::SongChartWidget* m_chartWidget = nullptr;
    QComboBox* m_songCombo = nullptr;
    QPushButton* m_playButton = nullptr;
    QSpinBox* m_tempoSpin = nullptr;
    playback::SilentPlaybackEngine* m_playback = nullptr;
    ireal::Playlist* m_playlist = nullptr; // owned pointer to avoid header includes

    // Last state for positioning
    int m_lastGuitarNote = -1;
    int m_lastVoiceNote = -1;
    double m_lastVoiceCents = 0.0;
    int m_lastVocalX = -1; // Track last X position for trail detection
    
    // Trail optimization: limit number of ghosts
    static constexpr int m_trailMaxGhosts = 15; // Increased for better trail visibility

protected:
    void resizeEvent(QResizeEvent* event) override;
};

#endif // NOTEMONITORWIDGET_H

