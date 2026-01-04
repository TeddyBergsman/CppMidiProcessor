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

static QVector<int> pickStepwiseFillNotes(int tonicPc,
                                          ScaleType scaleType,
                                          int startMidi,
                                          int nNotes,
                                          int lo,
                                          int hi,
                                          quint32* rngState) {
    QVector<int> out;
    if (nNotes <= 0) return out;

    const auto& sc = ScaleLibrary::get(scaleType);
    QVector<int> scalePcs;
    scalePcs.reserve(sc.intervals.size());
    for (int iv : sc.intervals) scalePcs.push_back(normalizePc(tonicPc + iv));
    std::sort(scalePcs.begin(), scalePcs.end());
    scalePcs.erase(std::unique(scalePcs.begin(), scalePcs.end()), scalePcs.end());
    if (scalePcs.isEmpty()) return out;

    auto nextU32 = [&]() -> quint32 {
        // xorshift32
        quint32 x = (*rngState == 0u) ? 1u : *rngState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        *rngState = x;
        return x;
    };
    auto randInt = [&](int hiExclusive) -> int {
        if (hiExclusive <= 0) return 0;
        return int(nextU32() % quint32(hiExclusive));
    };

    // Snap start to nearest scale tone.
    int startPc = normalizePc(startMidi);
    int bestPc = scalePcs[0];
    int bestDist = 999;
    for (int pc : scalePcs) {
        int d = std::abs(pc - startPc);
        d = std::min(d, 12 - d);
        if (d < bestDist) { bestDist = d; bestPc = pc; }
    }
    int cur = pickNearestMidiForPcInRange(bestPc, lo, hi, startMidi);
    out.push_back(cur);

    for (int i = 1; i < nNotes; ++i) {
        // Mostly stepwise; occasionally a third.
        const int stepChoices[] = {-2, -1, +1, +2, +3, -3};
        const int step = stepChoices[randInt(int(sizeof(stepChoices) / sizeof(stepChoices[0])))];
        int cand = cur + step;
        cand = clampInt(cand, lo, hi);
        // Snap to nearest scale pitch class.
        int candPc = normalizePc(cand);
        int best = scalePcs[0];
        int dist = 999;
        for (int pc : scalePcs) {
            int d = std::abs(pc - candPc);
            d = std::min(d, 12 - d);
            if (d < dist) { dist = d; best = pc; }
        }
        cand = pickNearestMidiForPcInRange(best, lo, hi, cand);
        out.push_back(cand);
        cur = cand;
    }
    return out;
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

QVector<int> JazzPianoGenerator::chooseVoicingPitchClasses(const ChordSymbol& chord,
                                                           bool rootless,
                                                           bool& outUsedTension) {
    outUsedTension = false;
    QVector<int> pcs = ChordDictionary::chordPitchClasses(chord);
    if (pcs.isEmpty()) return pcs;

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

    // Rootless comping: start from guide tones.
    pushIf(core, thirdPc);
    pushIf(core, seventhPc);
    if (core.isEmpty()) {
        for (int pc : basic) pushIf(core, pc);
    }

    // Decide whether to keep root.
    const bool includeRoot = !rootless;
    if (includeRoot) pushIf(core, chord.rootPc);

    // Add one or two tensions from chordPitchClasses if enabled.
    QVector<int> tensionCandidates;
    tensionCandidates.reserve(pcs.size());
    for (int pc : pcs) {
        pc = normalizePc(pc);
        if (pc == normalizePc(chord.rootPc)) continue;
        if (pc == thirdPc) continue;
        if (pc == seventhPc) continue;
        tensionCandidates.push_back(pc);
    }
    tensionCandidates = sortedUniqueMidi(tensionCandidates); // (pcs are 0..11)

    if (!tensionCandidates.isEmpty() && next01() < clamp01(m_profile.tensionProb)) {
        outUsedTension = true;
        // Prefer 9/13-ish colors: choose up to 2 candidates.
        // Deterministically pseudo-random by rng state.
        const int pick1 = int(nextU32() % quint32(tensionCandidates.size()));
        pushIf(core, tensionCandidates[pick1]);
        if (tensionCandidates.size() >= 2 && next01() < 0.45) {
            const int pick2 = int(nextU32() % quint32(tensionCandidates.size()));
            pushIf(core, tensionCandidates[pick2]);
        }
    }

    // Optional quartal / cluster color: swap one tone by nearby pitch-class.
    const double qProb = clamp01(m_profile.quartalProb);
    const double cProb = clamp01(m_profile.clusterProb);
    if (core.size() >= 3 && (next01() < qProb || next01() < cProb)) {
        const bool doQuartal = next01() < qProb;
        const int idx = int(nextU32() % quint32(core.size()));
        const int base = core[idx];
        const int alt = normalizePc(base + (doQuartal ? 5 : 1)); // +4th or +minor2nd
        core[idx] = alt;
    }

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
        m_lastLh.clear();
        m_lastRh.clear();
        return out;
    }

    const bool logOn = m_profile.reasoningLogEnabled;

    // --- Pedal management (CC64) ---
    if (m_profile.pedalEnabled && ctx.isNewChord && next01() < clamp01(m_profile.pedalChangeProb)) {
        // Refresh pedal on chord changes to avoid harmonic blur.
        if (m_profile.pedalReleaseOnChordChange) {
            PianoEvent up;
            up.kind = PianoEvent::Kind::CC;
            up.cc = 64;
            up.ccValue = m_profile.pedalUpValue;
            up.offsetBeats = 0.0;
            if (logOn) { up.function = "Pedal up"; up.reasoning = "Chord change → release sustain to avoid blur."; }
            out.push_back(up);

            PianoEvent down;
            down.kind = PianoEvent::Kind::CC;
            down.cc = 64;
            down.ccValue = m_profile.pedalDownValue;
            down.offsetBeats = 0.08; // small gap after release
            if (logOn) { down.function = "Pedal down"; down.reasoning = "Re-engage sustain for warmth after chord change."; }
            out.push_back(down);
        } else {
            PianoEvent down;
            down.kind = PianoEvent::Kind::CC;
            down.cc = 64;
            down.ccValue = m_profile.pedalDownValue;
            down.offsetBeats = 0.0;
            if (logOn) { down.function = "Pedal down"; down.reasoning = "Chord change → refresh sustain (no explicit release)."; }
            out.push_back(down);
        }
    }

    // --- Decide whether to comp this beat ---
    const bool strongBeat = (ctx.beatInBar == 0 || ctx.beatInBar == 2);
    double density = clamp01(m_profile.compDensity);
    if (m_profile.feelStyle == PianoFeelStyle::Ballad) {
        density = std::min(density, 0.45);
    }
    // Structural moments more likely to be articulated.
    if (ctx.isNewChord) density = std::min(1.0, density + 0.18);
    if (strongBeat) density = std::min(1.0, density + 0.10);

    bool doComp = next01() < density;
    if (!strongBeat && next01() < clamp01(m_profile.restProb)) doComp = false;

    // Choose rhythmic placement for comp.
    double compOffset = 0.0;
    const double antP = clamp01(m_profile.anticipationProb);
    const double synP = clamp01(m_profile.syncopationProb);
    if (!strongBeat) {
        const double r = next01();
        if (r < antP) compOffset = 0.5;              // upbeat 8th
        else if (r < antP + synP) compOffset = 0.75; // late 16th-ish
        else compOffset = 0.0;
    }

    // --- Build voicing if comping ---
    if (doComp) {
        const bool rootless = m_profile.preferRootless && (next01() < clamp01(m_profile.rootlessProb));

        // If bass is likely covering root (slash bass present), increase avoid-root tendency.
        bool bassImpliesRoot = (currentChord->bassPc >= 0) || (next01() < clamp01(m_profile.avoidRootProb));
        bool usedTension = false;
        QVector<int> voicingPcs = chooseVoicingPitchClasses(*currentChord, rootless || bassImpliesRoot, usedTension);
        if (!voicingPcs.isEmpty()) {
            // Split roughly: 2 tones LH, rest RH.
            QVector<int> lhPcs;
            QVector<int> rhPcs;
            lhPcs.reserve(2);
            rhPcs.reserve(voicingPcs.size());

            // Prefer guide-tones in LH (3rd/7th if present).
            // Simple: pick first two pcs as LH, rest RH.
            for (int i = 0; i < voicingPcs.size(); ++i) {
                if (i < 2) lhPcs.push_back(voicingPcs[i]);
                else rhPcs.push_back(voicingPcs[i]);
            }
            if (rhPcs.isEmpty() && lhPcs.size() >= 2) {
                // If only two tones total, duplicate one tone to RH an octave up for "piano" feel.
                rhPcs.push_back(lhPcs.last());
            }

            QVector<int> lhNotes = realizeToMidi(lhPcs, m_profile.lhMinMidiNote, m_profile.lhMaxMidiNote, m_lastLh, m_profile.maxHandLeap);
            QVector<int> rhNotes = realizeToMidi(rhPcs, m_profile.rhMinMidiNote, m_profile.rhMaxMidiNote, m_lastRh, m_profile.maxHandLeap);

            // Drop-2 flavor: if enabled, drop the 2nd-highest RH note by an octave (if possible).
            if (rhNotes.size() >= 3 && next01() < clamp01(m_profile.drop2Prob)) {
                const int idx = rhNotes.size() - 2;
                const int n = rhNotes[idx] - 12;
                if (n >= m_profile.rhMinMidiNote) rhNotes[idx] = n;
                rhNotes = sortedUniqueMidi(rhNotes);
            }

            // Repetition penalty: if exact voicing repeats, nudge one RH tone by octave if possible.
            const quint32 h = hashNotes(lhNotes) ^ (hashNotes(rhNotes) * 16777619u);
            if (m_lastVoicingHash != 0u && h == m_lastVoicingHash && next01() < clamp01(m_profile.repetitionPenalty)) {
                if (!rhNotes.isEmpty()) {
                    const int idx = int(nextU32() % quint32(rhNotes.size()));
                    const int pc = normalizePc(rhNotes[idx]);
                    int cand = rhNotes[idx] + 12;
                    if (cand <= m_profile.rhMaxMidiNote && normalizePc(cand) == pc) rhNotes[idx] = cand;
                    else {
                        cand = rhNotes[idx] - 12;
                        if (cand >= m_profile.rhMinMidiNote && normalizePc(cand) == pc) rhNotes[idx] = cand;
                    }
                    rhNotes = sortedUniqueMidi(rhNotes);
                }
            }

            // Save voicing memory.
            m_lastLh = lhNotes;
            m_lastRh = rhNotes;
            m_lastVoicingHash = hashNotes(lhNotes) ^ (hashNotes(rhNotes) * 16777619u);

            auto velForBeat = [&](int beatInBar) -> int {
                double mul = 1.0;
                if (beatInBar == 0) mul *= m_profile.accentDownbeat;
                else if (beatInBar == 1 || beatInBar == 3) mul *= m_profile.accentBackbeat;
                // Small random variance.
                const int v = m_profile.baseVelocity + int(std::llround((next01() * 2.0 - 1.0) * double(m_profile.velocityVariance)));
                return clampInt(int(std::llround(double(v) * mul)), 1, 127);
            };

            const int vel = velForBeat(ctx.beatInBar);
            const double len = (m_profile.feelStyle == PianoFeelStyle::Ballad) ? 0.92 : 0.78; // beats

            const QString chordText = !currentChord->originalText.trimmed().isEmpty()
                ? currentChord->originalText.trimmed()
                : QString("pc%1").arg(currentChord->rootPc);

            // Emit LH notes.
            for (int n : lhNotes) {
                PianoEvent ev;
                ev.kind = PianoEvent::Kind::Note;
                ev.midiNote = n;
                ev.velocity = vel;
                ev.offsetBeats = compOffset;
                ev.lengthBeats = len;
                if (logOn) {
                    ev.function = "Comp (LH)";
                    ev.reasoning = QString("Voicing (voice-led)%1%2; chord=%3")
                                       .arg(rootless ? " rootless" : "")
                                       .arg(usedTension ? " +tension" : "")
                                       .arg(chordText);
                }
                out.push_back(ev);
            }

            // Emit RH notes.
            for (int n : rhNotes) {
                PianoEvent ev;
                ev.kind = PianoEvent::Kind::Note;
                ev.midiNote = n;
                ev.velocity = clampInt(vel - 4, 1, 127);
                ev.offsetBeats = compOffset;
                ev.lengthBeats = len;
                if (logOn) {
                    ev.function = "Comp (RH)";
                    ev.reasoning = QString("Color/upper structure; chord=%1").arg(chordText);
                }
                out.push_back(ev);
            }
        }
    }

    // --- RH fills (phrase-end and occasional) ---
    const bool isPhraseSpot = ctx.isPhraseEnd || (ctx.beatInBar == 3);
    const double fillP = isPhraseSpot ? clamp01(m_profile.fillProbPhraseEnd) : clamp01(m_profile.fillProbAnyBeat);
    if (fillP > 0.0 && next01() < fillP) {
        const QVector<ScaleType> sugg = ScaleLibrary::suggestForChord(*currentChord);
        const ScaleType st = !sugg.isEmpty() ? sugg[0] : ScaleType::Ionian;

        // Start near top of current RH voicing if available.
        const int startMidi = !m_lastRh.isEmpty() ? m_lastRh.last() : (m_profile.fillMinMidiNote + m_profile.fillMaxMidiNote) / 2;
        const int nNotes = clampInt(m_profile.fillMaxNotes, 1, 8);

        QVector<int> fillNotes = pickStepwiseFillNotes(currentChord->rootPc, st, startMidi, nNotes,
                                                       m_profile.fillMinMidiNote, m_profile.fillMaxMidiNote, &m_rngState);
        if (!fillNotes.isEmpty()) {
            // Place as 8ths/16ths late in the beat.
            const double startOffset = (ctx.beatInBar == 3) ? 0.5 : 0.62;
            const double span = 0.34;
            for (int i = 0; i < fillNotes.size(); ++i) {
                const double t = (fillNotes.size() <= 1) ? 0.0 : (double(i) / double(fillNotes.size() - 1));
                PianoEvent ev;
                ev.kind = PianoEvent::Kind::Note;
                ev.midiNote = fillNotes[i];
                ev.velocity = clampInt(m_profile.baseVelocity + 6 + int(std::llround((next01() * 2.0 - 1.0) * double(m_profile.velocityVariance))), 1, 127);
                ev.offsetBeats = std::min(0.90, startOffset + t * span);
                ev.lengthBeats = 0.20;
                if (logOn) {
                    ev.function = "Fill (RH)";
                    ev.reasoning = QString("Phrase color using %1").arg(ScaleLibrary::get(st).name);
                }
                out.push_back(ev);
            }
        }
    }

    // Sort events by offset so the scheduler behaves deterministically.
    std::sort(out.begin(), out.end(), [](const PianoEvent& a, const PianoEvent& b) {
        return a.offsetBeats < b.offsetBeats;
    });
    return out;
}

} // namespace music

