#pragma once

#include <QObject>
#include <QSet>
#include <QHash>
#include <QVector>
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
        SmartThirds,         // DEPRECATED: Use HarmonyMode::SINGLE + HarmonyType::PARALLEL
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

    // Voice sustain: keep notes sounding while singing (CC2 > threshold)
    bool voiceSustainEnabled() const { return m_voiceSustainEnabled; }
    void setVoiceSustainEnabled(bool enabled);

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
        int harmonyNote = -1;     // Harmony note, -1 if not active
        double referenceHz = 0.0; // Hz of the snapped note (for pitch bend calculation)
        bool voiceSustained = false; // True if guitar note-off received but held by voice

        // Conformance behavior tracking
        ConformanceBehavior behavior = ConformanceBehavior::ALLOW;
        float conformanceBendTarget = 0.0f;  // Target bend in cents (for BEND behavior)
        float conformanceBendCurrent = 0.0f; // Current bend position (interpolates toward target)
        bool isDelayed = false;              // True if note is waiting for delay
        float delayRemainingMs = 0.0f;       // Remaining delay time
        int delayedVelocity = 0;             // Velocity for delayed note
    };

    // Core snapping logic
    int snapToNearestValidPc(int inputPc, const QSet<int>& validPcs) const;
    int generateHarmonyNote(int inputNote, const QSet<int>& chordTones, const QSet<int>& scaleTones) const;
    QSet<int> computeValidPitchClasses() const;
    QSet<int> computeChordTones(const music::ChordSymbol& chord) const;
    QSet<int> computeKeyScaleTones() const;  // Uses dynamic key detection
    ActiveChord buildActiveChord() const;    // Build ActiveChord from current context

    // MIDI output helpers
    void emitNoteOn(int channel, int note, int velocity);
    void emitNoteOff(int channel, int note);
    void emitPitchBend(int channel, int bendValue);
    void emitCC(int channel, int cc, int value);
    void emitAllNotesOff();
    void releaseVoiceSustainedNotes();  // Release all notes held by voice sustain
    void releaseNote(const ActiveNote& note);  // Release a single note based on mode

    // Pitch conversion utilities
    static int normalizePc(int pc);
    static int noteToOctave(int midiNote);
    static int pcToMidiNote(int pc, int targetOctave);
    static double midiNoteToHz(int midiNote);
    static double hzToCents(double hz, double referenceHz);

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
    bool m_voiceSustainEnabled = true;        // Enabled by default - hold notes while singing
    int m_currentCellIndex = -1;
    int m_lastCc2Value = 0;                   // Track current CC2 (breath) value for voice sustain
    float m_beatPosition = 0.0f;              // Current beat position (0.0-3.999 for 4/4)
    int m_lastPlayedNote = -1;                // For melodic direction analysis in conformance

    // Track last known chord (to persist across empty cells)
    music::ChordSymbol m_lastKnownChord;
    bool m_hasLastKnownChord = false;

    QHash<int, ActiveNote> m_activeNotes;  // key = original input note

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
