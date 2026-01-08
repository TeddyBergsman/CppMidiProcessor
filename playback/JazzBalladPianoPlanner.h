#pragma once

#include <QString>
#include <QVector>

#include "music/ChordSymbol.h"
#include "virtuoso/constraints/PianoDriver.h"
#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/groove/GrooveGrid.h"
#include "virtuoso/theory/FunctionalHarmony.h"
#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/memory/MotivicMemory.h"
#include "virtuoso/constraints/ConstraintsTypes.h"
#include "virtuoso/piano/PianoPerformancePlan.h"
#include "virtuoso/control/PerformanceWeightsV2.h"
#include "virtuoso/vocab/VocabularyRegistry.h"

namespace playback {

/**
 * JazzBalladPianoPlanner - "Bill Evans" Profile (Enhanced)
 *
 * A deterministic piano comping planner for jazz ballad style, implementing the
 * Virtuoso framework's Constraint Satisfaction Architecture.
 *
 * Key behaviors (per product spec):
 * - Voicing: Rootless Type A (3-5-7-9) & Type B (7-9-3-5), Shells (3-7) for sparse moments
 * - Extensions: Always extend to 9ths/13ths, Upper Structure Triads on dominants
 * - Density: Sparse by default, reacting to user activity
 * - Voice Leading: Minimal motion (nearest pitch-class to previous voicing)
 * - Pedaling: Half-pedal default, repedal on chord changes
 * - Interaction: Shells when user active, fills when user silent
 *
 * ENHANCED FEATURES:
 * - Phrase-level vocabulary patterns (rhythm cells from VocabularyRegistry)
 * - Full 10-weight integration with semantic mappings
 * - Microtime anticipation/delay for human feel
 * - Bass/piano register coordination (avoid collision)
 * - Gesture support (rolls/arps at cadences)
 */
class JazzBalladPianoPlanner {
public:
    // State for snapshot/restore (used by phrase planner and lookahead)
    struct PlannerState {
        QVector<int> lastVoicingMidi;    // last realized MIDI notes
        int lastTopMidi = -1;            // for RH continuity
        QString lastVoicingKey;          // ontology key of last voicing used

        // Phrase-level state for vocabulary coherence
        QString currentPhraseId;         // current phrase pattern ID
        int phraseStartBar = -1;         // bar where phrase pattern started

        // Constraints state (needed by JointCandidateModel for feasibility evaluation)
        virtuoso::constraints::PerformanceState perf;
    };

    struct CcIntent {
        int cc = 64;
        int value = 0;
        virtuoso::groove::GridPos startPos;
        bool structural = false;
        QString logic_tag;
    };

    struct BeatPlan {
        QVector<virtuoso::engine::AgentIntentNote> notes;
        QVector<CcIntent> ccs;
        virtuoso::piano::PianoPerformancePlan performance;
        QString chosenVoicingKey;   // ontology voicing key
        QString chosenScaleKey;     // ontology scale key
        QString chosenScaleName;    // scale display name
        QString motifSourceAgent;
        QString motifTransform;
    };

    struct Context {
        int bpm = 60;
        int playbackBarIndex = 0;
        int beatInBar = 0;
        bool chordIsNew = false;
        music::ChordSymbol chord;
        QString chordText;
        quint32 determinismSeed = 1;

        // Register windows (MIDI note numbers)
        int lhLo = 48, lhHi = 64;       // left hand (guide tones / shells)
        int rhLo = 65, rhHi = 84;       // right hand (color tones)
        int sparkleLo = 84, sparkleHi = 96; // optional high sparkle

        // Probability knobs (defaults for Bill Evans ballad profile)
        double skipBeat2ProbStable = 0.45;
        double addSecondColorProb = 0.25;
        double sparkleProbBeat4 = 0.18;
        bool preferShells = true;

        // User interaction state
        bool userDensityHigh = false;
        bool userIntensityPeak = false;
        bool userRegisterHigh = false;
        bool userSilence = false;
        bool userBusy = false;

        // Macro dynamics
        bool forceClimax = false;
        double energy = 0.25;           // 0..1

        // Phrase context
        int phraseBars = 4;
        int barInPhrase = 0;
        bool phraseEndBar = false;
        double cadence01 = 0.0;

        // Key context
        bool hasKey = false;
        int keyTonicPc = 0;
        virtuoso::theory::KeyMode keyMode = virtuoso::theory::KeyMode::Major;

        // Lookahead
        music::ChordSymbol nextChord;
        bool hasNextChord = false;
        bool nextChanges = false;
        int beatsUntilChordChange = 0;

        // Performance weights (negotiated) - ALL 10 weights
        virtuoso::control::PerformanceWeightsV2 weights{};

        // Functional harmony context
        QString chordFunction;  // "Tonic" | "Subdominant" | "Dominant" | "Other"
        QString roman;          // e.g. "V7", "iiø7"

        // *** NEW: Bass coordination ***
        int bassRegisterHi = 55;        // highest note bass might play (for spacing)
        double bassActivity = 0.5;      // how active bass is this beat (0..1)
        bool bassPlayingThisBeat = false;
    };

    JazzBalladPianoPlanner();

    void reset();
    PlannerState snapshotState() const;
    void restoreState(const PlannerState& s);

    void setVocabulary(const virtuoso::vocab::VocabularyRegistry* vocab) { m_vocab = vocab; }
    void setOntology(const virtuoso::ontology::OntologyRegistry* ont) { m_ont = ont; }
    void setMotivicMemory(const virtuoso::memory::MotivicMemory* mem) { m_mem = mem; }

    QVector<virtuoso::engine::AgentIntentNote> planBeat(const Context& c,
                                                        int midiChannel,
                                                        const virtuoso::groove::TimeSignature& ts);

    BeatPlan planBeatWithActions(const Context& c,
                                 int midiChannel,
                                 const virtuoso::groove::TimeSignature& ts);

private:
    // ============= Voicing Generation =============

    // Core voicing types from ontology
    enum class VoicingType {
        RootlessA,      // 3-5-7-9
        RootlessB,      // 7-9-3-5
        Shell,          // 3-7
        GuideTones,     // 3-7 (same as shell but explicit)
        Quartal,        // stacked 4ths
        UST             // Upper Structure Triad
    };

    enum class VoicingDensity {
        Guide,  // minimal: shells only
        Full    // extended: shells + colors
    };

    struct Voicing {
        VoicingType type = VoicingType::Shell;
        VoicingDensity density = VoicingDensity::Full;
        QVector<int> pcs;           // pitch classes (0..11)
        QVector<int> midiNotes;     // realized MIDI notes
        QString ontologyKey;
        double cost = 0.0;          // voice-leading cost
    };

    // Generate candidate voicings for a chord
    QVector<Voicing> generateVoicingCandidates(const Context& c, VoicingDensity density) const;

    // Realize pitch classes to MIDI notes within register
    QVector<int> realizePcsToMidi(const QVector<int>& pcs, int lo, int hi,
                                  const QVector<int>& prevVoicing) const;

    // Voice-leading cost (sum of semitone distances, penalize parallel motion)
    double voiceLeadingCost(const QVector<int>& prev, const QVector<int>& next) const;

    // Check feasibility with PianoDriver constraints
    bool isFeasible(const QVector<int>& midiNotes) const;

    // Repair voicing to satisfy constraints
    QVector<int> repairVoicing(QVector<int> midi) const;

    // ============= Chord/Scale Helpers =============

    static int pcForDegree(const music::ChordSymbol& c, int degree);
    static int thirdInterval(music::ChordQuality q);
    static int fifthInterval(music::ChordQuality q);
    static int seventhInterval(const music::ChordSymbol& c);

    // Nearest MIDI note for a pitch class within bounds
    static int nearestMidiForPc(int pc, int around, int lo, int hi);

    // ============= Vocabulary-Driven Rhythm =============

    // Query vocabulary for this beat's rhythm pattern
    struct VocabRhythmHit {
        int sub = 0;
        int count = 1;
        int durNum = 1;
        int durDen = 4;
        int velDelta = 0;
        VoicingDensity density = VoicingDensity::Full;
    };

    // Check if vocabulary is loaded
    bool hasVocabularyLoaded() const;

    // Get rhythm hits from vocabulary (if available)
    QVector<VocabRhythmHit> queryVocabularyHits(const Context& c) const;

    // Fallback: should we play on this beat? (deterministic from context)
    bool shouldPlayBeatFallback(const Context& c, quint32 hash) const;

    // ============= Weight Integration =============

    // Map all 10 weights to concrete piano decisions
    struct WeightMappings {
        // From weights
        double playProbMod = 1.0;      // density/rhythm -> attack probability
        double velocityMod = 1.0;      // intensity -> velocity scaling
        double durationMod = 1.0;      // warmth -> duration scaling
        double registerShiftMod = 0.0; // warmth -> register down (semitones)
        double voicingFullnessMod = 1.0; // dynamism -> prefer fuller voicings
        double creativityMod = 0.0;    // creativity -> prefer UST/alterations
        double tensionMod = 0.0;       // tension -> add upper extensions
        double interactivityMod = 1.0; // interactivity -> response to user
        double variabilityMod = 1.0;   // variability -> voicing variety
        double rubatoPushMs = 0;       // emotion -> timing offset
    };

    WeightMappings computeWeightMappings(const Context& c) const;

    // ============= Microtime / Humanization =============

    // Compute timing offset (push/pull) in milliseconds
    int computeTimingOffsetMs(const Context& c, quint32 hash) const;

    // Apply timing offset to a GridPos
    virtuoso::groove::GridPos applyTimingOffset(const virtuoso::groove::GridPos& pos,
                                                int offsetMs, int bpm,
                                                const virtuoso::groove::TimeSignature& ts) const;

    // ============= Pedal Logic =============

    // Generate pedal CC events for this beat
    QVector<CcIntent> planPedal(const Context& c,
                                const virtuoso::groove::TimeSignature& ts) const;

    // ============= Gesture Support =============

    // Choose and apply gesture (roll/arp) if appropriate
    void applyGesture(const Context& c, QVector<virtuoso::engine::AgentIntentNote>& notes,
                      const virtuoso::groove::TimeSignature& ts) const;

    // ============= Register Coordination =============

    // Adjust register to avoid bass collision
    void adjustRegisterForBass(Context& c) const;

    // ============= State =============

    virtuoso::constraints::PianoDriver m_driver;
    PlannerState m_state;

    const virtuoso::ontology::OntologyRegistry* m_ont = nullptr;
    const virtuoso::memory::MotivicMemory* m_mem = nullptr;
    const virtuoso::vocab::VocabularyRegistry* m_vocab = nullptr;
};

} // namespace playback
