#include "playback/VirtuosoBalladMvpPlaybackEngine.h"

#include "midiprocessor.h"
#include "playback/AgentCoordinator.h"
#include "playback/HarmonyContext.h"
#include "playback/LookaheadPlanner.h"
#include "playback/SemanticMidiAnalyzer.h"
#include "playback/TransportTimeline.h"
#include "playback/AutoWeightController.h"
#include "playback/WeightNegotiator.h"
#include "playback/ChordScaleTable.h"

#include <QHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThreadPool>
#include <QRunnable>
#include <QCoreApplication>
#include <QtGlobal>
#include <algorithm>
#include <atomic>
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
    m_harmony.setOwner(this);
    
    // Initialize global chord→scale lookup table (once at startup)
    // This provides O(1) scale selection during pre-planning.
    ChordScaleTable::initialize(m_ontology);
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
    // Weights v2 (single source of truth).
    if (m_weightsV2Auto) {
        AutoWeightController::Inputs wi;
        const int beatsPerBar = qMax(1, ts.num);
        const int playbackBarIndex = (beatsPerBar > 0) ? (stepNow / beatsPerBar) : 0;
        wi.sectionLabel = sectionLabelForPlaybackBar(m_model, playbackBarIndex);
        wi.repeatIndex = 0;
        wi.repeatsTotal = qMax(1, m_repeats);
        wi.playbackBarIndex = playbackBarIndex;
        wi.phraseBars = (m_bpm <= 84) ? 8 : 4;
        wi.barInPhrase = (wi.phraseBars > 0) ? (playbackBarIndex % wi.phraseBars) : 0;
        wi.phraseEndBar = (wi.phraseBars > 0) ? (wi.barInPhrase == (wi.phraseBars - 1)) : false;
        wi.cadence01 = 0.0; // lookahead-only; cadence is computed in runtime scheduler
        const auto snap = m_interaction.snapshot(QDateTime::currentMSecsSinceEpoch(), m_debugEnergyAuto, m_debugEnergy);
        wi.userSilence = snap.intent.silence;
        wi.userBusy = (snap.intent.densityHigh || snap.intent.intensityPeak || snap.intent.registerHigh);
        wi.userRegisterHigh = snap.intent.registerHigh;
        wi.userIntensityPeak = snap.intent.intensityPeak;
        li.weightsV2 = AutoWeightController::compute(wi);
    } else {
        li.weightsV2 = m_weightsV2Manual;
    }
    li.hasNegotiatorState = true;
    li.negotiatorState = m_weightNegState;
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
    
    // Reset energy band for fresh playback (start at Simmer)
    m_currentEnergyBand = EnergyBand::Simmer;
    
    // PERF: Build the complete pre-playback cache BEFORE starting playback.
    // This ensures ZERO computation during actual playback - only O(1) lookups.
    // Per product spec: "lag can never happen while the actual music has started playing"
    if (m_usePreCache) {
        buildPrePlaybackCache();
        qInfo().noquote() << QString("PrePlaybackCache ready: %1 steps, %2 energy branches, built in %3ms")
            .arg(m_preCache.totalSteps)
            .arg(m_preCache.energyBranches.size())
            .arg(m_preCache.buildTimeMs);
    }
    
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
    
    // DEBUG: Dump full chart structure at start of playback (emit to UI)
    // This is CRITICAL for diagnosing chord timing issues
    {
        QString dump = "\n╔══════════════════════════════════════════════════════════════════════╗\n";
        dump += "║                     CHART STRUCTURE DUMP                              ║\n";
        dump += "╠══════════════════════════════════════════════════════════════════════╣\n";
        dump += QString("║ Time Signature: %1/%2                                                   ║\n")
            .arg(m_model.timeSigNum).arg(m_model.timeSigDen);
        dump += "╠══════════════════════════════════════════════════════════════════════╣\n";
        
        int globalBarIdx = 0;
        int globalCellIdx = 0;
        for (int lineIdx = 0; lineIdx < m_model.lines.size(); ++lineIdx) {
            const auto& line = m_model.lines[lineIdx];
            if (!line.sectionLabel.isEmpty()) {
                dump += QString("║ --- Section: %1 ---\n").arg(line.sectionLabel);
            }
            for (int barIdx = 0; barIdx < line.bars.size(); ++barIdx) {
                const auto& bar = line.bars[barIdx];
                QStringList cells;
                for (int cellIdx = 0; cellIdx < bar.cells.size(); ++cellIdx) {
                    QString c = bar.cells[cellIdx].chord.trimmed();
                    if (c.isEmpty()) c = "·"; // Empty cell indicator
                    // Show global cell index in brackets for cross-reference with HARMONY traces
                    cells.push_back(QString("[%1]%2").arg(globalCellIdx).arg(c));
                    globalCellIdx++;
                }
                dump += QString("║ Bar %1: %2\n")
                    .arg(globalBarIdx, 2).arg(cells.join("  "));
                globalBarIdx++;
            }
        }
        
        dump += "╠══════════════════════════════════════════════════════════════════════╣\n";
        dump += QString("║ Total bars: %1  Total cells: %2  Sequence length: %3\n")
            .arg(globalBarIdx).arg(globalCellIdx).arg(m_sequence.size());
        
        // Show first 32 sequence entries (8 bars worth) to verify step-to-cell mapping
        dump += "║ Sequence (step→cell) first 32 entries:\n║   ";
        QStringList seqParts;
        for (int i = 0; i < qMin(32, m_sequence.size()); ++i) {
            seqParts.push_back(QString("%1→%2").arg(i).arg(m_sequence[i]));
            if ((i + 1) % 8 == 0 && i < 31) {
                seqParts.push_back("\n║   ");
            }
        }
        dump += seqParts.join(" ");
        dump += "\n╚══════════════════════════════════════════════════════════════════════╝\n";
        emit pianoDebugLog(dump);
    }
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

    // Legacy VirtuosityMatrix defaults removed; Weights v2 are the only global control surface.
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
        
        // Emit theory event for LibraryWindow at CURRENT playback position
        // (not the scheduled lookahead position)
        emitTheoryEventForStep(stepNow);

        // Emit energy (for UI slider animation)
        {
            double currentEnergy = m_debugEnergy;
            if (m_debugEnergyAuto) {
                const auto energySnap = m_interaction.snapshot(nowWallMs, true, m_debugEnergy);
                currentEnergy = energySnap.energy01;
            }
            emit debugEnergy(currentEnergy, m_debugEnergyAuto);
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
    // Critical: only update on step changes and only if there are listeners.
    // PERF: Limit to once per bar (not every beat) to reduce expensive planner copying.
    // This provides smoother playback at the cost of slightly less responsive UI updates.
    const int beatsPerBarLookahead = qMax(1, ts.num);
    const bool isBarStart = (stepNow % beatsPerBarLookahead) == 0;
    if (stepNow != m_lastLookaheadStepEmitted && isBarStart && receivers(SIGNAL(lookaheadPlanJson(QString))) > 0) {
        m_lastLookaheadStepEmitted = stepNow;
        scheduleLookaheadAsync(stepNow, ts, nowWallMs, elapsedMs);
    }

    // Lookahead scheduling window (tight timing).
    // We need to schedule far enough ahead for sample-library articulations that must be pressed
    // before the "previous note" (e.g. Ample Upright Legato Slide).
    //
    // IMPORTANT: With pre-computed cache, we can use a MUCH shorter lookahead!
    // - Old: 4000ms (needed buffer for expensive real-time computation)
    // - New: ~500ms when using cache (just enough for sample lib articulations)
    // 
    // This dramatically improves band responsiveness to energy changes!
    // Energy is read at scheduling time, so shorter lookahead = more responsive.
    const int kLookaheadMs = (m_usePreCache && m_preCache.isValid()) ? 500 : 4000;
    const int scheduleUntil = int(double(songMs + kLookaheadMs) / beatMs);
    const int maxStepToSchedule = std::min(total - 1, scheduleUntil);

    // PERF: When using pre-computed cache, we can schedule many steps per tick
    // because it's just O(1) lookups - no computation at all.
    // When not using cache, limit to 2 steps to prevent catch-up stalls.
    const int kMaxStepsPerTick = (m_usePreCache && m_preCache.isValid()) ? 16 : 2;
    int stepsScheduledThisTick = 0;

    while (m_nextScheduledStep <= maxStepToSchedule && stepsScheduledThisTick < kMaxStepsPerTick) {
        // PERF: Use pre-computed cache if available (O(1) lookup, zero computation)
        if (m_usePreCache && m_preCache.isValid()) {
            scheduleStepFromCache(m_nextScheduledStep);
        } else {
        scheduleStep(m_nextScheduledStep, seqLen);
        }
        m_nextScheduledStep++;
        stepsScheduledThisTick++;
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
    if (m_weightsV2Auto) {
        AutoWeightController::Inputs wi;
        const int beatsPerBar = qMax(1, ts.num);
        const int playbackBarIndex = (beatsPerBar > 0) ? (stepNow / beatsPerBar) : 0;
        wi.sectionLabel = sectionLabelForPlaybackBar(m_model, playbackBarIndex);
        wi.repeatIndex = 0;
        wi.repeatsTotal = qMax(1, m_repeats);
        wi.playbackBarIndex = playbackBarIndex;
        wi.phraseBars = (m_bpm <= 84) ? 8 : 4;
        wi.barInPhrase = (wi.phraseBars > 0) ? (playbackBarIndex % wi.phraseBars) : 0;
        wi.phraseEndBar = (wi.phraseBars > 0) ? (wi.barInPhrase == (wi.phraseBars - 1)) : false;
        wi.cadence01 = 0.0;
        wi.userSilence = li.intentSnapshot.silence;
        wi.userBusy = (li.intentSnapshot.densityHigh || li.intentSnapshot.intensityPeak || li.intentSnapshot.registerHigh);
        wi.userRegisterHigh = li.intentSnapshot.registerHigh;
        wi.userIntensityPeak = li.intentSnapshot.intensityPeak;
        li.weightsV2 = AutoWeightController::compute(wi);
    } else {
        li.weightsV2 = m_weightsV2Manual;
    }
    li.hasNegotiatorState = true;
    li.negotiatorState = m_weightNegState;
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
    // Negotiation smoothing (same for Auto + Manual):
    // Keep continuity when weights are stable, but respond quickly when weights jump (e.g. user drags sliders).
    auto maxAbsDiff = [&](const virtuoso::control::PerformanceWeightsV2& a,
                          const virtuoso::control::PerformanceWeightsV2& b) -> double {
        auto d = [&](double x, double y) { return qAbs(x - y); };
        double m = 0.0;
        m = qMax(m, d(a.density, b.density));
        m = qMax(m, d(a.rhythm, b.rhythm));
        m = qMax(m, d(a.intensity, b.intensity));
        m = qMax(m, d(a.dynamism, b.dynamism));
        m = qMax(m, d(a.emotion, b.emotion));
        m = qMax(m, d(a.creativity, b.creativity));
        m = qMax(m, d(a.tension, b.tension));
        m = qMax(m, d(a.interactivity, b.interactivity));
        m = qMax(m, d(a.variability, b.variability));
        m = qMax(m, d(a.warmth, b.warmth));
        return m;
    };
    const double baseAlpha = 0.22;
    double alpha = baseAlpha;
    if (m_hasPrevWeightsV2ForNegotiation) {
        const double diff = qBound(0.0, maxAbsDiff(gw, m_prevWeightsV2ForNegotiation), 1.0);
        // diff >= 0.25 -> essentially immediate; smaller diffs -> partially smoothed.
        const double t = qBound(0.0, diff / 0.25, 1.0);
        alpha = baseAlpha + (1.0 - baseAlpha) * t;
    }
    m_prevWeightsV2ForNegotiation = gw;
    m_hasPrevWeightsV2ForNegotiation = true;
    ai.negotiated = WeightNegotiator::negotiate(ni, m_weightNegState, /*alpha=*/alpha);

    ai.debugEnergyAuto = m_debugEnergyAuto;
    ai.debugEnergy = m_debugEnergy;
    ai.debugMutePianoLH = m_debugMutePianoLH;
    ai.debugMutePianoRH = m_debugMutePianoRH;
    ai.debugVerbose = m_debugVerbose;

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

void VirtuosoBalladMvpPlaybackEngine::buildPrePlaybackCache() {
    QElapsedTimer timer;
    timer.start();
    
    // Clear any existing cache
    m_preCache.clear();
    
    // Create and show the progress dialog
    if (!m_prePlanningDialog) {
        // Find parent widget for dialog (walk up to find a QWidget)
        QWidget* parentWidget = nullptr;
        QObject* p = parent();
        while (p) {
            if (auto* w = qobject_cast<QWidget*>(p)) {
                parentWidget = w;
                break;
            }
            p = p->parent();
        }
        m_prePlanningDialog = new PrePlanningDialog(parentWidget);
    }
    
    // Start the dialog with a generic title
    m_prePlanningDialog->start("Preparing Performance");
    
    // Build input structure for the cache builder
    PrePlaybackBuilder::Inputs in;
    in.model = &m_model;
    in.sequence = &m_sequence;
    in.repeats = m_repeats;
    in.bpm = m_bpm;
    in.stylePresetKey = m_stylePresetKey;
    in.bassPlanner = &m_bassPlanner;
    in.pianoPlanner = &m_pianoPlanner;
    in.drummer = &m_drummer;
    in.harmony = &m_harmony;
    in.engine = &m_engine;
    in.ontology = &m_ontology;
    in.interaction = &m_interaction;
    in.story = &m_story;
    in.chBass = m_chBass;
    in.chPiano = m_chPiano;
    in.chDrums = m_chDrums;
    in.agentEnergyMult = m_agentEnergyMult;
    
    // Track maximum progress seen from parallel branches (thread-safe via atomic)
    std::atomic<int> maxBranchProgressPct{0};
    
    // Progress callback that updates the dialog
    // currentBranch == -1 means Phase 1 (context building, main thread)
    // currentBranch >= 0 means Phase 2 (branch building, worker threads)
    auto progressCallback = [this, &maxBranchProgressPct](int currentStep, int totalSteps, int currentBranch, int totalBranches) {
        const double stepProgress = double(currentStep) / double(qMax(1, totalSteps));
        const int stepProgressPct = int(stepProgress * 100);
        
        if (currentBranch < 0) {
            // Phase 1: Context building (single-threaded, main thread)
            const int barIndex = currentStep / 4;
            const QString status = QString("Analyzing bar %1...").arg(barIndex + 1);
            
            if (m_prePlanningDialog) {
                m_prePlanningDialog->updateProgress(0, stepProgress, status);
            }
            emit prePlanningProgress(0, stepProgress, status);
            QCoreApplication::processEvents();
        } else {
            // Phase 2: Branch building (parallel worker threads)
            // Track max progress atomically - only update UI if this is a new max
            int oldMax = maxBranchProgressPct.load();
            while (stepProgressPct > oldMax && 
                   !maxBranchProgressPct.compare_exchange_weak(oldMax, stepProgressPct)) {
                // Loop until we either set the new max or someone else set a higher value
            }
            
            // Only update UI if we set a new maximum (reduces UI thrashing)
            if (stepProgressPct >= oldMax) {
                static const QStringList branchNames = {"Simmer", "Build", "Climax", "Cool Down"};
                const QString branchName = (currentBranch < branchNames.size()) 
                    ? branchNames[currentBranch] : QString::number(currentBranch + 1);
                
                const int barIndex = currentStep / 4;
                const QString status = QString("Generating %1 (bar %2)...").arg(branchName).arg(barIndex + 1);
                
                // Use QMetaObject::invokeMethod to safely update UI from worker thread
                QMetaObject::invokeMethod(this, [this, stepProgress, status]() {
                    if (m_prePlanningDialog) {
                        m_prePlanningDialog->updateProgress(1, stepProgress, status);
                    }
                    emit prePlanningProgress(1, stepProgress, status);
                    QCoreApplication::processEvents();
                }, Qt::QueuedConnection);
            }
        }
    };
    
    // Build the cache (optimized: ~400-800ms for a full song)
    m_preCache = PrePlaybackBuilder::build(in, progressCallback);
    
    // Process any pending queued UI updates from worker threads
    QCoreApplication::processEvents();
    
    // Mark completion and auto-close dialog
    if (m_prePlanningDialog) {
        m_prePlanningDialog->complete();
    }
    emit prePlanningProgress(2, 1.0, "Ready!");
    
    // Reset planners after cache building so real-time fallback starts fresh
    m_bassPlanner.reset();
    m_pianoPlanner.reset();
    m_harmony.resetRuntimeState();
    
    qInfo().noquote() << QString("buildPrePlaybackCache: Completed in %1ms").arg(timer.elapsed());
}

void VirtuosoBalladMvpPlaybackEngine::scheduleStepFromCache(int stepIndex) {
    // PERF: This function MUST be O(1) - no computation, just lookups and MIDI scheduling.
    // Any expensive computation here defeats the purpose of pre-caching.
    
    if (!m_preCache.isValid()) {
        qWarning() << "scheduleStepFromCache: Cache not valid, falling back to real-time";
        scheduleStep(stepIndex, m_sequence.size());
        return;
    }
    
    // Get current energy level for branch selection
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    double energy01 = m_debugEnergy;
    if (m_debugEnergyAuto) {
        const auto snap = m_interaction.snapshot(nowWallMs, /*debugEnergyAuto=*/true, /*debugEnergy01=*/m_debugEnergy);
        energy01 = snap.energy01;
    }
    
    // Select energy branch with hysteresis (prevents oscillation at boundaries)
    m_currentEnergyBand = PrePlaybackCache::energyToBandWithHysteresis(energy01, m_currentEnergyBand);
    const PreComputedBeat* beat = m_preCache.getBeat(stepIndex, m_currentEnergyBand);
    
    if (!beat) {
        qWarning() << "scheduleStepFromCache: No beat at step" << stepIndex << "band" << static_cast<int>(m_currentEnergyBand);
        return;
    }
    
    // Time signature for grid conversion
    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;
    
    // Schedule drums (direct intent notes)
    for (const auto& dn : beat->drumsNotes) {
        m_engine.scheduleNote(dn);
    }
    
    // Schedule bass notes
    for (const auto& bn : beat->bassPlan.notes) {
        m_engine.scheduleNote(bn);
    }
    
    // Schedule bass keyswitches
    for (const auto& ks : beat->bassPlan.keyswitches) {
        if (ks.midi >= 0) {
            m_engine.scheduleKeySwitch("Bass", m_chBass, ks.midi, ks.startPos,
                                       /*structural=*/true, ks.leadMs, ks.holdMs, ks.logic_tag);
        }
    }
    
    // Note: Bass BeatPlan doesn't have CCs (bass uses keyswitches for articulation)
    
    // Schedule piano notes (respecting LH/RH mute flags)
    for (const auto& pn : beat->pianoPlan.notes) {
        // Filter by hand based on logic_tag
        const bool isLH = pn.logic_tag.startsWith("LH");
        const bool isRH = pn.logic_tag.startsWith("RH");
        
        if (isLH && m_debugMutePianoLH) continue;  // LH is muted
        if (isRH && m_debugMutePianoRH) continue;  // RH is muted
        
        m_engine.scheduleNote(pn);
    }
    
    // Schedule piano CCs (sustain pedal, expression, etc.)
    for (const auto& cc : beat->pianoPlan.ccs) {
        m_engine.scheduleCC("Piano", m_chPiano, cc.cc, cc.value, cc.startPos,
                           cc.structural, cc.logic_tag);
    }
    
    // NOTE: Theory event emission moved to emitTheoryEventForStep() 
    // which is called from onTick() at the CURRENT playback position,
    // not the scheduled (lookahead) position.
}

void VirtuosoBalladMvpPlaybackEngine::emitTheoryEventForStep(int stepIndex) {
    // Emit candidate_pool event for LibraryWindow live-follow
    // This is called at the CURRENT playback position from onTick()
    
    if (!m_usePreCache || !m_preCache.isValid()) return;
    
    // Get current energy level for branch selection
    const qint64 nowWallMs = QDateTime::currentMSecsSinceEpoch();
    double energy01 = m_debugEnergy;
    if (m_debugEnergyAuto) {
        const auto snap = m_interaction.snapshot(nowWallMs, /*debugEnergyAuto=*/true, /*debugEnergy01=*/m_debugEnergy);
        energy01 = snap.energy01;
    }
    
    // Use the tracked current band (already has hysteresis applied)
    const PreComputedBeat* beat = m_preCache.getBeat(stepIndex, m_currentEnergyBand);
    if (!beat) return;
    
    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;
    
    QJsonObject root;
    root.insert("event_kind", "candidate_pool");
    root.insert("schema", 2);
    root.insert("tempo_bpm", m_bpm);
    root.insert("ts_num", ts.num);
    root.insert("ts_den", ts.den);
    root.insert("style_preset_key", m_stylePresetKey);
    
    // Core chord/key info (what LibraryWindow::applyLiveChoiceToUi expects)
    root.insert("chord_def_key", beat->chordDefKey);
    root.insert("chord_root_pc", beat->chordRootPc);
    root.insert("key_tonic_pc", beat->keyTonicPc);
    root.insert("key_mode", beat->keyMode == virtuoso::theory::KeyMode::Minor ? "minor" : "major");
    root.insert("chord_is_new", beat->chordIsNew);
    root.insert("groove_template", beat->grooveTemplateKey);
    
    // Grid position for timing
    const auto poolPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(
        beat->barIndex, beat->beatInBar, 0, 1, ts);
    root.insert("grid_pos", virtuoso::groove::GrooveGrid::toString(poolPos, ts));
    const qint64 baseMs = m_engine.gridBaseMsEnsure();
    root.insert("on_ms", qint64(virtuoso::groove::GrooveGrid::posToMs(poolPos, ts, m_bpm) + baseMs));
    
    // "chosen" object structure (what LibraryWindow expects)
    QJsonObject chosen;
    chosen.insert("scale_key", beat->scaleKey);
    chosen.insert("voicing_key", beat->voicingKey);
    if (!beat->pianoPlan.notes.isEmpty()) {
        const auto& pn = beat->pianoPlan.notes.first();
        chosen.insert("scale_used", pn.scale_used);
        chosen.insert("voicing_type", pn.voicing_type);
    }
    root.insert("chosen", chosen);
    
    const QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    emit theoryEventJson(json);
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

