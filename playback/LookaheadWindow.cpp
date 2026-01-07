#include "playback/LookaheadWindow.h"

#include "playback/HarmonyContext.h"
#include "virtuoso/groove/GrooveGrid.h"
#include "virtuoso/theory/FunctionalHarmony.h"

#include <QtGlobal>

namespace playback {
namespace {

static int normalizePcLocal(int pc) {
    int v = pc % 12;
    if (v < 0) v += 12;
    return v;
}

} // namespace

// NOTE: This is implemented as a free function in this TU to keep call-sites simple.
// It computes a *single* lookahead snapshot (used by runtime scheduling).
LookaheadWindow buildLookaheadWindow(const chart::ChartModel& model,
                                     const QVector<int>& sequence,
                                     int repeats,
                                     int stepNow,
                                     int horizonBars,
                                     int phraseBars,
                                     int keyWindowBars,
                                     HarmonyContext& harmony) {
    LookaheadWindow w;
    if (sequence.isEmpty()) return w;

    virtuoso::groove::TimeSignature ts{4, 4};
    ts.num = (model.timeSigNum > 0) ? model.timeSigNum : 4;
    ts.den = (model.timeSigDen > 0) ? model.timeSigDen : 4;

    const int beatsPerBar = qMax(1, ts.num);
    w.beatsPerBar = beatsPerBar;
    w.horizonBars = qMax(1, horizonBars);
    const int seqLen = sequence.size();
    const int total = seqLen * qMax(1, repeats);

    if (stepNow < 0) stepNow = 0;
    w.startStep = qMin(stepNow, qMax(0, total - 1));
    const int endStep = qMin(total, w.startStep + w.horizonBars * beatsPerBar);

    // Phrase model: adaptive 4–8 bars (provided by caller).
    w.phraseBars = qBound(4, phraseBars, 8);
    const int playbackBarIndex = w.startStep / beatsPerBar;
    w.barInPhrase = (w.phraseBars > 0) ? (qMax(0, playbackBarIndex) % w.phraseBars) : 0;
    w.phraseEndBar = (w.phraseBars > 0) ? (w.barInPhrase == (w.phraseBars - 1)) : false;
    const bool phraseSetupBar = (w.phraseBars > 1) ? (w.barInPhrase == (w.phraseBars - 2)) : false;

    // Current chord (mutating harmony state is OK for runtime; this is the single source of chord truth).
    {
        const int cellIndex = sequence[w.startStep % seqLen];
        w.haveCurrentChord = harmony.chordForCellIndex(model, cellIndex, w.currentChord, w.chordIsNew);
    }

    // Next chord boundary: scan for explicit changes within the current bar, fallback to next barline.
    w.haveNextChord = false;
    w.beatsUntilChange = 0;
    if (w.haveCurrentChord) {
        const int beatInBar = w.startStep % beatsPerBar;
        const int maxLook = qMax(1, beatsPerBar - beatInBar);
        for (int k = 1; k <= maxLook; ++k) {
            const int stepFwd = w.startStep + k;
            if (stepFwd >= total) break;
            const int cellNext = sequence[stepFwd % seqLen];
            bool explicitNext = false;
            const music::ChordSymbol cand = harmony.parseCellChordNoState(model, cellNext, w.currentChord, &explicitNext);
            if (!explicitNext || cand.noChord) continue;
            if (!HarmonyContext::sameChordKey(cand, w.currentChord)) {
                w.nextChord = cand;
                w.haveNextChord = true;
                w.beatsUntilChange = k;
                break;
            }
        }
        if (!w.haveNextChord) {
            const int stepNextBar = w.startStep + (beatsPerBar - beatInBar);
            if (stepNextBar < total) {
                const int cellNext = sequence[stepNextBar % seqLen];
                bool explicitNext = false;
                w.nextChord = harmony.parseCellChordNoState(model, cellNext, w.currentChord, &explicitNext);
                w.haveNextChord = explicitNext || (w.nextChord.rootPc >= 0);
                if (w.nextChord.noChord) w.haveNextChord = false;
                w.beatsUntilChange = beatsPerBar - beatInBar;
            }
        }
    }

    w.nextChanges = w.haveNextChord && !w.nextChord.noChord && (w.nextChord.rootPc >= 0) &&
                    ((w.nextChord.rootPc != w.currentChord.rootPc) || (w.nextChord.bassPc != w.currentChord.bassPc));

    // Sliding-window key estimate for this bar.
    const int barIdx = (sequence[w.startStep % seqLen]) / 4;
    w.key = harmony.estimateLocalKeyWindow(model, barIdx, qMax(1, keyWindowBars));
    const int keyPc = harmony.hasKeyPcGuess() ? w.key.tonicPc : normalizePcLocal(w.currentChord.rootPc);
    w.keyCenterStr = QString("%1 %2")
                         .arg(HarmonyContext::pcName(keyPc))
                         .arg(w.key.scaleName.isEmpty() ? QString("Ionian (Major)") : w.key.scaleName);

    // Functional harmony tagging (roman/function) in the current key window.
    {
        const auto* def = harmony.chordDefForSymbol(w.currentChord);
        if (def && w.currentChord.rootPc >= 0) {
            const auto h = virtuoso::theory::analyzeChordInKey(keyPc, w.key.mode, w.currentChord.rootPc, *def);
            w.roman = h.roman;
            w.chordFunction = h.function;
        }
    }

    // Cadence heuristic: phrase end/setup with "nextChanges" boost + functional cadence boost.
    w.cadence01 = 0.0;
    if (w.phraseEndBar) w.cadence01 = (w.nextChanges || w.chordIsNew) ? 1.0 : 0.65;
    else if (phraseSetupBar) w.cadence01 = (w.nextChanges ? 0.60 : 0.35);

    // If we can see a Dominant->Tonic move soon, strengthen cadence.
    if (w.haveNextChord && w.beatsUntilChange > 0 && w.beatsUntilChange <= 2) {
        const auto* defN = harmony.chordDefForSymbol(w.nextChord);
        if (defN && w.nextChord.rootPc >= 0) {
            const auto hn = virtuoso::theory::analyzeChordInKey(keyPc, w.key.mode, w.nextChord.rootPc, *defN);
            if (w.chordFunction == "Dominant" && hn.function == "Tonic") {
                w.cadence01 = qMax(w.cadence01, 1.0);
            }
        }
    }

    // Modulation detection (lightweight): compare current tonic to a mid-horizon tonic estimate.
    {
        const int midBar = qMin(barIdx + qMax(1, w.horizonBars / 2), barIdx + w.horizonBars - 1);
        const LocalKeyEstimate fut = harmony.estimateLocalKeyWindow(model, midBar, qMax(1, keyWindowBars));
        if (fut.coverage >= 0.60 && fut.score >= 0.40 && fut.tonicPc != w.key.tonicPc) {
            w.modulationLikely = true;
            w.modulationTargetTonicPc = fut.tonicPc;
        }
    }

    (void)endStep;
    return w;
}

} // namespace playback

