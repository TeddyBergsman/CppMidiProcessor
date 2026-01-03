#include "music/WalkingBassGenerator.h"

#include "music/ChordDictionary.h"
#include "music/Pitch.h"
#include "music/ScaleLibrary.h"

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

static int pcDistance(int a, int b) {
    a = normalizePc(a);
    b = normalizePc(b);
    int d = std::abs(a - b);
    return std::min(d, 12 - d);
}

} // namespace

WalkingBassGenerator::WalkingBassGenerator() = default;

namespace {
// MIDI note numbers assume C4=60 (so C0=12) which matches the rest of this project (e.g., E1=28).
struct UprightVst {
    // Articulation keyswitches
    static constexpr int KS_SustainAccent = 12;  // C0
    static constexpr int KS_NaturalHarmonic = 13; // C#0
    static constexpr int KS_PalmMute = 14;       // D0
    static constexpr int KS_SlideInOut = 15;     // D#0
    static constexpr int KS_LegatoSlide = 16;    // E0
    static constexpr int KS_HammerPull = 17;     // F0

    // FX sounds
    static constexpr int FX_HitRimMute = 66;       // F#4
    static constexpr int FX_HitTopPalmMute = 67;   // G4
    static constexpr int FX_HitTopFingerMute = 68; // G#4
    static constexpr int FX_HitTopOpen = 69;       // A4
    static constexpr int FX_HitRimOpen = 70;       // A#4
    static constexpr int FX_Scratch = 77;          // F5
    static constexpr int FX_Breath = 78;           // F#5
    static constexpr int FX_SingleStringSlap = 79; // G5
    static constexpr int FX_LeftHandSlapNoise = 80; // G#5
    static constexpr int FX_RightHandSlapNoise = 81; // A5
    static constexpr int FX_SlideTurn4 = 82;       // A#5
    static constexpr int FX_SlideTurn3 = 83;       // B5
    static constexpr int FX_SlideDown4 = 84;       // C6
    static constexpr int FX_SlideDown3 = 85;       // C#6
};

static void pushKeySwitch(QVector<BassEvent>& out, int midiNote, int velocity, double offsetBeats, int noteOffsetSemis) {
    BassEvent ks;
    ks.role = BassEvent::Role::KeySwitch;
    ks.midiNote = clampMidi(midiNote + noteOffsetSemis);
    ks.velocity = clampVelocity(velocity);
    ks.offsetBeats = std::max(0.0, std::min(0.95, offsetBeats));
    ks.lengthBeats = 0.06; // short
    out.push_back(ks);
}

static void pushFx(QVector<BassEvent>& out, int midiNote, int velocity, double offsetBeats, int noteOffsetSemis) {
    BassEvent fx;
    fx.role = BassEvent::Role::FxSound;
    fx.midiNote = clampMidi(midiNote + noteOffsetSemis);
    fx.velocity = clampVelocity(velocity);
    fx.offsetBeats = std::max(0.0, std::min(0.95, offsetBeats));
    fx.lengthBeats = 0.10; // short
    out.push_back(fx);
}

static void pushKeySwitch(QVector<BassEvent>& out, int midiNote, int velocity, double offsetBeats) {
    // Backward-compat helper (no offset).
    pushKeySwitch(out, midiNote, velocity, offsetBeats, 0);
}

static void pushFx(QVector<BassEvent>& out, int midiNote, int velocity, double offsetBeats) {
    // Backward-compat helper (no offset).
    pushFx(out, midiNote, velocity, offsetBeats, 0);
}
} // namespace

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
    m_intensity = std::max(0.0, std::min(1.0, m_profile.intensityBase));
    m_lastSectionHash = 0;
    m_planned.clear();
    m_forcedStrongPc = -1;
    m_phraseMode = 1;
    m_motifSteps.clear();
    m_motifIndex = 0;
    m_hasMotif = false;
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
    // Next-chord guide tones (for voice-leading decisions).
    QVector<int> nextGuidePcs;
    if (nextChord && nextChord->rootPc >= 0 && !nextChord->noChord && !nextChord->placeholder) {
        const int nThird = normalizePc(nextChord->rootPc + thirdIntervalForQuality(nextChord->quality));
        const int nSevInt = seventhIntervalForChord(*nextChord);
        const int nSev = (nSevInt != 0) ? normalizePc(nextChord->rootPc + nSevInt) : -1;
        nextGuidePcs = { normalizePc(nextRoot), nThird };
        if (nSev >= 0) nextGuidePcs.push_back(nSev);
    } else {
        nextGuidePcs = { normalizePc(nextRoot) };
    }

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
            // Forced resolution target (used by enclosures/runs).
            if ((beatInBar == 0 || beatInBar == 2) && m_forcedStrongPc >= 0 && normalizePc(pc) == normalizePc(m_forcedStrongPc)) {
                s += 6.5;
            }
            // Avoid boring repeated roots on strong beats.
            if ((beatInBar == 0 || beatInBar == 2) && m_lastMidi >= 0) {
                const int lastPc = normalizePc(m_lastMidi % 12);
                const int rootPc = normalizePc(curRoot);
                if (normalizePc(pc) == rootPc && lastPc == rootPc) {
                    s -= 5.0 * (0.5 + 0.5 * m_profile.repetitionPenalty);
                }
            }
            // Voice-leading: prefer tones close to next chord's guide tones.
            if (!nextGuidePcs.isEmpty()) {
                int bestD = 99;
                for (int ng : nextGuidePcs) bestD = std::min(bestD, pcDistance(pc, ng));
                s += (6 - std::min(6, bestD)) * 0.9; // 0..5.4
            }
            // A touch of randomness to avoid robotic repetition.
            // Keep this subtle; “creativity” comes from phrase planning/fills, not random note choice.
            s += (rng.generateDouble() - 0.5) * 0.18;
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
        // Weak beat: keep it *very* stepwise.
        // Previous logic used a full chord-scale candidate list which can sound "random"
        // because pitch-class selection is later mapped into register.
        const int lastPc = (m_lastMidi >= 0) ? normalizePc(m_lastMidi % 12) : normalizePc(curRoot);
        QVector<int> pcs;
        pcs.reserve(16);

        // Primary: stepwise options around the current pitch class.
        pcs.push_back(normalizePc(lastPc + 1));
        pcs.push_back(normalizePc(lastPc - 1));
        pcs.push_back(normalizePc(lastPc + 2));
        pcs.push_back(normalizePc(lastPc - 2));

        // Add chord tones (keeps harmony readable).
        for (int pc : targetTonePcs()) pcs.push_back(pc);

        // Add diatonic context *sparingly*: only the nearest scale step to the lastPc.
        const auto suggested = ScaleLibrary::suggestForChord(*currentChord);
        const auto scaleType = !suggested.isEmpty() ? suggested.first() : ScaleType::Ionian;
        const auto& scale = ScaleLibrary::get(scaleType);
        {
            int bestPc = lastPc;
            int bestD = 999;
            for (int iv : scale.intervals) {
                const int pc = normalizePc(currentChord->rootPc + iv);
                const int d = pcDistance(lastPc, pc);
                if (d < bestD) { bestD = d; bestPc = pc; }
            }
            pcs.push_back(bestPc);
        }

        // Bias toward moving toward the next guide tone with a 1–2 semitone step.
        if (!nextGuidePcs.isEmpty()) {
            int targetPc = nextGuidePcs.first();
            int bestD = 99;
            for (int ng : nextGuidePcs) {
                const int d = pcDistance(lastPc, ng);
                if (d < bestD) { bestD = d; targetPc = ng; }
            }
            const int upDist = normalizePc(targetPc - lastPc);
            const int downDist = normalizePc(lastPc - targetPc);
            const int dir = (upDist <= downDist) ? 1 : -1;
            pcs.push_back(normalizePc(lastPc + dir * 1));
            pcs.push_back(normalizePc(lastPc + dir * 2));
            // Optional chromatic neighbor near destination when chromaticism is high.
            if (rng.generateDouble() < (0.20 + 0.60 * m_profile.chromaticism)) {
                pcs.push_back(normalizePc(targetPc + (rng.bounded(2) == 0 ? -1 : +1)));
            }
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
    // Clear forced target when we land on a strong beat.
    if ((beatInBar == 0 || beatInBar == 2) && m_forcedStrongPc >= 0) {
        m_forcedStrongPc = -1;
    }
    return d;
}

QVector<BassEvent> WalkingBassGenerator::nextBeat(const BassBeatContext& ctx, const ChordSymbol* currentChord, const ChordSymbol* nextChord) {
    QVector<BassEvent> out;
    if (!m_profile.enabled) return out;
    if (!currentChord || currentChord->noChord || currentChord->placeholder || currentChord->rootPc < 0) return out;

    const int globalBeat = ctx.barIndex * 4 + ctx.beatInBar;
    if (m_planned.contains(globalBeat)) {
        out = m_planned.take(globalBeat);
        return out;
    }

    // Stable RNG.
    QRandomGenerator rng(m_rngState);
    const quint32 advance = rng.generate();
    m_rngState = advance ? advance : (m_rngState + 1u);

    // Phrase-level planner:
    // - Choose a phrase “mode” (sparse/normal/busy) at phrase starts
    // - Drift density up on later song passes
    // - Vary slightly per section (stable via sectionHash)
    const int phraseLen = std::max(1, ctx.phraseLengthBars);
    const int barInPhrase = (ctx.barInSection >= 0) ? (ctx.barInSection % phraseLen) : 0;
    const bool isPhraseStartBar = ctx.isNewBar && (barInPhrase == 0);
    if (isPhraseStartBar) {
        // Base mode selection from current intensity and song pass.
        const double passBoost = (ctx.totalPasses > 1) ? (double(ctx.songPass) / double(std::max(1, ctx.totalPasses - 1))) : 0.0;
        const double sectionBias = ((ctx.sectionHash % 101u) / 100.0) - 0.5; // [-0.5..0.5]
        const double desire = std::max(0.0, std::min(1.0, m_intensity + 0.20 * passBoost + 0.10 * sectionBias));
        const double r = rng.generateDouble();
        if (r < 0.22 - 0.15 * desire) m_phraseMode = 0; // sparse/broken tendency
        else if (r > 0.78 - 0.10 * desire) m_phraseMode = 2; // busy
        else m_phraseMode = 1; // normal
    }

    // Phrase style selection: walking vs 2-feel vs broken time.
    // Determined once per phrase start, stored in m_phraseMode:
    // 0 = broken time, 1 = walking, 2 = busy walking, 3 = 2-feel.
    if (isPhraseStartBar) {
        const double passBoost = (ctx.totalPasses > 1) ? (double(ctx.songPass) / double(std::max(1, ctx.totalPasses - 1))) : 0.0;
        const double desire = std::max(0.0, std::min(1.0, m_intensity + 0.20 * passBoost));
        const double r = rng.generateDouble();

        const double pTwo = m_profile.twoFeelPhraseProb * (0.65 + 0.35 * (1.0 - desire));
        const double pBroken = m_profile.brokenTimePhraseProb * (0.75 + 0.25 * (1.0 - desire));
        if (r < pBroken) m_phraseMode = 0;
        else if (r < pBroken + pTwo) m_phraseMode = 3;
        else if (r > 0.72 - 0.20 * desire) m_phraseMode = 2;
        else m_phraseMode = 1;
    }

    // Evolve intensity on bar boundaries.
    if (ctx.isNewBar) {
        // Reset-ish on section change, but keep continuity.
        if (ctx.isSectionChange && ctx.sectionHash != 0 && ctx.sectionHash != m_lastSectionHash) {
            // Start new section closer to base intensity.
            m_intensity = 0.65 * m_intensity + 0.35 * m_profile.intensityBase;
            m_lastSectionHash = ctx.sectionHash;
        }

        // Section ramp: slowly increases within section.
        const double ramp = (ctx.phraseLengthBars > 0)
            ? (double(ctx.barInSection % std::max(1, ctx.phraseLengthBars)) / double(std::max(1, ctx.phraseLengthBars - 1)))
            : 0.0;
        const double target = std::max(0.0, std::min(1.0, m_profile.intensityBase + m_profile.sectionRampStrength * 0.35 * ramp));

        // Random-walk noise (bounded).
        const double noise = (rng.generateDouble() - 0.5) * 2.0 * m_profile.intensityVariance * 0.20;
        m_intensity += (target - m_intensity) * m_profile.evolutionRate + noise;
        m_intensity = std::max(0.0, std::min(1.0, m_intensity));

        // First bar of a new section: be more restrained (humans often lay back initially).
        if (ctx.isSectionChange && ctx.barInSection == 0) {
            m_intensity = m_intensity * (1.0 - 0.65 * m_profile.sectionIntroRestraint);
        }
    }

    auto pickMidi = [&](int pc) {
        const int ref = (m_lastMidi >= 0) ? m_lastMidi : m_profile.registerCenterMidi;
        return pickMidiForPcNear(pc, ref, m_profile.minMidiNote, m_profile.maxMidiNote);
    };

    // Compute per-beat “effective intensity” including phrase mode + pass arc.
    const double passArc = (ctx.totalPasses > 1) ? (double(ctx.songPass) / double(std::max(1, ctx.totalPasses - 1))) : 0.0;
    double modeMult = 1.0;
    if (m_phraseMode == 0) modeMult = 0.70;      // broken time = more space
    else if (m_phraseMode == 2) modeMult = 1.35; // busy
    else if (m_phraseMode == 3) modeMult = 0.85; // 2-feel: fewer notes but strong shape
    const double phraseArc = (phraseLen > 1) ? (double(barInPhrase) / double(phraseLen - 1)) : 0.0; // build within phrase
    double eff = m_intensity * modeMult + 0.18 * passArc + 0.10 * phraseArc;
    eff = std::max(0.0, std::min(1.0, eff));
    const double swing = std::max(0.0, std::min(1.0, m_profile.swingAmount));

    // Motif: choose/refresh at phrase start so the line has identity and development.
    if (isPhraseStartBar) {
        const double motifP = m_profile.motifProb * (0.45 + 0.55 * eff);
        if (!m_hasMotif && rng.generateDouble() < motifP) {
            // Pick a small step motif (direction + small intervals).
            const int dir = (rng.bounded(2) == 0) ? 1 : -1;
            const int variant = int(rng.bounded(3));
            m_motifSteps.clear();
            if (variant == 0) m_motifSteps = {dir * 2, dir * 2, dir * 1};
            else if (variant == 1) m_motifSteps = {dir * 2, dir * 1, dir * 2};
            else m_motifSteps = {dir * 1, dir * 2, dir * 1, dir * -1};
            m_motifIndex = 0;
            m_hasMotif = true;
        } else if (m_hasMotif) {
            // Mutate slightly on later passes/sections.
            const double mutateP = m_profile.motifVariation * (0.20 + 0.80 * passArc);
            if (rng.generateDouble() < mutateP && !m_motifSteps.isEmpty()) {
                const int i = int(rng.bounded(m_motifSteps.size()));
                const int delta = (rng.bounded(2) == 0) ? -1 : +1;
                m_motifSteps[i] = std::max(-3, std::min(3, m_motifSteps[i] + delta));
            }
            // Occasionally drop motif to avoid overuse.
            if (rng.generateDouble() < 0.10 * (1.0 - m_profile.motifStrength)) {
                m_hasMotif = false;
                m_motifSteps.clear();
                m_motifIndex = 0;
            }
        }
    }

    // 2-feel / broken time gating happens after we decide the main note.
    const bool explain = m_profile.reasoningLogEnabled;
    auto setExplain = [&](BassEvent& ev, const QString& fn, const QString& why) {
        if (!explain) return;
        ev.function = fn;
        ev.reasoning = why;
    };

    auto planTwoBeatRun = [&](int startBeat, int targetPc) {
        // Create a simple 4x8th run across beats 3 & 4 that leads into targetPc next bar.
        // We end on an approach tone (±1) so the next bar resolves to targetPc.
        const int startMidi = (m_lastMidi >= 0) ? m_lastMidi : pickMidi(currentChord->rootPc);
        const int startPc = normalizePc(startMidi % 12);
        const int approachPc = normalizePc(targetPc + (rng.bounded(2) == 0 ? -1 : +1));

        // Direction: prefer moving toward the approach.
        int dir = 1;
        const int upDist = normalizePc(approachPc - startPc);
        const int downDist = normalizePc(startPc - approachPc);
        if (downDist < upDist) dir = -1;

        const QVector<int> pcs = {
            normalizePc(startPc + dir * 1),
            normalizePc(startPc + dir * 2),
            normalizePc(startPc + dir * 3),
            approachPc,
        };

        QVector<BassEvent> beat3;
        QVector<BassEvent> beat4;
        BassEvent a;
        a.offsetBeats = 0.0; a.lengthBeats = 0.48; a.velocity = clampVelocity(int(std::round(m_profile.baseVelocity * (0.90 + 0.25 * m_intensity))));
        a.midiNote = pickMidi(pcs[0]);
        BassEvent b = a; b.offsetBeats = 0.5; b.midiNote = pickMidi(pcs[1]);
        BassEvent c = a; c.offsetBeats = 0.0; c.midiNote = pickMidi(pcs[2]);
        BassEvent d = a; d.offsetBeats = 0.5; d.midiNote = pickMidi(pcs[3]); d.velocity = clampVelocity(int(std::round(double(a.velocity) * 1.05)));

        beat3 = {a, b};
        beat4 = {c, d};

        // Store beat 4 and force next bar strong-beat resolution.
        m_planned.insert(globalBeat + 1, beat4);
        m_forcedStrongPc = targetPc;
        return beat3;
    };

    // Always produce at least one “main” note for the beat.
    // IMPORTANT musical constraint: on the first beat of a *new chord* (wherever it occurs),
    // strongly anchor the harmony (usually the root). This makes the line recognizably "about the tune".
    BassDecision main = nextNote(ctx.beatInBar, currentChord, nextChord);
    bool forcedArrivalRoot = false;
    if (ctx.isNewChord && currentChord && currentChord->rootPc >= 0 && !currentChord->noChord && !currentChord->placeholder) {
        const double p = (ctx.beatInBar == 0) ? 0.92 : 0.82;
        if (rng.generateDouble() < p) {
            const bool honorSlash = m_profile.honorSlashBass && (rng.generateDouble() < m_profile.slashBassProb);
            const int rootPc = honorSlash ? ChordDictionary::bassRootPc(*currentChord) : currentChord->rootPc;
            const int ref = (m_lastMidi >= 0) ? m_lastMidi : m_profile.registerCenterMidi;
            main.midiNote = pickMidiForPcNear(rootPc, ref, m_profile.minMidiNote, m_profile.maxMidiNote);

            // Velocity: keep it firm and stable on arrivals (pros sound confident on chord hits).
            double vel = double(m_profile.baseVelocity) * beatAccent(m_profile, ctx.beatInBar);
            if (m_profile.velocityVariance > 0) {
                const int dv = int(rng.bounded(m_profile.velocityVariance * 2 + 1)) - m_profile.velocityVariance;
                vel += double(dv);
            }
            main.velocity = clampVelocity(int(std::round(vel)));
            forcedArrivalRoot = true;
            // Keep internal state consistent: subsequent decisions in this beat should see the anchored note.
            m_lastMidi = main.midiNote;
        }
    }
    if (main.midiNote < 0 || main.velocity <= 0) return out;

    // Syncopation: occasionally place the note slightly late/early within the beat.
    double mainOffset = 0.0;
    if (rng.generateDouble() < (m_profile.syncopationProb * (0.25 + 0.75 * eff))) {
        // 16th-ish placement
        mainOffset = (rng.bounded(2) == 0) ? 0.25 : 0.125;
    }

    BassEvent e;
    e.midiNote = main.midiNote;
    e.velocity = main.velocity;
    e.offsetBeats = mainOffset;
    if (explain) {
        auto phraseLabel = [&]() -> QString {
            switch (m_phraseMode) {
            case 0: return "broken time";
            case 2: return "busy walking";
            case 3: return "2-feel";
            case 1:
            default: return "walking";
            }
        };
        const QString chordText = !currentChord->originalText.trimmed().isEmpty()
            ? currentChord->originalText.trimmed()
            : QString("pc%1").arg(currentChord->rootPc);
        const QString why = forcedArrivalRoot
            ? QString("New chord (%1) → anchor the arrival with the bass root. Phrase feel: %2.")
                  .arg(chordText, phraseLabel())
            : QString("Primary tone for this beat (voice-leading + register + chord-tone bias). Phrase feel: %1.")
                  .arg(phraseLabel());
        setExplain(e, forcedArrivalRoot ? "Root (arrival)" : "Primary tone", why);
    }

    // 2-feel: articulate on beats 1 and 3, sustain across the following weak beat.
    if (m_phraseMode == 3) {
        if (ctx.beatInBar == 0 || ctx.beatInBar == 2) {
            e.lengthBeats = 2.0; // sustain for 2 beats
            out.push_back(e);
        } else if (ctx.beatInBar == 1) {
            return {}; // rest
        } else {
            // beat 4: optionally do a pickup/enclosure; else rest.
            const double phraseBoost = ctx.isPhraseEnd ? m_profile.fillProbPhraseEnd : 0.0;
            const double p = (m_profile.pickup8thProb + phraseBoost) * (0.20 + 0.80 * eff);
            if (rng.generateDouble() < p && nextChord && nextChord->rootPc >= 0) {
                out.push_back(e); // will be replaced by pickup/enclosure logic below
            } else {
                return {};
            }
        }
    } else {
        out.push_back(e);
    }

    // Apply phrase/section dynamic arc to the main note (and later to fills as well).
    // Phrase arc: build slightly toward the end; Section arc: later passes are a touch stronger.
    const double phraseEnv = 1.0 + (phraseArc - 0.5) * 2.0 * m_profile.phraseArcStrength * 0.25;
    const double sectionEnv = 1.0 + passArc * m_profile.sectionArcStrength * 0.20;
    for (auto& ev : out) {
        double v = double(ev.velocity) * phraseEnv * sectionEnv;
        ev.velocity = clampVelocity(int(std::round(v)));
    }

    // Broken time: probabilistically rest on weak beats; tie by extending strong beat note into next beat.
    if (m_phraseMode == 0) {
        const bool weak = (ctx.beatInBar == 1 || ctx.beatInBar == 3);
        if (weak) {
            const double pRest = m_profile.restProb * (0.40 + 0.60 * (1.0 - eff));
            if (rng.generateDouble() < pRest) {
                return {}; // rest
            }
            // if not resting, keep the note but slightly late for “laid-back broken time”
            out[0].offsetBeats = std::max(out[0].offsetBeats, 0.125);
        } else {
            // Strong beat: sometimes tie over the following weak beat by extending length.
            const double pTie = m_profile.tieProb * (0.25 + 0.75 * (1.0 - eff));
            if (rng.generateDouble() < pTie) {
                out[0].lengthBeats = std::max(out[0].lengthBeats, 2.0);
                // Plan next beat as empty so we don't re-articulate.
                m_planned.insert(globalBeat + 1, {});
            }
        }
    }

    // Motif influence: on beat 2 (index 1) and passing contexts, bias toward a motif step.
    // Motif: subtle, and avoid obscuring chord arrivals.
    if (m_hasMotif && !m_motifSteps.isEmpty() && (ctx.beatInBar == 1) && !ctx.isNewChord &&
        rng.generateDouble() < (m_profile.motifStrength * (0.60 + 0.40 * eff))) {
        const int refMidi = (m_lastMidi >= 0) ? m_lastMidi : out.first().midiNote;
        const int refPc = normalizePc(refMidi % 12);
        const int step = m_motifSteps[m_motifIndex % m_motifSteps.size()];
        m_motifIndex = (m_motifIndex + 1) % std::max(1, int(m_motifSteps.size()));
        const int targetPc = normalizePc(refPc + step);
        // Insert a quiet passing tone on the upbeat if we are otherwise “square”.
        if (rng.generateDouble() < 0.35 + 0.35 * eff) {
            BassEvent m;
            m.midiNote = pickMidi(targetPc);
            m.velocity = clampVelocity(int(std::round(double(out.first().velocity) * 0.65)));
            m.offsetBeats = 0.5;
            m.lengthBeats = 0.40;
            setExplain(m, "Motif passing tone", "Motif development: add a quiet upbeat passing tone to give the line identity (kept stepwise).");
            out.push_back(m);
        }
    }

    // Ghost notes on weak beats (2 and 4 typically).
    const bool weakBeat = (ctx.beatInBar == 1 || ctx.beatInBar == 3);
    if (weakBeat) {
        const double gProb = m_profile.ghostNoteProb * (0.45 + 0.55 * eff) * (0.60 + 0.40 * swing);
        if (rng.generateDouble() < gProb) {
            BassEvent g;
            g.ghost = true;
            g.midiNote = main.midiNote; // dead note at same pitch (feels percussive)
            g.velocity = std::max(1, std::min(60, m_profile.ghostVelocity + int((rng.generateDouble() - 0.5) * 8.0)));
            g.offsetBeats = 0.5;        // upbeat 8th
            g.lengthBeats = std::max(0.05, std::min(0.5, m_profile.ghostGatePct));
            setExplain(g, "Ghost note", "Weak-beat percussive ghost note to add groove and forward motion without changing harmony.");
            out.push_back(g);
        }
    }

    // If swing is high, add an occasional upbeat anticipation (audible swing even without explicit fills).
    if (weakBeat && swing > 0.55 && nextChord && nextChord->rootPc >= 0) {
        const double p = (0.06 + 0.22 * eff) * swing;
        if (rng.generateDouble() < p) {
            const int targetPc = normalizePc(nextChord->rootPc);
            const bool chrom = (rng.generateDouble() < m_profile.chromaticism);
            const int stepPc = chrom
                ? normalizePc(targetPc + (rng.bounded(2) == 0 ? -1 : +1))
                : normalizePc(targetPc + (rng.bounded(2) == 0 ? -2 : +2));
            BassEvent a;
            a.midiNote = pickMidi(stepPc);
            a.velocity = clampVelocity(int(std::round(double(main.velocity) * 0.60)));
            a.offsetBeats = 0.5;
            a.lengthBeats = 0.35;
            setExplain(a, "Anticipation", "Upbeat anticipation: lightly points toward the next chord without stepping on the downbeat.");
            out.push_back(a);
        }
    }

    // Multi-beat run fill: start on beat 3 (index 2), spans beats 3–4.
    if (ctx.beatInBar == 2 && nextChord && nextChord->rootPc >= 0) {
        const double p = m_profile.twoBeatRunProb * (0.25 + 0.75 * eff) + (ctx.isPhraseEnd ? 0.14 : 0.0);
        if (rng.generateDouble() < p) {
            const int targetPc = normalizePc(nextChord->rootPc);
            out = planTwoBeatRun(globalBeat, targetPc);
            return out;
        }
    }

    // Beat-4 options: enclosure into next bar, or classic pickup 8ths.
    if (ctx.beatInBar == 3 && nextChord && nextChord->rootPc >= 0) {
        const double phraseBoost = ctx.isPhraseEnd ? m_profile.fillProbPhraseEnd : 0.0;
        const double enclosureP = (m_profile.enclosureProb + phraseBoost * 0.7) * (0.35 + 0.65 * eff);
        if (rng.generateDouble() < enclosureP) {
            const int targetPc = normalizePc(nextChord->rootPc);
            const int above = normalizePc(targetPc + 1 + (rng.bounded(2) == 0 ? 0 : 1)); // +1 or +2
            const int below = normalizePc(targetPc - 1);
            BassEvent e1;
            e1.midiNote = pickMidi(above);
            e1.velocity = clampVelocity(int(std::round(double(main.velocity) * 0.85)));
            e1.offsetBeats = 0.0;
            e1.lengthBeats = 0.45;
            BassEvent e2 = e1;
            e2.midiNote = pickMidi(below);
            e2.velocity = clampVelocity(int(std::round(double(main.velocity) * 0.92)));
            e2.offsetBeats = 0.5;
            e2.lengthBeats = 0.45;
            out.clear();
            out.push_back(e1);
            out.push_back(e2);
            m_lastMidi = e2.midiNote;
            m_forcedStrongPc = targetPc;
            setExplain(out[0], "Enclosure (above)", "Classic enclosure: approach the next root from above then below to create tension→release.");
            setExplain(out[1], "Enclosure (below)", "Classic enclosure: resolve the enclosure into the next bar's root.");
        } else {
            const double base = m_profile.pickup8thProb;
            const double p = (base + phraseBoost) * (0.35 + 0.65 * eff);
            if (rng.generateDouble() < p) {
                const int targetPc = normalizePc(nextChord->rootPc);
                const int approachPc = normalizePc(targetPc + (rng.bounded(2) == 0 ? -1 : +1));
                const int passingPc = normalizePc(approachPc + (rng.bounded(2) == 0 ? -2 : +2));

                BassEvent p1;
                p1.midiNote = pickMidi(passingPc);
                p1.velocity = clampVelocity(int(std::round(double(main.velocity) * 0.85)));
                p1.offsetBeats = 0.0;
                p1.lengthBeats = 0.45;

                BassEvent p2;
                p2.midiNote = pickMidi(approachPc);
                p2.velocity = clampVelocity(int(std::round(double(main.velocity) * 0.95)));
                p2.offsetBeats = 0.5;
                p2.lengthBeats = 0.45;

                out.clear();
                out.push_back(p1);
                out.push_back(p2);
                m_lastMidi = p2.midiNote;
                m_forcedStrongPc = targetPc;
                setExplain(out[0], "Pickup (passing)", "Pickup figure: two 8ths on beat 4 to lead into the next bar's root.");
                setExplain(out[1], "Pickup (approach)", "Chromatic/diatonic approach tone aiming to resolve on the next downbeat.");
            }
        }
    }

    // Ensure deterministic ordering by offset.
    std::sort(out.begin(), out.end(), [](const BassEvent& a, const BassEvent& b) {
        return a.offsetBeats < b.offsetBeats;
    });

    // --- Articulations & FX planning (Ample Bass Upright) ---
    // This is intentionally rule-driven (not random replacements):
    // - Accent uses the plugin's velocity trigger (126/127) on musically meaningful moments (new chord / strong beats).
    // - Legato/HP only when there are adjacent notes close enough to plausibly connect.
    // - FX hits replace some ghost/dead notes to sound like real string contact/percussion.
    {
        const int noteOffs = m_profile.ampleNoteNameOffsetSemitones;
        // Re-sort after we append extra events.
        auto resort = [&]() {
            std::sort(out.begin(), out.end(), [](const BassEvent& a, const BassEvent& b) {
                if (a.offsetBeats == b.offsetBeats) {
                    auto rank = [](BassEvent::Role r) -> int {
                        // Keyswitch must arrive before the musical note.
                        switch (r) {
                        case BassEvent::Role::KeySwitch: return 0;
                        case BassEvent::Role::FxSound: return 1;
                        case BassEvent::Role::MusicalNote: default: return 2;
                        }
                    };
                    return rank(a.role) < rank(b.role);
                }
                return a.offsetBeats < b.offsetBeats;
            });
        };

        // Replace some ghost "dead notes" with FX hits when enabled.
        // IMPORTANT: do NOT replace *all* ghosts; that becomes "too many muted notes" and reads like mistakes.
        for (auto& ev : out) {
            if (ev.role != BassEvent::Role::MusicalNote) continue;
            if (!ev.ghost) continue;

            // Choose an appropriate muted hit based on availability.
            int hit = -1;
            if (m_profile.fxHitTopPalmMute) hit = UprightVst::FX_HitTopPalmMute;
            else if (m_profile.fxHitTopFingerMute) hit = UprightVst::FX_HitTopFingerMute;
            else if (m_profile.fxHitRimMute) hit = UprightVst::FX_HitRimMute;

            const double replaceP = (0.35 + 0.20 * eff); // 35–55% depending on intensity
            if (hit >= 0 && rng.generateDouble() < replaceP) {
                pushFx(out, hit, std::min(80, std::max(20, ev.velocity + 25)), ev.offsetBeats, noteOffs);
                if (explain && !out.isEmpty()) {
                    auto& fxEv = out.last();
                    fxEv.function = "FX hit (mute)";
                    fxEv.reasoning = "Replace some ghost notes with realistic string/contact FX (kept sparse to avoid over-muting).";
                }
                ev.rest = true; // suppress pitched ghost note
            }
        }

        // Cross-beat legato: if the previous beat extended into this beat, decide HP/Legato Slide now.
        // (We can only decide based on the actual interval once we know the destination note.)
        if (m_pendingCrossBeatLegato && m_pendingCrossBeatFromMidi >= 0) {
            for (auto& ev : out) {
                if (ev.role != BassEvent::Role::MusicalNote || ev.rest) continue;
                if (ev.offsetBeats > 0.20) break; // only treat on-beat/near-on-beat as legato destination
                const int interval = ev.midiNote - m_pendingCrossBeatFromMidi;
                const int absIv = std::abs(interval);
                if (absIv > 0) {
                    const double ksOffset = std::max(0.0, ev.offsetBeats - 0.03);
                    if (absIv <= 2 && m_profile.artHammerPull) {
                        pushKeySwitch(out, UprightVst::KS_HammerPull, 75, ksOffset, noteOffs);
                        ev.allowOverlap = true;
                    } else if (absIv <= 12 && m_profile.artLegatoSlide) {
                        const int ksVel = (absIv >= 7) ? 120 : 85;
                        pushKeySwitch(out, UprightVst::KS_LegatoSlide, ksVel, ksOffset, noteOffs);
                        ev.allowOverlap = true;
                        // Optional slide noise to match direction.
                        const bool down = (interval < 0);
                        if (down) {
                            if (absIv >= 4 && m_profile.fxSlideDown4) pushFx(out, UprightVst::FX_SlideDown4, 55, ksOffset, noteOffs);
                            else if (m_profile.fxSlideDown3) pushFx(out, UprightVst::FX_SlideDown3, 50, ksOffset, noteOffs);
                        } else {
                            if (absIv >= 4 && m_profile.fxSlideTurn4) pushFx(out, UprightVst::FX_SlideTurn4, 55, ksOffset, noteOffs);
                            else if (m_profile.fxSlideTurn3) pushFx(out, UprightVst::FX_SlideTurn3, 50, ksOffset, noteOffs);
                        }
                    }
                }
                break;
            }
            m_pendingCrossBeatLegato = false;
            m_pendingCrossBeatFromMidi = -1;
        }

        // Accent logic (Sustain & Accent keyswitch is C0; Accent is velocity 126/127).
        if (m_profile.artSustainAccent) {
            for (auto& ev : out) {
                if (ev.role != BassEvent::Role::MusicalNote) continue;
                if (ev.rest) continue;

                const bool strong = (ctx.beatInBar == 0 || ctx.beatInBar == 2);
                const bool accentMoment =
                    (ctx.isNewChord && ctx.beatInBar == 0) ||
                    (strong && ctx.isPhraseEnd && ctx.beatInBar == 2);

                if (accentMoment) {
                    // Keep it musically intentional: only promote to Accent when the line is already fairly strong.
                    if (ev.velocity >= 95) ev.velocity = std::max(ev.velocity, 126);
                }
            }
        }

        // Apply legato/HP between adjacent notes *within this beat* when plausible.
        // (This covers pickups, enclosures, and runs — places a real player naturally connects notes.)
        for (int i = 0; i + 1 < out.size(); ++i) {
            auto& a = out[i];
            auto& b = out[i + 1];
            if (a.role != BassEvent::Role::MusicalNote || b.role != BassEvent::Role::MusicalNote) continue;
            if (a.rest || b.rest) continue;

            const double gap = b.offsetBeats - a.offsetBeats;
            if (gap <= 0.0) continue;

            // Only connect when these are close in time (e.g., 8ths inside a beat).
            if (gap > 0.55) continue;

            const int interval = b.midiNote - a.midiNote;
            const int absIv = std::abs(interval);
            if (absIv == 0) continue;

            // Create a small overlap so the VST's poly-legato logic can trigger.
            const double overlap = 0.06; // ~a 16th at 1 beat; actual ms depends on tempo
            const double desiredLen = gap + overlap;
            if (a.lengthBeats <= 0.0) a.lengthBeats = std::min(0.95, std::max(m_profile.gatePct, desiredLen));
            else a.lengthBeats = std::max(a.lengthBeats, std::min(0.95, desiredLen));
            // IMPORTANT: engine decides whether to cut the previous note based on the *destination* event.
            // So mark the destination as allowOverlap to prevent the engine from pre-cutting `a`.
            b.allowOverlap = true;

            // Insert the articulation keyswitch slightly before the destination note.
            const double ksOffset = std::max(0.0, b.offsetBeats - 0.03);

            if (absIv <= 2 && m_profile.artHammerPull) {
                // HP: small, connected motion.
                pushKeySwitch(out, UprightVst::KS_HammerPull, 75, ksOffset, noteOffs);
            } else if (absIv > 2 && m_profile.artLegatoSlide) {
                // Legato slide: bigger connected motion. Use keyswitch velocity to hint "position change" on bigger shifts.
                const int ksVel = (absIv >= 7) ? 120 : 85;
                pushKeySwitch(out, UprightVst::KS_LegatoSlide, ksVel, ksOffset, noteOffs);

                // Optional matching slide noise FX (directional and interval-based).
                const bool down = (interval < 0);
                if (down) {
                    if (absIv >= 4 && m_profile.fxSlideDown4) pushFx(out, UprightVst::FX_SlideDown4, 55, ksOffset, noteOffs);
                    else if (m_profile.fxSlideDown3) pushFx(out, UprightVst::FX_SlideDown3, 50, ksOffset, noteOffs);
                } else {
                    if (absIv >= 4 && m_profile.fxSlideTurn4) pushFx(out, UprightVst::FX_SlideTurn4, 55, ksOffset, noteOffs);
                    else if (m_profile.fxSlideTurn3) pushFx(out, UprightVst::FX_SlideTurn3, 50, ksOffset, noteOffs);
                }
            }
        }

        // Palm mute: use sparingly as a deliberate color on section intros.
        // Overuse reads like "muted notes" / mistakes.
        if (m_profile.artPalmMute) {
            const bool intro = (ctx.isNewBar && ctx.isSectionChange && ctx.barInSection == 0);
            const bool weak = (ctx.beatInBar == 1 || ctx.beatInBar == 3);
            const double pMute = (0.18 + 0.12 * (1.0 - eff)); // slightly more likely when restrained
            if (intro && weak && rng.generateDouble() < pMute) {
                for (auto& ev : out) {
                    if (ev.role != BassEvent::Role::MusicalNote || ev.rest) continue;
                    if (ev.velocity >= 120) continue; // don't mute accents
                    pushKeySwitch(out, UprightVst::KS_PalmMute, 75, std::max(0.0, ev.offsetBeats - 0.03), noteOffs);
                    // Slightly shorter notes feel more muted.
                    ev.lengthBeats = (ev.lengthBeats > 0.0) ? std::min(ev.lengthBeats, 0.65) : 0.65;
                    break;
                }
            }
        }

        // Natural harmonic: use sparingly as an intentional color at phrase ends.
        if (m_profile.artNaturalHarmonic && ctx.isPhraseEnd && ctx.beatInBar == 3) {
            for (auto& ev : out) {
                if (ev.role != BassEvent::Role::MusicalNote || ev.rest) continue;
                // Avoid harmonics on very low notes (tends to sound odd/unrealistic).
                if (ev.midiNote < 40) break;
                pushKeySwitch(out, UprightVst::KS_NaturalHarmonic, 75, std::max(0.0, ev.offsetBeats - 0.03), noteOffs);
                break;
            }
        }

        // Slide-in on section changes / phrase starts when the first note is a meaningful "arrival".
        if (m_profile.artSlideInOut) {
            for (const auto& ev : out) {
                if (ev.role != BassEvent::Role::MusicalNote) continue;
                if (ev.rest) continue;
                if (!(ctx.isNewBar && (ctx.isSectionChange || ctx.barInSection == 0) && ctx.beatInBar == 0)) continue;

                // Only when it's not extremely low (avoid "slide in" below fret 2 guidance).
                if (ev.midiNote < 30) continue;

                pushKeySwitch(out, UprightVst::KS_SlideInOut, 85, std::max(0.0, ev.offsetBeats - 0.03), noteOffs);
                break;
            }
        }

        // Slide-out: if we’re holding a long note (2-feel / tied broken time), optionally end it with slide-out.
        if (m_profile.artSlideInOut) {
            for (auto& ev : out) {
                if (ev.role != BassEvent::Role::MusicalNote || ev.rest) continue;
                if (ev.lengthBeats < 1.5) break;
                // Trigger slide-out late in the beat while the note is still sounding.
                pushKeySwitch(out, UprightVst::KS_SlideInOut, 85, 0.85, noteOffs);
                break;
            }
        }

        // Ensure Sustain keyswitch is present when articulations are enabled (keeps state consistent).
        if (m_profile.artSustainAccent) {
            // Put C0 at the very start of a bar (safe, stable), so any transient articulation reverts correctly.
            if (ctx.isNewBar && ctx.beatInBar == 0) {
                pushKeySwitch(out, UprightVst::KS_SustainAccent, 64, 0.0, noteOffs);
            }
        }

        // Prepare cross-beat overlap for *next* beat in normal walking modes.
        // We extend the main note slightly beyond the beat to enable true overlap if the next note is nearby.
        if (m_profile.artLegatoSlide || m_profile.artHammerPull) {
            if (m_phraseMode == 1 || m_phraseMode == 2) {
                int musicalCount = 0;
                BassEvent* mainEv = nullptr;
                for (auto& ev : out) {
                    if (ev.role != BassEvent::Role::MusicalNote || ev.rest) continue;
                    musicalCount++;
                    if (!mainEv || ev.offsetBeats < mainEv->offsetBeats) mainEv = &ev;
                }
                if (musicalCount == 1 && mainEv && !mainEv->ghost && mainEv->offsetBeats <= 0.15) {
                    mainEv->lengthBeats = (mainEv->lengthBeats > 0.0) ? std::max(mainEv->lengthBeats, 1.06) : 1.06;
                    m_pendingCrossBeatLegato = true;
                    m_pendingCrossBeatFromMidi = mainEv->midiNote;
                } else {
                    m_pendingCrossBeatLegato = false;
                    m_pendingCrossBeatFromMidi = -1;
                }
            } else {
                m_pendingCrossBeatLegato = false;
                m_pendingCrossBeatFromMidi = -1;
            }
        }

        resort();
    }

    return out;
}

} // namespace music

