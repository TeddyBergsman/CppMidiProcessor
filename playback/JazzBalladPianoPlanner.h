#pragma once

#include <QString>
#include <QVector>

#include "music/ChordSymbol.h"
#include "virtuoso/constraints/PianoDriver.h"
#include "virtuoso/engine/VirtuosoEngine.h"

namespace playback {

// Deterministic ballad piano comp planner:
// - Rootless A/B + shells
// - Simple voice-leading via "nearest pitch class to previous voicing" heuristic
// - Constraint-gated by PianoDriver (polyphony + span)
class JazzBalladPianoPlanner {
public:
    struct Context {
        int bpm = 60;
        int playbackBarIndex = 0;
        int beatInBar = 0;
        bool chordIsNew = false;
        music::ChordSymbol chord;
        QString chordText;
        quint32 determinismSeed = 1;
    };

    JazzBalladPianoPlanner();

    void reset();

    QVector<virtuoso::engine::AgentIntentNote> planBeat(const Context& c,
                                                        int midiChannel,
                                                        const virtuoso::groove::TimeSignature& ts);

private:
    static int thirdIntervalForQuality(music::ChordQuality q);
    static int fifthIntervalForQuality(music::ChordQuality q);
    static int seventhIntervalFor(const music::ChordSymbol& c);

    static int pcForDegree(const music::ChordSymbol& c, int degree);
    static int nearestMidiForPc(int pc, int around, int lo, int hi);
    static int bestNearestToPrev(int pc, const QVector<int>& prev, int lo, int hi);

    static void sortUnique(QVector<int>& v);
    static QVector<int> makeRootlessA(const music::ChordSymbol& c);
    static QVector<int> makeRootlessB(const music::ChordSymbol& c);
    static QVector<int> makeShell(const music::ChordSymbol& c);

    bool feasible(const QVector<int>& midiNotes) const;
    QVector<int> repairToFeasible(QVector<int> midiNotes) const;

    virtuoso::constraints::PianoDriver m_driver;
    QVector<int> m_lastVoicing;
};

} // namespace playback

