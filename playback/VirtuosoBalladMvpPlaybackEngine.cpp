#include "playback/VirtuosoBalladMvpPlaybackEngine.h"

#include "midiprocessor.h"
#include "playback/AgentCoordinator.h"
#include "playback/HarmonyContext.h"
#include "playback/LookaheadPlanner.h"
#include "playback/SemanticMidiAnalyzer.h"
#include "playback/TransportTimeline.h"

#include <QHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPointer>
#include <QThreadPool>
#include <QRunnable>
#include <QtGlobal>
#include <algorithm>

namespace playback {
namespace {

class LookaheadRunnable final : public QRunnable {
public:
    LookaheadRunnable(QPointer<VirtuosoBalladMvpPlaybackEngine> owner,
                      quint64 jobId,
                      int stepNow,
                      int horizonBars,
                      LookaheadPlanner::Inputs inputs,
                      JazzBalladBassPlanner bass,
                      JazzBalladPianoPlanner piano,
                      BrushesBalladDrummer drummer)
        : m_owner(std::move(owner))
        , m_jobId(jobId)
        , m_stepNow(stepNow)
        , m_horizonBars(horizonBars)
        , m_inputs(std::move(inputs))
        , m_bass(std::move(bass))
        , m_piano(std::move(piano))
        , m_drummer(std::move(drummer)) {
        setAutoDelete(true);
    }

    void run() override {
        QElapsedTimer t;
        t.start();
        m_inputs.bassPlanner = &m_bass;
        m_inputs.pianoPlanner = &m_piano;
        m_inputs.drummer = &m_drummer;
        const QString json = LookaheadPlanner::buildLookaheadPlanJson(m_inputs, m_stepNow, m_horizonBars);
        const int ms = int(t.elapsed());

        if (!m_owner) return;
        QMetaObject::invokeMethod(m_owner.data(),
                                  "applyLookaheadResult",
                                  Qt::QueuedConnection,
                                  Q_ARG(quint64, m_jobId),
                                  Q_ARG(int, m_stepNow),
                                  Q_ARG(QString, json),
                                  Q_ARG(int, ms));
    }

private:
    QPointer<VirtuosoBalladMvpPlaybackEngine> m_owner;
    quint64 m_jobId = 0;
    int m_stepNow = 0;
    int m_horizonBars = 4;
    LookaheadPlanner::Inputs m_inputs;
    JazzBalladBassPlanner m_bass;
    JazzBalladPianoPlanner m_piano;
    BrushesBalladDrummer m_drummer;
};

} // namespace

void VirtuosoBalladMvpPlaybackEngine::onGuitarNoteOn(int note, int vel) {
    m_interaction.ingestGuitarNoteOn(note, vel, QDateTime::currentMSecsSinceEpoch());
}

void VirtuosoBalladMvpPlaybackEngine::onGuitarNoteOff(int note) {
    m_interaction.ingestGuitarNoteOff(note, QDateTime::currentMSecsSinceEpoch());
}

void VirtuosoBalladMvpPlaybackEngine::onVoiceCc2Stream(int cc2) {
    m_interaction.ingestCc2(cc2, QDateTime::currentMSecsSinceEpoch());
}

void VirtuosoBalladMvpPlaybackEngine::onVoiceNoteOn(int note, int vel) {
    m_interaction.ingestVoiceNoteOn(note, vel, QDateTime::currentMSecsSinceEpoch());
}

void VirtuosoBalladMvpPlaybackEngine::onVoiceNoteOff(int note) {
    m_interaction.ingestVoiceNoteOff(note, QDateTime::currentMSecsSinceEpoch());
}

VirtuosoBalladMvpPlaybackEngine::VirtuosoBalladMvpPlaybackEngine(QObject* parent)
    : QObject(parent)
    , m_registry(virtuoso::groove::GrooveRegistry::builtins()) {
    m_tickTimer.setInterval(10);
    m_tickTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_tickTimer, &QTimer::timeout, this, &VirtuosoBalladMvpPlaybackEngine::onTick);

    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::theoryEventJson,
            this, &VirtuosoBalladMvpPlaybackEngine::theoryEventJson);
    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::plannedTheoryEventJson,
            this, &VirtuosoBalladMvpPlaybackEngine::plannedTheoryEventJson);

    // Load data-driven vocabulary (rhythmic/phrase patterns) from resources.
    {
        QString err;
        m_vocabLoaded = m_vocab.loadFromResourcePath(":/virtuoso/vocab/cool_jazz_vocabulary.json", &err);
        m_vocabError = err;
        // Bass planner consumes VocabularyRegistry directly.
        m_bassPlanner.setVocabulary(m_vocabLoaded ? &m_vocab : nullptr);
        // Piano planner consumes VocabularyRegistry for comping rhythm grammar.
        m_pianoPlanner.setVocabulary(m_vocabLoaded ? &m_vocab : nullptr);
    }

    // Ontology is the canonical musical truth for voicing choices.
    m_pianoPlanner.setOntology(&m_ontology);
    m_pianoPlanner.setMotivicMemory(&m_motivicMemory);

    // Harmony context uses ontology as its substrate.
    m_harmony.setOntology(&m_ontology);
}

void VirtuosoBalladMvpPlaybackEngine::emitLookaheadPlanOnce() {
    if (m_sequence.isEmpty()) return;

    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;

    // If we have a live playhead, preview from the current bar; otherwise preview from song start.
    const int stepNow = (m_lastPlayheadStep >= 0) ? m_lastPlayheadStep : 0;

    LookaheadPlanner::Inputs li;
    li.bpm = m_bpm;
    li.ts = ts;
    li.repeats = m_repeats;
    li.model = &m_model;
    li.sequence = &m_sequence;
    li.hasLastChord = m_harmony.hasLastChord();
    li.lastChord = m_harmony.lastChord();
    li.harmonyCtx = &m_harmony;
    li.keyWindowBars = 8;
    li.listener = &m_interaction.listener();
    li.vibe = &m_interaction.vibe();
    li.bassPlanner = &m_bassPlanner;
    li.pianoPlanner = &m_pianoPlanner;
    li.drummer = &m_drummer;
    li.chDrums = m_chDrums;
    li.chBass = m_chBass;
    li.chPiano = m_chPiano;
    li.stylePresetKey = m_stylePresetKey;
    li.agentEnergyMult = m_agentEnergyMult;
    li.debugEnergyAuto = m_debugEnergyAuto;
    li.debugEnergy = m_debugEnergy;
    li.virtAuto = m_virtAuto;
    li.virtHarmonicRisk = m_virtHarmonicRisk;
    li.virtRhythmicComplexity = m_virtRhythmicComplexity;
    li.virtInteraction = m_virtInteraction;
    li.virtToneDark = m_virtToneDark;
    li.engineNowMs = m_engine.elapsedMs();
    li.nowMs = QDateTime::currentMSecsSinceEpoch();

    const QString json = LookaheadPlanner::buildLookaheadPlanJson(li, stepNow, /*horizonBars=*/4);
    if (!json.trimmed().isEmpty()) emit lookaheadPlanJson(json);
}

void VirtuosoBalladMvpPlaybackEngine::setMidiProcessor(MidiProcessor* midi) {
    m_midi = midi;
    if (!m_midi) return;

    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::noteOn,
            m_midi, &MidiProcessor::sendVirtualNoteOn, Qt::UniqueConnection);
    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::noteOff,
            m_midi, &MidiProcessor::sendVirtualNoteOff, Qt::UniqueConnection);
    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::allNotesOff,
            m_midi, &MidiProcessor::sendVirtualAllNotesOff, Qt::UniqueConnection);
    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::cc,
            m_midi, &MidiProcessor::sendVirtualCC, Qt::UniqueConnection);

    // Listening MVP: tap *transposed* live performance notes.
    // Use QueuedConnection because MidiProcessor may emit from its worker thread.
    connect(m_midi, &MidiProcessor::guitarNoteOn,
            this, &VirtuosoBalladMvpPlaybackEngine::onGuitarNoteOn,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
    connect(m_midi, &MidiProcessor::guitarNoteOff,
            this, &VirtuosoBalladMvpPlaybackEngine::onGuitarNoteOff,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
    connect(m_midi, &MidiProcessor::voiceCc2Stream,
            this, &VirtuosoBalladMvpPlaybackEngine::onVoiceCc2Stream,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));

    // Vocal melody tracking (NOT used for density): allows later call/response.
    connect(m_midi, &MidiProcessor::voiceNoteOn,
            this, &VirtuosoBalladMvpPlaybackEngine::onVoiceNoteOn,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
    connect(m_midi, &MidiProcessor::voiceNoteOff,
            this, &VirtuosoBalladMvpPlaybackEngine::onVoiceNoteOff,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
}

void VirtuosoBalladMvpPlaybackEngine::setTempoBpm(int bpm) {
    m_bpm = qBound(30, bpm, 300);
    m_engine.setTempoBpm(m_bpm);
}

void VirtuosoBalladMvpPlaybackEngine::setRepeats(int repeats) {
    m_repeats = qMax(1, repeats);
}

void VirtuosoBalladMvpPlaybackEngine::setChartModel(const chart::ChartModel& model) {
    m_model = model;
    m_transport.setModel(&m_model);
    rebuildSequence();

    virtuoso::groove::TimeSignature ts;
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;
    m_engine.setTimeSignature(ts);

    // Harmony analysis (global key + local keys).
    m_harmony.rebuildFromModel(m_model);
}

void VirtuosoBalladMvpPlaybackEngine::setStylePresetKey(const QString& key) {
    const QString k = key.trimmed();
    if (k.isEmpty()) return;
    m_stylePresetKey = k;
    // Apply immediately so lookahead/auditions and the next scheduled events reflect the preset.
    applyPresetToEngine();
}

void VirtuosoBalladMvpPlaybackEngine::play() {
    if (m_playing) return;
    if (m_sequence.isEmpty()) return;

    applyPresetToEngine();
    m_engine.start();

    m_playing = true;
    m_lastPlayheadStep = -1;
    m_lastEmittedCell = -1;
    m_nextScheduledStep = 0;
    m_lastLookaheadStepEmitted = -1;
    m_playStartWallMs = QDateTime::currentMSecsSinceEpoch();
    m_harmony.resetRuntimeState();
    m_bassPlanner.reset();
    m_pianoPlanner.reset();
    m_interaction.reset();
    m_motivicMemory.clear();
    m_story.reset();
    // Keep drummer profile wired to channel/mapping choices.
    {
        auto p = m_drummer.profile();
        p.channel = m_chDrums;
        p.noteKick = m_noteKick;
        p.noteSnareSwish = m_noteSnareHit;
        p.noteBrushLoopA = m_noteBrushLoop;
        m_drummer.setProfile(p);
    }

    m_tickTimer.start();
}

void VirtuosoBalladMvpPlaybackEngine::stop() {
    if (!m_playing) return;
    m_playing = false;

    m_tickTimer.stop();
    m_engine.stop();

    // Hard silence (safety against stuck notes)
    if (m_midi) {
        m_midi->sendVirtualAllNotesOff(m_chDrums);
        m_midi->sendVirtualAllNotesOff(m_chBass);
        m_midi->sendVirtualAllNotesOff(m_chPiano);
        m_midi->sendVirtualCC(m_chPiano, 64, 0);
    }
}

void VirtuosoBalladMvpPlaybackEngine::rebuildSequence() {
    m_transport.rebuild();
    m_sequence = m_transport.sequence();
}

void VirtuosoBalladMvpPlaybackEngine::applyPresetToEngine() {
    const auto* preset = m_registry.stylePreset(m_stylePresetKey);
    if (!preset) return;

    // Tempo/TS remain owned by UI; preset provides defaults elsewhere. Here we only apply groove params.
    const auto* gt = m_registry.grooveTemplate(preset->grooveTemplateKey);
    if (gt) {
        virtuoso::groove::GrooveTemplate scaled = *gt;
        scaled.amount = qBound(0.0, preset->templateAmount, 1.0);
        m_engine.setGrooveTemplate(scaled);
    }

    if (preset->instrumentProfiles.contains("Drums")) m_engine.setInstrumentGrooveProfile("Drums", preset->instrumentProfiles.value("Drums"));
    if (preset->instrumentProfiles.contains("Bass"))  m_engine.setInstrumentGrooveProfile("Bass",  preset->instrumentProfiles.value("Bass"));
    if (preset->instrumentProfiles.contains("Piano")) m_engine.setInstrumentGrooveProfile("Piano", preset->instrumentProfiles.value("Piano"));

    // Stage 3 Virtuosity Matrix defaults are preset-driven (not just groove).
    // In Auto mode, these are treated as baseline weights; in Manual mode, they are the defaults.
    m_virtHarmonicRisk = qBound(0.0, preset->virtuosityDefaults.harmonicRisk, 1.0);
    m_virtRhythmicComplexity = qBound(0.0, preset->virtuosityDefaults.rhythmicComplexity, 1.0);
    m_virtInteraction = qBound(0.0, preset->virtuosityDefaults.interaction, 1.0);
    m_virtToneDark = qBound(0.0, preset->virtuosityDefaults.toneDark, 1.0);
}

const chart::Cell* VirtuosoBalladMvpPlaybackEngine::cellForFlattenedIndex(int cellIndex) const {
    return m_transport.cellForFlattenedIndex(cellIndex);
}

bool VirtuosoBalladMvpPlaybackEngine::chordForCellIndex(int cellIndex, music::ChordSymbol& outChord, bool& isNewChord) {
    return m_harmony.chordForCellIndex(m_model, cellIndex, outChord, isNewChord);
}

void VirtuosoBalladMvpPlaybackEngine::onTick() {
    const int seqLen = m_sequence.size();
    if (!m_playing || seqLen <= 0) return;

    // Beat duration (quarter-note BPM); apply time signature denominator.
    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;

    const double quarterMs = 60000.0 / double(qMax(1, m_bpm));
    const double beatMs = quarterMs * (4.0 / double(qMax(1, ts.den)));

    const qint64 elapsedMs = m_engine.elapsedMs();
    const qint64 nowWallMs = (m_playStartWallMs > 0) ? (m_playStartWallMs + elapsedMs) : QDateTime::currentMSecsSinceEpoch();
    const int stepNow = int(double(elapsedMs) / beatMs);

    const int total = seqLen * qMax(1, m_repeats);
    if (stepNow >= total) {
        stop();
        return;
    }

    // Update playhead highlight once per beat-step.
    if (stepNow != m_lastPlayheadStep) {
        m_lastPlayheadStep = stepNow;
        const int cellIndex = m_sequence[stepNow % seqLen];
        if (cellIndex != m_lastEmittedCell) {
            m_lastEmittedCell = cellIndex;
            emit currentCellChanged(cellIndex);
        }
    }

    // --- Lookahead plan (4 bars) for UI ---
    // Critical: only update on step changes (not every 10ms tick) and only if there are listeners.
    if (stepNow != m_lastLookaheadStepEmitted && receivers(SIGNAL(lookaheadPlanJson(QString))) > 0) {
        m_lastLookaheadStepEmitted = stepNow;
        scheduleLookaheadAsync(stepNow, ts, nowWallMs, elapsedMs);
    }

    // Lookahead scheduling window (tight timing).
    // We need to schedule far enough ahead for sample-library articulations that must be pressed
    // before the "previous note" (e.g. Ample Upright Legato Slide).
    constexpr int kLookaheadMs = 2600;
    const int scheduleUntil = int(double(elapsedMs + kLookaheadMs) / beatMs);
    const int maxStepToSchedule = std::min(total - 1, scheduleUntil);

    while (m_nextScheduledStep <= maxStepToSchedule) {
        scheduleStep(m_nextScheduledStep, seqLen);
        m_nextScheduledStep++;
    }
}

void VirtuosoBalladMvpPlaybackEngine::scheduleLookaheadAsync(int stepNow,
                                                            const virtuoso::groove::TimeSignature& ts,
                                                            qint64 nowWallMs,
                                                            qint64 engineNowMs) {
    LookaheadPlanner::Inputs li;
    li.bpm = m_bpm;
    li.ts = ts;
    li.repeats = m_repeats;
    li.model = &m_model;
    li.sequence = &m_sequence;
    li.hasLastChord = m_harmony.hasLastChord();
    li.lastChord = m_harmony.lastChord();
    li.harmonyCtx = &m_harmony;
    li.keyWindowBars = 8;

    // Snapshot interaction on the UI thread (avoid worker touching shared state).
    li.hasIntentSnapshot = true;
    li.intentSnapshot = m_interaction.listener().compute(nowWallMs);
    {
        VibeStateMachine vibeSim = m_interaction.vibe();
        li.hasVibeSnapshot = true;
        li.vibeSnapshot = vibeSim.update(li.intentSnapshot, nowWallMs);
    }
    li.listener = nullptr;
    li.vibe = nullptr;

    // Channels + style context
    li.chDrums = m_chDrums;
    li.chBass = m_chBass;
    li.chPiano = m_chPiano;
    li.stylePresetKey = m_stylePresetKey;
    li.agentEnergyMult = m_agentEnergyMult;
    li.debugEnergyAuto = m_debugEnergyAuto;
    li.debugEnergy = m_debugEnergy;
    li.virtAuto = m_virtAuto;
    li.virtHarmonicRisk = m_virtHarmonicRisk;
    li.virtRhythmicComplexity = m_virtRhythmicComplexity;
    li.virtInteraction = m_virtInteraction;
    li.virtToneDark = m_virtToneDark;
    li.engineNowMs = engineNowMs;
    li.nowMs = nowWallMs;

    // Coalesce: only latest job result is applied.
    const quint64 jobId = ++m_lookaheadJobId;

    // Copy planners into the runnable so background work never reads mutable live planner state.
    auto* r = new LookaheadRunnable(QPointer<VirtuosoBalladMvpPlaybackEngine>(this),
                                    jobId,
                                    stepNow,
                                    /*horizonBars=*/4,
                                    li,
                                    m_bassPlanner,
                                    m_pianoPlanner,
                                    m_drummer);
    QThreadPool::globalInstance()->start(r);
}

void VirtuosoBalladMvpPlaybackEngine::applyLookaheadResult(quint64 jobId, int stepNow, const QString& json, int buildMs) {
    // Drop stale results.
    if (jobId != m_lookaheadJobId.load()) return;
    if (!m_playing) return;
    if (stepNow != m_lastLookaheadStepEmitted) return;
    m_lastLookaheadBuildMs = buildMs;
    // Lightweight instrumentation: warn if lookahead generation is unexpectedly expensive.
    if (buildMs >= 25) {
        qWarning().noquote() << "Virtuoso lookahead build slow:" << buildMs << "ms (step" << stepNow << ")";
    }
    if (!json.trimmed().isEmpty()) emit lookaheadPlanJson(json);
}

void VirtuosoBalladMvpPlaybackEngine::scheduleStep(int stepIndex, int seqLen) {
    (void)seqLen;
    AgentCoordinator::Inputs ai;
    ai.owner = this;
    ai.model = &m_model;
    ai.sequence = &m_sequence;
    ai.repeats = m_repeats;
    ai.bpm = m_bpm;
    ai.stylePresetKey = m_stylePresetKey;
    ai.agentEnergyMult = m_agentEnergyMult;

    ai.virtAuto = m_virtAuto;
    ai.virtHarmonicRisk = m_virtHarmonicRisk;
    ai.virtRhythmicComplexity = m_virtRhythmicComplexity;
    ai.virtInteraction = m_virtInteraction;
    ai.virtToneDark = m_virtToneDark;

    ai.debugEnergyAuto = m_debugEnergyAuto;
    ai.debugEnergy = m_debugEnergy;

    ai.chDrums = m_chDrums;
    ai.chBass = m_chBass;
    ai.chPiano = m_chPiano;
    ai.noteKick = m_noteKick;
    ai.kickLocksBass = m_kickLocksBass;
    ai.kickLockMaxMs = m_kickLockMaxMs;

    ai.harmony = &m_harmony;
    ai.interaction = &m_interaction;
    ai.engine = &m_engine;
    ai.ontology = &m_ontology;
    ai.bassPlanner = &m_bassPlanner;
    ai.pianoPlanner = &m_pianoPlanner;
    ai.drummer = &m_drummer;
    ai.motivicMemory = &m_motivicMemory;
    ai.story = &m_story;

    AgentCoordinator::scheduleStep(ai, stepIndex);
}

int VirtuosoBalladMvpPlaybackEngine::thirdIntervalForQuality(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::Minor:
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished: return 3;
        case music::ChordQuality::Sus2: return 2;
        case music::ChordQuality::Sus4: return 5;
        case music::ChordQuality::Power5: return 0;
        default: return 4;
    }
}

int VirtuosoBalladMvpPlaybackEngine::seventhIntervalFor(const music::ChordSymbol& c) {
    if (c.seventh == music::SeventhQuality::Major7) return 11;
    if (c.seventh == music::SeventhQuality::Dim7) return 9;
    if (c.seventh == music::SeventhQuality::Minor7) return 10;
    return -1;
}

int VirtuosoBalladMvpPlaybackEngine::chooseBassMidi(int pc) {
    // Keep in a warm ballad range, roughly E1..E2.
    if (pc < 0) pc = 0;
    int midi = 36 + (pc % 12); // C2 base
    while (midi < 36) midi += 12;
    while (midi > 52) midi -= 12;
    return midi;
}

int VirtuosoBalladMvpPlaybackEngine::choosePianoMidi(int pc, int targetLow, int targetHigh) {
    if (pc < 0) pc = 0;
    int midi = targetLow + (pc - (targetLow % 12));
    while (midi < targetLow) midi += 12;
    while (midi > targetHigh) midi -= 12;
    return midi;
}

// NOTE: legacy MVP bass/piano scheduling functions removed in favor of planners.

} // namespace playback

