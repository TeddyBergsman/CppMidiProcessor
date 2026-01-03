#pragma once

#include <QString>
#include <QVector>

class QSettings;

namespace music {

// High-level musical feel selector (in addition to the detailed knobs below).
// - BalladSwing: default to 2-feel / long tones, clear chord arrivals, sparse fills.
// - WalkingSwing: default to quarter-note walking, more continuous forward motion.
enum class BassFeelStyle {
    BalladSwing = 0,
    WalkingSwing = 1,
};

// Per-song “human musician” configuration for the walking bass generator.
// Versioned and persisted via QSettings.
struct BassProfile {
    int version = 4;
    QString name; // optional label, e.g. "Default Walking"

    BassFeelStyle feelStyle = BassFeelStyle::BalladSwing;

    // Routing / range
    bool enabled = false;
    int midiChannel = 3;         // 1..16
    int minMidiNote = 28;        // E1
    int maxMidiNote = 48;        // C3
    int registerCenterMidi = 36; // C2-ish center
    int registerRange = 12;      // +/- semitones around center
    int maxLeap = 7;             // semitones; larger leaps get penalized

    // Harmony interpretation
    bool honorSlashBass = true;
    double slashBassProb = 1.0;  // 0..1
    bool treatMaj6AsMaj7 = false;

    // --- VST performance controls (Ample Bass Upright) ---
    // These are *behavioral* toggles: they change how the generator uses keyswitches / FX notes.
    // Musical notes will be octave-shifted by the engine to match the plugin's "normal center";
    // keyswitches and FX notes must never be transposed.
    //
    // IMPORTANT: DAWs/VSTs disagree on octave numbering (e.g. "C3=60" vs "C4=60").
    // The Ample manual's note names are commonly "C3=60", while this app uses "C4=60".
    // Set this offset so that manual note names map to the correct MIDI numbers.
    int ampleNoteNameOffsetSemitones = 12; // typically +12 for manuals using C3=60

    // Articulations (keyswitches)
    bool artSustainAccent = true;   // C0; velocity >=126 triggers Accent
    bool artNaturalHarmonic = false; // C#0
    bool artPalmMute = true;        // D0
    bool artSlideInOut = true;      // D#0
    bool artLegatoSlide = true;     // E0
    bool artHammerPull = true;      // F0

    // FX sounds (played as specific MIDI notes in the VST)
    bool fxHitRimMute = true;       // F#4
    bool fxHitTopPalmMute = true;   // G4
    bool fxHitTopFingerMute = true; // G#4
    bool fxHitTopOpen = false;      // A4
    bool fxHitRimOpen = false;      // A#4
    bool fxScratch = false;         // F5
    bool fxBreath = false;          // F#5
    bool fxSingleStringSlap = false; // G5
    bool fxLeftHandSlapNoise = false; // G#5
    bool fxRightHandSlapNoise = false; // A5
    bool fxSlideTurn4 = true;       // A#5
    bool fxSlideTurn3 = true;       // B5
    bool fxSlideDown4 = true;       // C6
    bool fxSlideDown3 = true;       // C#6

    // Feel / timing
    double swingAmount = 0.0;    // 0..1 (reserved for later subdivisions)
    double swingRatio = 2.0;     // e.g. 2.0 (2:1), 3.0 (3:1)
    int microJitterMs = 3;       // +/- ms random timing (pros are tight)
    int laidBackMs = 5;          // constant behind-the-beat
    int pushMs = 0;              // constant ahead-of-the-beat (negative feel)
    int driftMaxMs = 10;         // slow timing drift max (+/-) across bars (human feel)
    double driftRate = 0.15;     // 0..1 random-walk rate per bar
    int attackVarianceMs = 4;    // additional per-note attack variance (+/-)
    int noteLengthMs = 0;        // 0 => derived from gatePct
    double gatePct = 0.85;       // 0..1 of beat length
    quint32 humanizeSeed = 1;    // stable per-song randomness

    // Dynamics
    int baseVelocity = 85;       // 1..127
    int velocityVariance = 12;   // random +/- per note
    double accentBeat1 = 1.00;   // multipliers
    double accentBeat2 = 0.78;
    double accentBeat3 = 0.88;
    double accentBeat4 = 0.78;
    double phraseContourStrength = 0.15; // 0..1 bar-level contour
    double phraseArcStrength = 0.25;     // 0..1 phrase-level cresc/decresc
    double sectionArcStrength = 0.20;    // 0..1 across section / song passes

    // Musical line shaping
    double chromaticism = 0.55;  // 0..1 overall
    double leapPenalty = 0.25;   // 0..1
    double repetitionPenalty = 0.35; // 0..1

    // Evolution / creativity (so the “musician” changes over the song)
    double intensityBase = 0.55;        // 0..1 average intensity
    double intensityVariance = 0.35;    // 0..1 random-walk amplitude
    double evolutionRate = 0.18;        // 0..1 how quickly intensity drifts
    double sectionRampStrength = 0.25;  // 0..1 ramp within section
    int phraseLengthBars = 4;           // typical jazz phrasing

    // Broken time / space (to avoid “every beat forever”)
    double twoFeelPhraseProb = 0.18;      // probability a phrase switches to 2-feel (half notes)
    double brokenTimePhraseProb = 0.12;   // probability a phrase uses broken time (rests/ties)
    double restProb = 0.10;               // chance of resting on a weak beat in broken time
    double tieProb = 0.22;                // chance to tie/sustain across the next beat in broken time

    // Rhythmic variation (walking-oriented)
    double ghostNoteProb = 0.18;        // probability of dead/ghost note on weak beats
    int ghostVelocity = 18;             // 1..50 typical
    double ghostGatePct = 0.20;         // short length for dead notes
    double pickup8thProb = 0.20;        // 8th-note pickup on beat 4 (two notes in beat)
    double fillProbPhraseEnd = 0.22;    // additional fill chance at phrase ends (beat 4)
    double syncopationProb = 0.06;      // 0..1 occasional offbeat placement (within beat)

    // More “human musician” features
    double twoBeatRunProb = 0.18;       // 2-beat 8th-note run spanning beats 3–4
    double enclosureProb = 0.20;        // 2-note enclosure into next bar target (beat 4)
    double sectionIntroRestraint = 0.55; // 0..1 reduces intensity in first bar after section change

    // Motif / development (phrase-level melodic identity)
    double motifProb = 0.35;           // probability a phrase adopts a motif
    double motifStrength = 0.45;       // 0..1 how strongly it influences passing tones/fills
    double motifVariation = 0.25;      // 0..1 how much the motif mutates across repeats/passes

    // Target chord-tone weights for strong beats (1 & 3): root/3rd/5th/7th
    double wRoot = 1.00;
    double wThird = 0.75;
    double wFifth = 0.60;
    double wSeventh = 0.90;

    // Approach type weights on beat 4 into the next chord
    double wApproachChromatic = 0.60;
    double wApproachDiatonic = 0.30;
    double wApproachEnclosure = 0.10;

    // --- Explainability / UI ---
    // When enabled, the engine emits a human-readable explanation for each played event.
    // Keep this OFF by default to avoid extra allocations and UI churn.
    bool reasoningLogEnabled = false;
};

BassProfile defaultBassProfile();

// Persist/load profile under a prefix like "<overrideGroup>/bassProfile".
BassProfile loadBassProfile(QSettings& settings, const QString& prefix);
void saveBassProfile(QSettings& settings, const QString& prefix, const BassProfile& p);

} // namespace music

