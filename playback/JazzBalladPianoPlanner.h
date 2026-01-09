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
        QVector<int> lastVoicingMidi;    // last realized MIDI notes (combined)
        int lastTopMidi = -1;            // for RH continuity
        QString lastVoicingKey;          // ontology key of last voicing used

        // Phrase-level state for vocabulary coherence
        QString currentPhraseId;         // current phrase pattern ID
        int phraseStartBar = -1;         // bar where phrase pattern started

        // Constraints state (needed by JointCandidateModel for feasibility evaluation)
        virtuoso::constraints::PerformanceState perf;
        
        // ========== NEW: Separate LH/RH state for Bill Evans style ==========
        QVector<int> lastLhMidi;         // LH rootless voicing (3-4 notes)
        QVector<int> lastRhMidi;         // RH melodic dyad/triad (2-3 notes)
        int lastRhTopMidi = 74;          // RH melodic line top note tracking
        int lastRhSecondMidi = 69;       // RH second voice for melodic dyads
        bool lastLhWasTypeA = true;      // Alternate Type A/B for voice-leading
        int rhMelodicDirection = 0;      // -1 descending, 0 neutral, +1 ascending
        int rhMotionsThisChord = 0;      // Count of RH melodic movements on current chord
        music::ChordSymbol lastChordForRh; // Track when chord changes for RH reset
        
        // ========== PHRASE-LEVEL PLANNING ==========
        // Tracks melodic arcs, motifs, and phrase-level intent across multiple bars
        int lastPhraseStartBar = -1;     // Bar index when current phrase started
        int phraseArcPhase = 0;          // 0=building, 1=peak, 2=resolving
        int phraseTargetMidi = 76;       // The note we're building toward (phrase peak)
        int phraseResolveMidi = 72;      // The note we resolve to at phrase end
        QVector<int> phraseMotifPcs;     // 2-3 pitch classes of our motif (relative to chord)
        int phraseMotifStartDegree = 5;  // Starting degree of motif (3, 5, 7, 9, etc.)
        bool phraseMotifAscending = true;// Direction of original motif
        int phraseMotifVariation = 0;    // 0=original, 1=transposed, 2=inverted, 3=rhythmic
        
        // ========== REGISTER VARIETY ==========
        // Tracks register usage over time to ensure variety and musical contour
        int recentRegisterSum = 0;       // Running sum of recent MIDI notes (for average)
        int recentRegisterCount = 0;     // Count for averaging
        int preferredRegisterOffset = 0; // Offset to apply to target register (-6 to +6)
        int barsInCurrentRegister = 0;   // How long we've been in current register zone
        bool lastPhraseWasHigh = false;  // Alternate between high/low phrase peaks
        
        // ========== CALL-AND-RESPONSE ==========
        // Tracks user activity for interactive fill/response behavior
        bool userWasBusy = false;        // Was user playing on the previous beat?
        int responseWindowBeats = 0;     // Beats remaining in response window
        bool inResponseMode = false;     // Currently responding to user?
        int userLastRegisterHigh = 72;   // Last high note user played
        int userLastRegisterLow = 60;    // Last low note user played
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
        int userMeanMidi = 72;           // Mean MIDI note of recent user activity
        int userHighMidi = 84;           // Highest recent user note
        int userLowMidi = 60;            // Lowest recent user note

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
        RootlessA,      // 3-5-7-9 (Bill Evans Type A)
        RootlessB,      // 7-9-3-5 (Bill Evans Type B)
        Shell,          // 3-7 guide tones
        GuideTones,     // 3-7 (same as shell but explicit)
        Quartal,        // stacked 4ths (McCoy Tyner)
        UST,            // Upper Structure Triad
        Drop2,          // Drop 2 voicing (2nd from top drops octave)
        Cluster,        // Close position with seconds (modern)
        Spread,         // Wide intervals (ballad texture)
        Block           // George Shearing style locked hands
    };

    enum class VoicingDensity {
        Sparse,   // minimal: 2 notes (shells)
        Guide,    // 2-3 notes (guide tones)
        Medium,   // 3-4 notes (typical comping)
        Full,     // 4-5 notes (richer texture)
        Lush      // 5+ notes (climax moments)
    };

    struct Voicing {
        VoicingType type = VoicingType::Shell;
        VoicingDensity density = VoicingDensity::Full;
        QVector<int> pcs;           // pitch classes (0..11)
        QVector<int> midiNotes;     // realized MIDI notes
        QString ontologyKey;
        double cost = 0.0;          // voice-leading cost
        bool avoidsSlashBass = false; // true if slash bass PC was filtered out
        int topNotePc = -1;         // preferred top note PC for melodic continuity
        int topNoteMidi = -1;       // realized top note MIDI
    };

    // Generate candidate voicings for a chord
    QVector<Voicing> generateVoicingCandidates(const Context& c, VoicingDensity density) const;

    // Context-aware density: considers phrase position, energy, cadence
    VoicingDensity computeContextDensity(const Context& c) const;
    
    // ========== NEW: Separate LH/RH Bill Evans Style Generation ==========
    
    // Left Hand: Rootless voicing (Type A or Type B)
    // Type A: 3-5-7-9 (when chord root is in lower half of cycle)
    // Type B: 7-9-3-5 (when chord root is in upper half of cycle)
    // Returns 3-4 notes in register 48-68
    struct LhVoicing {
        QVector<int> midiNotes;
        bool isTypeA = true;
        QString ontologyKey;
        double cost = 0.0;
    };
    LhVoicing generateLhRootlessVoicing(const Context& c) const;
    
    // Right Hand: Melodic dyads/triads for color and movement
    // Based on chord extensions (9, 11, 13) and guide tones
    // Creates stepwise melodic motion in top voice
    // Returns 2-3 notes in register 69-88
    struct RhMelodic {
        QVector<int> midiNotes;
        int topNoteMidi = -1;        // The melodic line note
        int melodicDirection = 0;    // -1=down, 0=hold, +1=up
        QString ontologyKey;         // e.g., "RH_Dyad_37", "RH_Triad_UST"
        bool isColorTone = false;    // Uses extensions (9/11/13)?
    };
    RhMelodic generateRhMelodicVoicing(const Context& c, int targetTopMidi) const;
    
    // ========== UPPER STRUCTURE TRIADS (Bill Evans signature) ==========
    // A simple triad played in the RH that creates sophisticated extensions
    // Examples:
    //   C7  + D major triad  → creates 9, #11, 13
    //   Dm7 + F major triad  → creates b3, 5, b7
    //   G7alt + Ab major triad → creates b9, #11, b13
    
    struct UpperStructureTriad {
        int rootPc;           // Root of the triad (0-11)
        bool isMajor;         // true = major triad, false = minor triad
        double tensionLevel;  // How much tension this UST adds (0.0 = safe, 1.0 = very tense)
        QString colorDescription; // e.g., "9-#11-13", "b9-#11-b13"
    };
    
    // Get available UST options for a chord (returns candidates sorted by consonance)
    QVector<UpperStructureTriad> getUpperStructureTriads(const music::ChordSymbol& chord) const;
    
    // Build a UST voicing in the RH register
    RhMelodic buildUstVoicing(const Context& c, const UpperStructureTriad& ust) const;
    
    // ========== MELODIC FRAGMENTS (Lick Library) ==========
    // Pre-composed melodic gestures that sound pianistic and intentional.
    // These replace random chord-tone movement with beautiful phrases.
    //
    // Fragment types:
    //   - Approach: chromatic or diatonic lead-in to target
    //   - Enclosure: surround target from above and below
    //   - Arpeggio: broken chord figure
    //   - Turn: ornamental figure around a note
    //   - Scale Run: short scalar passage
    //   - Resolution: tension-to-resolution gesture
    
    enum class FragmentType {
        Approach,       // Single approach note to target
        DoubleApproach, // Two approach notes (e.g., chromatic from below)
        Enclosure,      // Above-below-target or below-above-target
        Turn,           // Target-above-target-below-target (ornament)
        ArpeggioUp,     // Broken chord ascending
        ArpeggioDown,   // Broken chord descending
        ScaleRun3,      // 3-note scale fragment
        ScaleRun4,      // 4-note scale fragment
        Resolution,     // Tension note resolving to chord tone
        Pedal,          // Repeated note with changing harmony
        Octave          // Octave displacement for drama
    };
    
    struct MelodicFragment {
        FragmentType type;
        QVector<int> intervalPattern;  // Intervals from target note (negative = below)
        QVector<double> rhythmPattern; // Duration multipliers (1.0 = normal)
        QVector<int> velocityPattern;  // Velocity deltas
        double tensionLevel;           // How "out" this fragment sounds (0-1)
        QString name;                  // For debugging/logging
    };
    
    // Get appropriate melodic fragments for current context
    QVector<MelodicFragment> getMelodicFragments(const Context& c, int targetPc) const;
    
    // Apply a melodic fragment starting from a target note
    // Returns the MIDI notes with timing offsets
    struct FragmentNote {
        int midiNote;
        int subBeatOffset;    // 0-3 for 16th note subdivisions
        double durationMult;  // Multiplier for base duration
        int velocityDelta;    // Adjustment to base velocity
    };
    QVector<FragmentNote> applyMelodicFragment(
        const Context& c, 
        const MelodicFragment& fragment,
        int targetMidi,
        int startSub) const;
    
    // Determine if LH should play this beat (sparse: beat 1, sometimes 3)
    bool shouldLhPlayBeat(const Context& c, quint32 hash) const;
    
    // Determine RH activity level (0-4 hits per beat based on context)
    int rhActivityLevel(const Context& c, quint32 hash) const;
    
    // Select next melodic target for RH (stepwise motion preferred)
    int selectNextRhMelodicTarget(const Context& c) const;

    // Realize pitch classes to MIDI notes within register, with melodic top note
    QVector<int> realizePcsToMidi(const QVector<int>& pcs, int lo, int hi,
                                  const QVector<int>& prevVoicing,
                                  int targetTopMidi = -1) const;

    // Realize a voicing template by stacking intervals properly (Bill Evans style)
    QVector<int> realizeVoicingTemplate(const QVector<int>& degrees,
                                        const music::ChordSymbol& chord,
                                        int bassMidi, int ceiling) const;

    // Ensure top note follows melodic principles (stepwise preferred, avoid large leaps)
    int selectMelodicTopNote(const QVector<int>& candidatePcs, int rhLo, int rhHi,
                              int lastTopMidi, const Context& c) const;

    // Voice-leading cost (comprehensive: motion, crossing, parallel, soprano)
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

    // Determine what chord degree a pitch class represents
    int getDegreeForPc(int pc, const music::ChordSymbol& chord) const;

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
    QVector<VocabRhythmHit> queryVocabularyHits(const Context& c, QString* outPhraseId = nullptr) const;

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
    
    // ============= Phrase-Level Planning =============
    
    // Compute the phrase arc phase (0=building, 1=peak, 2=resolving)
    int computePhraseArcPhase(const Context& c) const;
    
    // Get the target register for the current arc phase
    // Building: mid-register, gradually ascending
    // Peak: high register, maximum activity
    // Resolving: descending toward rest
    int getArcTargetMidi(const Context& c, int arcPhase) const;
    
    // Generate a new motif for the phrase (called at phrase start)
    void generatePhraseMotif(const Context& c);
    
    // Get the motif variation for current position in phrase
    // Returns: 0=original, 1=transposed up, 2=inverted, 3=transposed down
    int getMotifVariation(const Context& c) const;
    
    // Apply motif to create melodic targets (returns target PCs for RH)
    QVector<int> applyMotifToContext(const Context& c, int variation) const;
    
    // Get melodic direction based on arc phase
    // Building: ascending, Peak: sustained high, Resolving: descending
    int getArcMelodicDirection(int arcPhase, int barInPhase, int phraseBars) const;
    
    // ============= Register Variety =============
    
    // Update register tracking with a new note
    void updateRegisterTracking(int midiNote);
    
    // Get register adjustment based on recent usage and phrase context
    // Returns an offset (-6 to +6) to apply to target registers
    int computeRegisterVariety(const Context& c) const;
    
    // Decide if this phrase should peak high or low (alternates)
    bool shouldPhrasePeakHigh(const Context& c) const;
    
    // ============= Rhythmic Vocabulary =============
    
    // Rhythmic feel types for advanced patterns
    enum class RhythmicFeel {
        Straight,     // Standard 16th note grid
        Swing,        // Jazz swing feel (delayed "and")
        Triplet,      // Triplet-based patterns
        Hemiola,      // 3-against-4 polyrhythm
        Displaced     // Metric displacement (shifted 8th)
    };
    
    // Get appropriate rhythmic feel for current context
    RhythmicFeel chooseRhythmicFeel(const Context& c, quint32 hash) const;
    
    // Apply rhythmic feel to a subdivision position
    // Returns timing offset in milliseconds
    int applyRhythmicFeel(RhythmicFeel feel, int subdivision, int beatInBar, int bpm) const;
    
    // Generate triplet-based RH pattern (3 notes per beat)
    QVector<std::tuple<int, int, bool>> generateTripletPattern(const Context& c, int activity) const;
    
    // Generate hemiola pattern (3 notes across 2 beats)
    QVector<std::tuple<int, int, bool>> generateHemiolaPattern(const Context& c) const;
    
    // ============= Call-and-Response =============
    
    // Update response state based on user activity
    void updateResponseState(const Context& c);
    
    // Check if we should respond (user just stopped playing)
    bool shouldRespondToUser(const Context& c) const;
    
    // Get register for response (complement or echo user's register)
    int getResponseRegister(const Context& c, bool complement) const;
    
    // Boost activity level for response fills
    int getResponseActivityBoost(const Context& c) const;
    
    // ============= Texture Modes =============
    // Different playing modes for various musical situations
    
    enum class TextureMode {
        Comp,       // Standard comping: LH + minimal RH
        Fill,       // Fill mode: active RH melodic fills
        Solo,       // Solo mode: virtuosic RH with LH support
        Sparse,     // Ultra-sparse: shells only, minimal activity
        Lush        // Lush mode: full voicings, rich texture
    };
    
    // Determine appropriate texture mode from context
    TextureMode determineTextureMode(const Context& c) const;
    
    // Apply texture mode modifications
    void applyTextureMode(TextureMode mode, int& lhActivity, int& rhActivity, 
                          bool& preferDyads, bool& preferTriads) const;
    
    // ============= Style Presets =============
    // Different pianist styles with characteristic voicings and rhythms
    
    enum class PianistStyle {
        BillEvans,      // Introspective, quartal voicings, sparse but rich
        RussFreeman,    // West coast cool, melodic, bluesy touches
        OscarPeterson,  // Driving, virtuosic, block chords
        KeithJarrett,   // Gospel touches, singing lines, spontaneous
        Default         // Neutral balanced style
    };
    
    // Style profile data
    struct StyleProfile {
        double voicingSparseness = 0.5;   // 0=full, 1=sparse
        double rhythmicDrive = 0.5;       // 0=laid back, 1=driving
        double melodicFocus = 0.5;        // 0=chordal, 1=melodic
        double useQuartalVoicings = 0.0;  // 0-1 probability
        double useBlockChords = 0.0;      // 0-1 probability
        double bluesInfluence = 0.0;      // 0-1 blue notes
        double gospelTouches = 0.0;       // 0-1 gospel influence
        int preferredRegisterLow = 48;
        int preferredRegisterHigh = 84;
    };
    
    // Get profile for a style
    static StyleProfile getStyleProfile(PianistStyle style);
    
    // Current style (can be set externally)
    PianistStyle m_currentStyle = PianistStyle::BillEvans;
    
    // Apply style profile modifications
    void applyStyleProfile(const StyleProfile& profile, Context& c) const;

    // ============= State =============

    virtuoso::constraints::PianoDriver m_driver;
    PlannerState m_state;

    const virtuoso::ontology::OntologyRegistry* m_ont = nullptr;
    const virtuoso::memory::MotivicMemory* m_mem = nullptr;
    const virtuoso::vocab::VocabularyRegistry* m_vocab = nullptr;
};

} // namespace playback
