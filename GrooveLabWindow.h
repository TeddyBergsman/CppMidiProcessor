#pragma once

#include <QMainWindow>
#include <QHash>

#include "virtuoso/groove/FeelTemplate.h"
#include "virtuoso/groove/GrooveRegistry.h"
#include "virtuoso/groove/TimingHumanizer.h"
#include "virtuoso/engine/VirtuosoEngine.h"

class MidiProcessor;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QTextEdit;
class QTimer;
class QScrollArea;
class QLabel;

// A dedicated harness window for auditioning the Groove/Grid/Microtiming engine.
// This intentionally avoids any legacy musician logic and focuses on timing/velocity.
class GrooveLabWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit GrooveLabWindow(MidiProcessor* midi, QWidget* parent = nullptr);

private slots:
    void onStart();
    void onStop();
    void onClearLog();
    void onApplySettings();
    void onAnySettingChanged();
    void onTickSchedule();
    void onTheoryJson(const QString& json);

private:
    enum class PatternKind {
        QuarterClick = 0,
        SwingEighths,
        TripletEighths,
    };

    void buildUi();
    void wireEngineOutputs();

    virtuoso::groove::GrooveTemplate currentGrooveTemplate() const;
    virtuoso::groove::InstrumentGrooveProfile currentInstrumentProfileLaneA() const;
    virtuoso::groove::InstrumentGrooveProfile currentInstrumentProfileLaneB() const;
    QString laneAAgentId() const;
    QString laneBAgentId() const;
    PatternKind currentPattern() const;

    void resetPatternState();
    void scheduleAhead();
    void applyNow(bool restartIfRunning);

    static QString patternName(PatternKind k);

    // Musical scheduling helpers
    static virtuoso::groove::Rational stepWholeFor(PatternKind k, const virtuoso::groove::TimeSignature& ts);
    static void advancePos(virtuoso::groove::GridPos& p,
                           const virtuoso::groove::Rational& stepWhole,
                           const virtuoso::groove::TimeSignature& ts);

    MidiProcessor* m_midi = nullptr; // not owned
    virtuoso::engine::VirtuosoEngine m_engine;

    // UI
    QSpinBox* m_bpm = nullptr;
    QSpinBox* m_tsNum = nullptr;
    QSpinBox* m_tsDen = nullptr;

    QComboBox* m_preset = nullptr;   // optional jazz preset
    QComboBox* m_template = nullptr; // groove template
    QDoubleSpinBox* m_templateAmount = nullptr;
    QLabel* m_presetNotes = nullptr;

    QComboBox* m_agent = nullptr;
    QSpinBox* m_channel = nullptr;
    QSpinBox* m_seed = nullptr;

    QSpinBox* m_pushMs = nullptr;
    QSpinBox* m_laidBackMs = nullptr;
    QSpinBox* m_jitterMs = nullptr;
    QSpinBox* m_attackVarMs = nullptr;
    QSpinBox* m_driftMaxMs = nullptr;
    QDoubleSpinBox* m_driftRate = nullptr;

    QSpinBox* m_baseVel = nullptr;
    QSpinBox* m_velJitter = nullptr;
    QDoubleSpinBox* m_accentDownbeat = nullptr;
    QDoubleSpinBox* m_accentBackbeat = nullptr;
    QDoubleSpinBox* m_gatePct = nullptr;

    // Lane B (optional)
    QCheckBox* m_laneBEnabled = nullptr;
    QComboBox* m_agentB = nullptr;
    QSpinBox* m_channelB = nullptr;
    QSpinBox* m_seedB = nullptr;

    QSpinBox* m_pushMsB = nullptr;
    QSpinBox* m_laidBackMsB = nullptr;
    QSpinBox* m_jitterMsB = nullptr;
    QSpinBox* m_attackVarMsB = nullptr;
    QSpinBox* m_driftMaxMsB = nullptr;
    QDoubleSpinBox* m_driftRateB = nullptr;

    QSpinBox* m_baseVelB = nullptr;
    QSpinBox* m_velJitterB = nullptr;
    QDoubleSpinBox* m_accentDownbeatB = nullptr;
    QDoubleSpinBox* m_accentBackbeatB = nullptr;
    QDoubleSpinBox* m_gatePctB = nullptr;

    QComboBox* m_pattern = nullptr;
    QSpinBox* m_testMidi = nullptr;
    QSpinBox* m_testMidiB = nullptr;
    QSpinBox* m_lookaheadMs = nullptr;

    QPushButton* m_start = nullptr;
    QPushButton* m_stop = nullptr;
    QPushButton* m_clearLog = nullptr;
    QTextEdit* m_log = nullptr;

    QTimer* m_tick = nullptr;
    QTimer* m_applyDebounce = nullptr;

    // Pattern state
    virtuoso::groove::GridPos m_nextPos;
    qint64 m_lastScheduledOnMs = -1;

    // Local humanizers (used for deterministic scheduling + groove-lock blending)
    virtuoso::groove::TimingHumanizer m_hA;
    virtuoso::groove::TimingHumanizer m_hB;

    // Groove lock controls
    QComboBox* m_lockMode = nullptr;
    QDoubleSpinBox* m_lockStrength = nullptr;

    virtuoso::groove::GrooveRegistry m_grooveRegistry;
};

