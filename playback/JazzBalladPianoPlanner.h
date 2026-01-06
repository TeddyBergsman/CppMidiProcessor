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

        // Reference tuning knobs (Chet Baker – My Funny Valentine: sparse, airy, gentle).
        // Ranges are MIDI note numbers.
        int lhLo = 50, lhHi = 66;      // guide tones
        int rhLo = 67, rhHi = 84;      // main color tones
        int sparkleLo = 84, sparkleHi = 96; // optional top sparkle

        double skipBeat2ProbStable = 0.45;   // if chord is stable, often skip beat 2
        double addSecondColorProb = 0.25;    // add a second color tone sometimes
        double sparkleProbBeat4 = 0.18;      // occasional high sparkle on beat 4
        bool preferShells = true;            // favor shells over thicker rootless

        // Listening MVP (optional): comping space + interaction.
        bool userDensityHigh = false;
        bool userIntensityPeak = false;
        bool userRegisterHigh = false;
        bool userSilence = false;

        // Macro dynamics / debug forcing
        bool forceClimax = false;
        double energy = 0.25; // 0..1
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

