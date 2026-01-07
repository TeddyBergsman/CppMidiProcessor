#pragma once

#include <QString>
#include <QVector>

#include "music/ChordSymbol.h"
#include "playback/HarmonyTypes.h"

namespace chart { struct ChartModel; }

namespace playback {

class HarmonyContext;

struct LookaheadWindow {
    int startStep = 0;
    int horizonBars = 8;
    int beatsPerBar = 4;

    // Current-step derived facts
    bool haveCurrentChord = false;
    music::ChordSymbol currentChord;
    bool chordIsNew = false;

    bool haveNextChord = false;
    music::ChordSymbol nextChord;
    int beatsUntilChange = 0;
    bool nextChanges = false;

    LocalKeyEstimate key;
    QString keyCenterStr;
    QString roman;
    QString chordFunction;

    // Modulation heuristic over the horizon.
    bool modulationLikely = false;
    int modulationTargetTonicPc = -1;

    // Phrase/cadence heuristics
    int phraseBars = 4;
    int barInPhrase = 0;
    bool phraseEndBar = false;
    double cadence01 = 0.0;
};

// Computes a canonical sliding-window lookahead snapshot for runtime scheduling.
LookaheadWindow buildLookaheadWindow(const chart::ChartModel& model,
                                     const QVector<int>& sequence,
                                     int repeats,
                                     int stepNow,
                                     int horizonBars,
                                     int keyWindowBars,
                                     HarmonyContext& harmony);

} // namespace playback

