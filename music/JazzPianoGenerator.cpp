#include "music/JazzPianoGenerator.h"

#include "music/ChordDictionary.h"
#include "music/Pitch.h"
#include "music/ScaleLibrary.h"

#include <algorithm>
#include <cmath>

namespace music {
namespace {

static int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

static double clamp01(double x) { return std::max(0.0, std::min(1.0, x)); }

static int pickNearestMidiForPcInRange(int pc, int lo, int hi, int target) {
    pc = normalizePc(pc);
    int best = -1;
    int bestDist = 1e9;

    // Scan octaves; range is small (<=128).
    for (int n = lo; n <= hi; ++n) {
        if (normalizePc(n) != pc) continue;
        const int d = std::abs(n - target);
        if (d < bestDist) { bestDist = d; best = n; }
    }
    if (best < 0) {
        // Fallback: clamp target then snap by semitone search.
        int t = clampInt(target, lo, hi);
        for (int delta = 0; delta <= 24; ++delta) {
            for (int sgn : {+1, -1}) {
                const int n = t + sgn * delta;
                if (n < lo || n > hi) continue;
                if (normalizePc(n) == pc) return n;
            }
        }
        return clampInt(target, lo, hi);
    }
    return best;
}

static int avgOrCenter(const QVector<int>& v, int center) {
    if (v.isEmpty()) return center;
    long long sum = 0;
    for (int n : v) sum += n;
    return int(std::llround(double(sum) / double(v.size())));
}

static QVector<int> sortedUniqueMidi(QVector<int> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

static QVector<int> midiCandidatesForPc(int pc, int lo, int hi) {
    QVector<int> out;
    pc = normalizePc(pc);
    for (int n = lo; n <= hi; ++n) {
        if (normalizePc(n) == pc) out.push_back(n);
    }
    return out;
}

static int avgOr(const QVector<int>& v, int fallback) {
    if (v.isEmpty()) return fallback;
    long long s = 0;
    for (int x : v) s += x;
    return int(std::llround(double(s) / double(v.size())));
}

static QVector<int> bestPairForPcs(int pcA,
                                   int pcB,
                                   int lo,
                                   int hi,
                                   int target,
                                   int minInterval,
                                   int minNote,
                                   int maxNote) {
    QVector<int> best;
    double bestCost = 1e18;

    const int lo2 = std::max(lo, minNote);
    const int hi2 = std::min(hi, maxNote);
    const QVector<int> aCand = midiCandidatesForPc(pcA, lo2, hi2);
    const QVector<int> bCand = midiCandidatesForPc(pcB, lo2, hi2);
    if (aCand.isEmpty() || bCand.isEmpty()) return best;

    for (int a : aCand) {
        for (int b : bCand) {
            if (a == b) continue;
            const int low = std::min(a, b);
            const int high = std::max(a, b);
            if (high - low < minInterval) continue;
            const double center = 0.5 * double(low + high);
            const double width = double(high - low);
            const double cost = std::abs(center - double(target)) + 0.18 * width;
            if (cost < bestCost) {
                bestCost = cost;
                best = {low, high};
            }
        }
    }
    return best;
}

static int bestSingleForPc(int pc, int lo, int hi, int target, int minNote) {
    const QVector<int> cand = midiCandidatesForPc(pc, std::max(lo, minNote), hi);
    if (cand.isEmpty()) return std::max(lo, std::min(hi, target));
    int best = cand[0];
    int bestDist = std::abs(best - target);
    for (int n : cand) {
        const int d = std::abs(n - target);
        if (d < bestDist) { bestDist = d; best = n; }
    }
    return best;
}

static QVector<int> orderPcsByDegreeFromRoot(int rootPc, QVector<int> pcs) {
    rootPc = normalizePc(rootPc);
    for (int& pc : pcs) pc = normalizePc(pc);
    std::sort(pcs.begin(), pcs.end(), [&](int a, int b) {
        const int da = normalizePc(a - rootPc);
        const int db = normalizePc(b - rootPc);
        return da < db;
    });
    pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
    return pcs;
}

static int chordThirdPcFromSymbol(const ChordSymbol& c) {
    if (c.rootPc < 0) return -1;
    int thirdIv = 4;
    if (c.quality == ChordQuality::Minor || c.quality == ChordQuality::HalfDiminished || c.quality == ChordQuality::Diminished) thirdIv = 3;
    if (c.quality == ChordQuality::Sus2) thirdIv = 2;
    if (c.quality == ChordQuality::Sus4) thirdIv = 5;
    if (c.quality == ChordQuality::Power5) return -1;
    return normalizePc(c.rootPc + thirdIv);
}

static int chordSeventhPcFromSymbol(const ChordSymbol& c) {
    if (c.rootPc < 0) return -1;
    int sevIv = -1;
    switch (c.seventh) {
    case SeventhQuality::Major7: sevIv = 11; break;
    case SeventhQuality::Minor7: sevIv = 10; break;
    case SeventhQuality::Dim7: sevIv = 9; break;
    default: sevIv = -1; break;
    }
    if (sevIv < 0) return -1;
    return normalizePc(c.rootPc + sevIv);
}

static int pcDistance(int a, int b) {
    a = normalizePc(a);
    b = normalizePc(b);
    int d = std::abs(a - b);
    return std::min(d, 12 - d);
}

} // namespace

JazzPianoGenerator::JazzPianoGenerator() {
    setProfile(defaultPianoProfile());
}

void JazzPianoGenerator::setProfile(const PianoProfile& p) {
    m_profile = p;
    m_rngState = (m_profile.humanizeSeed == 0u) ? 1u : m_profile.humanizeSeed;
    if (m_rngState == 0u) m_rngState = 1u;
}

void JazzPianoGenerator::reset() {
    m_lastLh.clear();
    m_lastRh.clear();
    m_lastVoicingHash = 0;
    m_planned.clear();
    m_lastPlannedGlobalBeat = -1;
    m_lastPatternId = -1;
    m_lastTopMidi = -1;
    m_pedalIsDown = false;
    m_pedalDownAtBeatTime = -1.0;
    m_pedalReleaseAtBeatTime = -1.0;
    m_rngState = (m_profile.humanizeSeed == 0u) ? 1u : m_profile.humanizeSeed;
    if (m_rngState == 0u) m_rngState = 1u;
}

quint32 JazzPianoGenerator::nextU32() {
    // xorshift32
    quint32 x = (m_rngState == 0u) ? 1u : m_rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m_rngState = x;
    return x;
}

double JazzPianoGenerator::next01() {
    return double(nextU32()) / double(0xFFFFFFFFu);
}

quint32 JazzPianoGenerator::hashNotes(const QVector<int>& notes) {
    quint32 h = 2166136261u;
    for (int n : notes) {
        h ^= quint32(n + 1);
        h *= 16777619u;
    }
    return h;
}

int JazzPianoGenerator::globalBeatIndex(const PianoBeatContext& ctx) const {
    return ctx.barIndex * 4 + ctx.beatInBar;
}

JazzPianoGenerator::VoicingPcs JazzPianoGenerator::buildTraditionalVoicingPcs(const ChordSymbol& chord,
                                                                              const ChordSymbol* nextChord,
                                                                              bool ballad,
                                                                              bool rootless) {
    VoicingPcs v;
    v.usedTension = false;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return v;

    const int r = normalizePc(chord.rootPc);
    // Third
    int thirdIv = 4;
    if (chord.quality == ChordQuality::Minor || chord.quality == ChordQuality::HalfDiminished || chord.quality == ChordQuality::Diminished) thirdIv = 3;
    if (chord.quality == ChordQuality::Sus2) thirdIv = 2;
    if (chord.quality == ChordQuality::Sus4) thirdIv = 5;
    const int third = normalizePc(r + thirdIv);

    // Seventh
    int sevIv = -1;
    switch (chord.seventh) {
    case SeventhQuality::Major7: sevIv = 11; break;
    case SeventhQuality::Minor7: sevIv = 10; break;
    case SeventhQuality::Dim7: sevIv = 9; break;
    default: sevIv = -1; break;
    }
    const int sev = (sevIv >= 0) ? normalizePc(r + sevIv) : -1;

    auto pushPc = [](QVector<int>& dst, int pc) {
        if (pc < 0) return;
        pc = normalizePc(pc);
        if (!dst.contains(pc)) dst.push_back(pc);
    };

    // LH: shell. RH: color tones. This is intentionally "standard jazz pianist" vocabulary.
    // Keep ballad slightly simpler but still rich enough to feel time.
    if (sev >= 0) {
        pushPc(v.lh, third);
        pushPc(v.lh, sev);
    } else {
        // No 7th: use 3rd + 5th as shell.
        int fifthIv = 7;
        if (chord.quality == ChordQuality::HalfDiminished || chord.quality == ChordQuality::Diminished) fifthIv = 6;
        else if (chord.quality == ChordQuality::Augmented) fifthIv = 8;
        pushPc(v.lh, third);
        pushPc(v.lh, normalizePc(r + fifthIv));
    }

    // RH color tones by quality.
    const int ninth = normalizePc(r + 14);
    const int eleventh = normalizePc(r + 17);
    const int thirteenth = normalizePc(r + 21);
    const int fifth = normalizePc(r + 7);

    auto maybeAdd = [&](int pc, double prob) {
        if (next01() < prob) {
            pushPc(v.rh, pc);
            v.usedTension = true;
        }
    };

    const double baseTension = clamp01(m_profile.tensionProb);
    const double t1 = ballad ? std::min(0.55, baseTension + 0.15) : baseTension;

    switch (chord.quality) {
    case ChordQuality::Major:
        // Maj7: "pretty" colors are 9 and 13. Avoid 11 on major (can sound pokey).
        if (ballad) {
            pushPc(v.rh, ninth);
            pushPc(v.rh, thirteenth);
        } else {
            maybeAdd(ninth, t1);
            maybeAdd(thirteenth, t1 * 0.45);
        }
        if (v.rh.size() < 2) pushPc(v.rh, fifth);
        break;
    case ChordQuality::Minor:
        // Min7: 9 + 11 is classic and very "Bill Evans" for ballads.
        if (ballad) {
            pushPc(v.rh, ninth);
            pushPc(v.rh, eleventh);
        } else {
            maybeAdd(ninth, t1);
            maybeAdd(eleventh, t1 * 0.55);
        }
        if (v.rh.size() < 2) pushPc(v.rh, fifth);
        break;
    case ChordQuality::Dominant:
        // Dom7: 9 + 13 is the "pretty" default. Keep it stable in ballads.
        if (ballad) {
            pushPc(v.rh, ninth);
            pushPc(v.rh, thirteenth);
        } else {
            maybeAdd(ninth, t1);
            maybeAdd(thirteenth, t1 * 0.75);
        }
        if (v.rh.size() < 2) pushPc(v.rh, fifth);
        break;
    case ChordQuality::HalfDiminished:
        // ø: keep it gentle—9 + 11 reads more "inside" than emphasizing b5.
        if (ballad) {
            pushPc(v.rh, ninth);
            pushPc(v.rh, eleventh);
        } else {
            maybeAdd(eleventh, t1 * 0.55);
            maybeAdd(ninth, t1 * 0.45);
        }
        if (v.rh.size() < 2) pushPc(v.rh, normalizePc(r + 6)); // b5 as fallback color
        break;
    case ChordQuality::Sus2:
    case ChordQuality::Sus4:
        // Sus: 9 + 13 is safe.
        if (ballad) {
            pushPc(v.rh, ninth);
            pushPc(v.rh, thirteenth);
        } else {
            maybeAdd(ninth, t1);
            maybeAdd(thirteenth, t1 * 0.6);
        }
        if (v.rh.size() < 2) pushPc(v.rh, fifth);
        break;
    default:
        maybeAdd(ninth, t1);
        if (v.rh.size() < 2) pushPc(v.rh, fifth);
        break;
    }

    // Root: generally left to bass; only add in swing or when chord is ambiguous.
    if (!ballad && !rootless && next01() < 0.12) {
        pushPc(v.lh, r);
    }

    return v;
}

JazzPianoGenerator::VoicingPcs JazzPianoGenerator::buildLiteralChordPcs(const ChordSymbol& chord) {
    VoicingPcs v;
    v.usedTension = false;
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return v;

    // EXACT chord content (extensions + alt + alterations) as pitch classes.
    QVector<int> pcs = ChordDictionary::chordPitchClasses(chord);
    pcs = orderPcsByDegreeFromRoot(chord.rootPc, pcs);

    const int root = normalizePc(chord.rootPc);
    const int bass = (chord.bassPc >= 0) ? normalizePc(chord.bassPc) : root;

    // Very plain split:
    // - LH: bass (slash bass if present, else root) + an optional fifth if present.
    // - RH: everything else (so we don't "reinterpret" the symbol).
    v.lh.push_back(bass);
    const int fifth = normalizePc(root + 7);
    if (pcs.contains(fifth) && fifth != bass) v.lh.push_back(fifth);

    for (int pc : pcs) {
        pc = normalizePc(pc);
        if (v.lh.contains(pc)) continue;
        v.rh.push_back(pc);
    }

    // If RH ended empty (e.g. power5), keep at least the root+5 somewhere.
    if (v.rh.isEmpty()) {
        for (int pc : pcs) if (!v.lh.contains(pc)) v.rh.push_back(pc);
    }

    return v;
}

JazzPianoGenerator::VoicingPcs JazzPianoGenerator::buildBasicChordPcs(const ChordSymbol& chord) {
    VoicingPcs v;
    v.usedTension = false;
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return v;

    // "Most correct, no fancy": root/3/5/7 only (ignore 9/11/13/alt/alterations entirely).
    QVector<int> pcs = ChordDictionary::basicTones(chord);
    pcs = orderPcsByDegreeFromRoot(chord.rootPc, pcs);

    const int root = normalizePc(chord.rootPc);
    const int bass = (chord.bassPc >= 0) ? normalizePc(chord.bassPc) : root;
    const int fifth = normalizePc(root + 7);

    // LH: bass (slash bass if present, else root) + optional 5th for stability.
    v.lh.push_back(bass);
    if (pcs.contains(fifth) && fifth != bass) v.lh.push_back(fifth);

    // RH: remaining basic tones (3/7, sometimes root if slash bass).
    for (int pc : pcs) {
        pc = normalizePc(pc);
        if (v.lh.contains(pc)) continue;
        v.rh.push_back(pc);
    }
    // Ensure RH has at least something beyond LH when possible.
    if (v.rh.isEmpty()) {
        for (int pc : pcs) {
            if (!v.lh.contains(pc)) { v.rh.push_back(pc); break; }
        }
    }

    return v;
}

JazzPianoGenerator::VoicingPcs JazzPianoGenerator::buildEvansVoicingPcs(const ChordSymbol& chord, bool ballad) {
    VoicingPcs v;
    v.usedTension = false;
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return v;

    const int r = normalizePc(chord.rootPc);

    auto pushPc = [](QVector<int>& dst, int pc) {
        if (pc < 0) return;
        pc = normalizePc(pc);
        if (!dst.contains(pc)) dst.push_back(pc);
    };

    // Use the actual chord pitch-class set (including extensions/alterations) so we don't
    // "misinterpret" symbols like C7alt, Cmaj7#11, etc.
    const QVector<int> pcsAll = ChordDictionary::chordPitchClasses(chord);

    auto hasPc = [&](int pc) -> bool { return pcsAll.contains(normalizePc(pc)); };
    auto hasIv = [&](int iv) -> bool { return hasPc(r + iv); };

    auto pcForIv = [&](int iv) -> int { return normalizePc(r + iv); };

    auto pickFirstAvailable = [&](const QVector<int>& ivs) -> int {
        for (int iv : ivs) {
            if (hasIv(iv)) return pcForIv(iv);
        }
        return -1;
    };

    // Core tones from quality/seventh (but validated against pcsAll for safety).
    int thirdIv = 4;
    if (chord.quality == ChordQuality::Minor || chord.quality == ChordQuality::HalfDiminished || chord.quality == ChordQuality::Diminished) thirdIv = 3;
    if (chord.quality == ChordQuality::Sus2) thirdIv = 2;
    if (chord.quality == ChordQuality::Sus4) thirdIv = 5;
    const int third = normalizePc(r + thirdIv);

    int sevIv = -1;
    switch (chord.seventh) {
    case SeventhQuality::Major7: sevIv = 11; break;
    case SeventhQuality::Minor7: sevIv = 10; break;
    case SeventhQuality::Dim7: sevIv = 9; break;
    default: sevIv = -1; break;
    }
    const int sev = (sevIv >= 0) ? normalizePc(r + sevIv) : -1;

    const int fifth = pcForIv(7);

    // LH: prefer 3+7, but fall back if the symbol doesn't actually contain them (safety).
    const bool hasThird = hasPc(third);
    const bool hasSev = (sev >= 0) && hasPc(sev);
    if (hasThird && hasSev) {
        pushPc(v.lh, third);
        pushPc(v.lh, sev);
    } else if (hasThird) {
        pushPc(v.lh, third);
        // try 7 then 5
        const int sevPc = pickFirstAvailable({11, 10, 9});
        if (sevPc >= 0) pushPc(v.lh, sevPc);
        else if (hasPc(fifth)) pushPc(v.lh, fifth);
    } else {
        // As last resort, use basic tones.
        const QVector<int> basic = ChordDictionary::basicTones(chord);
        for (int pc : basic) pushPc(v.lh, pc);
        while (v.lh.size() > 2) v.lh.removeLast();
    }

    // RH: symbol-aware color selection (choose two) from the actual chord set.
    // We interpret common symbols "musically": prefer 9/13 (and #11 when present) for beauty,
    // but if the symbol is altered, prefer the altered tones so we aren't "wrong".
    int c1 = -1;
    int c2 = -1;

    const bool isAltered = chord.alt || !chord.alterations.isEmpty();
    const bool wants11 = (chord.extension >= 11) || hasIv(5) || hasIv(6);
    const bool wants13 = (chord.extension >= 13) || hasIv(9) || hasIv(8);

    switch (chord.quality) {
    case ChordQuality::Major: {
        // Maj: 9 + 13; if #11 present, use it as a color tone.
        if (hasIv(6)) { // #11
            c1 = pcForIv(6);
            c2 = pickFirstAvailable({2, 9}); // 9 then 13
        } else {
            c1 = pickFirstAvailable({2, 9}); // 9 then 13
            c2 = pickFirstAvailable({9, 2}); // 13 then 9
        }
        break;
    }
    case ChordQuality::Minor: {
        // Min: 9 + 11 is the inside sound; allow 13 if explicitly present.
        c1 = pickFirstAvailable({2, 5}); // 9 then 11
        c2 = pickFirstAvailable({5, 2, wants13 ? 9 : -999}); // 11 then 9 then (optional) 13
        break;
    }
    case ChordQuality::Dominant: {
        if (isAltered) {
            // Alt dominant: pick one altered 9 (b9/#9 if present) and one of b13/#11/b5/#5 if present.
            c1 = pickFirstAvailable({1, 3, 2});       // b9, #9, else natural 9
            c2 = pickFirstAvailable({8, 6, 9, 5, 7});  // b13/#5, #11/b5, 13, 11, 5
        } else {
            // Inside dom: 9 + 13; include 11/#11 only if explicitly present.
            if (hasIv(6) && wants11) {
                c1 = pcForIv(6);
                c2 = pickFirstAvailable({2, 9});
            } else {
                c1 = pickFirstAvailable({2, 9});
                c2 = pickFirstAvailable({9, 2});
            }
        }
        break;
    }
    case ChordQuality::HalfDiminished:
    case ChordQuality::Diminished: {
        // ø / dim: prioritize 11 and 9; include b5 only if present/needed.
        c1 = pickFirstAvailable({5, 2});      // 11 then 9
        c2 = pickFirstAvailable({2, 6, 8});   // 9 then b5/#11 then b13 (rare)
        break;
    }
    case ChordQuality::Sus2:
    case ChordQuality::Sus4: {
        // Sus: 9 + 13; include #11 if explicitly in symbol.
        c1 = pickFirstAvailable({2, 9});
        c2 = pickFirstAvailable({9, 2, hasIv(6) ? 6 : -999});
        break;
    }
    default: {
        c1 = pickFirstAvailable({2, 9, 5});
        c2 = pickFirstAvailable({9, 2, 7});
        break;
    }
    }

    // Fall back to any non-basic chord tones if needed.
    auto isBasic = [&](int pc) -> bool {
        pc = normalizePc(pc);
        if (pc == r) return true;
        for (int x : v.lh) if (normalizePc(x) == pc) return true;
        if (hasThird && pc == third) return true;
        if (hasSev && pc == sev) return true;
        if (pc == fifth) return true;
        return false;
    };
    if (c1 < 0 || c2 < 0 || c1 == c2) {
        QVector<int> colors;
        for (int pc : pcsAll) if (!isBasic(pc)) colors.push_back(pc);
        if (colors.size() >= 1 && c1 < 0) c1 = colors[0];
        if (colors.size() >= 2 && (c2 < 0 || c2 == c1)) c2 = colors[1];
    }
    if (c1 >= 0) { pushPc(v.rh, c1); v.usedTension = true; }
    if (c2 >= 0) { pushPc(v.rh, c2); v.usedTension = true; }

    // Keep exactly 2+2 for ballads (stable, “voicing language”).
    if (ballad) {
        while (v.lh.size() > 2) v.lh.removeLast();
        while (v.rh.size() > 2) v.rh.removeLast();
    }

    // If we still ended up without RH tones, use 5 as an inside fallback.
    if (v.rh.isEmpty() && hasPc(fifth)) pushPc(v.rh, fifth);

    // Note: placement/top-voice continuity is handled later in realize stage.
    return v;
}

QVector<int> JazzPianoGenerator::chooseVoicingPitchClasses(const ChordSymbol& chord,
                                                           bool rootless,
                                                           bool& outUsedTension) {
    outUsedTension = false;
    // Traditional jazz ballad/swing: build from guide tones with *safe* extensions.
    // Avoid avant-garde/random clusters and quartal substitutions unless explicitly requested by profile.

    // Build guide-tone core: 3rd + 7th if present, else fall back to basic tones.
    const QVector<int> basic = ChordDictionary::basicTones(chord);
    QVector<int> core;

    // Attempt to find 3rd and 7th pitch classes in basic tones by interval logic.
    // We can infer via chord quality and seventh.
    int thirdPc = -1;
    int seventhPc = -1;
    if (chord.rootPc >= 0) {
        int thirdIv = 4;
        if (chord.quality == ChordQuality::Minor || chord.quality == ChordQuality::HalfDiminished || chord.quality == ChordQuality::Diminished) thirdIv = 3;
        if (chord.quality == ChordQuality::Sus2) thirdIv = 2;
        if (chord.quality == ChordQuality::Sus4) thirdIv = 5;
        thirdPc = normalizePc(chord.rootPc + thirdIv);

        int sevIv = -1;
        switch (chord.seventh) {
        case SeventhQuality::Major7: sevIv = 11; break;
        case SeventhQuality::Minor7: sevIv = 10; break;
        case SeventhQuality::Dim7: sevIv = 9; break;
        case SeventhQuality::None: default: sevIv = -1; break;
        }
        if (sevIv >= 0) seventhPc = normalizePc(chord.rootPc + sevIv);
    }

    auto pushIf = [&](QVector<int>& v, int pc) {
        if (pc < 0) return;
        pc = normalizePc(pc);
        if (!v.contains(pc)) v.push_back(pc);
    };

    const bool isBallad = (m_profile.feelStyle == PianoFeelStyle::Ballad);

    // For traditional comping, start from guide tones.
    pushIf(core, thirdPc);    // for sus chords this becomes the sus tone (2 or 4)
    pushIf(core, seventhPc);  // if present
    if (core.isEmpty()) {
        for (int pc : basic) pushIf(core, pc);
    }

    // Decide whether to keep root.
    const bool includeRoot = (!rootless) && (!isBallad); // ballads almost always assume bass covers root
    if (includeRoot) pushIf(core, chord.rootPc);

    // Add safe tensions (idiomatic hotel-bar ballad language):
    // - Maj7: add 9; optionally 6/13 if requested by symbol
    // - Min7: add 9; optionally 11
    // - Dom7: add 9 and/or 13 (avoid b9/#9 unless explicitly altered)
    // - Half-dim: add 11 or 9 (lightly)
    auto addSafeTensions = [&]() {
        if (next01() >= clamp01(m_profile.tensionProb)) return;

        auto hasAlter = [&](int degree, int delta) -> bool {
            for (const auto& a : chord.alterations) {
                if (a.degree == degree && a.delta == delta) return true;
            }
            return false;
        };

        QVector<int> wants;
        wants.reserve(2);

        switch (chord.quality) {
        case ChordQuality::Major:
            if (chord.seventh == SeventhQuality::Major7) {
                wants.push_back(normalizePc(chord.rootPc + 14)); // 9
                // Optional #11 only if explicitly present.
                if (hasAlter(11, +1)) wants.push_back(normalizePc(chord.rootPc + 18)); // #11
                // If symbol indicates 6/13, allow 13 color.
                if (chord.extension == 6 || chord.extension >= 13) wants.push_back(normalizePc(chord.rootPc + 21)); // 13
            } else {
                // Plain major: keep it simple.
                wants.push_back(normalizePc(chord.rootPc + 14)); // 9
            }
            break;
        case ChordQuality::Minor:
            wants.push_back(normalizePc(chord.rootPc + 14)); // 9
            if (chord.extension >= 11) wants.push_back(normalizePc(chord.rootPc + 17)); // 11
            break;
        case ChordQuality::Dominant:
            wants.push_back(normalizePc(chord.rootPc + 14)); // 9
            if (chord.extension >= 13) wants.push_back(normalizePc(chord.rootPc + 21)); // 13
            // If explicitly altered, we may include that alteration, but keep it conservative on ballads.
            if (!isBallad && (chord.alt || !chord.alterations.isEmpty())) {
                if (hasAlter(9, -1)) wants.push_back(normalizePc(chord.rootPc + 13)); // b9
                else if (hasAlter(9, +1)) wants.push_back(normalizePc(chord.rootPc + 15)); // #9
            }
            break;
        case ChordQuality::HalfDiminished:
            wants.push_back(normalizePc(chord.rootPc + 17)); // 11
            wants.push_back(normalizePc(chord.rootPc + 14)); // 9
            break;
        default:
            break;
        }

        // Add at most one tension for ballads, up to two for swing.
        const int maxAdd = isBallad ? 1 : 2;
        int added = 0;
        for (int pc : wants) {
            if (added >= maxAdd) break;
            if (pc == normalizePc(chord.rootPc)) continue;
            if (pc == thirdPc) continue;
            if (pc == seventhPc) continue;
            pushIf(core, pc);
            added++;
        }
        if (added > 0) outUsedTension = true;
    };

    addSafeTensions();

    // Keep unique.
    QVector<int> out;
    for (int pc : core) pushIf(out, pc);
    return out;
}

QVector<int> JazzPianoGenerator::realizeToMidi(const QVector<int>& pcs,
                                               int lo,
                                               int hi,
                                               const QVector<int>& prev,
                                               int maxLeap) const {
    if (pcs.isEmpty()) return {};

    const int center = (lo + hi) / 2;
    const int target = avgOrCenter(prev, center);
    QVector<int> out;
    out.reserve(pcs.size());

    // Greedy: choose each pitch class near the target, then spread to avoid extreme clustering.
    for (int pc : pcs) {
        int n = pickNearestMidiForPcInRange(pc, lo, hi, target);
        if (!prev.isEmpty() && maxLeap > 0) {
            // Clamp to a max leap from previous average.
            const int delta = n - target;
            if (std::abs(delta) > maxLeap) {
                n = clampInt(target + (delta > 0 ? maxLeap : -maxLeap), lo, hi);
                n = pickNearestMidiForPcInRange(pc, lo, hi, n);
            }
        }
        out.push_back(n);
    }
    out = sortedUniqueMidi(out);

    // Ensure the voicing spans at least a 5th if possible (open sound).
    if (out.size() >= 3) {
        const int span = out.last() - out.first();
        if (span < 7) {
            // Try to drop the lowest by an octave if within range.
            int n0 = out.first() - 12;
            if (n0 >= lo && normalizePc(n0) == normalizePc(out.first())) {
                out[0] = n0;
                out = sortedUniqueMidi(out);
            }
        }
    }

    return out;
}

void JazzPianoGenerator::planBar(const PianoBeatContext& ctx, const ChordSymbol& cur, const ChordSymbol* nextChord) {
    const bool ballad = (m_profile.feelStyle == PianoFeelStyle::Ballad);
    const int g0 = ctx.barIndex * 4;

    // Choose a bar-level comping pattern. These are common pro patterns, not random hits.
    // Offsets are within-beat; the scheduler will humanize timing.
    struct Hit { int beat; double offset; double velMul; double lenMul; bool anticipation; };
    QVector<Hit> pattern;
    pattern.reserve(6);

    const bool phraseEnd = ctx.isPhraseEnd || (ctx.beatInBar == 3);

    if (ballad) {
        // Ballad patterns: "2-feel" foundation + tasteful offbeat motion.
        // Important: keep harmony safe (same chord on offbeats), but avoid the dead air on 2 and 4.
        const double dens = clamp01(m_profile.compDensity);
        const double phraseT = (ctx.phraseLengthBars <= 1) ? 0.0
            : double(ctx.barInSection % ctx.phraseLengthBars) / double(ctx.phraseLengthBars - 1);
        const double midPhrase = std::sin(M_PI * phraseT); // 0..1..0

        struct Pat { int id; double weight; QVector<Hit> hits; };
        QVector<Pat> pats;
        pats.reserve(8);

        // A: plain 1 + 3
        pats.push_back({0, 0.30, { {0, 0.0, 1.00, 1.05, false},
                                   {2, 0.0, 0.92, 0.95, false} }});
        // B: add 2& (push into 3)
        pats.push_back({1, 0.28 + 0.25 * dens, { {0, 0.0, 1.00, 1.00, false},
                                                 {1, 0.5, 0.62, 0.55, false},
                                                 {2, 0.0, 0.92, 0.90, false} }});
        // C: add 4& (pickup into next bar)
        pats.push_back({2, 0.22 + 0.22 * dens, { {0, 0.0, 1.00, 1.00, false},
                                                 {2, 0.0, 0.90, 0.92, false},
                                                 {3, 0.5, 0.58, 0.45, false} }});
        // D: both 2& and 4&
        pats.push_back({3, 0.18 + 0.30 * dens, { {0, 0.0, 1.00, 0.98, false},
                                                 {1, 0.5, 0.60, 0.50, false},
                                                 {2, 0.0, 0.90, 0.88, false},
                                                 {3, 0.5, 0.56, 0.42, false} }});
        // E: Charleston-ish (1 then 1&)
        pats.push_back({4, 0.10 + 0.10 * dens, { {0, 0.0, 1.00, 0.88, false},
                                                 {0, 0.5, 0.64, 0.52, false},
                                                 {2, 0.0, 0.90, 0.88, false} }});
        // F: 1, 2&, 4 (gentle backbeat color; common ballad comp cell)
        pats.push_back({5, (0.12 + 0.22 * dens) * (0.55 + 0.75 * midPhrase), { {0, 0.0, 1.00, 0.92, false},
                                                                               {1, 0.5, 0.62, 0.50, false},
                                                                               {3, 0.0, 0.72, 0.65, false} }});
        // G: 1&, 3 (light push then settle)
        pats.push_back({6, (0.10 + 0.18 * dens) * (0.60 + 0.65 * midPhrase), { {0, 0.5, 0.70, 0.55, false},
                                                                               {2, 0.0, 0.95, 0.95, false} }});
        // H: 1, 3&, 4 (late-in-bar lift; feels like breathing)
        pats.push_back({7, (0.08 + 0.16 * dens) * (0.55 + 0.80 * midPhrase), { {0, 0.0, 1.00, 0.98, false},
                                                                               {2, 0.5, 0.62, 0.48, false},
                                                                               {3, 0.0, 0.70, 0.65, false} }});
        // I: 1, 2, 3 (more "inside" support when harmony is moving)
        pats.push_back({8, (0.10 + 0.12 * dens) * (0.45 + 0.55 * midPhrase), { {0, 0.0, 1.00, 0.95, false},
                                                                               {1, 0.0, 0.78, 0.80, false},
                                                                               {2, 0.0, 0.92, 0.90, false} }});

        auto pickPattern = [&]() -> QVector<Hit> {
            double sum = 0.0;
            for (const auto& p : pats) sum += std::max(0.0, p.weight);
            const double r = next01() * (sum > 0.0 ? sum : 1.0);
            double acc = 0.0;
            int chosen = pats[0].id;
            QVector<Hit> outHits = pats[0].hits;
            for (const auto& p : pats) {
                acc += std::max(0.0, p.weight);
                if (r <= acc) { chosen = p.id; outHits = p.hits; break; }
            }
            // Avoid repeating the exact same pattern back-to-back.
            if (chosen == m_lastPatternId && pats.size() > 1) {
                const double r2 = next01() * (sum > 0.0 ? sum : 1.0);
                acc = 0.0;
                for (const auto& p : pats) {
                    if (p.id == m_lastPatternId) continue;
                    acc += std::max(0.0, p.weight);
                    if (r2 <= acc) { chosen = p.id; outHits = p.hits; break; }
                }
            }
            m_lastPatternId = chosen;
            return outHits;
        };

        pattern = pickPattern();

        // Phrase ends: thin out and let it breathe.
        if (phraseEnd && next01() < 0.40 && pattern.size() > 2) {
            pattern.resize(2);
        }
    } else {
        // Swing comp patterns (simplified for now): 1, 2&, 3, 4&
        pattern = { {0, 0.0, 1.00, 0.85, false},
                    {1, 0.5, 0.70, 0.55, false},
                    {2, 0.0, 0.90, 0.75, false},
                    {3, 0.5, 0.74, 0.55, false} };
        if (phraseEnd && next01() < 0.25) {
            // Add a small “kick” on 4 (not a lick, just comp punctuation).
            pattern.push_back({3, 0.0, 0.85, 0.55, false});
        }
    }

    // Determine chord lookahead for this bar (best-effort).
    auto chordAtBeat = [&](int b) -> const ChordSymbol& {
        const int idx = b;
        if (idx >= 0 && idx < ctx.lookaheadChords.size()) return ctx.lookaheadChords[idx];
        return cur;
    };
    auto sameHarmony = [&](const ChordSymbol& a, const ChordSymbol& b) -> bool {
        return (a.rootPc == b.rootPc && a.quality == b.quality && a.seventh == b.seventh);
    };

    // If harmony changes on a beat within the bar, make sure we comp on the change.
    if (!ballad) {
        // swing already dense enough; keep as-is.
    } else {
        for (int b = 1; b <= 3 && b < ctx.lookaheadChords.size(); ++b) {
            const auto& prev = chordAtBeat(b - 1);
            const auto& now = chordAtBeat(b);
            if (now.placeholder || now.noChord || now.rootPc < 0) continue;
            if (!sameHarmony(prev, now)) {
                bool has = false;
                for (const auto& h : pattern) {
                    if (h.beat == b && std::abs(h.offset) < 1e-6 && !h.anticipation) { has = true; break; }
                }
                if (!has) pattern.push_back({b, 0.0, 0.82, 0.70, false});
            }
        }
    }

    // Add anticipation into next chord change: a single top voice approach tone (not a lick).
    // We only do this when harmony changes at/after the next beat.
    auto findNextDifferentChord = [&]() -> const ChordSymbol* {
        for (int i = 1; i < ctx.lookaheadChords.size(); ++i) {
            const auto& c = ctx.lookaheadChords[i];
            if (c.placeholder || c.noChord || c.rootPc < 0) continue;
            if (c.rootPc != cur.rootPc || c.quality != cur.quality || c.seventh != cur.seventh) return &c;
        }
        return nullptr;
    };
    const ChordSymbol* upcoming = findNextDifferentChord();
    // Disable random approach tones for now; they read "wrong" without deep targeting logic.
    (void)upcoming;

    // Materialize events per hit (and stash into m_planned).
    for (const Hit& h : pattern) {
        const ChordSymbol& chordHere = chordAtBeat(h.beat);
        if (chordHere.noChord || chordHere.placeholder || chordHere.rootPc < 0) continue;

        // HARD "correctness" mode: basic chord tones only (root/3/5/7).
        // No fancy voicings, no extensions, no altered colors.
        const bool basicMode = true;
        VoicingPcs pcs = basicMode
            ? buildBasicChordPcs(chordHere)
            : (ballad
                   ? buildEvansVoicingPcs(chordHere, /*ballad=*/true)
                   : buildTraditionalVoicingPcs(chordHere, nextChord, /*ballad=*/false,
                                                (m_profile.preferRootless && next01() < clamp01(m_profile.rootlessProb))));

        // Realize notes.
        const int lhCenter = (m_profile.lhMinMidiNote + m_profile.lhMaxMidiNote) / 2;
        const int rhCenter = (m_profile.rhMinMidiNote + m_profile.rhMaxMidiNote) / 2;
        const int lhTarget = avgOr(m_lastLh, lhCenter);
        const int rhTarget = avgOr(m_lastRh, rhCenter);

        QVector<int> lhNotes;
        QVector<int> rhNotes;
        QString chordFn = "Comp";
        QString chordWhy = "basic tones only";

        if (basicMode) {
            // Place requested basic tones, but make the *top voice* intentionally lead through changes
            // (still chord tones only: no non-chord passing notes yet).
            lhNotes = realizeToMidi(pcs.lh, m_profile.lhMinMidiNote, m_profile.lhMaxMidiNote, m_lastLh, m_profile.maxHandLeap);
            lhNotes = sortedUniqueMidi(lhNotes);

            const int lhTop = !lhNotes.isEmpty() ? lhNotes.last() : m_profile.lhMaxMidiNote;
            const int rhLo = std::max(m_profile.rhMinMidiNote, lhTop + 3);

            auto findNextDifferentFromHere = [&]() -> const ChordSymbol* {
                for (int b = h.beat + 1; b < ctx.lookaheadChords.size() && b <= 3; ++b) {
                    const auto& c = chordAtBeat(b);
                    if (c.placeholder || c.noChord || c.rootPc < 0) continue;
                    if (!sameHarmony(chordHere, c)) return &c;
                }
                return nullptr;
            };
            const ChordSymbol* nextDiff = findNextDifferentFromHere();

            const QVector<int> hereBasic = ChordDictionary::basicTones(chordHere);
            const QVector<int> nextBasic = nextDiff ? ChordDictionary::basicTones(*nextDiff) : QVector<int>{};

            auto containsPc = [](const QVector<int>& v, int pc) -> bool {
                pc = normalizePc(pc);
                for (int x : v) if (normalizePc(x) == pc) return true;
                return false;
            };

            const int here3 = chordThirdPcFromSymbol(chordHere);
            const int here7 = chordSeventhPcFromSymbol(chordHere);
            const int next3 = nextDiff ? chordThirdPcFromSymbol(*nextDiff) : -1;
            const int next7 = nextDiff ? chordSeventhPcFromSymbol(*nextDiff) : -1;

            const bool hasHere3 = (here3 >= 0) && containsPc(hereBasic, here3);
            const bool hasHere7 = (here7 >= 0) && containsPc(hereBasic, here7);
            const bool hasNext3 = (next3 >= 0) && containsPc(nextBasic, next3);
            const bool hasNext7 = (next7 >= 0) && containsPc(nextBasic, next7);

            int topPc = -1;
            if (nextDiff && chordHere.quality == ChordQuality::Dominant && hasHere7 && hasNext3) {
                topPc = here7;
                chordWhy = "basic tones; top voice=7th (dominant) → resolves to next 3rd";
            } else if (nextDiff) {
                // Prefer common tone.
                for (int pc : hereBasic) {
                    if (containsPc(nextBasic, pc)) {
                        topPc = normalizePc(pc);
                        chordWhy = "basic tones; top voice=common tone";
                        break;
                    }
                }
                // Otherwise, aim toward next 3rd/7th by smallest pitch-class move.
                if (topPc < 0) {
                    const int target = hasNext3 ? next3 : (hasNext7 ? next7 : -1);
                    if (target >= 0) {
                        int bestPc = -1;
                        int bestD = 99;
                        for (int pc : hereBasic) {
                            const int d = pcDistance(pc, target);
                            if (d < bestD) { bestD = d; bestPc = normalizePc(pc); }
                        }
                        if (bestPc >= 0) {
                            topPc = bestPc;
                            chordWhy = hasNext3 ? "basic tones; top voice→next 3rd" : "basic tones; top voice→next 7th";
                        }
                    }
                }
            }

            if (topPc < 0) {
                if (hasHere3) { topPc = here3; chordWhy = "basic tones; top voice=3rd"; }
                else if (hasHere7) { topPc = here7; chordWhy = "basic tones; top voice=7th"; }
                else if (!hereBasic.isEmpty()) { topPc = normalizePc(hereBasic.last()); chordWhy = "basic tones; top voice=chord tone"; }
            }

            const int topTargetMidi = (m_lastTopMidi >= 0) ? m_lastTopMidi : rhTarget;
            const int topMidi = (topPc >= 0)
                ? bestSingleForPc(topPc, m_profile.rhMinMidiNote, m_profile.rhMaxMidiNote, topTargetMidi, rhLo)
                : clampInt(topTargetMidi, rhLo, m_profile.rhMaxMidiNote);

            // Optional resolved tension: a single diatonic neighbor that resolves into the top voice.
            // Only if we have time before the chord hit (i.e., comp offset is on an upbeat).
            const bool allowNeighbor = (h.offset >= 0.25) && (next01() < (ballad ? 0.38 : 0.28));
            if (allowNeighbor && topPc >= 0) {
                const auto types = ScaleLibrary::suggestForChord(chordHere);
                if (!types.isEmpty()) {
                    const auto& sc = ScaleLibrary::get(types.first());
                    QVector<int> scalePcs;
                    scalePcs.reserve(sc.intervals.size());
                    for (int iv : sc.intervals) scalePcs.push_back(normalizePc(chordHere.rootPc + iv));

                    auto inScale = [&](int pc) -> bool { return scalePcs.contains(normalizePc(pc)); };
                    auto inChord = [&](int pc) -> bool { return hereBasic.contains(normalizePc(pc)); };

                    int neighPc = -1;
                    // Prefer diatonic step if available (±2), else chromatic (±1) only if it's in the suggested scale.
                    for (int d : {2, -2, 1, -1}) {
                        const int cand = normalizePc(topPc + d);
                        if (!inScale(cand)) continue;
                        if (cand == normalizePc(topPc)) continue;
                        // Prefer a true non-chord tone for tension.
                        if (!inChord(cand)) { neighPc = cand; break; }
                        if (neighPc < 0) neighPc = cand;
                    }

                    if (neighPc >= 0) {
                        const int neighMidi = bestSingleForPc(neighPc,
                                                              m_profile.rhMinMidiNote, m_profile.rhMaxMidiNote,
                                                              topMidi - 2,
                                                              rhLo);
                        // Schedule neighbor earlier in the same beat; it resolves into the chord hit at h.offset.
                        PianoEvent nv;
                        nv.kind = PianoEvent::Kind::Note;
                        nv.midiNote = neighMidi;
                        nv.velocity = 0; // filled later once we compute vel; store as 0 sentinel
                        nv.offsetBeats = std::max(0.0, h.offset - 0.18);
                        nv.lengthBeats = 0.14;
                        if (m_profile.reasoningLogEnabled) {
                            nv.function = "Approach";
                            nv.reasoning = QString("neighbor %1→%2 (%3)")
                                               .arg(normalizePc(neighPc - chordHere.rootPc))
                                               .arg(normalizePc(topPc - chordHere.rootPc))
                                               .arg(sc.name);
                        }
                        // Stash temporarily in a local scratch bucket; we'll push it after we know vel.
                        // (we keep it by appending to a local vector and later into bucket)
                        // We'll append it immediately to the planned bucket here; velocity will be fixed below.
                        m_planned[g0 + h.beat].push_back(nv);
                    }
                }
            }

            // Supporting tone below (prefer the other guide tone).
            int supportPc = -1;
            if (hasHere3 && normalizePc(here3) != normalizePc(topPc)) supportPc = here3;
            else if (hasHere7 && normalizePc(here7) != normalizePc(topPc)) supportPc = here7;
            else {
                for (int pc : hereBasic) {
                    pc = normalizePc(pc);
                    if (pc == normalizePc(topPc)) continue;
                    supportPc = pc;
                    break;
                }
            }

            QVector<int> rhChosen;
            if (supportPc >= 0) {
                const int supMidi = bestSingleForPc(supportPc, m_profile.rhMinMidiNote, m_profile.rhMaxMidiNote, topMidi - 7, rhLo);
                if (supMidi < topMidi - 2) rhChosen = { supMidi, topMidi };
                else rhChosen = { topMidi };
            } else {
                rhChosen = { topMidi };
            }
            rhNotes = sortedUniqueMidi(rhChosen);
        } else if (pcs.lh.size() >= 2) {
            const int pcA = normalizePc(pcs.lh[0]);
            const int pcB = normalizePc(pcs.lh[1]);
            int dist = std::abs(pcA - pcB);
            dist = std::min(dist, 12 - dist);
            // Low tritones / seconds in the LH shell read harsh. Force them to be "compound" (spread wider).
            const bool harsh = (dist <= 2) || (dist == 6);
            const int lhMinInterval = harsh ? 11 : (ballad ? 7 : 5);
            lhNotes = bestPairForPcs(pcs.lh[0], pcs.lh[1],
                                     m_profile.lhMinMidiNote, m_profile.lhMaxMidiNote,
                                     lhTarget,
                                     lhMinInterval,
                                     m_profile.lhMinMidiNote, m_profile.lhMaxMidiNote);
        } else if (pcs.lh.size() == 1) {
            lhNotes = { bestSingleForPc(pcs.lh[0],
                                        m_profile.lhMinMidiNote, m_profile.lhMaxMidiNote,
                                        lhTarget,
                                        m_profile.lhMinMidiNote) };
        }

        if (!basicMode) {
            lhNotes = sortedUniqueMidi(lhNotes);
            const int lhTop = !lhNotes.isEmpty() ? lhNotes.last() : m_profile.lhMaxMidiNote;
            const int rhMin = std::max(m_profile.rhMinMidiNote, lhTop + 6);

            // RH: pick a "top voice" that moves gently across chords (more melodic, less random color stacking).
            const int topTarget = (m_lastTopMidi >= 0) ? m_lastTopMidi : rhTarget;
            const int rhMinInterval = ballad ? 4 : 3;

            if (ballad && pcs.rh.size() >= 2) {
                // Choose which pc becomes the top voice by nearest match to last top.
                int topIdx = 0;
                int bestDist = 1e9;
                for (int i = 0; i < pcs.rh.size(); ++i) {
                    const int cand = bestSingleForPc(pcs.rh[i], m_profile.rhMinMidiNote, m_profile.rhMaxMidiNote, topTarget, rhMin);
                    const int d = std::abs(cand - topTarget);
                    if (d < bestDist) { bestDist = d; topIdx = i; }
                }

                const int topPc = pcs.rh[topIdx];
                int otherPc = pcs.rh[(topIdx == 0) ? 1 : 0];

                const int topMidi = bestSingleForPc(topPc, m_profile.rhMinMidiNote, m_profile.rhMaxMidiNote, topTarget, rhMin);
                int botMidi = bestSingleForPc(otherPc, m_profile.rhMinMidiNote, m_profile.rhMaxMidiNote, topMidi - 5, rhMin);
                if (botMidi > topMidi - rhMinInterval) {
                    // Try swapping the other tone if there are more options.
                    for (int i = 0; i < pcs.rh.size(); ++i) {
                        if (i == topIdx) continue;
                        const int altPc = pcs.rh[i];
                        const int altBot = bestSingleForPc(altPc, m_profile.rhMinMidiNote, m_profile.rhMaxMidiNote, topMidi - 5, rhMin);
                        if (altBot <= topMidi - rhMinInterval) { otherPc = altPc; botMidi = altBot; break; }
                    }
                }
                if (botMidi <= topMidi - rhMinInterval) rhNotes = { botMidi, topMidi };
                else rhNotes = { topMidi };
            } else if (pcs.rh.size() >= 2) {
                rhNotes = bestPairForPcs(pcs.rh[0], pcs.rh[1],
                                         m_profile.rhMinMidiNote, m_profile.rhMaxMidiNote,
                                         rhTarget,
                                         rhMinInterval,
                                         rhMin, m_profile.rhMaxMidiNote);
            }
            if (rhNotes.isEmpty() && pcs.rh.size() >= 1) {
                rhNotes = { bestSingleForPc(pcs.rh[0],
                                            m_profile.rhMinMidiNote, m_profile.rhMaxMidiNote,
                                            topTarget,
                                            rhMin) };
            }
            rhNotes = sortedUniqueMidi(rhNotes);
        }

        // Voice-leading memory update on each *comp hit* (so anticipations lead somewhere sensible).
        if (!lhNotes.isEmpty()) m_lastLh = lhNotes;
        if (!rhNotes.isEmpty()) {
            m_lastRh = rhNotes;
            m_lastTopMidi = rhNotes.last();
        }

        // Determine velocity and length from pattern slot.
        const int maxVel = ballad ? 84 : 96;
        const int baseVel = std::min(maxVel,
                                     clampInt(m_profile.baseVelocity + int(std::llround((next01() * 2.0 - 1.0) * double(m_profile.velocityVariance))), 1, 127));
        double beatMul = 1.0;
        if (h.beat == 0) beatMul *= m_profile.accentDownbeat;
        if (h.beat == 2) beatMul *= 1.05;
        double phraseMul = 1.0;
        if (ctx.phraseLengthBars > 0) {
            const int idx = (ctx.barInSection % ctx.phraseLengthBars);
            const double t = (ctx.phraseLengthBars <= 1) ? 0.0 : double(idx) / double(ctx.phraseLengthBars - 1);
            phraseMul = ballad ? (0.92 + 0.18 * std::sin(M_PI * t)) : (0.96 + 0.10 * std::sin(M_PI * t));
            if (ctx.isPhraseEnd) phraseMul *= 0.92;
        }
        const int vel = std::min(maxVel, clampInt(int(std::llround(double(baseVel) * beatMul * h.velMul * phraseMul)), 1, 127));

        // Length in beats:
        // - With pedal enabled, keep key-down shorter so CC64 hold time is audible.
        // - Without pedal, ballads can tie/hold more.
        const double baseLen = ballad
            ? (m_profile.pedalEnabled ? 0.55 : 2.20)
            : 0.78;
        double len = std::max(0.15, baseLen * h.lenMul);

        auto beatsUntilChange = [&](int beat, double offset, const ChordSymbol& here) -> double {
            const double t = double(beat) + offset;
            for (int b = beat + 1; b < 4 && b < ctx.lookaheadChords.size(); ++b) {
                const auto& c = chordAtBeat(b);
                if (c.placeholder || c.noChord || c.rootPc < 0) continue;
                if (!sameHarmony(here, c)) return std::max(0.15, double(b) - t);
            }
            return std::max(0.15, 4.0 - t);
        };
        // Don't sustain across a chord change inside the bar.
        len = std::min(len, beatsUntilChange(h.beat, h.offset, chordHere) - 0.02);
        if (len < 0.15) len = 0.15;

        // Optional tiny roll on ballads.
        const double rollStep = ballad ? 0.02 : 0.0;
        int roll = 0;

        const int g = g0 + h.beat;
        QVector<PianoEvent>& bucket = m_planned[g];

        // If we inserted any approach notes above with velocity sentinel 0, fill them now.
        for (auto& ev : bucket) {
            if (ev.kind == PianoEvent::Kind::Note && ev.velocity == 0 && ev.function == "Approach") {
                ev.velocity = clampInt(int(std::llround(double(vel) * 0.42)), 1, 127);
            }
        }

        if (!h.anticipation) {
            for (int n : lhNotes) {
                PianoEvent ev;
                ev.kind = PianoEvent::Kind::Note;
                ev.midiNote = n;
                ev.velocity = clampInt(vel + (ballad ? 2 : 0), 1, 127);
                ev.offsetBeats = h.offset + rollStep * double(roll++);
                ev.lengthBeats = len;
                if (m_profile.reasoningLogEnabled) {
                    ev.function = chordFn;
                    ev.reasoning = chordWhy;
                }
                bucket.push_back(ev);
            }
            for (int n : rhNotes) {
                PianoEvent ev;
                ev.kind = PianoEvent::Kind::Note;
                ev.midiNote = n;
                ev.velocity = clampInt(vel - 4, 1, 127);
                ev.offsetBeats = h.offset + rollStep * double(roll++);
                ev.lengthBeats = len;
                if (m_profile.reasoningLogEnabled) {
                    ev.function = chordFn;
                    ev.reasoning = chordWhy;
                }
                bucket.push_back(ev);
            }

        }
    }

    // Keep events ordered inside each beat.
    for (auto it = m_planned.begin(); it != m_planned.end(); ++it) {
        auto& vec = it.value();
        std::sort(vec.begin(), vec.end(), [](const PianoEvent& a, const PianoEvent& b) {
            return a.offsetBeats < b.offsetBeats;
        });
    }
    m_lastPlannedGlobalBeat = g0 + 3;
}

QVector<PianoEvent> JazzPianoGenerator::nextBeat(const PianoBeatContext& ctx,
                                                 const ChordSymbol* currentChord,
                                                 const ChordSymbol* nextChord) {
    QVector<PianoEvent> out;
    if (!currentChord) return out;
    if (currentChord->noChord || currentChord->placeholder || currentChord->rootPc < 0) {
        // On N.C. / no harmony: release pedal if used (handled by playback engine too, but be explicit).
        if (m_profile.pedalEnabled) {
            PianoEvent ev;
            ev.kind = PianoEvent::Kind::CC;
            ev.cc = 64;
            ev.ccValue = m_profile.pedalUpValue;
            ev.offsetBeats = 0.0;
            if (m_profile.reasoningLogEnabled) {
                ev.function = "Pedal up";
                ev.reasoning = "No chord (N.C.) → clear sustain.";
            }
            out.push_back(ev);
        }
        m_pedalIsDown = false;
        m_pedalDownAtBeatTime = -1.0;
        m_pedalReleaseAtBeatTime = -1.0;
        m_lastLh.clear();
        m_lastRh.clear();
        return out;
    }

    const bool logOn = m_profile.reasoningLogEnabled;
    const bool isBallad = (m_profile.feelStyle == PianoFeelStyle::Ballad);

    // --- Phrase-aware planning ---
    const int gb = globalBeatIndex(ctx);
    // Ensure we have events planned for this bar/beat.
    if (!m_planned.contains(gb) && (ctx.isNewBar || (m_lastPlannedGlobalBeat < gb))) {
        planBar(ctx, *currentChord, nextChord);
    }
    if (m_planned.contains(gb)) out = m_planned.take(gb);

    // --- Pedal management (CC64) ---
    // Fix: pedal events must be generated even when note events are planned.
    if (m_profile.pedalEnabled) {
        const double beatMs = (ctx.tempoBpm > 0) ? (60000.0 / double(ctx.tempoBpm)) : 500.0;
        const double beatStartTime = double(gb); // current beat start (in beat units)

        auto holdBeats = [&]() -> double {
            const int lo = std::max(0, m_profile.pedalMinHoldMs);
            const int hi = std::max(lo, m_profile.pedalMaxHoldMs);
            const int ms = lo + int(std::llround(next01() * double(hi - lo)));
            return std::max(0.10, double(ms) / beatMs);
        };

        auto emitPedalUp = [&](double off, const QString& why) {
            PianoEvent up;
            up.kind = PianoEvent::Kind::CC;
            up.cc = 64;
            up.ccValue = m_profile.pedalUpValue;
            up.offsetBeats = clamp01(off);
            if (logOn) { up.function = "Pedal up"; up.reasoning = why; }
            out.push_back(up);
            m_pedalIsDown = false;
            m_pedalDownAtBeatTime = -1.0;
            m_pedalReleaseAtBeatTime = -1.0;
        };

        auto emitPedalDown = [&](double off, const QString& why) {
            PianoEvent down;
            down.kind = PianoEvent::Kind::CC;
            down.cc = 64;
            down.ccValue = m_profile.pedalDownValue;
            down.offsetBeats = clamp01(off);
            if (logOn) { down.function = "Pedal down"; down.reasoning = why; }
            out.push_back(down);
            m_pedalIsDown = true;
            m_pedalDownAtBeatTime = beatStartTime + clamp01(off);
            // Store the target release time; we'll emit pedal-up when its beat window is reached.
            m_pedalReleaseAtBeatTime = m_pedalDownAtBeatTime + holdBeats();
        };

        // Determine if we're actually playing notes on this beat.
        const bool hasNotesThisBeat = std::any_of(out.begin(), out.end(), [](const PianoEvent& e) {
            return e.kind == PianoEvent::Kind::Note && e.midiNote >= 0 && e.velocity > 0;
        });

        // Chord-change behavior:
        // - If enabled, RELEASE on chord change to avoid blur,
        // - but DO NOT immediately re-pedal (that makes min/max hold irrelevant and can feel "always sustaining").
        if (ctx.isNewChord && m_profile.pedalReleaseOnChordChange) {
            if (m_pedalIsDown) {
                emitPedalUp(0.0, "Chord change → pedal up (let harmony speak).");
            }
        }

        // Engage pedal only when we're actually playing notes, and let min/max hold control the release.
        // Deterministic: if pedal is enabled and we play notes, we will use the pedal (otherwise hold can't matter).
        // Also: don't slam pedal every beat—prefer engaging on new chords / bar starts.
        if (hasNotesThisBeat && !m_pedalIsDown && (ctx.isNewChord || ctx.beatInBar == 0)) {
            emitPedalDown(0.02, "Pedal down (note event) → timed by min/max hold.");
        }

        // Timed release (must run AFTER pedal-down, otherwise the release time can't land within this beat).
        if (m_pedalIsDown && m_pedalReleaseAtBeatTime >= 0.0) {
            if (m_pedalReleaseAtBeatTime < beatStartTime) {
                emitPedalUp(0.0, "Timed pedal release (past due).");
            } else if (m_pedalReleaseAtBeatTime >= beatStartTime && m_pedalReleaseAtBeatTime < (beatStartTime + 1.0)) {
                // Ensure we don't release before we even pressed (tiny safety).
                const double off = std::max(0.0, m_pedalReleaseAtBeatTime - beatStartTime);
                emitPedalUp(off, "Timed pedal release (per hold ms).");
            }
        }
    }

    // Ensure CC events (pedal) are scheduled before notes at same offset.
    if (!out.isEmpty()) {
        std::stable_sort(out.begin(), out.end(), [](const PianoEvent& a, const PianoEvent& b) {
            if (a.offsetBeats != b.offsetBeats) return a.offsetBeats < b.offsetBeats;
            // CC before Note when simultaneous.
            if (a.kind != b.kind) return a.kind == PianoEvent::Kind::CC;
            return a.midiNote < b.midiNote;
        });
    }

    // Fallback: if nothing planned and no pedal event, be silent.
    return out;
}

} // namespace music

