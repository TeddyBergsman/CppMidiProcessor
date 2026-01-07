#pragma once

#include <QObject>
#include <QHash>
#include <QString>

#include "virtuoso/engine/VirtuosoClock.h"
#include "virtuoso/engine/VirtuosoScheduler.h"
#include "virtuoso/groove/TimingHumanizer.h"
#include "virtuoso/control/VirtuosityMatrix.h"

namespace virtuoso::engine {

// Abstract event (what to play) before timing humanization.
struct AgentIntentNote {
    QString agent; // e.g. "Bass"
    int channel = 1; // 1..16
    int note = 60;   // 0..127
    int baseVelocity = 90; // 1..127

    groove::GridPos startPos;
    groove::Rational durationWhole{1, 4}; // default: quarter note

    bool structural = false; // chord arrival / strong beat etc.

    // Optional glass-box fields (propagate to TheoryEvent).
    QString chord_context;
    QString scale_used;
    QString key_center;      // e.g. "C Ionian", "A Aeolian", "D Dorian"
    QString roman;           // e.g. "V7", "iiø7", "V/ii"
    QString chord_function;  // "Tonic" | "Subdominant" | "Dominant" | "Other"
    QString voicing_type;
    QString logic_tag;
    QString target_note;

    // Interaction/macro state (optional, filled by higher-level playback engines).
    QString vibe_state;
    QString user_intents;
    double user_outside_ratio = 0.0;

    // Virtuosity Matrix snapshot (optional; used for glass-box logging and later solver decisions).
    bool has_virtuosity = false;
    virtuoso::control::VirtuosityMatrix virtuosity{};
};

// Stage 1 engine: schedules intents through groove humanization and emits MIDI + TheoryEvent JSON.
class VirtuosoEngine : public QObject {
    Q_OBJECT
public:
    explicit VirtuosoEngine(QObject* parent = nullptr);

    void setTempoBpm(int bpm);
    void setTimeSignature(const groove::TimeSignature& ts);
    void setFeelTemplate(const groove::FeelTemplate& t);
    void setGrooveTemplate(const groove::GrooveTemplate& t);

    void setInstrumentGrooveProfile(const QString& agent, const groove::InstrumentGrooveProfile& p);

    bool isRunning() const { return m_clock.isRunning(); }
    qint64 elapsedMs() const { return m_clock.elapsedMs(); }

public slots:
    void start();
    void stop();

    // Manual scheduling API (used until agent planners are implemented).
    void scheduleNote(const AgentIntentNote& note);

    // Schedule a MIDI CC event aligned to the groove grid (for embodiment actions like sustain pedal).
    void scheduleCC(const QString& agent,
                    int channel,
                    int cc,
                    int value,
                    const groove::GridPos& startPos,
                    bool structural = false,
                    const QString& logicTag = QString());

    // Schedule a keyswitch note for sample-library articulations.
    // This is scheduled with a small lead so the articulation reliably applies to the note on the beat.
    void scheduleKeySwitch(const QString& agent,
                           int channel,
                           int keyswitchMidi,
                           const groove::GridPos& startPos,
                           bool structural = true,
                           int leadMs = 16,
                           int holdMs = 28,
                           const QString& logicTag = QString());

    // Low-level helper: schedule a keyswitch at an absolute engine-clock time.
    // Used to "restore" the prior articulation after transient legato modes (LS/HP) that can stick in some VSTs.
    void scheduleKeySwitchAtMs(const QString& agent,
                               int channel,
                               int keyswitchMidi,
                               qint64 onMs,
                               int holdMs = 60,
                               const QString& logicTag = QString());

    // Humanize an intent using the engine's per-agent humanizer stream.
    // IMPORTANT: This advances the agent's RNG/drift state (same as scheduleNote()).
    groove::HumanizedEvent humanizeIntent(const AgentIntentNote& note);

    // Schedule an already-humanized intent (used for inter-agent groove locking while preserving glass-box fields).
    void scheduleHumanizedIntentNote(const AgentIntentNote& note,
                                     const groove::HumanizedEvent& he,
                                     const QString& logicTagOverride = QString());

    // Harness API: schedule an already-humanized event at absolute ms times (engine-clock domain).
    // This enables explicit inter-lane groove locking while still emitting TheoryEvent JSON.
    void scheduleHumanizedNote(const QString& agent,
                               int channel,
                               int note,
                               const virtuoso::groove::HumanizedEvent& he,
                               const QString& logicTag = QString());

signals:
    // MIDI-like outputs (connectable to MidiProcessor::sendVirtual*)
    void noteOn(int channel, int note, int velocity);
    void noteOff(int channel, int note);
    void allNotesOff(int channel);
    void cc(int channel, int cc, int value);

    // Explainability output (JSON string of virtuoso::theory::TheoryEvent).
    void theoryEventJson(const QString& json);

    // Planned explainability output: emitted immediately when notes are scheduled (not when they sound).
    // This is used by UIs that want to render "next 4 bars" lookahead.
    void plannedTheoryEventJson(const QString& json);

private:
    groove::TimingHumanizer& humanizerFor(const QString& agent);
    quint32 nextNoteId() { return ++m_noteId; }

    int m_bpm = 120;
    groove::TimeSignature m_ts{};
    groove::FeelTemplate m_feel = groove::FeelTemplate::straight();
    bool m_hasGrooveTemplate = false;
    groove::GrooveTemplate m_grooveTemplate{};

    VirtuosoClock m_clock;
    VirtuosoScheduler m_sched;

    QHash<QString, groove::InstrumentGrooveProfile> m_profiles;
    QHash<QString, groove::TimingHumanizer> m_humanizers;
    quint32 m_noteId = 0;
};

} // namespace virtuoso::engine

