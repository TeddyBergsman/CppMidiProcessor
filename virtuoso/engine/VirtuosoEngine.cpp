#include "virtuoso/engine/VirtuosoEngine.h"

#include "virtuoso/theory/TheoryEvent.h"
#include "virtuoso/groove/GrooveGrid.h"

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
    m_gridBaseInitialized = false;
    m_gridBaseMs = 0;
    for (auto it = m_humanizers.begin(); it != m_humanizers.end(); ++it) {
        it.value().reset();
        it.value().setFeelTemplate(m_feel);
    }
}

void VirtuosoEngine::stop() {
    // Always panic-silence first (explicit NOTE_OFF), even if clock is already stopped.
    m_sched.panicSilence();
    if (m_clock.isRunning()) m_clock.stop();
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

qint64 VirtuosoEngine::ensureGridBaseMs() {
    if (!m_clock.isRunning()) return 0;
    if (m_gridBaseInitialized) return m_gridBaseMs;
    // Guard ensures the first downbeat is in the future relative to scheduling,
    // preventing a compressed first interval where beat 2 feels early.
    const qint64 now = m_clock.elapsedMs();
    const qint64 guardMs = 35;
    m_gridBaseMs = qMax<qint64>(0, now + guardMs);
    m_gridBaseInitialized = true;
    return m_gridBaseMs;
}

void VirtuosoEngine::scheduleNote(const AgentIntentNote& note) {
    if (!m_clock.isRunning()) return;
    if (note.channel < 1 || note.channel > 16) return;
    if (note.note < 0 || note.note > 127) return;
    if (note.baseVelocity < 1 || note.baseVelocity > 127) return;

    const qint64 baseMs = ensureGridBaseMs();
    auto& h = humanizerFor(note.agent);
    auto he = h.humanizeNote(note.startPos, m_ts, m_bpm, note.baseVelocity, note.durationWhole, note.structural);
    he.onMs += baseMs;
    he.offMs += baseMs;
    const quint32 id = nextNoteId();

    VirtuosoScheduler::ScheduledEvent on;
    on.dueMs = he.onMs;
    on.kind = VirtuosoScheduler::Kind::NoteOn;
    on.channel = note.channel;
    on.note = note.note;
    on.velocity = he.velocity;
    on.noteId = id;
    m_sched.schedule(on);

    VirtuosoScheduler::ScheduledEvent off;
    off.dueMs = he.offMs;
    off.kind = VirtuosoScheduler::Kind::NoteOff;
    off.channel = note.channel;
    off.note = note.note;
    off.noteId = id;
    m_sched.schedule(off);

    // Explainability: emit a TheoryEvent JSON (minimal, groove-focused).
    virtuoso::theory::TheoryEvent te;
    te.agent = note.agent;
    te.timestamp = he.grid_pos; // Stage 1: use grid position as the timestamp string.
    te.chord_context = note.chord_context;
    te.scale_used = note.scale_used;
    te.key_center = note.key_center;
    te.roman = note.roman;
    te.chord_function = note.chord_function;
    te.voicing_type = note.voicing_type;
    te.logic_tag = note.logic_tag;
    te.target_note = note.target_note;
    te.dynamic_marking = QString::number(he.velocity); // placeholder; later becomes "mf"/etc.
    te.groove_template = he.groove_template;
    te.grid_pos = he.grid_pos;
    te.timing_offset_ms = he.timing_offset_ms;
    te.velocity_adjustment = he.velocity_adjustment;
    te.humanize_seed = he.humanize_seed;
    te.channel = note.channel;
    te.note = note.note;
    te.on_ms = he.onMs;
    te.off_ms = he.offMs;
    te.tempo_bpm = m_bpm;
    te.ts_num = m_ts.num;
    te.ts_den = m_ts.den;
    te.engine_now_ms = m_clock.elapsedMs();
    te.vibe_state = note.vibe_state;
    te.user_intents = note.user_intents;
    te.user_outside_ratio = note.user_outside_ratio;
    te.has_virtuosity = note.has_virtuosity;
    te.virtuosity = note.virtuosity;

    emit plannedTheoryEventJson(te.toJsonString(true));

    VirtuosoScheduler::ScheduledEvent tj;
    tj.dueMs = he.onMs;
    tj.kind = VirtuosoScheduler::Kind::TheoryEventJson;
    tj.theoryJson = te.toJsonString(true);
    m_sched.schedule(tj);
}

void VirtuosoEngine::scheduleCC(const QString& agent,
                                int channel,
                                int cc,
                                int value,
                                const groove::GridPos& startPos,
                                bool structural,
                                const QString& logicTag) {
    if (!m_clock.isRunning()) return;
    if (channel < 1 || channel > 16) return;
    if (cc < 0 || cc > 127) return;
    if (value < 0 || value > 127) return;

    // CC timing should be "decisive" and must not arrive after notes that depend on it
    // (especially sustain pedal). So we schedule CCs grid-aligned with a tiny lead.
    const qint64 baseOn = virtuoso::groove::GrooveGrid::posToMs(startPos, m_ts, m_bpm) + ensureGridBaseMs();
    const int leadMs = structural ? 12 : 8;
    const qint64 on = qMax<qint64>(0, baseOn - qMax(0, leadMs));

    VirtuosoScheduler::ScheduledEvent ev;
    ev.dueMs = on;
    ev.kind = VirtuosoScheduler::Kind::CC;
    ev.channel = channel;
    ev.cc = cc;
    ev.ccValue = value;
    m_sched.schedule(ev);

    // Explainability: represent CC actions as TheoryEvents too.
    virtuoso::theory::TheoryEvent te;
    te.event_kind = "cc";
    te.agent = agent;
    te.timestamp = virtuoso::groove::GrooveGrid::toString(startPos, m_ts);
    te.logic_tag = logicTag;
    te.dynamic_marking = QString::number(value);
    te.grid_pos = virtuoso::groove::GrooveGrid::toString(startPos, m_ts);
    te.timing_offset_ms = int(on - baseOn);
    te.velocity_adjustment = 0;
    te.humanize_seed = 0u;
    te.channel = channel;
    te.note = -1;
    te.cc = cc;
    te.cc_value = value;
    te.on_ms = on;
    te.off_ms = on;
    te.tempo_bpm = m_bpm;
    te.ts_num = m_ts.num;
    te.ts_den = m_ts.den;
    te.engine_now_ms = m_clock.elapsedMs();

    emit plannedTheoryEventJson(te.toJsonString(true));

    VirtuosoScheduler::ScheduledEvent tj;
    tj.dueMs = on;
    tj.kind = VirtuosoScheduler::Kind::TheoryEventJson;
    tj.theoryJson = te.toJsonString(true);
    m_sched.schedule(tj);
}

void VirtuosoEngine::scheduleKeySwitch(const QString& agent,
                                       int channel,
                                       int keyswitchMidi,
                                       const groove::GridPos& startPos,
                                       bool structural,
                                       int leadMs,
                                       int holdMs,
                                       const QString& logicTag) {
    if (!m_clock.isRunning()) return;
    if (channel < 1 || channel > 16) return;
    if (keyswitchMidi < 0 || keyswitchMidi > 127) return;

    const qint64 baseOn = virtuoso::groove::GrooveGrid::posToMs(startPos, m_ts, m_bpm) + ensureGridBaseMs();
    const int lead = qMax(0, leadMs);
    const qint64 on = qMax<qint64>(0, baseOn - lead);
    const bool latch = (holdMs <= 0);
    const qint64 off = on + qMax<qint64>(1, qMax(6, holdMs));
    const quint32 id = nextNoteId();

    VirtuosoScheduler::ScheduledEvent evOn;
    evOn.dueMs = on;
    evOn.kind = VirtuosoScheduler::Kind::NoteOn;
    evOn.channel = channel;
    evOn.note = keyswitchMidi;
    evOn.velocity = 1; // keyswitch velocity generally irrelevant
    evOn.noteId = id;
    m_sched.schedule(evOn);

    if (!latch) {
        VirtuosoScheduler::ScheduledEvent evOff;
        evOff.dueMs = off;
        evOff.kind = VirtuosoScheduler::Kind::NoteOff;
        evOff.channel = channel;
        evOff.note = keyswitchMidi;
        evOff.noteId = id;
        m_sched.schedule(evOff);
    }

    virtuoso::theory::TheoryEvent te;
    te.event_kind = "keyswitch";
    te.agent = agent;
    te.timestamp = virtuoso::groove::GrooveGrid::toString(startPos, m_ts);
    te.logic_tag = logicTag;
    te.dynamic_marking = "1";
    te.grid_pos = virtuoso::groove::GrooveGrid::toString(startPos, m_ts);
    te.timing_offset_ms = int(on - baseOn);
    te.velocity_adjustment = 0;
    te.humanize_seed = 0u;
    te.channel = channel;
    te.note = keyswitchMidi;
    te.on_ms = on;
    te.off_ms = latch ? on : off;
    te.tempo_bpm = m_bpm;
    te.ts_num = m_ts.num;
    te.ts_den = m_ts.den;
    te.engine_now_ms = m_clock.elapsedMs();
    emit plannedTheoryEventJson(te.toJsonString(true));

    VirtuosoScheduler::ScheduledEvent tj;
    tj.dueMs = on;
    tj.kind = VirtuosoScheduler::Kind::TheoryEventJson;
    tj.theoryJson = te.toJsonString(true);
    m_sched.schedule(tj);

    Q_UNUSED(structural);
}

void VirtuosoEngine::scheduleKeySwitchAtMs(const QString& agent,
                                           int channel,
                                           int keyswitchMidi,
                                           qint64 onMs,
                                           int holdMs,
                                           const QString& logicTag) {
    if (!m_clock.isRunning()) return;
    if (channel < 1 || channel > 16) return;
    if (keyswitchMidi < 0 || keyswitchMidi > 127) return;
    if (onMs < 0) onMs = 0;

    const bool latch = (holdMs <= 0);
    const qint64 offMs = onMs + qMax<qint64>(1, qMax(6, holdMs));
    const quint32 id = nextNoteId();

    VirtuosoScheduler::ScheduledEvent evOn;
    evOn.dueMs = onMs;
    evOn.kind = VirtuosoScheduler::Kind::NoteOn;
    evOn.channel = channel;
    evOn.note = keyswitchMidi;
    evOn.velocity = 1;
    evOn.noteId = id;
    m_sched.schedule(evOn);

    if (!latch) {
        VirtuosoScheduler::ScheduledEvent evOff;
        evOff.dueMs = offMs;
        evOff.kind = VirtuosoScheduler::Kind::NoteOff;
        evOff.channel = channel;
        evOff.note = keyswitchMidi;
        evOff.noteId = id;
        m_sched.schedule(evOff);
    }

    // Glass-box: represent this as a keyswitch action at an absolute time.
    virtuoso::theory::TheoryEvent te;
    te.event_kind = "keyswitch";
    te.agent = agent;
    te.timestamp = "";
    te.logic_tag = logicTag;
    te.dynamic_marking = "1";
    te.grid_pos = ""; // unknown here (not grid-scheduled)
    te.channel = channel;
    te.note = keyswitchMidi;
    te.on_ms = onMs;
    te.off_ms = latch ? onMs : offMs;
    te.tempo_bpm = m_bpm;
    te.ts_num = m_ts.num;
    te.ts_den = m_ts.den;
    te.engine_now_ms = m_clock.elapsedMs();
    emit plannedTheoryEventJson(te.toJsonString(true));

    VirtuosoScheduler::ScheduledEvent tj;
    tj.dueMs = onMs;
    tj.kind = VirtuosoScheduler::Kind::TheoryEventJson;
    tj.theoryJson = te.toJsonString(true);
    m_sched.schedule(tj);
}

groove::HumanizedEvent VirtuosoEngine::humanizeIntent(const AgentIntentNote& note) {
    groove::HumanizedEvent empty;
    if (!m_clock.isRunning()) return empty;
    if (note.channel < 1 || note.channel > 16) return empty;
    if (note.note < 0 || note.note > 127) return empty;
    if (note.baseVelocity < 1 || note.baseVelocity > 127) return empty;

    auto& h = humanizerFor(note.agent);
    auto he = h.humanizeNote(note.startPos, m_ts, m_bpm, note.baseVelocity, note.durationWhole, note.structural);
    const qint64 baseMs = ensureGridBaseMs();
    he.onMs += baseMs;
    he.offMs += baseMs;
    return he;
}

void VirtuosoEngine::scheduleHumanizedIntentNote(const AgentIntentNote& note,
                                                const groove::HumanizedEvent& he,
                                                const QString& logicTagOverride) {
    if (!m_clock.isRunning()) return;
    if (note.channel < 1 || note.channel > 16) return;
    if (note.note < 0 || note.note > 127) return;
    if (he.velocity < 1 || he.velocity > 127) return;
    if (he.offMs <= he.onMs) return;

    const quint32 id = nextNoteId();
    VirtuosoScheduler::ScheduledEvent on;
    on.dueMs = he.onMs;
    on.kind = VirtuosoScheduler::Kind::NoteOn;
    on.channel = note.channel;
    on.note = note.note;
    on.velocity = he.velocity;
    on.noteId = id;
    m_sched.schedule(on);

    VirtuosoScheduler::ScheduledEvent off;
    off.dueMs = he.offMs;
    off.kind = VirtuosoScheduler::Kind::NoteOff;
    off.channel = note.channel;
    off.note = note.note;
    off.noteId = id;
    m_sched.schedule(off);

    // Explainability: preserve full glass-box fields, but use the provided humanized timing.
    virtuoso::theory::TheoryEvent te;
    te.agent = note.agent;
    te.timestamp = he.grid_pos;
    te.chord_context = note.chord_context;
    te.scale_used = note.scale_used;
    te.key_center = note.key_center;
    te.roman = note.roman;
    te.chord_function = note.chord_function;
    te.voicing_type = note.voicing_type;
    te.logic_tag = logicTagOverride.isEmpty() ? note.logic_tag : logicTagOverride;
    te.target_note = note.target_note;
    te.dynamic_marking = QString::number(he.velocity);
    te.groove_template = he.groove_template;
    te.grid_pos = he.grid_pos;
    te.timing_offset_ms = he.timing_offset_ms;
    te.velocity_adjustment = he.velocity_adjustment;
    te.humanize_seed = he.humanize_seed;
    te.channel = note.channel;
    te.note = note.note;
    te.on_ms = he.onMs;
    te.off_ms = he.offMs;
    te.tempo_bpm = m_bpm;
    te.ts_num = m_ts.num;
    te.ts_den = m_ts.den;
    te.engine_now_ms = m_clock.elapsedMs();
    te.vibe_state = note.vibe_state;
    te.user_intents = note.user_intents;
    te.user_outside_ratio = note.user_outside_ratio;
    te.has_virtuosity = note.has_virtuosity;
    te.virtuosity = note.virtuosity;

    emit plannedTheoryEventJson(te.toJsonString(true));

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

    const quint32 id = nextNoteId();
    VirtuosoScheduler::ScheduledEvent on;
    on.dueMs = he.onMs;
    on.kind = VirtuosoScheduler::Kind::NoteOn;
    on.channel = channel;
    on.note = note;
    on.velocity = he.velocity;
    on.noteId = id;
    m_sched.schedule(on);

    VirtuosoScheduler::ScheduledEvent off;
    off.dueMs = he.offMs;
    off.kind = VirtuosoScheduler::Kind::NoteOff;
    off.channel = channel;
    off.note = note;
    off.noteId = id;
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
    te.channel = channel;
    te.note = note;
    te.on_ms = he.onMs;
    te.off_ms = he.offMs;
    te.tempo_bpm = m_bpm;
    te.ts_num = m_ts.num;
    te.ts_den = m_ts.den;
    te.engine_now_ms = m_clock.elapsedMs();

    emit plannedTheoryEventJson(te.toJsonString(true));

    VirtuosoScheduler::ScheduledEvent tj;
    tj.dueMs = he.onMs;
    tj.kind = VirtuosoScheduler::Kind::TheoryEventJson;
    tj.theoryJson = te.toJsonString(true);
    m_sched.schedule(tj);
}

void VirtuosoEngine::scheduleTheoryJsonAtGridPos(const QString& json, const groove::GridPos& startPos, int leadMs) {
    if (!m_clock.isRunning()) return;
    const qint64 baseOn = virtuoso::groove::GrooveGrid::posToMs(startPos, m_ts, m_bpm) + ensureGridBaseMs();
    const qint64 on = qMax<qint64>(0, baseOn - qMax(0, leadMs));
    VirtuosoScheduler::ScheduledEvent tj;
    tj.dueMs = on;
    tj.kind = VirtuosoScheduler::Kind::TheoryEventJson;
    tj.theoryJson = json;
    m_sched.schedule(tj);
}

} // namespace virtuoso::engine

