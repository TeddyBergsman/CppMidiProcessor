#include "playback/VirtuosoBalladMvpPlaybackEngine.h"

#include "midiprocessor.h"
#include "playback/BalladReferenceTuning.h"
#include "playback/SemanticMidiAnalyzer.h"
#include "playback/VibeStateMachine.h"

#include <QHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>
#include <algorithm>
#include <limits>

namespace playback {
namespace {

static QVector<const chart::Bar*> flattenBarsFrom(const chart::ChartModel& model) {
    QVector<const chart::Bar*> bars;
    for (const auto& line : model.lines) {
        for (const auto& bar : line.bars) {
            bars.push_back(&bar);
        }
    }
    return bars;
}

// Copied (intentionally) from BandPlaybackEngine/SilentPlaybackEngine to keep repeat/ending behavior stable.
static QVector<int> buildPlaybackSequenceFrom(const chart::ChartModel& model) {
    const QVector<const chart::Bar*> bars = flattenBarsFrom(model);
    const int nBars = bars.size();
    QVector<int> seq;
    if (nBars <= 0) return seq;
    seq.reserve(nBars * 4);

    int fineBar = -1;
    int segnoBar = -1;
    for (int i = 0; i < nBars; ++i) {
        const QString ann = bars[i]->annotation.trimmed();
        if (fineBar < 0 && ann.compare("Fine", Qt::CaseInsensitive) == 0) fineBar = i;
        if (segnoBar < 0 && ann.contains("Segno", Qt::CaseInsensitive)) segnoBar = i;
    }

    const QString footer = model.footerText.trimmed();
    const bool wantsJump = footer.startsWith("D.C.", Qt::CaseInsensitive) || footer.startsWith("D.S.", Qt::CaseInsensitive);
    const bool jumpIsDS = footer.startsWith("D.S.", Qt::CaseInsensitive);
    const bool alFine = footer.contains("al Fine", Qt::CaseInsensitive);
    const int jumpTarget = jumpIsDS ? (segnoBar >= 0 ? segnoBar : 0) : 0;

    QVector<int> repeatStartStack;
    QHash<int, int> startToEnd;
    QHash<int, int> endToStart;
    repeatStartStack.reserve(8);
    for (int i = 0; i < nBars; ++i) {
        const QString l = bars[i]->barlineLeft;
        const QString r = bars[i]->barlineRight;
        if (l.contains('{')) {
            repeatStartStack.push_back(i);
        }
        if (r.contains('}')) {
            int start = 0;
            if (!repeatStartStack.isEmpty()) {
                start = repeatStartStack.takeLast();
            }
            startToEnd.insert(start, i);
            endToStart.insert(i, start);
        }
    }

    QHash<int, int> endingStartToEnd;
    for (int i = 0; i < nBars; ++i) {
        const int n = bars[i]->endingStart;
        if (n <= 0) continue;
        int end = i;
        for (int j = i; j < nBars; ++j) {
            if (bars[j]->endingEnd == n) { end = j; break; }
        }
        endingStartToEnd.insert(i, end);
    }

    QHash<int, int> repeatEndToPasses;
    for (auto it = startToEnd.constBegin(); it != startToEnd.constEnd(); ++it) {
        const int start = it.key();
        const int end = it.value();
        int maxEnding = 0;
        for (int i = start; i <= end && i < nBars; ++i) {
            maxEnding = std::max(maxEnding, bars[i]->endingStart);
            maxEnding = std::max(maxEnding, bars[i]->endingEnd);
        }
        repeatEndToPasses.insert(end, std::max(2, maxEnding));
    }

    struct RepeatCtx { int start = 0; int end = -1; int pass = 1; int passes = 2; };
    QVector<RepeatCtx> stack;
    stack.reserve(4);

    bool jumped = false;
    int pc = 0;
    int guardSteps = 0;
    const int guardMax = 20000;

    auto currentPass = [&]() -> int { return stack.isEmpty() ? 1 : stack.last().pass; };

    while (pc < nBars && guardSteps++ < guardMax) {
        if (startToEnd.contains(pc)) {
            const int end = startToEnd.value(pc);
            bool already = false;
            if (!stack.isEmpty() && stack.last().start == pc && stack.last().end == end) already = true;
            if (!already) {
                RepeatCtx ctx;
                ctx.start = pc;
                ctx.end = end;
                ctx.pass = 1;
                ctx.passes = repeatEndToPasses.value(end, 2);
                stack.push_back(ctx);
            }
        }

        if (!stack.isEmpty()) {
            const int n = bars[pc]->endingStart;
            if (n > 0 && n != currentPass()) {
                const int end = endingStartToEnd.value(pc, pc);
                pc = end + 1;
                continue;
            }
        }

        for (int c = 0; c < 4; ++c) {
            seq.push_back(pc * 4 + c);
        }

        if (jumped && alFine && fineBar >= 0 && pc == fineBar) {
            break;
        }

        if (!stack.isEmpty() && pc == stack.last().end) {
            if (stack.last().pass < stack.last().passes) {
                stack.last().pass += 1;
                pc = stack.last().start;
                continue;
            }
            stack.removeLast();
            pc += 1;
            continue;
        }

        pc += 1;
        if (pc >= nBars) {
            if (wantsJump && !jumped) {
                jumped = true;
                pc = jumpTarget;
                continue;
            }
            break;
        }
    }

    return seq;
}

static bool sameChordKey(const music::ChordSymbol& a, const music::ChordSymbol& b) {
    return (a.rootPc == b.rootPc && a.bassPc == b.bassPc && a.quality == b.quality && a.seventh == b.seventh && a.extension == b.extension && a.alt == b.alt);
}

static virtuoso::groove::Rational durationWholeFromHoldMs(int holdMs, int bpm) {
    // GrooveGrid::wholeNotesToMs: wholeMs = 240000 / bpm
    // => whole = holdMs / wholeMs = holdMs * bpm / 240000
    if (holdMs <= 0) return virtuoso::groove::Rational(1, 16);
    if (bpm <= 0) bpm = 120;
    return virtuoso::groove::Rational(qint64(holdMs) * qint64(bpm), qint64(240000));
}

static QString intentsToString(const SemanticMidiAnalyzer::IntentState& i) {
    QStringList out;
    if (i.densityHigh) out << "DENSITY_HIGH";
    if (i.registerHigh) out << "REGISTER_HIGH";
    if (i.intensityPeak) out << "INTENSITY_PEAK";
    if (i.playingOutside) out << "PLAYING_OUTSIDE";
    if (i.silence) out << "SILENCE";
    return out.join(",");
}

static double energyForOverride(int overrideVibe) {
    // 0=Auto (ignored), 1..4 map to vibe energies
    switch (overrideVibe) {
        case 1: return 0.25; // Simmer
        case 2: return 0.55; // Build
        case 3: return 0.90; // Climax
        case 4: return 0.18; // CoolDown
        default: return 0.25;
    }
}

} // namespace

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

    // Cool-jazz vocabulary pack (data-driven): used by bass/piano planners and drum add-ons.
    {
        QString err;
        m_vocab.loadFromResourcePath(":/virtuoso/vocab/cool_jazz_vocabulary.json", &err);
        m_bassPlanner.setVocabulary(&m_vocab);
        m_pianoPlanner.setVocabulary(&m_vocab);
        Q_UNUSED(err);
    }
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
    connect(m_midi, &MidiProcessor::guitarNoteOn, this, [this](int note, int vel) {
        m_listener.ingestGuitarNoteOn(note, vel, QDateTime::currentMSecsSinceEpoch());
    }, Qt::QueuedConnection);
    connect(m_midi, &MidiProcessor::guitarNoteOff, this, [this](int note) {
        m_listener.ingestGuitarNoteOff(note, QDateTime::currentMSecsSinceEpoch());
    }, Qt::QueuedConnection);
    connect(m_midi, &MidiProcessor::voiceCc2Stream, this, [this](int cc2) {
        m_listener.ingestCc2(cc2, QDateTime::currentMSecsSinceEpoch());
    }, Qt::QueuedConnection);

    // Vocal melody tracking (NOT used for density): allows later call/response.
    connect(m_midi, &MidiProcessor::voiceNoteOn, this, [this](int note, int vel) {
        m_listener.ingestVoiceNoteOn(note, vel, QDateTime::currentMSecsSinceEpoch());
    }, Qt::QueuedConnection);
    connect(m_midi, &MidiProcessor::voiceNoteOff, this, [this](int note) {
        m_listener.ingestVoiceNoteOff(note, QDateTime::currentMSecsSinceEpoch());
    }, Qt::QueuedConnection);
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
    rebuildSequence();

    virtuoso::groove::TimeSignature ts;
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;
    m_engine.setTimeSignature(ts);
}

void VirtuosoBalladMvpPlaybackEngine::setStylePresetKey(const QString& key) {
    const QString k = key.trimmed();
    if (k.isEmpty()) return;
    m_stylePresetKey = k;
    if (m_playing) applyPresetToEngine();
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
    m_hasLastChord = false;
    m_lastChord = music::ChordSymbol{};
    m_bassPlanner.reset();
    m_pianoPlanner.reset();
    m_listener.reset();
    m_vibe.reset();
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
    m_sequence = buildPlaybackSequenceFromModel();
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
}

QVector<const chart::Bar*> VirtuosoBalladMvpPlaybackEngine::flattenBars() const {
    return flattenBarsFrom(m_model);
}

QVector<int> VirtuosoBalladMvpPlaybackEngine::buildPlaybackSequenceFromModel() const {
    return buildPlaybackSequenceFrom(m_model);
}

const chart::Cell* VirtuosoBalladMvpPlaybackEngine::cellForFlattenedIndex(int cellIndex) const {
    if (cellIndex < 0) return nullptr;
    const QVector<const chart::Bar*> bars = flattenBars();
    const int barIndex = cellIndex / 4;
    const int cellInBar = cellIndex % 4;
    if (barIndex < 0 || barIndex >= bars.size()) return nullptr;
    const auto* bar = bars[barIndex];
    if (!bar) return nullptr;
    if (cellInBar < 0 || cellInBar >= bar->cells.size()) return nullptr;
    return &bar->cells[cellInBar];
}

bool VirtuosoBalladMvpPlaybackEngine::chordForCellIndex(int cellIndex, music::ChordSymbol& outChord, bool& isNewChord) {
    isNewChord = false;
    const chart::Cell* c = cellForFlattenedIndex(cellIndex);
    if (!c) return false;

    const QString t = c->chord.trimmed();
    if (t.isEmpty()) {
        if (m_hasLastChord) { outChord = m_lastChord; return true; }
        return false;
    }

    music::ChordSymbol parsed;
    if (!music::parseChordSymbol(t, parsed)) {
        if (m_hasLastChord) { outChord = m_lastChord; return true; }
        return false;
    }
    if (parsed.placeholder) {
        if (m_hasLastChord) { outChord = m_lastChord; return true; }
        return false;
    }

    outChord = parsed;
    if (!m_hasLastChord) {
        isNewChord = true;
    } else {
        isNewChord = !sameChordKey(outChord, m_lastChord);
    }
    m_lastChord = outChord;
    m_hasLastChord = true;
    return true;
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

    // --- Lookahead plan (4 bars) for UI: emit a full fixed window as JSON array. ---
    // This is *not* scheduling: it has no side-effects on engine timing or planner state.
    {
        const int beatsPerBar = qMax(1, ts.num);
        const int horizonBeats = beatsPerBar * 4; // 4 bars
        const int total = seqLen * qMax(1, m_repeats);
        // Anchor to the current BAR start so notes do not disappear immediately after being played.
        const int startStep = qMax(0, stepNow - (stepNow % beatsPerBar));
        const int endStep = qMin(total, startStep + horizonBeats);

        // Local chord simulation (no mutation of m_lastChord state).
        music::ChordSymbol simLast = m_hasLastChord ? m_lastChord : music::ChordSymbol{};
        bool simHasLast = m_hasLastChord;

        auto parseCellChordNoStateLocal = [&](int anyCellIndex, const music::ChordSymbol& fallback, bool* outIsExplicit = nullptr) -> music::ChordSymbol {
            if (outIsExplicit) *outIsExplicit = false;
            const chart::Cell* c = cellForFlattenedIndex(anyCellIndex);
            if (!c) return fallback;
            const QString t = c->chord.trimmed();
            if (t.isEmpty()) return fallback;
            music::ChordSymbol parsed;
            if (!music::parseChordSymbol(t, parsed)) return fallback;
            if (parsed.placeholder) return fallback;
            if (outIsExplicit) *outIsExplicit = true;
            return parsed;
        };

        // Snapshot interaction state once for this lookahead block (stable).
        const qint64 nowMsWall = QDateTime::currentMSecsSinceEpoch();
        const auto intent = m_listener.compute(nowMsWall);
        auto vibeEff = m_vibe.update(intent, nowMsWall);
        if (m_debugVibeOverride != 0) {
            VibeStateMachine::Vibe v = VibeStateMachine::Vibe::Simmer;
            if (m_debugVibeOverride == 2) v = VibeStateMachine::Vibe::Build;
            if (m_debugVibeOverride == 3) v = VibeStateMachine::Vibe::Climax;
            if (m_debugVibeOverride == 4) v = VibeStateMachine::Vibe::CoolDown;
            vibeEff.vibe = v;
            vibeEff.energy = energyForOverride(m_debugVibeOverride);
        }
        const QString vibeStr = VibeStateMachine::vibeName(vibeEff.vibe);
        const QString intentStr = intentsToString(intent);

        // Clone planners so the lookahead does not mutate live state.
        JazzBalladBassPlanner bassSim = m_bassPlanner;
        JazzBalladPianoPlanner pianoSim = m_pianoPlanner;

        QJsonArray arr;

        auto emitIntentAsJson = [&](const virtuoso::engine::AgentIntentNote& n, int bpm, const virtuoso::groove::TimeSignature& ts) {
            virtuoso::theory::TheoryEvent te;
            te.agent = n.agent;
            te.timestamp = ""; // leave empty; UI uses on_ms/grid_pos
            te.chord_context = n.chord_context;
            te.scale_used = n.scale_used;
            te.voicing_type = n.voicing_type;
            te.logic_tag = n.logic_tag;
            te.target_note = n.target_note;
            te.dynamic_marking = QString::number(n.baseVelocity);
            te.grid_pos = virtuoso::groove::GrooveGrid::toString(n.startPos, ts);
            te.channel = n.channel;
            te.note = n.note;
            te.tempo_bpm = bpm;
            te.ts_num = ts.num;
            te.ts_den = ts.den;
            te.engine_now_ms = m_engine.elapsedMs();
            // Use clean grid timing for plan (no micro jitter).
            const qint64 on = virtuoso::groove::GrooveGrid::posToMs(n.startPos, ts, bpm);
            const qint64 off = on + qMax<qint64>(1, virtuoso::groove::GrooveGrid::wholeNotesToMs(n.durationWhole, bpm));
            te.on_ms = on;
            te.off_ms = off;
            te.vibe_state = vibeStr;
            te.user_intents = intentStr;
            te.user_outside_ratio = n.user_outside_ratio;
            arr.push_back(te.toJsonObject());
        };

        for (int step = startStep; step < endStep; ++step) {
            const int playbackBarIndex = (beatsPerBar > 0) ? (step / beatsPerBar) : (step / 4);
            const int beatInBar = (beatsPerBar > 0) ? (step % beatsPerBar) : (step % 4);
            const int cellIndex = m_sequence[step % seqLen];

            // Determine chord and chordIsNew in this simulated stream.
            music::ChordSymbol chord = simHasLast ? simLast : music::ChordSymbol{};
            bool chordIsNew = false;
            {
                bool explicitChord = false;
                const music::ChordSymbol parsed = parseCellChordNoStateLocal(cellIndex, chord, &explicitChord);
                if (explicitChord) chord = parsed;
                if (!simHasLast) chordIsNew = explicitChord;
                else chordIsNew = explicitChord && !sameChordKey(chord, simLast);
                if (explicitChord) { simLast = chord; simHasLast = true; }
            }
            if (!simHasLast) continue;

            // Next chord (for bass approach logic)
            music::ChordSymbol nextChord = chord;
            bool haveNext = false;
            {
                const int stepNextBar = step + (beatsPerBar - beatInBar);
                if (stepNextBar < total) {
                    const int cellNext = m_sequence[stepNextBar % seqLen];
                    bool explicitNext = false;
                    nextChord = parseCellChordNoStateLocal(cellNext, /*fallback*/chord, &explicitNext);
                    haveNext = explicitNext || (nextChord.rootPc >= 0);
                    if (nextChord.noChord) haveNext = false;
                }
            }

            const QString chordText = chord.originalText.trimmed().isEmpty() ? QString("pc=%1").arg(chord.rootPc) : chord.originalText.trimmed();
            const bool structural = (beatInBar == 0 || beatInBar == 2) || chordIsNew;

            // Drums (base drummer)
            {
                BrushesBalladDrummer::Context dc;
                dc.bpm = m_bpm;
                dc.ts = ts;
                dc.playbackBarIndex = playbackBarIndex;
                dc.beatInBar = beatInBar;
                dc.structural = structural;
                const quint32 detSeed = quint32(qHash(QString("ballad|") + m_stylePresetKey));
                dc.determinismSeed = detSeed ^ 0xD00D'BEEFu;
                dc.energy = qBound(0.0, vibeEff.energy * qMax(0.0, m_debugInteractionBoost), 1.0);
                dc.intensityPeak = intent.intensityPeak;
                const auto dnotes = m_drummer.planBeat(dc);
                for (auto n : dnotes) {
                    n.vibe_state = vibeStr;
                    n.user_intents = intentStr;
                    n.user_outside_ratio = intent.outsideRatio;
                    emitIntentAsJson(n, m_bpm, ts);
                }
            }

            // Bass + piano (planner choices)
            if (!chord.noChord) {
                JazzBalladBassPlanner::Context bc;
                bc.bpm = m_bpm;
                bc.playbackBarIndex = playbackBarIndex;
                bc.beatInBar = beatInBar;
                bc.chordIsNew = chordIsNew;
                bc.chord = chord;
                bc.hasNextChord = haveNext && !nextChord.noChord;
                bc.nextChord = nextChord;
                bc.chordText = chordText;
                const quint32 detSeed = quint32(qHash(QString("ballad|") + m_stylePresetKey));
                bc.determinismSeed = detSeed;
                bc.userDensityHigh = intent.densityHigh;
                bc.userIntensityPeak = intent.intensityPeak;
                bc.userSilence = intent.silence;
                bc.forceClimax = (m_debugVibeOverride == 3) || (vibeEff.vibe == VibeStateMachine::Vibe::Climax);
                bc.energy = qBound(0.0, vibeEff.energy * qMax(0.0, m_debugInteractionBoost), 1.0);
                auto bnotes = bassSim.planBeat(bc, m_chBass, ts);
                for (auto& n : bnotes) {
                    n.vibe_state = vibeStr;
                    n.user_intents = intentStr;
                    n.user_outside_ratio = intent.outsideRatio;
                    emitIntentAsJson(n, m_bpm, ts);
                }

                JazzBalladPianoPlanner::Context pc;
                pc.bpm = m_bpm;
                pc.playbackBarIndex = playbackBarIndex;
                pc.beatInBar = beatInBar;
                pc.chordIsNew = chordIsNew;
                pc.chord = chord;
                pc.chordText = chordText;
                pc.determinismSeed = detSeed ^ 0xBADC0FFEu;
                pc.userDensityHigh = intent.densityHigh;
                pc.userIntensityPeak = intent.intensityPeak;
                pc.userRegisterHigh = intent.registerHigh;
                pc.userSilence = intent.silence;
                pc.forceClimax = (m_debugVibeOverride == 3) || (vibeEff.vibe == VibeStateMachine::Vibe::Climax);
                pc.energy = qBound(0.0, vibeEff.energy * qMax(0.0, m_debugInteractionBoost), 1.0);
                auto pnotes = pianoSim.planBeat(pc, m_chPiano, ts);
                for (auto& n : pnotes) {
                    n.vibe_state = vibeStr;
                    n.user_intents = intentStr;
                    n.user_outside_ratio = intent.outsideRatio;
                    emitIntentAsJson(n, m_bpm, ts);
                }
            }
        }

        const QJsonDocument doc(arr);
        emit lookaheadPlanJson(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    }

    // Lookahead scheduling window (tight timing).
    constexpr int kLookaheadMs = 220;
    const int scheduleUntil = int(double(elapsedMs + kLookaheadMs) / beatMs);
    const int maxStepToSchedule = std::min(total - 1, scheduleUntil);

    while (m_nextScheduledStep <= maxStepToSchedule) {
        scheduleStep(m_nextScheduledStep, seqLen);
        m_nextScheduledStep++;
    }
}

void VirtuosoBalladMvpPlaybackEngine::scheduleStep(int stepIndex, int seqLen) {
    // "Playback position" defines absolute time (monotonic), independent of chart jumps (D.C./D.S.).
    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;

    const int playbackBarIndex = (ts.num > 0) ? (stepIndex / ts.num) : (stepIndex / 4);
    const int beatInBar = (ts.num > 0) ? (stepIndex % ts.num) : (stepIndex % 4);

    music::ChordSymbol chord;
    bool chordIsNew = false;
    const int cellIndex = m_sequence[stepIndex % seqLen];
    const bool haveChord = chordForCellIndex(cellIndex, chord, chordIsNew);

    // Update listener harmonic context for "playing outside" classification.
    if (haveChord && !chord.noChord) m_listener.setChordContext(chord);

    // Lookahead: next bar's beat-1 chord (for bass approaches / piano anticipation later).
    // IMPORTANT: do NOT call chordForCellIndex() here, because it mutates last-chord state.
    auto parseCellChordNoState = [&](int anyCellIndex, const music::ChordSymbol& fallback, bool* outIsExplicit = nullptr) -> music::ChordSymbol {
        if (outIsExplicit) *outIsExplicit = false;
        const chart::Cell* c = cellForFlattenedIndex(anyCellIndex);
        if (!c) return fallback;
        const QString t = c->chord.trimmed();
        if (t.isEmpty()) return fallback;
        music::ChordSymbol parsed;
        if (!music::parseChordSymbol(t, parsed)) return fallback;
        if (parsed.placeholder) return fallback;
        if (outIsExplicit) *outIsExplicit = true;
        return parsed;
    };

    music::ChordSymbol nextChord;
    bool haveNext = false;
    if (seqLen > 0 && haveChord) {
        const int beatsPerBar = (ts.num > 0) ? ts.num : 4;
        const int stepNextBar = stepIndex + (beatsPerBar - beatInBar);
        const int total = seqLen * qMax(1, m_repeats);
        if (stepNextBar < total) {
            const int cellNext = m_sequence[stepNextBar % seqLen];
            bool explicitNext = false;
            nextChord = parseCellChordNoState(cellNext, /*fallback*/chord, &explicitNext);
            haveNext = explicitNext || (nextChord.rootPc >= 0);
            if (nextChord.noChord) haveNext = false;
        }
    }

    const bool strongBeat = (beatInBar == 0 || beatInBar == 2);
    const bool structural = strongBeat || chordIsNew;

    // Snapshot live intent state once per scheduled step (ms domain).
    const qint64 nowMsWall = QDateTime::currentMSecsSinceEpoch();
    const auto intent = m_listener.compute(nowMsWall);
    auto vibeEff = m_vibe.update(intent, nowMsWall);

    // Debug override: force vibe to make behavior obvious during validation.
    QString vibeStr;
    if (m_debugVibeOverride != 0) {
        VibeStateMachine::Vibe v = VibeStateMachine::Vibe::Simmer;
        if (m_debugVibeOverride == 2) v = VibeStateMachine::Vibe::Build;
        if (m_debugVibeOverride == 3) v = VibeStateMachine::Vibe::Climax;
        if (m_debugVibeOverride == 4) v = VibeStateMachine::Vibe::CoolDown;
        vibeEff.vibe = v;
        vibeEff.energy = energyForOverride(m_debugVibeOverride);
        vibeStr = VibeStateMachine::vibeName(vibeEff.vibe) + " (forced)";
    } else {
        vibeStr = VibeStateMachine::vibeName(vibeEff.vibe);
    }

    const QString intentStr = intentsToString(intent);

    // Debug UI status (emitted once per beat step).
    emit debugStatus(QString("Vibe=%1  energy=%2  intents=%3  nps=%4  reg=%5  gVel=%6  cc2=%7  vNote=%8  silenceMs=%9  outside=%10")
                         .arg(vibeStr)
                         .arg(vibeEff.energy, 0, 'f', 2)
                         .arg(intentStr.isEmpty() ? "-" : intentStr)
                         .arg(intent.notesPerSec, 0, 'f', 2)
                         .arg(intent.registerCenterMidi)
                         .arg(intent.lastGuitarVelocity)
                         .arg(intent.lastCc2)
                         .arg(intent.lastVoiceMidi)
                         .arg(intent.msSinceLastActivity == std::numeric_limits<qint64>::max() ? -1 : intent.msSinceLastActivity)
                         .arg(intent.outsideRatio, 0, 'f', 2));

    // Drums always run (ballad texture), even if harmony missing/N.C.
    // We schedule Drums first, because Bass groove-lock references Drums kick timing.
    const quint32 detSeed = quint32(qHash(QString("ballad|") + m_stylePresetKey));
    virtuoso::engine::AgentIntentNote kickIntent;
    virtuoso::groove::HumanizedEvent kickHe;
    bool haveKickHe = false;

    {
        BrushesBalladDrummer::Context dc;
        dc.bpm = m_bpm;
        dc.ts = ts;
        dc.playbackBarIndex = playbackBarIndex;
        dc.beatInBar = beatInBar;
        dc.structural = structural;
        dc.determinismSeed = detSeed ^ 0xD00D'BEEFu;
        dc.energy = qBound(0.0, vibeEff.energy * qMax(0.0, m_debugInteractionBoost), 1.0);
        dc.intensityPeak = intent.intensityPeak;

        auto drumIntents = m_drummer.planBeat(dc);

        // If there is a kick intent, humanize/schedule it first so bass can lock to its onMs.
        int kickIndex = -1;
        for (int i = 0; i < drumIntents.size(); ++i) {
            if (drumIntents[i].note == m_noteKick) { kickIndex = i; break; }
        }
        if (kickIndex >= 0) {
            kickIntent = drumIntents[kickIndex];
            kickIntent.vibe_state = vibeStr;
            kickIntent.user_intents = intentStr;
            kickIntent.user_outside_ratio = intent.outsideRatio;
            kickHe = m_engine.humanizeIntent(kickIntent);
            haveKickHe = (kickHe.offMs > kickHe.onMs);
            if (haveKickHe) {
                m_engine.scheduleHumanizedIntentNote(kickIntent, kickHe);
            }
            drumIntents.removeAt(kickIndex);
        }

        for (auto n : drumIntents) {
            // Macro dynamics: scale drums velocity slightly by energy.
            const double e = qBound(0.0, vibeEff.energy, 1.0);
            n.baseVelocity = qBound(1, int(llround(double(n.baseVelocity) * (0.85 + 0.35 * e))), 127);
            n.vibe_state = vibeStr;
            n.user_intents = intentStr;
            n.user_outside_ratio = intent.outsideRatio;
            m_engine.scheduleNote(n);
        }

        // IMPORTANT: single source of truth for drums is `BrushesBalladDrummer`.
        // We intentionally do not add any extra JSON-driven drum vocabulary here.
    }

    if (!haveChord || chord.noChord) return; // leave space on N.C.

    // Bass + piano planners (Ballad Brain v1).
    const QString chordText = chord.originalText.trimmed().isEmpty() ? QString("pc=%1").arg(chord.rootPc) : chord.originalText.trimmed();
    const BalladRefTuning tune = tuningForReferenceTrack(m_stylePresetKey);

    JazzBalladBassPlanner::Context bc;
    bc.bpm = m_bpm;
    bc.playbackBarIndex = playbackBarIndex;
    bc.beatInBar = beatInBar;
    bc.chordIsNew = chordIsNew;
    bc.chord = chord;
    bc.hasNextChord = haveNext && !nextChord.noChord;
    bc.nextChord = nextChord;
    bc.chordText = chordText;
    bc.determinismSeed = detSeed;
    bc.approachProbBeat3 = tune.bassApproachProbBeat3;
    bc.skipBeat3ProbStable = tune.bassSkipBeat3ProbStable;
    bc.allowApproachFromAbove = tune.bassAllowApproachFromAbove;
    bc.userDensityHigh = intent.densityHigh;
    bc.userIntensityPeak = intent.intensityPeak;
    bc.userSilence = intent.silence;
    bc.forceClimax = (m_debugVibeOverride == 3) || (vibeEff.vibe == VibeStateMachine::Vibe::Climax);
    bc.energy = qBound(0.0, vibeEff.energy * qMax(0.0, m_debugInteractionBoost), 1.0);
    // Interaction heuristics: when user is dense/high/intense, bass simplifies and avoids chromaticism.
    if (intent.densityHigh || intent.intensityPeak) {
        bc.approachProbBeat3 *= 0.35;
        bc.skipBeat3ProbStable = qMin(0.65, bc.skipBeat3ProbStable + 0.20);
    }
    if (vibeEff.vibe == VibeStateMachine::Vibe::Climax) {
        bc.approachProbBeat3 *= 0.60; // slightly more motion, but still ballad-safe
        bc.skipBeat3ProbStable = qMax(0.10, bc.skipBeat3ProbStable - 0.08);
    }
    if (vibeEff.vibe == VibeStateMachine::Vibe::Build) {
        // Make Build audibly different: fewer omissions, slightly more motion.
        bc.approachProbBeat3 = qMin(1.0, bc.approachProbBeat3 + 0.12);
        bc.skipBeat3ProbStable = qMax(0.0, bc.skipBeat3ProbStable - 0.12);
    }
    auto bassIntents = m_bassPlanner.planBeat(bc, m_chBass, ts);
    for (auto& n : bassIntents) {
        n.vibe_state = vibeStr;
        n.user_intents = intentStr;
        n.user_outside_ratio = intent.outsideRatio;
        // Macro dynamics: small velocity lift under higher energy.
        const double e = qBound(0.0, vibeEff.energy, 1.0);
        n.baseVelocity = qBound(1, int(llround(double(n.baseVelocity) * (0.90 + 0.25 * e))), 127);
        // Groove lock (prototype): on beat 1, if a feather kick exists, align bass attack to kick.
        if (m_kickLocksBass && beatInBar == 0 && haveKickHe) {
            auto bhe = m_engine.humanizeIntent(n);
            if (bhe.offMs > bhe.onMs) {
                const qint64 delta = kickHe.onMs - bhe.onMs;
                if (qAbs(delta) <= qMax<qint64>(0, m_kickLockMaxMs)) {
                    bhe.onMs += delta;
                    bhe.offMs += delta;
                    bhe.timing_offset_ms += int(delta);
                    const QString tag = n.logic_tag.isEmpty() ? "GrooveLock:Kick" : (n.logic_tag + "|GrooveLock:Kick");
                    m_engine.scheduleHumanizedIntentNote(n, bhe, tag);
                    continue;
                }
            }
        }
        m_engine.scheduleNote(n);
    }

    JazzBalladPianoPlanner::Context pc;
    pc.bpm = m_bpm;
    pc.playbackBarIndex = playbackBarIndex;
    pc.beatInBar = beatInBar;
    pc.chordIsNew = chordIsNew;
    pc.chord = chord;
    pc.chordText = chordText;
    pc.determinismSeed = detSeed ^ 0xBADC0FFEu;
    pc.lhLo = tune.pianoLhLo; pc.lhHi = tune.pianoLhHi;
    pc.rhLo = tune.pianoRhLo; pc.rhHi = tune.pianoRhHi;
    pc.sparkleLo = tune.pianoSparkleLo; pc.sparkleHi = tune.pianoSparkleHi;
    pc.skipBeat2ProbStable = tune.pianoSkipBeat2ProbStable;
    pc.addSecondColorProb = tune.pianoAddSecondColorProb;
    pc.sparkleProbBeat4 = tune.pianoSparkleProbBeat4;
    pc.preferShells = tune.pianoPreferShells;
    pc.userDensityHigh = intent.densityHigh;
    pc.userIntensityPeak = intent.intensityPeak;
    pc.userRegisterHigh = intent.registerHigh;
    pc.userSilence = intent.silence;
    pc.forceClimax = (m_debugVibeOverride == 3) || (vibeEff.vibe == VibeStateMachine::Vibe::Climax);
    pc.energy = qBound(0.0, vibeEff.energy * qMax(0.0, m_debugInteractionBoost), 1.0);
    // Interaction heuristics:
    // - User register high => piano stays lower (and reduces sparkle)
    // - User density high/intensity peak => piano comp sparser
    // - User silence => piano is allowed to fill slightly more
    if (intent.registerHigh) {
        pc.rhHi = qMax(pc.rhLo + 4, pc.rhHi - 6);
        pc.sparkleProbBeat4 *= 0.25;
    }
    if (intent.densityHigh || intent.intensityPeak) {
        pc.skipBeat2ProbStable = qMin(0.95, pc.skipBeat2ProbStable + 0.25);
        pc.preferShells = true;
        pc.sparkleProbBeat4 *= 0.20;
    } else if (intent.silence) {
        pc.skipBeat2ProbStable = qMax(0.0, pc.skipBeat2ProbStable - 0.12);
        pc.sparkleProbBeat4 = qMin(0.40, pc.sparkleProbBeat4 + 0.08);
    }
    if (vibeEff.vibe == VibeStateMachine::Vibe::Climax) {
        pc.skipBeat2ProbStable = qMax(0.0, pc.skipBeat2ProbStable - 0.10);
        pc.addSecondColorProb = qMin(0.65, pc.addSecondColorProb + 0.10);
        pc.sparkleProbBeat4 = qMin(0.55, pc.sparkleProbBeat4 + 0.08);
    }
    if (vibeEff.vibe == VibeStateMachine::Vibe::Build) {
        // Make Build audibly different without needing huge boost:
        // more comp density + color, but not full Climax.
        pc.skipBeat2ProbStable = qMax(0.0, pc.skipBeat2ProbStable - 0.18);
        pc.addSecondColorProb = qMin(0.60, pc.addSecondColorProb + 0.15);
        pc.sparkleProbBeat4 = qMin(0.45, pc.sparkleProbBeat4 + 0.10);
    }
    if (vibeEff.vibe == VibeStateMachine::Vibe::CoolDown) {
        pc.skipBeat2ProbStable = qMin(0.98, pc.skipBeat2ProbStable + 0.10);
        pc.sparkleProbBeat4 *= 0.20;
    }
    auto pianoIntents = m_pianoPlanner.planBeat(pc, m_chPiano, ts);
    for (auto& n : pianoIntents) {
        n.vibe_state = vibeStr;
        n.user_intents = intentStr;
        n.user_outside_ratio = intent.outsideRatio;
        // Macro dynamics: piano a bit more responsive to vibe energy.
        const double e = qBound(0.0, vibeEff.energy, 1.0);
        n.baseVelocity = qBound(1, int(llround(double(n.baseVelocity) * (0.82 + 0.40 * e))), 127);
        m_engine.scheduleNote(n);
    }
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

