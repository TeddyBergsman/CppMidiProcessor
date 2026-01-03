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

    // Harmony interpretation
    bool honorSlashBass = true;
    double slashBassProb = 1.0;  // 0..1
    bool treatMaj6AsMaj7 = false;

    // Feel / timing
    double swingAmount = 0.0;    // 0..1 (reserved for later subdivisions)
    double swingRatio = 2.0;     // e.g. 2.0 (2:1), 3.0 (3:1)
    int microJitterMs = 6;       // +/- ms random timing
    int laidBackMs = 5;          // constant behind-the-beat
    int pushMs = 0;              // constant ahead-of-the-beat (negative feel)
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

    // Musical line shaping
    double chromaticism = 0.55;  // 0..1 overall
    double leapPenalty = 0.25;   // 0..1
    double repetitionPenalty = 0.35; // 0..1

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

