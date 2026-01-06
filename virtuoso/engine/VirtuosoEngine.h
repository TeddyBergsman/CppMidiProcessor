#pragma once

#include <QObject>
#include <QHash>
#include <QString>

#include "virtuoso/engine/VirtuosoClock.h"
#include "virtuoso/engine/VirtuosoScheduler.h"
#include "virtuoso/groove/TimingHumanizer.h"
#include "virtuoso/theory/TheoryEvent.h"

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
    QString voicing_type;
    QString logic_tag;
    QString target_note;

    // Interaction/macro state (optional, filled by higher-level playback engines).
    QString vibe_state;
    QString user_intents;
    double user_outside_ratio = 0.0;
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

private:
    groove::TimingHumanizer& humanizerFor(const QString& agent);

    int m_bpm = 120;
    groove::TimeSignature m_ts{};
    groove::FeelTemplate m_feel = groove::FeelTemplate::straight();
    bool m_hasGrooveTemplate = false;
    groove::GrooveTemplate m_grooveTemplate{};

    VirtuosoClock m_clock;
    VirtuosoScheduler m_sched;

    QHash<QString, groove::InstrumentGrooveProfile> m_profiles;
    QHash<QString, groove::TimingHumanizer> m_humanizers;
};

} // namespace virtuoso::engine

