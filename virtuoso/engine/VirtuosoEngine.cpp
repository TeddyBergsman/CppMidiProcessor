#include "virtuoso/engine/VirtuosoEngine.h"

#include <QtGlobal>

namespace virtuoso::engine {

VirtuosoEngine::VirtuosoEngine(QObject* parent)
    : QObject(parent)
    , m_sched(&m_clock, this) {
    connect(&m_sched, &VirtuosoScheduler::noteOn, this, &VirtuosoEngine::noteOn);
    connect(&m_sched, &VirtuosoScheduler::noteOff, this, &VirtuosoEngine::noteOff);
    connect(&m_sched, &VirtuosoScheduler::allNotesOff, this, &VirtuosoEngine::allNotesOff);
    connect(&m_sched, &VirtuosoScheduler::cc, this, &VirtuosoEngine::cc);
    connect(&m_sched, &VirtuosoScheduler::theoryEventJson, this, &VirtuosoEngine::theoryEventJson);
}

void VirtuosoEngine::setTempoBpm(int bpm) {
    m_bpm = qBound(30, bpm, 300);
}

void VirtuosoEngine::setTimeSignature(const groove::TimeSignature& ts) {
    m_ts = ts;
    if (m_ts.den <= 0) m_ts.den = 4;
    if (m_ts.num <= 0) m_ts.num = 4;
}

void VirtuosoEngine::setFeelTemplate(const groove::FeelTemplate& t) {
    m_feel = t;
    for (auto it = m_humanizers.begin(); it != m_humanizers.end(); ++it) {
        it.value().setFeelTemplate(m_feel);
    }
}

void VirtuosoEngine::setGrooveTemplate(const groove::GrooveTemplate& t) {
    m_hasGrooveTemplate = true;
    m_grooveTemplate = t;
    for (auto it = m_humanizers.begin(); it != m_humanizers.end(); ++it) {
        it.value().setGrooveTemplate(m_grooveTemplate);
    }
}

void VirtuosoEngine::setInstrumentGrooveProfile(const QString& agent, const groove::InstrumentGrooveProfile& p) {
    m_profiles.insert(agent, p);
    auto& h = humanizerFor(agent);
    h.setProfile(p);
    h.setFeelTemplate(m_feel);
    if (m_hasGrooveTemplate) h.setGrooveTemplate(m_grooveTemplate);
}

void VirtuosoEngine::start() {
    m_sched.clear();
    m_clock.start();
    for (auto it = m_humanizers.begin(); it != m_humanizers.end(); ++it) {
        it.value().reset();
        it.value().setFeelTemplate(m_feel);
    }
}

void VirtuosoEngine::stop() {
    if (!m_clock.isRunning()) return;
    m_clock.stop();
    m_sched.clear();
}

groove::TimingHumanizer& VirtuosoEngine::humanizerFor(const QString& agent) {
    auto it = m_humanizers.find(agent);
    if (it != m_humanizers.end()) return it.value();

    groove::InstrumentGrooveProfile p;
    p.instrument = agent;
    p.humanizeSeed = 1;
    // If a profile was previously set, use it.
    if (m_profiles.contains(agent)) p = m_profiles.value(agent);

    groove::TimingHumanizer h(p);
    h.setFeelTemplate(m_feel);
    if (m_hasGrooveTemplate) h.setGrooveTemplate(m_grooveTemplate);
    m_humanizers.insert(agent, h);
    return m_humanizers[agent];
}

void VirtuosoEngine::scheduleNote(const AgentIntentNote& note) {
    if (!m_clock.isRunning()) return;
    if (note.channel < 1 || note.channel > 16) return;
    if (note.note < 0 || note.note > 127) return;
    if (note.baseVelocity < 1 || note.baseVelocity > 127) return;

    auto& h = humanizerFor(note.agent);
    const auto he = h.humanizeNote(note.startPos, m_ts, m_bpm, note.baseVelocity, note.durationWhole, note.structural);

    VirtuosoScheduler::ScheduledEvent on;
    on.dueMs = he.onMs;
    on.kind = VirtuosoScheduler::Kind::NoteOn;
    on.channel = note.channel;
    on.note = note.note;
    on.velocity = he.velocity;
    m_sched.schedule(on);

    VirtuosoScheduler::ScheduledEvent off;
    off.dueMs = he.offMs;
    off.kind = VirtuosoScheduler::Kind::NoteOff;
    off.channel = note.channel;
    off.note = note.note;
    m_sched.schedule(off);

    // Explainability: emit a TheoryEvent JSON (minimal, groove-focused).
    virtuoso::theory::TheoryEvent te;
    te.agent = note.agent;
    te.timestamp = he.grid_pos; // Stage 1: use grid position as the timestamp string.
    te.dynamic_marking = QString::number(he.velocity); // placeholder; later becomes "mf"/etc.
    te.groove_template = he.groove_template;
    te.grid_pos = he.grid_pos;
    te.timing_offset_ms = he.timing_offset_ms;
    te.velocity_adjustment = he.velocity_adjustment;
    te.humanize_seed = he.humanize_seed;

    VirtuosoScheduler::ScheduledEvent tj;
    tj.dueMs = he.onMs;
    tj.kind = VirtuosoScheduler::Kind::TheoryEventJson;
    tj.theoryJson = te.toJsonString(true);
    m_sched.schedule(tj);
}

void VirtuosoEngine::scheduleHumanizedNote(const QString& agent,
                                           int channel,
                                           int note,
                                           const virtuoso::groove::HumanizedEvent& he,
                                           const QString& logicTag) {
    if (!m_clock.isRunning()) return;
    if (channel < 1 || channel > 16) return;
    if (note < 0 || note > 127) return;
    if (he.velocity < 1 || he.velocity > 127) return;
    if (he.offMs <= he.onMs) return;

    VirtuosoScheduler::ScheduledEvent on;
    on.dueMs = he.onMs;
    on.kind = VirtuosoScheduler::Kind::NoteOn;
    on.channel = channel;
    on.note = note;
    on.velocity = he.velocity;
    m_sched.schedule(on);

    VirtuosoScheduler::ScheduledEvent off;
    off.dueMs = he.offMs;
    off.kind = VirtuosoScheduler::Kind::NoteOff;
    off.channel = channel;
    off.note = note;
    m_sched.schedule(off);

    virtuoso::theory::TheoryEvent te;
    te.agent = agent;
    te.timestamp = he.grid_pos;
    te.logic_tag = logicTag;
    te.dynamic_marking = QString::number(he.velocity);
    te.groove_template = he.groove_template;
    te.grid_pos = he.grid_pos;
    te.timing_offset_ms = he.timing_offset_ms;
    te.velocity_adjustment = he.velocity_adjustment;
    te.humanize_seed = he.humanize_seed;

    VirtuosoScheduler::ScheduledEvent tj;
    tj.dueMs = he.onMs;
    tj.kind = VirtuosoScheduler::Kind::TheoryEventJson;
    tj.theoryJson = te.toJsonString(true);
    m_sched.schedule(tj);
}

} // namespace virtuoso::engine

