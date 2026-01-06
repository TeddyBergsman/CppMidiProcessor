#pragma once

#include <QString>
#include <QVector>

#include "music/ChordSymbol.h"
#include "virtuoso/constraints/BassDriver.h"
#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/vocab/VocabularyRegistry.h"

namespace playback {

// Deterministic two-feel bass planner with basic approach-tone logic.
// State: lastFret (via PerformanceState) + last chosen midi note.
class JazzBalladBassPlanner {
public:
    struct Context {
        int bpm = 60;
        int playbackBarIndex = 0; // timeline bar (not chart bar index)
        int beatInBar = 0;
        bool chordIsNew = false;
        music::ChordSymbol chord;
        music::ChordSymbol nextChord; // may be empty/unset if unknown
        bool hasNextChord = false;
        QString chordText; // for explainability

        // Deterministic stylistic shaping (tuned per reference).
        quint32 determinismSeed = 1;
        double approachProbBeat3 = 0.55;      // probability of chromatic approach into next bar when it changes
        double skipBeat3ProbStable = 0.25;    // when harmony is stable, sometimes omit beat 3 (more space)
        bool allowApproachFromAbove = true;   // allow +1 approach as well as -1

        // Listening MVP (optional): used to simplify or support interaction.
        bool userDensityHigh = false;
        bool userIntensityPeak = false;
        bool userSilence = false;

        // Macro dynamics / debug forcing
        bool forceClimax = false;
        double energy = 0.25; // 0..1

        // Stage 3 solver weights (Virtuosity Matrix-style, 0..1).
        double harmonicRisk = 0.20;
        double rhythmicComplexity = 0.25;
        double interaction = 0.50;
        double toneDark = 0.60;

        // Optional Stage 2 context (for smarter choices).
        QString chordFunction; // "Tonic" | "Subdominant" | "Dominant" | "Other"
        QString roman;         // e.g. "V7"
    };

    JazzBalladBassPlanner();

    void reset();

    void setVocabulary(const virtuoso::vocab::VocabularyRegistry* vocab) { m_vocab = vocab; }

    // Returns 0..N intent notes to schedule at this beat.
    QVector<virtuoso::engine::AgentIntentNote> planBeat(const Context& c,
                                                        int midiChannel,
                                                        const virtuoso::groove::TimeSignature& ts);

private:
    static int pcToBassMidiInRange(int pc, int lo, int hi);
    static int clampMidi(int m) { return (m < 0) ? 0 : (m > 127 ? 127 : m); }
    static int chooseApproachMidi(int nextRootMidi, int lastMidi);
    static int parseFretFromReasonLine(const QString& s);

    bool feasibleOrRepair(int& midi);

    virtuoso::constraints::BassDriver m_driver;
    virtuoso::constraints::PerformanceState m_state;
    int m_lastMidi = -1;
    const virtuoso::vocab::VocabularyRegistry* m_vocab = nullptr; // not owned
};

} // namespace playback

