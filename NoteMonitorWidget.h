#ifndef NOTEMONITORWIDGET_H
#define NOTEMONITORWIDGET_H

#include <QWidget>
#include "chart/ChartModel.h"
#include "music/BassProfile.h"
#include "music/PianoProfile.h"
class WaveVisualizer;

class QLabel;
class QVBoxLayout;
class QComboBox;
class QPushButton;
class QSpinBox;
class QCheckBox;
class QTextEdit;
class QSlider;

namespace ireal { struct Playlist; }
namespace chart { class SongChartWidget; }
namespace playback { class BandPlaybackEngine; }
namespace playback { class VirtuosoBalladMvpPlaybackEngine; }
class MidiProcessor;

class NoteMonitorWidget : public QWidget {
    Q_OBJECT
public:
    explicit NoteMonitorWidget(QWidget* parent = nullptr);
    ~NoteMonitorWidget() override;
    void setKeyCenter(const QString& keyCenter);
    void setIRealPlaylist(const ireal::Playlist& playlist);
    void setMidiProcessor(MidiProcessor* processor);
    void openBassStyleEditor();
    void openPianoStyleEditor();

public slots:
    // Used by MainWindow on app quit to guarantee we stop generating/scheduling notes.
    void stopAllPlayback();
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
    void loadSongAtIndex(int idx);

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
    QString m_detectedSongKeyCenter;     // key parsed from iReal song metadata
    QString m_currentSongId;             // stable ID used for per-song overrides
    chart::ChartModel m_baseChartModel;  // untransposed model (as parsed from iReal)
    bool m_hasBaseChartModel = false;
    bool m_isApplyingSongState = false;  // guards preference writes during programmatic updates

    // Absolute-position overlay above waves
    QWidget* m_notesOverlay = nullptr;
    QWidget* m_guitarSection = nullptr;
    QWidget* m_vocalSection = nullptr;
    QWidget* m_trailLayer = nullptr; // Layer for fading trail ghosts

    // iReal chart UI (top half)
    QWidget* m_chartContainer = nullptr;
    chart::SongChartWidget* m_chartWidget = nullptr;
    QComboBox* m_songCombo = nullptr;
    QComboBox* m_keyCombo = nullptr;
    QPushButton* m_playButton = nullptr;
    // Virtuoso MVP controls (new engine, not legacy players)
    QComboBox* m_virtuosoPresetCombo = nullptr;
    QPushButton* m_virtuosoPlayButton = nullptr;
    QSpinBox* m_tempoSpin = nullptr;
    QSpinBox* m_repeatsSpin = nullptr;
    playback::BandPlaybackEngine* m_playback = nullptr;
    playback::VirtuosoBalladMvpPlaybackEngine* m_virtuosoPlayback = nullptr;
    ireal::Playlist* m_playlist = nullptr; // owned pointer to avoid header includes

    // Virtuoso debug / visualization (glass box)
    QCheckBox* m_virtuosoDebugToggle = nullptr;
    QLabel* m_virtuosoHud = nullptr;
    QComboBox* m_virtuosoVibeOverride = nullptr;
    QSlider* m_virtuosoInteractionBoost = nullptr;
    QTextEdit* m_virtuosoTheoryLog = nullptr;

    MidiProcessor* m_midiProcessor = nullptr; // not owned

    // Current per-song bass profile (mirrored for UI/editor convenience)
    music::BassProfile m_bassProfile;
    // Current per-song piano profile (mirrored for UI/editor convenience)
    music::PianoProfile m_pianoProfile;

    // Last state for positioning
    int m_lastGuitarNote = -1;
    int m_lastVoiceNote = -1;
    double m_lastVoiceCents = 0.0;
    int m_lastVocalX = -1; // Track last X position for trail detection
    
    // Trail optimization: limit number of ghosts
    static constexpr int m_trailMaxGhosts = 5; // Increased for better trail visibility

protected:
    void resizeEvent(QResizeEvent* event) override;
};

#endif // NOTEMONITORWIDGET_H

