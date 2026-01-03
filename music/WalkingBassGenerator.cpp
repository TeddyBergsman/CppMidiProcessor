#include "music/WalkingBassGenerator.h"

#include "music/ChordDictionary.h"
#include "music/Pitch.h"

#include <QRandomGenerator>
#include <algorithm>

namespace music {
namespace {

static int clampMidi(int n) {
    if (n < 0) return 0;
    if (n > 127) return 127;
    return n;
}

static int pickMidiForPcNear(int pc, int referenceMidi, int minMidi, int maxMidi) {
    pc = normalizePc(pc);
    if (minMidi > maxMidi) std::swap(minMidi, maxMidi);
    minMidi = clampMidi(minMidi);
    maxMidi = clampMidi(maxMidi);

    // Choose the nearest pitch-class match to the reference, within range.
    int best = -1;
    int bestDist = 9999;
    for (int n = minMidi; n <= maxMidi; ++n) {
        if ((n % 12) != pc) continue;
        const int dist = (referenceMidi >= 0) ? std::abs(n - referenceMidi) : std::abs(n - ((minMidi + maxMidi) / 2));
        if (dist < bestDist) {
            bestDist = dist;
            best = n;
        }
    }
    // Fallback: snap to range middle with pc.
    if (best < 0) {
        int mid = (minMidi + maxMidi) / 2;
        best = mid - ((mid % 12) - pc);
        while (best < minMidi) best += 12;
        while (best > maxMidi) best -= 12;
    }
    return clampMidi(best);
}

static double beatAccent(const BassProfile& p, int beatInBar) {
    switch (beatInBar) {
    case 0: return p.accentBeat1;
    case 1: return p.accentBeat2;
    case 2: return p.accentBeat3;
    case 3: return p.accentBeat4;
    default: return 1.0;
    }
}

static int thirdIntervalForQuality(ChordQuality q) {
    switch (q) {
    case ChordQuality::Minor:
    case ChordQuality::HalfDiminished:
    case ChordQuality::Diminished: return 3;
    case ChordQuality::Sus2: return 2;
    case ChordQuality::Sus4: return 5;
    case ChordQuality::Power5: return 0;
    default: return 4;
    }
}

static int fifthIntervalForQuality(ChordQuality q) {
    switch (q) {
    case ChordQuality::HalfDiminished:
    case ChordQuality::Diminished: return 6;
    case ChordQuality::Augmented: return 8;
    case ChordQuality::Power5: return 7;
    default: return 7;
    }
}

static int seventhIntervalForChord(const ChordSymbol& c) {
    switch (c.seventh) {
    case SeventhQuality::Major7: return 11;
    case SeventhQuality::Minor7: return 10;
    case SeventhQuality::Dim7: return 9;
    case SeventhQuality::None: default: return 0;
    }
}

static double weightedChoice(QRandomGenerator& rng, const QVector<double>& weights) {
    double sum = 0.0;
    for (double w : weights) sum += std::max(0.0, w);
    if (sum <= 0.0) return -1.0;
    const double r = rng.generateDouble() * sum;
    double acc = 0.0;
    for (int i = 0; i < weights.size(); ++i) {
        acc += std::max(0.0, weights[i]);
        if (r <= acc) return double(i);
    }
    return double(weights.size() - 1);
}

static int clampVelocity(int v) { return std::max(1, std::min(127, v)); }

} // namespace

WalkingBassGenerator::WalkingBassGenerator() = default;

void WalkingBassGenerator::setProfile(const BassProfile& p) {
    m_profile = p;
    // Ensure the seed is non-zero (Qt treats 0 as a valid seed, but we want stable non-degenerate behavior).
    if (m_profile.humanizeSeed == 0) m_profile.humanizeSeed = 1;
    reset();
}

void WalkingBassGenerator::reset() {
    m_lastMidi = -1;
    m_lastBarBeat = -1;
    m_lastStepPc = -1;
    m_rngState = (m_profile.humanizeSeed == 0) ? 1u : m_profile.humanizeSeed;
}

BassDecision WalkingBassGenerator::nextNote(int beatInBar, const ChordSymbol* currentChord, const ChordSymbol* nextChord) {
    BassDecision d;
    if (!m_profile.enabled) return d;
    if (beatInBar < 0 || beatInBar > 3) return d;
    if (!currentChord || currentChord->noChord || currentChord->placeholder || currentChord->rootPc < 0) return d;

    // Local RNG per generator for stable per-song feel.
    // Seed varies per call but stays deterministic given the same start/reset and step order.
    QRandomGenerator rng(m_rngState);
    // Advance state by consuming a value (store the updated state back).
    const quint32 advance = rng.generate();
    m_rngState = advance ? advance : (m_rngState + 1u);

    // Decide whether to honor slash bass this beat.
    const bool honorSlash = m_profile.honorSlashBass && (rng.generateDouble() < m_profile.slashBassProb);
    const int curRoot = honorSlash ? ChordDictionary::bassRootPc(*currentChord) : currentChord->rootPc;
    const int nextRoot = (nextChord && nextChord->rootPc >= 0)
        ? (honorSlash ? ChordDictionary::bassRootPc(*nextChord) : nextChord->rootPc)
        : curRoot;

    // Build chord-tone pitch classes (for strong beats).
    const int thirdPc = normalizePc(currentChord->rootPc + thirdIntervalForQuality(currentChord->quality));
    const int fifthPc = normalizePc(currentChord->rootPc + fifthIntervalForQuality(currentChord->quality));
    const int sevInt = seventhIntervalForChord(*currentChord);
    const int seventhPc = (sevInt != 0) ? normalizePc(currentChord->rootPc + sevInt) : -1;

    auto roleWeight = [&](int pc) -> double {
        if (pc == normalizePc(curRoot)) return m_profile.wRoot;
        if (pc == thirdPc) return m_profile.wThird;
        if (pc == fifthPc) return m_profile.wFifth;
        if (seventhPc >= 0 && pc == seventhPc) return m_profile.wSeventh;
        return 0.15;
    };

    auto scoreMidi = [&](int midi) -> double {
        // Preference: stay near register center, avoid big leaps, avoid repetition.
        const int ref = (m_lastMidi >= 0) ? m_lastMidi : m_profile.registerCenterMidi;
        const int leap = std::abs(midi - ref);
        const int distCenter = std::abs(midi - m_profile.registerCenterMidi);
        double s = 0.0;
        s -= (double)leap * (0.08 + 0.18 * m_profile.leapPenalty);
        s -= (double)distCenter * 0.02;
        if (m_lastMidi >= 0 && midi == m_lastMidi) s -= 5.0 * m_profile.repetitionPenalty;
        if (m_profile.maxLeap > 0 && leap > m_profile.maxLeap) s -= (double)(leap - m_profile.maxLeap) * 0.8;
        return s;
    };

    auto pickMidiForPc = [&](int pc) -> int {
        const int ref = (m_lastMidi >= 0) ? m_lastMidi : m_profile.registerCenterMidi;
        int minMidi = m_profile.minMidiNote;
        int maxMidi = m_profile.maxMidiNote;
        // Preferred range around center (soft constraint).
        const int prefMin = std::max(minMidi, m_profile.registerCenterMidi - m_profile.registerRange);
        const int prefMax = std::min(maxMidi, m_profile.registerCenterMidi + m_profile.registerRange);
        // Try preferred range first.
        int best = pickMidiForPcNear(pc, ref, prefMin, prefMax);
        // If that yields outside the global range (shouldn't), fall back.
        if (best < minMidi || best > maxMidi) best = pickMidiForPcNear(pc, ref, minMidi, maxMidi);
        return best;
    };

    auto bestCandidateFromPcs = [&](const QVector<int>& pcs) -> int {
        int bestMidi = -1;
        double bestScore = -1e9;
        for (int pc : pcs) {
            const int midi = pickMidiForPc(pc);
            double s = roleWeight(normalizePc(pc)) * 10.0;
            s += scoreMidi(midi);
            // A touch of randomness to avoid robotic repetition.
            s += (rng.generateDouble() - 0.5) * 0.6;
            if (s > bestScore) { bestScore = s; bestMidi = midi; }
        }
        return bestMidi;
    };

    auto targetTonePcs = [&]() -> QVector<int> {
        QVector<int> pcs;
        pcs.reserve(4);
        pcs.push_back(normalizePc(curRoot));
        if (thirdPc != normalizePc(curRoot)) pcs.push_back(thirdPc);
        if (fifthPc != normalizePc(curRoot) && fifthPc != thirdPc) pcs.push_back(fifthPc);
        if (seventhPc >= 0 && seventhPc != normalizePc(curRoot) && seventhPc != thirdPc && seventhPc != fifthPc) pcs.push_back(seventhPc);
        return pcs;
    };

    int chosenMidi = -1;

    if (beatInBar == 0 || beatInBar == 2) {
        // Strong-beat target: weighted chord tone choice, then scored for register/voice-leading.
        QVector<int> pcs = targetTonePcs();
        if (!pcs.isEmpty()) {
            // Apply weight-based filtering by duplicating pcs into a soft distribution.
            // (Keeps the scoring model dominant while preserving a stylistic bias.)
            QVector<int> expanded;
            for (int pc : pcs) {
                const double w = roleWeight(pc);
                const int copies = std::max(1, int(std::round(w * 2.0)));
                for (int k = 0; k < copies; ++k) expanded.push_back(pc);
            }
            // Sample a few candidates and pick best by score.
            QVector<int> sample;
            const int sampleN = std::min(6, int(expanded.size()));
            for (int k = 0; k < sampleN; ++k) {
                sample.push_back(expanded[int(rng.bounded(expanded.size()))]);
            }
            chosenMidi = bestCandidateFromPcs(sample);
        }
    } else if (beatInBar == 1) {
        // Weak beat: stepwise motion is king. Prefer chord tones and neighbors.
        QVector<int> pcs = targetTonePcs();
        // Add chromatic neighbors around current root/third/seventh to create motion.
        for (int pc : targetTonePcs()) {
            pcs.push_back(normalizePc(pc - 1));
            pcs.push_back(normalizePc(pc + 1));
        }
        chosenMidi = bestCandidateFromPcs(pcs);
    } else if (beatInBar == 3) {
        // Approach into next chord: target next root/3rd/7th.
        int nextThirdPc = normalizePc((nextChord ? nextChord->rootPc : currentChord->rootPc) + thirdIntervalForQuality(nextChord ? nextChord->quality : currentChord->quality));
        int nextSev = (nextChord ? seventhIntervalForChord(*nextChord) : seventhIntervalForChord(*currentChord));
        int nextSeventhPc = (nextSev != 0) ? normalizePc((nextChord ? nextChord->rootPc : currentChord->rootPc) + nextSev) : -1;

        QVector<int> nextTargets;
        nextTargets.push_back(normalizePc(nextRoot));
        nextTargets.push_back(nextThirdPc);
        if (nextSeventhPc >= 0) nextTargets.push_back(nextSeventhPc);

        // Choose one target using chord-tone weights as proxy.
        QVector<double> w;
        w.reserve(nextTargets.size());
        for (int pc : nextTargets) w.push_back(roleWeight(pc));
        const int targetIdx = int(std::max(0.0, weightedChoice(rng, w)));
        const int targetPc = nextTargets[std::min(targetIdx, int(nextTargets.size()) - 1)];

        // Choose approach type.
        QVector<double> aw = {m_profile.wApproachChromatic, m_profile.wApproachDiatonic, m_profile.wApproachEnclosure};
        const int aType = int(std::max(0.0, weightedChoice(rng, aw)));

        QVector<int> approachPcs;
        const bool preferChrom = (rng.generateDouble() < m_profile.chromaticism);
        if (aType == 2) {
            // “Enclosure” (single beat approximation): pick either +1 or -1, biased against repeating direction.
            approachPcs.push_back(normalizePc(targetPc + 1));
            approachPcs.push_back(normalizePc(targetPc - 1));
            if (preferChrom) {
                approachPcs.push_back(normalizePc(targetPc + 2));
                approachPcs.push_back(normalizePc(targetPc - 2));
            }
        } else if (aType == 1) {
            // Diatonic-ish: +/-2
            approachPcs.push_back(normalizePc(targetPc - 2));
            approachPcs.push_back(normalizePc(targetPc + 2));
            if (preferChrom) {
                approachPcs.push_back(normalizePc(targetPc - 1));
                approachPcs.push_back(normalizePc(targetPc + 1));
            }
        } else {
            // Chromatic: +/-1
            approachPcs.push_back(normalizePc(targetPc - 1));
            approachPcs.push_back(normalizePc(targetPc + 1));
            if (!preferChrom) {
                approachPcs.push_back(normalizePc(targetPc - 2));
                approachPcs.push_back(normalizePc(targetPc + 2));
            }
        }
        chosenMidi = bestCandidateFromPcs(approachPcs);
    }

    if (chosenMidi < 0) {
        chosenMidi = pickMidiForPc(curRoot);
    }

    m_lastMidi = chosenMidi;
    d.midiNote = chosenMidi;

    // Dynamics: base velocity + beat accent + small variance + phrase contour.
    double vel = double(m_profile.baseVelocity) * beatAccent(m_profile, beatInBar);
    const double phase = (beatInBar / 3.0) - 0.5; // -0.5..+0.5
    vel *= (1.0 + (phase * 0.20) * m_profile.phraseContourStrength);
    if (m_profile.velocityVariance > 0) {
        const int dv = int(rng.bounded(m_profile.velocityVariance * 2 + 1)) - m_profile.velocityVariance;
        vel += double(dv);
    }
    d.velocity = clampVelocity(int(std::round(vel)));
    return d;
}

} // namespace music

