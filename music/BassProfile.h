#pragma once

#include <QString>
#include <QVector>

class QSettings;

namespace music {

// Per-song “human musician” configuration for the walking bass generator.
// Versioned and persisted via QSettings.
struct BassProfile {
    int version = 1;
    QString name; // optional label, e.g. "Default Walking"

    // Routing / range
    bool enabled = false;
    int midiChannel = 3;         // 1..16
    int minMidiNote = 28;        // E1
    int maxMidiNote = 48;        // C3
    int registerCenterMidi = 36; // C2-ish center
    int registerRange = 12;      // +/- semitones around center
    int maxLeap = 7;             // semitones; larger leaps get penalized
    int transposeSemitones = 12; // output transpose (default +12 = 1 octave up)

    // Harmony interpretation
    bool honorSlashBass = true;
    double slashBassProb = 1.0;  // 0..1
    bool treatMaj6AsMaj7 = false;

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
};

BassProfile defaultBassProfile();

// Persist/load profile under a prefix like "<overrideGroup>/bassProfile".
BassProfile loadBassProfile(QSettings& settings, const QString& prefix);
void saveBassProfile(QSettings& settings, const QString& prefix, const BassProfile& p);

} // namespace music

