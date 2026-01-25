#pragma once

#include <QObject>
#include <QSet>
#include <QHash>
#include <QVector>
#include <QReadWriteLock>
#include <array>

#include "music/ChordSymbol.h"
#include "playback/HarmonyTypes.h"
#include "playback/ChordOntology.h"
#include "playback/PitchConformanceEngine.h"

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
    // Lead mode: controls how guitar notes are processed -> Channel 1
    enum class LeadMode {
        Off = 0,
        Original,            // Pass through original notes with vocal bend -> channel 1
        Conformed            // Apply pitch conformance (gravity-based) -> channel 1
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
    void harmonyVibratoEnabledChanged(bool enabled);
    void harmonyHumanizationEnabledChanged(bool enabled);
    void voiceSustainEnabledChanged(bool enabled);

public slots:
    // Guitar input handlers (connected to MidiProcessor signals)
    void onGuitarNoteOn(int midiNote, int velocity);
    void onGuitarNoteOff(int midiNote);
    void onGuitarHzUpdated(double hz);

    // CC2 (breath) forwarding from voice
    void onVoiceCc2Updated(int value);

    // Voice Hz (for AsPlayedPlusBend mode - measures delta from snapped note for vibrato)
    void onVoiceHzUpdated(double hz);

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

    // State
    LeadMode m_leadMode = LeadMode::Off;
    HarmonyMode m_harmonyMode = HarmonyMode::OFF;
    HarmonyModeCompat m_harmonyModeCompat = HarmonyModeCompat::Off;
    HarmonyConfig m_harmonyConfig;
    LeadConfig m_leadConfig;
    PitchConformanceEngine m_conformanceEngine;
    bool m_vocalBendEnabled = true;           // Enabled by default
    double m_vocalVibratoRangeCents = 200.0;  // ±200 cents (default), or ±100 cents
    bool m_vibratoCorrectionEnabled = true;   // Enabled by default - filter out DC offset from voice
    bool m_harmonyVibratoEnabled = false;     // Disabled by default - harmony voices get no vibrato pitch bend
    bool m_harmonyHumanizationEnabled = true; // Enabled by default - add timing offsets to harmony
    int m_tempoBpm = 120;                     // Current tempo for humanization calculations
    bool m_voiceSustainEnabled = true;        // Enabled by default - hold notes while singing
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

    QHash<int, ActiveNote> m_activeNotes;  // key = original input note
    mutable QReadWriteLock m_activeNotesLock{QReadWriteLock::Recursive};  // Thread safety for m_activeNotes (recursive to allow nested locking)

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
    static constexpr int kVoiceSustainCc2Threshold = 5;      // CC2 must be above this to sustain notes
    static constexpr float kConformanceBendRatePerMs = 0.5f; // Cents per ms for conformance bend

    // Output channels (1-indexed, matching sendVirtualNoteOn expectations)
    // - Lead mode (Original/Conformed): output notes -> channel 1
    // - Harmony mode: harmony notes -> channels 12-15
    static constexpr int kChannelLead = channels::LEAD;        // MIDI channel 1 (lead output)
    static constexpr int kChannelHarmony1 = channels::HARMONY_1;  // MIDI channel 12 (primary harmony)
    static constexpr int kChannelHarmony2 = channels::HARMONY_2;  // MIDI channel 13 (second harmony)
    static constexpr int kChannelHarmony3 = channels::HARMONY_3;  // MIDI channel 14 (third harmony)
    static constexpr int kChannelHarmony4 = channels::HARMONY_4;  // MIDI channel 15 (fourth harmony)
};

} // namespace playback
