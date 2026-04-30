#pragma once

#include <QObject>
#include <QSet>
#include <QHash>
#include <QVector>
#include <QReadWriteLock>
#include <array>
#include <atomic>

#include "music/ChordSymbol.h"
#include "playback/HarmonyTypes.h"
#include "playback/ChordOntology.h"
#include "playback/PitchConformanceEngine.h"
#include "playback/GlissandoProcessor.h"

class MidiProcessor;

namespace chart { class ChartModel; }
namespace virtuoso::ontology { class OntologyRegistry; }

namespace playback {

class HarmonyContext;

/**
 * ScaleSnapProcessor - MIDI processor for guitar note processing and harmony generation
 *
 * HARMONIC INTELLIGENCE ENGINE v3.0
 *
 * Listens to guitar MIDI input and outputs processed notes on dedicated channels:
 * - Lead mode (Off/Original/Conformed): Controls guitar note processing -> channel 1
 * - Harmony mode (Off/Single/PrePlanned/Voice): Generates harmony voices -> channels 12-15
 *
 * Lead and Harmony modes operate independently - both can be active simultaneously.
 *
 * When Lead mode is NOT Off, guitar passthrough to channel 1 in MidiProcessor is suppressed,
 * and this processor outputs the processed lead on channel 1 with vocal bend, conformance, etc.
 *
 * Pitch conformance uses a 4-tier system:
 * - T1: Chord tones (always valid)
 * - T2: Tensions (9th, 11th, 13th - valid with light gravity)
 * - T3: Scale tones (moderate gravity, avoid notes flagged)
 * - T4: Chromatic (strong gravity, must resolve)
 *
 * Conformance behaviors: ALLOW, SNAP, BEND, ANTICIPATE, DELAY
 */
class ScaleSnapProcessor : public QObject {
    Q_OBJECT

public:
    // Lead mode: controls how guitar notes are processed -> Channel 1 (or Channel 2 for VocalSync)
    enum class LeadMode {
        Off = 0,
        Original,            // Pass through original notes with vocal bend -> channel 1
        Conformed,           // Apply pitch conformance (gravity-based) -> channel 1
        VocalSync            // Output pitch target for AU plugin -> channel 2 (with glissando)
    };
    Q_ENUM(LeadMode)

    // Harmony mode: controls harmony generation -> Channels 12-15
    // Uses playback::HarmonyMode from HarmonyTypes.h for full mode set
    // This enum is for backwards compatibility with existing UI
    enum class HarmonyModeCompat {
        Off = 0,
        SmartThirds,         // Parallel motion - harmony follows lead direction (3rds/5ths)
        Contrary,            // Contrary motion - harmony moves opposite to lead direction
        Similar,             // Similar motion - same direction, different intervals (no perfect consonances)
        Oblique,             // Oblique motion - harmony holds pedal tone while lead moves
        Single,              // User-selected harmony type
        PrePlanned,          // Automatic phrase-based selection
        Voice                // Vocal MIDI as harmony source
    };
    Q_ENUM(HarmonyModeCompat)

    explicit ScaleSnapProcessor(QObject* parent = nullptr);
    ~ScaleSnapProcessor() override;

    // Dependencies (must be set before use)
    void setMidiProcessor(MidiProcessor* midi);
    void setHarmonyContext(HarmonyContext* harmony);
    void setOntology(const virtuoso::ontology::OntologyRegistry* ontology);
    void setChartModel(const chart::ChartModel* model);

    // Lead mode control
    LeadMode leadMode() const { return m_leadMode; }
    void setLeadMode(LeadMode mode);

    // Harmony mode control (uses HarmonyModeCompat for backward compatibility)
    HarmonyModeCompat harmonyModeCompat() const { return m_harmonyModeCompat; }
    void setHarmonyModeCompat(HarmonyModeCompat mode);

    // New harmony configuration (full control)
    const HarmonyConfig& harmonyConfig() const { return m_harmonyConfig; }
    void setHarmonyConfig(const HarmonyConfig& config);

    // Harmony type (when mode is Single)
    HarmonyType harmonyType() const { return m_harmonyConfig.singleType; }
    void setHarmonyType(HarmonyType type);

    // Voice count (1-4)
    int harmonyVoiceCount() const { return m_harmonyConfig.voiceCount; }
    void setHarmonyVoiceCount(int count);

    // Lead conformance gravity multiplier (0.0-2.0)
    float leadGravityMultiplier() const { return m_leadConfig.gravityMultiplier; }
    void setLeadGravityMultiplier(float multiplier);

    // Vocal bend control (applies pitch bend from voice Hz to all output)
    bool vocalBendEnabled() const { return m_vocalBendEnabled; }
    void setVocalBendEnabled(bool enabled);

    // Vocal vibrato range in cents (how much voice deviation affects pitch bend)
    double vocalVibratoRangeCents() const { return m_vocalVibratoRangeCents; }
    void setVocalVibratoRangeCents(double cents);

    // Vibrato correction: filters out DC offset, keeping only the oscillation
    bool vibratoCorrectionEnabled() const { return m_vibratoCorrectionEnabled; }
    void setVibratoCorrectionEnabled(bool enabled);

    // Octave guard: rejects sudden octave jumps from voice MIDI tracking errors.
    // Large pitch jumps (>9 semitones) must be stable for ~30ms before accepted.
    bool octaveGuardEnabled() const { return m_octaveGuardEnabled; }
    void setOctaveGuardEnabled(bool enabled);

    // Harmony vibrato: apply vocal vibrato pitch bend to harmony voices
    // When disabled (default), harmony voices stay at center pitch (no vibrato wobble)
    bool harmonyVibratoEnabled() const { return m_harmonyVibratoEnabled; }
    void setHarmonyVibratoEnabled(bool enabled);

    // Harmony humanization: add BPM-constrained timing offsets to harmony notes
    // Creates more natural, human-like timing variation between voices
    bool harmonyHumanizationEnabled() const { return m_harmonyHumanizationEnabled; }
    void setHarmonyHumanizationEnabled(bool enabled);

    // BPM for humanization timing calculations
    void setTempoBpm(int bpm);
    int tempoBpm() const { return m_tempoBpm; }

    // Voice sustain: keep notes sounding while singing (CC2 > threshold)
    bool voiceSustainEnabled() const { return m_voiceSustainEnabled; }
    void setVoiceSustainEnabled(bool enabled);

    // Sustain smoothing: hold sustain briefly after CC2 drops to survive short silences
    bool sustainSmoothingEnabled() const { return m_sustainSmoothingEnabled; }
    void setSustainSmoothingEnabled(bool enabled);
    int sustainSmoothingMs() const { return m_sustainSmoothingMs; }
    void setSustainSmoothingMs(int ms);

    // Release bend prevention: freeze guitar pitch bend on voice-sustained notes
    bool releaseBendPreventionEnabled() const { return m_releaseBendPreventionEnabled; }
    void setReleaseBendPreventionEnabled(bool enabled);

    // Voice sustain sensitivity: CC2 threshold for triggering/releasing sustain (lower = more sensitive)
    int voiceSustainThreshold() const { return m_voiceSustainThreshold; }
    void setVoiceSustainThreshold(int threshold);

    // Glissando control (VocalSync mode: smooth pitch transitions between guitar notes)
    const GlissandoProcessor::Config& glissandoConfig() const { return m_glissando.config(); }
    void setGlissandoEnabled(bool enabled);
    void setGlissandoRateStPerSec(float rate);
    void setGlissandoIntervalThresholdSt(float threshold);
    void setGlissandoCurveExponent(float exponent);

    // Harmony instrument range (constrains harmony notes to playable range)
    // Default is full MIDI range (0-127). Set to instrument-specific range.
    // Common ranges: Trumpet E3-C6 (52-84), Alto Sax Db3-Ab5 (49-80), Violin G3-E7 (55-100)
    int harmonyRangeMin() const { return m_harmonyRangeMin; }
    int harmonyRangeMax() const { return m_harmonyRangeMax; }
    void setHarmonyRange(int minNote, int maxNote);

    // Multi-voice harmony configuration (4 voices on channels 12-15)
    const HarmonyVoiceConfig& voiceConfig(int voiceIndex) const;
    void setVoiceConfig(int voiceIndex, const HarmonyVoiceConfig& config);
    void setVoiceMotionType(int voiceIndex, VoiceMotionType type);
    void setVoiceRange(int voiceIndex, int minNote, int maxNote);
    bool isMultiVoiceModeActive() const;  // True if any voice is enabled

    // --- Live harmony master switch (footswitch-driven) ---
    // When false, all harmony output is suppressed regardless of the
    // configured per-voice modes / harmony mode / chord. Disabling sends
    // all-notes-off on harmony channels 12-15 so nothing sticks. The
    // user's snapping-window config is preserved across toggles.
    bool harmonyEnabled() const { return m_harmonyEnabled.load(); }
    void setHarmonyEnabled(bool enabled);

    // Verbose diagnostic logging for the harmony pipeline. When true, key
    // decision points push human-readable lines into MidiProcessor's console
    // (the in-app log) so a "no harmony output" symptom can be debugged
    // without rebuilding. Off by default; toggled from the UI.
    void setHarmonyDebug(bool on) { m_harmonyDebug.store(on); }
    bool harmonyDebug() const { return m_harmonyDebug.load(); }

    // --- Default chord override (for performance mode without a chart) ---
    // Sets the chord the harmony engine uses when no chart-driven chord is
    // active. Triggered from the footswitch chord-stepper UI. Pass the
    // already-parsed ChordSymbol; the caller is responsible for parsing.
    void setDefaultHarmonyChord(const music::ChordSymbol& chord);

    // Human-readable summary of the scale currently used to conform the
    // harmony output (e.g. "B♭ Ionian — B♭ C D E♭ F G A"). Returns empty
    // string when no chord is set or scale lookup fails. Used by the
    // Audio Track Switch editor for visual debugging.
    QString currentScaleSummary(bool preferFlats = true) const;

    // --- Voice Channel-10 scale snap relay ---
    // The actual snapping happens on MidiProcessor's worker thread (lock-free
    // via an atomic 12-bit pitch-class mask). These methods are convenience
    // relays so the SnappingWindow can drive the feature without holding a
    // direct MidiProcessor pointer. publishVoiceScaleMask() recomputes the
    // current valid-PC mask from the active chord/scale and pushes it to
    // MidiProcessor; call it whenever the chord changes.
    void setVoiceCh10SnapEnabled(bool enabled);
    bool voiceCh10SnapEnabled() const;
    void publishVoiceScaleMask();

    // Cell index tracking (called by engine on each step)
    void setCurrentCellIndex(int cellIndex);
    int currentCellIndex() const { return m_currentCellIndex; }

    // Beat position tracking for conformance (0.0 = beat 1, 1.0 = beat 2, etc.)
    void setBeatPosition(float beatPosition);
    float beatPosition() const { return m_beatPosition; }

    // Periodic update for time-based conformance (call from audio/timer callback)
    // deltaMs: milliseconds since last update
    void updateConformance(float deltaMs);

signals:
    void leadModeChanged(LeadMode newMode);
    void harmonyModeChanged(HarmonyMode newMode);
    void vocalBendEnabledChanged(bool enabled);
    void vocalVibratoRangeCentsChanged(double cents);
    void vibratoCorrectionEnabledChanged(bool enabled);
    void octaveGuardEnabledChanged(bool enabled);
    void harmonyVibratoEnabledChanged(bool enabled);
    void harmonyHumanizationEnabledChanged(bool enabled);
    void voiceSustainEnabledChanged(bool enabled);
    void sustainSmoothingEnabledChanged(bool enabled);
    void sustainSmoothingMsChanged(int ms);
    void releaseBendPreventionEnabledChanged(bool enabled);
    void voiceSustainThresholdChanged(int threshold);

public slots:
    // Guitar input handlers (connected to MidiProcessor signals)
    void onGuitarNoteOn(int midiNote, int velocity);
    void onGuitarNoteOff(int midiNote);
    void onGuitarHzUpdated(double hz);

    // CC2 (breath) forwarding from voice
    void onVoiceCc2Updated(int value);

    // Voice Hz (for AsPlayedPlusBend mode - measures delta from snapped note for vibrato)
    void onVoiceHzUpdated(double hz);

    // Voice MIDI note (for VocalSync mode - stable integer note, no Hz jitter)
    void onVoiceNoteOn(int midiNote);
    void onVoiceNoteOff(int midiNote);

    // Clear all active notes
    void reset();

private:
    // Active note tracking (for note-off routing and pitch bend)
    // Defined here so it can be used by helper method declarations below
    struct ActiveNote {
        int originalNote = 0;
        int snappedNote = 0;      // Output note (may be snapped from original)
        int harmonyNote = -1;     // Legacy: single harmony note (channel 12), -1 if not active
        std::array<int, 4> harmonyNotes = {-1, -1, -1, -1};  // Multi-voice: channels 12-15, -1 if not active
        double referenceHz = 0.0; // Hz of the snapped note (for pitch bend calculation)
        bool voiceSustained = false; // True if guitar note-off received but held by voice
        int velocity = 64;        // Velocity of the note

        // Conformance behavior tracking
        ConformanceBehavior behavior = ConformanceBehavior::ALLOW;
        float conformanceBendTarget = 0.0f;  // Target bend in cents (for BEND behavior)
        float conformanceBendCurrent = 0.0f; // Current bend position (interpolates toward target)
        bool isDelayed = false;              // True if note is waiting for delay
        float delayRemainingMs = 0.0f;       // Remaining delay time
        int delayedVelocity = 0;             // Velocity for delayed note

        // TIMED_SNAP behavior tracking
        bool isTimedSnap = false;            // True if waiting for timed snap
        float timedSnapRemainingMs = 0.0f;   // Time remaining before snap
        int timedSnapTarget = 0;             // Note to snap to when timer expires

        // TIMED_BEND behavior tracking
        bool isTimedBend = false;            // True if performing timed bend
        float timedBendDurationMs = 0.0f;    // Total duration of bend
        float timedBendElapsedMs = 0.0f;     // Time elapsed since bend started
        float timedBendTargetCents = 0.0f;   // Target bend in cents
    };

    // Core snapping logic
    int snapToNearestValidPc(int inputPc, const QSet<int>& validPcs) const;
    int generateHarmonyNote(int inputNote, const QSet<int>& chordTones, const QSet<int>& scaleTones) const;
    int generateParallelHarmonyNote(int inputNote, int previousLeadNote, int previousHarmonyNote, const QSet<int>& chordTones, const QSet<int>& validPcs, bool harmonyAbove = false) const;
    int generateContraryHarmonyNote(int inputNote, int previousLeadNote, int previousHarmonyNote, const QSet<int>& chordTones, const QSet<int>& validPcs, bool harmonyAbove = false) const;
    int generateSimilarHarmonyNote(int inputNote, int previousLeadNote, int previousHarmonyNote, const QSet<int>& chordTones, const QSet<int>& validPcs, bool harmonyAbove = false) const;
    int generateObliqueHarmonyNote(int inputNote, int previousLeadNote, int previousHarmonyNote, const QSet<int>& chordTones, const QSet<int>& validPcs, bool harmonyAbove = false) const;

    // PARALLEL_FIXED: lead + interval (semitones), then snap result to the
    //   nearest pitch class in validPcs (chord+scale tones derived from the
    //   current default chord). Returns a MIDI note ready to be range-applied.
    int generateParallelFixedHarmonyNote(int inputNote, int intervalSemitones,
                                         const QSet<int>& validPcs) const;
    // DRONE: emit the chord root in the configured base octave.
    //   Returns rootPc + (octave * 12), clamped to MIDI range, or -1 if no
    //   chord is set yet.
    int generateDroneHarmonyNote(int octave) const;
    // SCALE_PARALLEL: find the lead's position in the current scale (or the
    //   nearest scale tone if it's chromatic), then offset by N scale steps.
    //   Always lands on a scale tone; consecutive leads never collide.
    int generateScaleParallelHarmonyNote(int inputNote, int scaleStepOffset,
                                         const QSet<int>& validPcs) const;

    // Final validation: ensures harmony note is T1, T2, or T3 (not chromatic T4)
    // If T4, snaps to nearest T1 (chord tone). Returns validated MIDI note.
    int validateHarmonyNote(int harmonyNote, int leadNote, const ActiveChord& chord) const;

    // Multi-voice harmony generation
    // otherVoiceNotes: harmony notes already generated by other voices (for inter-voice consonance)
    int generateHarmonyForVoice(int voiceIndex, int inputNote, const QSet<int>& chordTones,
                                 const QSet<int>& validPcs, const QVector<int>& otherVoiceNotes = {}) const;
    int applyVoiceRange(int note, int minNote, int maxNote) const;  // Octave-shift to fit range
    bool wouldClashWithOtherVoices(int candidateNote, const QVector<int>& otherVoiceNotes) const;  // Check inter-voice dissonance

    QSet<int> computeValidPitchClasses() const;
    QSet<int> computeChordTones(const music::ChordSymbol& chord) const;
    QSet<int> computeKeyScaleTones() const;  // Uses dynamic key detection
    ActiveChord buildActiveChord() const;    // Build ActiveChord from current context

    // Chord change handling
    void checkAndReconformOnChordChange(int previousCellIndex);  // Re-conform notes when chord changes

    // MIDI output helpers
    void emitNoteOn(int channel, int note, int velocity);
    void emitNoteOff(int channel, int note);
    void emitPitchBend(int channel, int bendValue);
    void emitCC(int channel, int cc, int value);
    void emitAllNotesOff();
    void releaseVoiceSustainedNotes();  // Release all notes held by voice sustain
    void releaseNote(const ActiveNote& note);  // Release a single note based on mode

    // Humanization helpers
    int calculateHumanizationDelayMs(int voiceIndex) const;  // BPM-constrained delay for a voice
    void emitHarmonyNoteOn(int channel, int note, int velocity, int voiceIndex);  // Delayed if humanization enabled
    void emitHarmonyNoteOff(int channel, int note, int voiceIndex);  // Matches delay from note-on

    // Pitch conversion utilities
    static int normalizePc(int pc);
    static int noteToOctave(int midiNote);
    static int pcToMidiNote(int pc, int targetOctave);
    static double midiNoteToHz(int midiNote);
    static double hzToCents(double hz, double referenceHz);

    // Counterpoint interval analysis utilities
    // Get the interval in semitones between two notes (0-11, always positive, mod 12)
    static int getIntervalClass(int note1, int note2);
    // Is this interval consonant? (unison, 3rd, 5th, 6th, octave - 0, 3, 4, 7, 8, 9 semitones)
    static bool isConsonant(int intervalSemitones);
    // Is this a perfect consonance? (unison, 5th, octave - 0, 7, 12 semitones)
    static bool isPerfectConsonance(int intervalSemitones);
    // Is this an imperfect consonance? (3rd, 6th - 3, 4, 8, 9 semitones)
    static bool isImperfectConsonance(int intervalSemitones);
    // Would moving from (prevLead, prevHarmony) to (newLead, newHarmony) create parallel 5ths or octaves?
    static bool wouldCreateParallelPerfect(int prevLead, int prevHarmony, int newLead, int newHarmony);

    // Dependencies (not owned)
    MidiProcessor* m_midi = nullptr;
    HarmonyContext* m_harmony = nullptr;
    const virtuoso::ontology::OntologyRegistry* m_ontology = nullptr;
    const chart::ChartModel* m_model = nullptr;

    // True iff legacy single-voice harmony output is currently allowed:
    // master-switch on AND configured harmony mode is non-OFF.
    inline bool legacyHarmonyOn() const {
        return m_harmonyEnabled.load() && (m_harmonyMode != HarmonyMode::OFF);
    }

    // Map a chord directly to the scale that best matches the user's
    // expectation when they've stepped through the footswitch chord
    // controls. The legacy explicitHintScalesForContext only handles
    // major / minor / dominant cleanly; this covers dim / aug / sus
    // / m7b5 etc. with the right diatonic completion (e.g. natural
    // minor for "Bm", locrian for "Bdim", etc.).
    QString scaleKeyForChord(const music::ChordSymbol& c) const;

    // State
    LeadMode m_leadMode = LeadMode::Original;
    HarmonyMode m_harmonyMode = HarmonyMode::OFF;
    HarmonyModeCompat m_harmonyModeCompat = HarmonyModeCompat::Off;
    HarmonyConfig m_harmonyConfig;
    LeadConfig m_leadConfig;
    PitchConformanceEngine m_conformanceEngine;
    GlissandoProcessor m_glissando;
    bool m_vocalBendEnabled = true;           // Enabled by default
    double m_vocalVibratoRangeCents = 200.0;  // ±200 cents (default), or ±100 cents
    bool m_vibratoCorrectionEnabled = true;   // Enabled by default - filter out DC offset from voice
    bool m_octaveGuardEnabled = true;         // Enabled by default - reject octave tracking glitches

    // Octave guard state: applied to voice Hz before any mode-specific processing
    double m_octaveGuardAcceptedHz = 0.0;     // Last accepted voice Hz
    double m_octaveGuardCandidateHz = 0.0;    // Candidate Hz awaiting confirmation
    int m_octaveGuardConfirmCount = 0;        // Ticks the candidate has been stable
    static constexpr int kOctaveGuardConfirmTicks = 3; // ~30ms at 10ms tick
    bool m_harmonyVibratoEnabled = false;     // Disabled by default - harmony voices get no vibrato pitch bend
    bool m_harmonyHumanizationEnabled = true; // Enabled by default - add timing offsets to harmony
    int m_tempoBpm = 120;                     // Current tempo for humanization calculations
    bool m_voiceSustainEnabled = true;        // Enabled by default - hold notes while singing
    bool m_sustainSmoothingEnabled = true;    // Enabled by default - delay release on brief silences
    int m_sustainSmoothingMs = 500;           // Default 500ms hold time after CC2 drops
    bool m_sustainReleaseTimerActive = false; // True when waiting to release after CC2 dropped
    bool m_releaseBendPreventionEnabled = true; // Enabled by default - freeze pitch bend on voice-sustained notes
    int m_voiceSustainThreshold = 5;           // CC2 threshold for sustain (1-10, lower = more sensitive)
    int m_harmonyRangeMin = 0;                // Min MIDI note for harmony (default: no limit)
    int m_harmonyRangeMax = 127;              // Max MIDI note for harmony (default: no limit)
    int m_currentCellIndex = -1;
    int m_lastCc2Value = 0;                   // Track current CC2 (breath) value for voice sustain
    float m_beatPosition = 0.0f;              // Current beat position (0.0-3.999 for 4/4)
    int m_lastPlayedNote = -1;                // For melodic direction analysis in conformance

    // Fast playing detection and machine-gun prevention
    qint64 m_lastNoteOnTimestamp = 0;         // Timestamp of last note-on (for fast playing detection)
    int m_currentlyPlayingNote = -1;          // The note currently sounding (after any snapping)
    bool m_currentNoteWasSnapped = false;     // True if current note was a correction (not played correctly)

    static constexpr qint64 kFastPlayingThresholdMs = 100;  // Notes faster than this = fast playing

    // Lead melody direction tracking for CONTRARY harmony
    int m_lastHarmonyLeadNote = -1;   // Previous lead note for direction calculation
    int m_leadMelodyDirection = 0;    // +1 = ascending, -1 = descending, 0 = none
    int m_lastHarmonyOutputNote = -1; // Previous harmony output note (for contrary motion)
    qint64 m_lastGuitarNoteOffTimestamp = 0;  // Timestamp of last guitar note-off (for phrase detection)
    int m_guitarNotesHeld = 0;                // Count of guitar notes currently being held (before voice sustain)

    // Multi-voice harmony configuration (4 voices on channels 12-15)
    std::array<HarmonyVoiceConfig, 4> m_voiceConfigs;

    // Humanization state (per voice)
    mutable std::array<int, 4> m_humanizationDelayMs = {0, 0, 0, 0};  // Last delay used for each voice
    mutable quint32 m_humanizationRngState = 12345;  // Simple LCG state for humanization

    static constexpr qint64 kPhraseTimeoutMs = 500;  // Silence longer than this = new phrase

    // Chromatic sweep detection - track recent intervals
    static constexpr int kRecentIntervalsSize = 4;  // Track last N intervals
    std::array<int, kRecentIntervalsSize> m_recentIntervals = {0, 0, 0, 0};  // Circular buffer
    int m_recentIntervalsIndex = 0;           // Current index in circular buffer
    int m_lastInputNote = -1;                 // Last input note for interval calculation

    // Returns true if recent playing pattern looks like a chromatic sweep
    bool isLikelyChromaticSweep() const;

    // Track last known chord (to persist across empty cells)
    music::ChordSymbol m_lastKnownChord;
    bool m_hasLastKnownChord = false;
    // True when the chord was set explicitly via setDefaultHarmonyChord
    // (footswitch / editor). Forces harmony to use this chord even when an
    // iReal chart model is loaded — prevents the chart from silently
    // overwriting the user's footswitch-driven chord choice.
    bool m_useDefaultHarmonyChord = false;

    // Live master switch for harmony output (atomic so worker thread reads cheaply).
    std::atomic<bool> m_harmonyEnabled{false};
    std::atomic<bool> m_harmonyDebug{false};

    // Voice-sustained tie semantics. Pre-computed for the in-flight
    // onGuitarNoteOn so that releaseVoiceSustainedNotes() (which fires
    // mid-flight, before the new note's harmony is emitted) can decide:
    //   "if this voice's NEW harmony pitch matches the SUSTAINED harmony
    //    we're about to release, don't release it; keep it sounding and
    //    suppress the about-to-fire note-on for the same pitch."
    // m_upcomingHarmony[v]   = MIDI note we're going to emit on voice v
    //                          for the new lead, or -1 if voice off / no
    //                          new harmony.
    // m_skipUpcomingOn[v]    = true once a voice-sustained release has
    //                          "transferred" its held pitch to the new
    //                          lead — the upcoming ON for that voice is
    //                          a no-op because the note is already playing.
    std::array<int, 4>  m_upcomingHarmony  = {-1, -1, -1, -1};
    std::array<bool, 4> m_skipUpcomingOn   = {false, false, false, false};

    QHash<int, ActiveNote> m_activeNotes;  // key = original input note
    mutable QReadWriteLock m_activeNotesLock{QReadWriteLock::Recursive};  // Thread safety for m_activeNotes (recursive to allow nested locking)

    // VocalSync: continuous Hz tracking for shift calculation
    double m_vocalSyncGuitarHz = 0.0;              // Current guitar Hz (note + pitch bend)
    double m_vocalSyncVoiceHz = 0.0;               // Current voice Hz
    int m_vocalSyncVoiceHoldTicks = 0;             // Freeze voice Hz updates after guitar attack
    int m_vocalSyncLastShiftSent = 999;            // Last bend value sent

    // Helper: compute and emit VocalSync shift as pitch bend
    void emitVocalSyncShift();

    // Hz tracking for pitch bend
    double m_lastGuitarHz = 0.0;
    double m_lastGuitarCents = 0.0;  // Guitar pitch deviation (for combining with vocal bend)

    // Voice pitch tracking (for vocal bend mode)
    double m_lastVoiceCents = 0.0;

    // Vibrato correction: exponential moving average to track DC offset
    double m_voiceCentsAverage = 0.0;
    bool m_voiceCentsAverageInitialized = false;  // True after first voice sample
    int m_settlingCounter = 0;                    // Counts samples during settling period
    int m_vibratoFadeInSamples = 0;               // Counter for fade-in (counts up from 0)
    bool m_oscillationDetected = false;           // True once we've detected vibrato oscillation
    double m_lastOscillation = 0.0;               // Previous oscillation value (for zero-crossing detection)

    static constexpr double kVibratoCorrectionAlpha = 0.03;  // Smoothing factor (lower = slower adaptation)
    static constexpr int kSettlingDuration = 30;             // ~300ms settling period before detecting vibrato
    static constexpr int kVibratoFadeInDuration = 15;        // ~150ms fade-in once vibrato is detected
    static constexpr double kOscillationThreshold = 8.0;     // Minimum cents deviation to consider as oscillation
    static constexpr float kConformanceBendRatePerMs = 0.5f; // Cents per ms for conformance bend

    // Output channels (1-indexed, matching sendVirtualNoteOn expectations)
    // - Lead mode (Original/Conformed): output notes -> channel 1
    // - Harmony mode: harmony notes -> channels 12-15
    // VocalSync outputs on channel 1 (same as lead) since guitar passthrough is suppressed
    // and Logic Pro instruments default to responding on channel 1.
    static constexpr int kChannelVocalSync = channels::LEAD;
    static constexpr int kChannelLead = channels::LEAD;        // MIDI channel 1 (lead output)
    static constexpr int kChannelHarmony1 = channels::HARMONY_1;  // MIDI channel 12 (primary harmony)
    static constexpr int kChannelHarmony2 = channels::HARMONY_2;  // MIDI channel 13 (second harmony)
    static constexpr int kChannelHarmony3 = channels::HARMONY_3;  // MIDI channel 14 (third harmony)
    static constexpr int kChannelHarmony4 = channels::HARMONY_4;  // MIDI channel 15 (fourth harmony)
};

} // namespace playback
