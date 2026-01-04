#pragma once

#include <QDialog>
#include <QMetaObject>
#include <QStringList>

#include "music/PianoProfile.h"

class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QDialogButtonBox;
class QComboBox;
class QPushButton;
class QListWidget;
class QGroupBox;

namespace playback { class BandPlaybackEngine; }

class PianoStyleEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit PianoStyleEditorDialog(const music::PianoProfile& initial,
                                    playback::BandPlaybackEngine* playback = nullptr,
                                    QWidget* parent = nullptr);

signals:
    // Fires on any control change for live preview (does NOT imply persistence).
    void profilePreview(const music::PianoProfile& profile);
    // Fires on Apply/OK. Caller should persist per-song.
    void profileCommitted(const music::PianoProfile& profile);

private:
    void buildUi();
    void setUiFromProfile(const music::PianoProfile& p);
    music::PianoProfile profileFromUi() const;
    void emitPreview();

    void appendLiveLogLine(const QString& line);
    void setLiveLogActive(bool active);
    void flushPendingLog();

    music::PianoProfile m_initial;
    playback::BandPlaybackEngine* m_playback = nullptr; // not owned
    QMetaObject::Connection m_logConn;
    QStringList m_pendingLog;
    class QTimer* m_logFlushTimer = nullptr;

    // Presets
    QComboBox* m_presetCombo = nullptr;
    QPushButton* m_loadPresetBtn = nullptr;
    QCheckBox* m_keepRanges = nullptr;
    QCheckBox* m_keepEnable = nullptr;

    // Core
    QCheckBox* m_enabled = nullptr;
    QSpinBox* m_channel = nullptr;
    QComboBox* m_feelStyle = nullptr;

    // Ranges
    QSpinBox* m_lhMin = nullptr;
    QSpinBox* m_lhMax = nullptr;
    QSpinBox* m_rhMin = nullptr;
    QSpinBox* m_rhMax = nullptr;

    // Timing
    QSpinBox* m_jitterMs = nullptr;
    QSpinBox* m_laidBackMs = nullptr;
    QSpinBox* m_pushMs = nullptr;
    QSpinBox* m_driftMaxMs = nullptr;
    QDoubleSpinBox* m_driftRate = nullptr;

    // Dynamics
    QSpinBox* m_baseVel = nullptr;
    QSpinBox* m_velVar = nullptr;
    QDoubleSpinBox* m_accentDown = nullptr;
    QDoubleSpinBox* m_accentBack = nullptr;

    // Rhythm
    QDoubleSpinBox* m_compDensity = nullptr;
    QDoubleSpinBox* m_anticipation = nullptr;
    QDoubleSpinBox* m_syncop = nullptr;
    QDoubleSpinBox* m_restProb = nullptr;

    // Voicing
    QCheckBox* m_preferRootless = nullptr;
    QDoubleSpinBox* m_rootlessProb = nullptr;
    QDoubleSpinBox* m_drop2Prob = nullptr;
    QDoubleSpinBox* m_quartalProb = nullptr;
    QDoubleSpinBox* m_clusterProb = nullptr;
    QDoubleSpinBox* m_tensionProb = nullptr;
    QDoubleSpinBox* m_avoidRootProb = nullptr;
    QDoubleSpinBox* m_avoidThirdProb = nullptr;
    QSpinBox* m_maxHandLeap = nullptr;
    QDoubleSpinBox* m_voiceLeading = nullptr;
    QDoubleSpinBox* m_repeatPenalty = nullptr;

    // Fills
    QDoubleSpinBox* m_fillPhraseEnd = nullptr;
    QDoubleSpinBox* m_fillAnyBeat = nullptr;
    QSpinBox* m_phraseBars = nullptr;
    QSpinBox* m_fillMaxNotes = nullptr;
    QSpinBox* m_fillMinNote = nullptr;
    QSpinBox* m_fillMaxNote = nullptr;

    // Pedal
    QCheckBox* m_pedalEnabled = nullptr;
    QCheckBox* m_pedalReleaseOnChange = nullptr;
    QSpinBox* m_pedalDown = nullptr;
    QSpinBox* m_pedalUp = nullptr;
    QSpinBox* m_pedalMinHoldMs = nullptr;
    QSpinBox* m_pedalMaxHoldMs = nullptr;
    QDoubleSpinBox* m_pedalChangeProb = nullptr;

    // Live log
    QCheckBox* m_reasoningLogEnabled = nullptr;
    QPushButton* m_clearLogBtn = nullptr;
    QListWidget* m_liveLog = nullptr;

    QDialogButtonBox* m_buttons = nullptr;
};

