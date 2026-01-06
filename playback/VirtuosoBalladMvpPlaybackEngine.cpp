#include "playback/VirtuosoBalladMvpPlaybackEngine.h"

#include "midiprocessor.h"

#include <QHash>
#include <QtGlobal>
#include <algorithm>

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

} // namespace

VirtuosoBalladMvpPlaybackEngine::VirtuosoBalladMvpPlaybackEngine(QObject* parent)
    : QObject(parent)
    , m_registry(virtuoso::groove::GrooveRegistry::builtins()) {
    m_tickTimer.setInterval(10);
    m_tickTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_tickTimer, &QTimer::timeout, this, &VirtuosoBalladMvpPlaybackEngine::onTick);

    connect(&m_engine, &virtuoso::engine::VirtuosoEngine::theoryEventJson,
            this, &VirtuosoBalladMvpPlaybackEngine::theoryEventJson);
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

    // Drums always run (ballad texture), even if harmony missing/N.C.
    scheduleDrumsBrushes(playbackBarIndex, beatInBar, structural);

    if (!haveChord || chord.noChord) return; // leave space on N.C.

    // Bass + piano planners (Ballad Brain v1).
    const QString chordText = chord.originalText.trimmed().isEmpty() ? QString("pc=%1").arg(chord.rootPc) : chord.originalText.trimmed();

    JazzBalladBassPlanner::Context bc;
    bc.bpm = m_bpm;
    bc.playbackBarIndex = playbackBarIndex;
    bc.beatInBar = beatInBar;
    bc.chordIsNew = chordIsNew;
    bc.chord = chord;
    bc.hasNextChord = haveNext && !nextChord.noChord;
    bc.nextChord = nextChord;
    bc.chordText = chordText;
    auto bassIntents = m_bassPlanner.planBeat(bc, m_chBass, ts);
    for (auto& n : bassIntents) {
        n.startPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(playbackBarIndex, beatInBar, 0, 1, ts);
        m_engine.scheduleNote(n);
    }

    JazzBalladPianoPlanner::Context pc;
    pc.bpm = m_bpm;
    pc.playbackBarIndex = playbackBarIndex;
    pc.beatInBar = beatInBar;
    pc.chordIsNew = chordIsNew;
    pc.chord = chord;
    pc.chordText = chordText;
    pc.determinismSeed = 1337u;
    auto pianoIntents = m_pianoPlanner.planBeat(pc, m_chPiano, ts);
    for (auto& n : pianoIntents) {
        n.startPos = virtuoso::groove::GrooveGrid::fromBarBeatTuplet(playbackBarIndex, beatInBar, 0, 1, ts);
        m_engine.scheduleNote(n);
    }
}

void VirtuosoBalladMvpPlaybackEngine::scheduleDrumsBrushes(int playbackBarIndex, int beatInBar, bool structural) {
    // Brushes MVP:
    // - sustained brushing texture per bar (E3: hold ~4s for full sample)
    // - light swish/accents on 2 & 4 (short hits)
    // - feather kick occasionally on 1
    using virtuoso::groove::GrooveGrid;
    using virtuoso::groove::Rational;

    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (m_model.timeSigNum > 0) ? m_model.timeSigNum : 4;
    ts.den = (m_model.timeSigDen > 0) ? m_model.timeSigDen : 4;

    // Sustained brushing texture once per bar (at beat 1).
    if (beatInBar == 0) {
        virtuoso::engine::AgentIntentNote n;
        n.agent = "Drums";
        n.channel = m_chDrums; // requested: channel 6
        n.note = m_noteBrushLoop; // E3: "Snare/Hits - Brushing"
        n.baseVelocity = 32;
        n.startPos = GrooveGrid::fromBarBeatTuplet(playbackBarIndex, beatInBar, 0, 1, ts);
        n.durationWhole = durationWholeFromHoldMs(/*holdMs*/4000, m_bpm);
        n.structural = true;
        m_engine.scheduleNote(n);
    }

    // Swish on 2&4 (beat starts): short right-hand snare hits (quiet).
    if ((beatInBar % 2) == 1) {
        virtuoso::engine::AgentIntentNote sw;
        sw.agent = "Drums";
        sw.channel = m_chDrums;
        sw.note = m_noteSnareHit;
        sw.baseVelocity = 34;
        sw.startPos = GrooveGrid::fromBarBeatTuplet(playbackBarIndex, beatInBar, 0, 1, ts);
        sw.durationWhole = Rational(1, 16);
        sw.structural = true; // treat as landmark
        m_engine.scheduleNote(sw);
    }

    // Feather kick on downbeats (light, deterministic per bar/beat via humanizer seed)
    if (beatInBar == 0) {
        virtuoso::engine::AgentIntentNote k;
        k.agent = "Drums";
        k.channel = m_chDrums;
        k.note = m_noteKick;
        k.baseVelocity = structural ? 22 : 16;
        k.startPos = GrooveGrid::fromBarBeatTuplet(playbackBarIndex, beatInBar, 0, 1, ts);
        k.durationWhole = Rational(1, 16);
        k.structural = structural;
        m_engine.scheduleNote(k);
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

