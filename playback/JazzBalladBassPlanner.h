#pragma once

#include <QString>
#include <QVector>

#include "music/ChordSymbol.h"
#include "virtuoso/constraints/BassDriver.h"
#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/control/PerformanceWeightsV2.h"
#include "virtuoso/vocab/VocabularyRegistry.h"

namespace playback {

// Deterministic two-feel bass planner with basic approach-tone logic.
// State: lastFret (via PerformanceState) + last chosen midi note.
class JazzBalladBassPlanner {
public:
    struct PlannerState {
        virtuoso::constraints::PerformanceState perf;
        int lastMidi = -1;
        int walkPosBlockStartBar = -1;
        int walkPosMidi = -1;

        // Articulation latch state (store as ints to keep this POD-ish).
        bool artInit = false;
        int art = 0; // 0=Sustain, 1=PalmMute
        int lastArtBar = -1;
        bool haveSentArt = false;
        int sentArt = 0; // 0=Sustain, 1=PalmMute

        int prevMidiBeforeLast = -1;
    };

    struct KeySwitchIntent {
        int midi = -1;
        virtuoso::groove::GridPos startPos;
        QString logic_tag;
        int leadMs = 18;  // when to press before the beat
        int holdMs = 60;  // how long to hold keyswitch
    };

    struct BeatPlan {
        QVector<virtuoso::engine::AgentIntentNote> notes;
        QVector<KeySwitchIntent> keyswitches;
        QVector<virtuoso::engine::AgentIntentNote> fxNotes; // library FX notes (not constrained by BassDriver)
        int desiredArtKeyswitchMidi = -1; // Sustain vs PalmMute keyswitch MIDI
        // Ontology-first: explicit key for the harmonic substrate used.
        QString chosenScaleKey; // ontology scale key (e.g. "mixolydian")
    };

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
        double energy = 0.12; // 0..1 (start very low, 12%)

        // Phrase model (lightweight, deterministic): 4-bar phrases by default.
        int phraseBars = 4;
        int barInPhrase = 0;     // 0..phraseBars-1
        bool phraseEndBar = false;
        double cadence01 = 0.0;  // 0..1

        // Long-horizon register arc target (center MIDI note). This is NOT a hard lane:
        // the planner may deviate for voice-leading, but it biases the phrase-level motion.
        int registerCenterMidi = 45;

        // Global weights v2 (0..1) negotiated for this agent, plus any local shaping.
        virtuoso::control::PerformanceWeightsV2 weights{};

        // Optional Stage 2 context (for smarter choices).
        QString chordFunction; // "Tonic" | "Subdominant" | "Dominant" | "Other"
        QString roman;         // e.g. "V7"
    };

    JazzBalladBassPlanner();

    void reset();

    void setVocabulary(const virtuoso::vocab::VocabularyRegistry* vocab) { m_vocab = vocab; }

    PlannerState snapshotState() const;
    void restoreState(const PlannerState& s);

    // Returns 0..N intent notes to schedule at this beat.
    QVector<virtuoso::engine::AgentIntentNote> planBeat(const Context& c,
                                                        int midiChannel,
                                                        const virtuoso::groove::TimeSignature& ts);

    BeatPlan planBeatWithActions(const Context& c,
                                 int midiChannel,
                                 const virtuoso::groove::TimeSignature& ts);

private:
    static int pcToBassMidiInRange(int pc, int lo, int hi);
    static int clampMidi(int m) { return (m < 0) ? 0 : (m > 127 ? 127 : m); }
    static int chooseApproachMidi(int nextRootMidi, int lastMidi);

    bool feasibleOrRepair(int& midi);
    int chooseApproachMidiWithConstraints(int nextRootMidi, QString* outChoiceId = nullptr) const;

    virtuoso::constraints::BassDriver m_driver;
    virtuoso::constraints::PerformanceState m_state;
    int m_lastMidi = -1;
    int m_walkPosBlockStartBar = -1; // 2-bar block anchor for register/position
    int m_walkPosMidi = -1;
    const virtuoso::vocab::VocabularyRegistry* m_vocab = nullptr; // not owned

    // Embodiment: Ample Upright articulation state (keyswitch lanes).
    enum class Articulation { Sustain, PalmMute };
    bool m_artInit = false;
    Articulation m_art = Articulation::Sustain;
    int m_lastArtBar = -1;
    bool m_haveSentArt = false;
    Articulation m_sentArt = Articulation::Sustain;

    // For legato-technique decisions (HP/LegatoSlide): previous note context.
    int m_prevMidiBeforeLast = -1;
};

} // namespace playback

