#include "playback/VirtuosoBalladMvpPlaybackEngine.h"

#include "midiprocessor.h"
#include "playback/AgentCoordinator.h"
#include "playback/HarmonyContext.h"
#include "playback/LookaheadPlanner.h"
#include "playback/SemanticMidiAnalyzer.h"
#include "playback/TransportTimeline.h"
#include "playback/AutoWeightController.h"
#include "playback/WeightNegotiator.h"

#include <QHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPointer>
#include <QThreadPool>
#include <QRunnable>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace playback {
namespace {

static QString sectionLabelForPlaybackBar(const chart::ChartModel& m, int playbackBarIndex) {
    if (playbackBarIndex < 0) playbackBarIndex = 0;
    int bar = 0;
    QString last;
    for (const auto& line : m.lines) {
        if (!line.sectionLabel.trimmed().isEmpty()) last = line.sectionLabel.trimmed();
        const int n = line.bars.size();
        if (playbackBarIndex < bar + n) return last;
        bar += n;
    }
    return last;
}

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

void VirtuosoBalladMvpPlaybackEngine::updateRealtimeEnergyGains(double energy01) {
    const double e = qBound(0.0, energy01, 1.0);

    // Make Energy feel continuous without changing planning:
    // - CC11 affects currently-sounding notes (\"inside the beat\")
    //
    // Important: do NOT let this become a master-volume knob that can mute instruments.
    // Energy primarily shapes *what* gets played; real-time CC11 should be subtle and have a floor.
    m_engine.setRealtimeVelocityScale(1.0); // avoid compounding with planner/intensity velocity logic

    auto cc11FromEnergy = [&](double e01, int floor) -> int {
        const double shaped = std::pow(qBound(0.0, e01, 1.0), 0.55);
        return qBound(floor, int(std::llround(double(floor) + (127.0 - double(floor)) * shaped)), 127);
    };

    // Floors chosen so \"simmer\" stays audible and energy=0 never silences.
    const int cc11P = cc11FromEnergy(e, /*floor=*/78);
    const int cc11B = cc11FromEnergy(e, /*floor=*/88);
    const int cc11D = 127; // avoid changing drum bus loudness (often less musical)

    auto sendIfChanged = [&](int ch, int ccValue, int& last) {
        if (ch < 1 || ch > 16) return;
        if (ccValue == last) return;
        last = ccValue;
        m_engine.sendCcNow(ch, /*cc=*/11, ccValue);
    };

    sendIfChanged(m_chPiano, cc11P, m_lastCc11Piano);
    sendIfChanged(m_chBass, cc11B, m_lastCc11Bass);
    sendIfChanged(m_chDrums, cc11D, m_lastCc11Drums);
}

void VirtuosoBalladMvpPlaybackEngine::setDebugEnergyAuto(bool on) {
    m_debugEnergyAuto = on;
    // If Auto is turned off, immediately apply the currently-set manual energy.
    if (!m_debugEnergyAuto) {
        updateRealtimeEnergyGains(m_debugEnergy);
    }
}

void VirtuosoBalladMvpPlaybackEngine::setDebugEnergy(double energy01) {
    m_debugEnergy = qBound(0.0, energy01, 1.0);
    // Always apply immediately so it feels continuous while dragging the slider.
    if (!m_debugEnergyAuto) {
        updateRealtimeEnergyGains(m_debugEnergy);
    }
}

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
    // Grid base is anchored slightly in the future at the moment we first schedule grid events.
    // IMPORTANT: do not pre-initialize it here, because scheduling may begin a bit later (first tick),
    // and pre-initializing would make beat 1 land "in the past", compressing beat 2 (too early).
    m_engineGridBaseMs = 0;

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
    // Latch engine grid base once it becomes known (after first scheduling).
    if (m_engineGridBaseMs <= 0) {
        const qint64 b = m_engine.gridBaseMs();
        if (b > 0) m_engineGridBaseMs = b;
    }
    const qint64 baseMs = qMax<qint64>(0, m_engineGridBaseMs);
    // Until base is established, keep UI playhead at song time 0 (prevents first-bar drift).
    const qint64 songMs = (baseMs > 0) ? qMax<qint64>(0, elapsedMs - baseMs) : 0;
    const qint64 nowWallMs = (m_playStartWallMs > 0) ? (m_playStartWallMs + elapsedMs) : QDateTime::currentMSecsSinceEpoch();
    const int stepNow = int(double(songMs) / beatMs);

    // Keep real-time gain continuous even inside beats.
    // Manual mode uses the slider directly; Auto mode uses the vibe engine snapshot.
    {
        // Rate-limit to avoid excessive CC spam and listener compute cost.
        constexpr qint64 kMinUpdateMs = 45; // ~22 Hz; feels continuous
        if (m_lastRealtimeGainUpdateElapsedMs < 0 || (elapsedMs - m_lastRealtimeGainUpdateElapsedMs) >= kMinUpdateMs) {
            m_lastRealtimeGainUpdateElapsedMs = elapsedMs;

            double eTarget = m_debugEnergy;
            if (m_debugEnergyAuto) {
                const auto snap = m_interaction.snapshot(nowWallMs, /*debugEnergyAuto=*/true, /*debugEnergy01=*/m_debugEnergy);
                eTarget = snap.energy01;
            }

            // Smooth slightly to avoid zippering when the vibe engine jitters.
            // EMA alpha chosen for ~150ms-ish response.
            const double alpha = 0.28;
            m_realtimeEnergySmoothed = qBound(0.0,
                                             (1.0 - alpha) * m_realtimeEnergySmoothed + alpha * qBound(0.0, eTarget, 1.0),
                                             1.0);

            updateRealtimeEnergyGains(m_realtimeEnergySmoothed);
        }
    }

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

        // Emit weights v2 (for UI slider animation / debugging).
        {
            const int beatsPerBar = qMax(1, (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4);
            const int playbackBarIndex = qMax(0, stepNow / beatsPerBar);
            const QString sec = sectionLabelForPlaybackBar(m_model, playbackBarIndex);
            const auto snap = m_interaction.snapshot(nowWallMs, m_debugEnergyAuto, m_debugEnergy);

            virtuoso::control::PerformanceWeightsV2 w = m_weightsV2Manual;
            w.clamp01();
            if (m_weightsV2Auto) {
                AutoWeightController::Inputs wi;
                wi.sectionLabel = sec;
                wi.repeatIndex = qMax(0, stepNow / qMax(1, seqLen));
                wi.repeatsTotal = qMax(1, m_repeats);
                wi.playbackBarIndex = playbackBarIndex;
                wi.phraseBars = qMax(1, m_story.phraseBars);
                wi.barInPhrase = (wi.phraseBars > 0) ? (playbackBarIndex % wi.phraseBars) : 0;
                wi.phraseEndBar = (wi.barInPhrase == (wi.phraseBars - 1));
                wi.cadence01 = 0.0;
                wi.userSilence = snap.intent.silence;
                wi.userBusy = snap.userBusy;
                wi.userRegisterHigh = snap.intent.registerHigh;
                wi.userIntensityPeak = snap.intent.intensityPeak;
                w = AutoWeightController::compute(wi);
            }

            emit debugWeightsV2(w.density, w.rhythm, w.intensity, w.dynamism, w.emotion,
                                w.creativity, w.tension, w.interactivity, w.variability, w.warmth,
                                m_weightsV2Auto);
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
    const int scheduleUntil = int(double(songMs + kLookaheadMs) / beatMs);
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

    // --- Weights v2 (primary) ---
    ai.weightsV2Auto = m_weightsV2Auto;
    // Derive section label for macro controller (from iReal section markers).
    const int beatsPerBar = qMax(1, (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4);
    const int playbackBarIndex = qMax(0, stepIndex / beatsPerBar);
    const QString sec = sectionLabelForPlaybackBar(m_model, playbackBarIndex);

    // Snapshot interaction at scheduling time (good enough for near-horizon).
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    const auto snap = m_interaction.snapshot(nowWallMs, m_debugEnergyAuto, m_debugEnergy);

    virtuoso::control::PerformanceWeightsV2 gw = m_weightsV2Manual;
    gw.clamp01();
    if (m_weightsV2Auto) {
        AutoWeightController::Inputs wi;
        wi.sectionLabel = sec;
        wi.repeatIndex = qMax(0, stepIndex / qMax(1, seqLen));
        wi.repeatsTotal = qMax(1, m_repeats);
        wi.playbackBarIndex = playbackBarIndex;
        wi.phraseBars = qMax(1, m_story.phraseBars);
        wi.barInPhrase = (wi.phraseBars > 0) ? (playbackBarIndex % wi.phraseBars) : 0;
        wi.phraseEndBar = (wi.barInPhrase == (wi.phraseBars - 1));
        wi.cadence01 = 0.0; // refined cadence comes from HarmonyContext downstream; start conservative
        wi.userSilence = snap.intent.silence;
        wi.userBusy = snap.userBusy;
        wi.userRegisterHigh = snap.intent.registerHigh;
        wi.userIntensityPeak = snap.intent.intensityPeak;
        gw = AutoWeightController::compute(wi);
    }
    ai.weightsV2 = gw;

    // Negotiate per-agent applied weights (for now: deterministic heuristic + smoothing state).
    WeightNegotiator::Inputs ni;
    ni.global = gw;
    ni.userBusy = snap.userBusy;
    ni.userSilence = snap.intent.silence;
    ni.cadence = false;
    ni.phraseEnd = false;
    ni.sectionLabel = sec;
    ai.negotiated = WeightNegotiator::negotiate(ni, m_weightNegState, /*alpha=*/0.22);

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

