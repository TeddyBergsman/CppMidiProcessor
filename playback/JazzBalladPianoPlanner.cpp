#include "playback/JazzBalladPianoPlanner.h"

#include <QtGlobal>
#include <algorithm>

namespace playback {
namespace {
static int clampMidi(int m) { return (m < 0) ? 0 : (m > 127 ? 127 : m); }
} // namespace

JazzBalladPianoPlanner::JazzBalladPianoPlanner() {
    reset();
}

void JazzBalladPianoPlanner::reset() {
    m_lastVoicing.clear();
    m_lastRhythmBar = -1;
    m_barHits.clear();
    m_lastTopMidi = -1;
    m_barTopHits.clear();
    m_motifBlockStartBar = -1;
    m_motifA.clear();
    m_motifB.clear();
}

int JazzBalladPianoPlanner::thirdIntervalForQuality(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::Minor:
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished: return 3;
        case music::ChordQuality::Sus2: return 2;
        case music::ChordQuality::Sus4: return 5;
        default: return 4;
    }
}

int JazzBalladPianoPlanner::fifthIntervalForQuality(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished: return 6;
        case music::ChordQuality::Augmented: return 8;
        default: return 7;
    }
}

int JazzBalladPianoPlanner::seventhIntervalFor(const music::ChordSymbol& c) {
    if (c.seventh == music::SeventhQuality::Major7) return 11;
    if (c.seventh == music::SeventhQuality::Dim7) return 9;
    if (c.seventh == music::SeventhQuality::Minor7) return 10;
    // If extensions imply a 7th, default to minor7 unless explicitly maj-marked (handled in parser).
    if (c.extension >= 7) return 10;
    return -1;
}

int JazzBalladPianoPlanner::pcForDegree(const music::ChordSymbol& c, int degree) {
    const int root = (c.rootPc >= 0) ? c.rootPc : 0;
    auto applyAlter = [&](int deg, int basePc) -> int {
        for (const auto& a : c.alterations) {
            if (a.degree != deg) continue;
            return (basePc + a.delta + 1200) % 12;
        }
        return basePc;
    };

    int pc = root;
    switch (degree) {
        case 1: pc = root; break;
        case 3: pc = (root + thirdIntervalForQuality(c.quality)) % 12; break;
        case 5: pc = (root + fifthIntervalForQuality(c.quality)) % 12; pc = applyAlter(5, pc); break;
        case 7: {
            const int iv = seventhIntervalFor(c);
            // IMPORTANT: do NOT invent a b7 on triads/6-chords. If there is no 7th, return sentinel.
            if (iv < 0) return -1;
            pc = (root + iv) % 12;
        } break;
        case 9: pc = (root + 14) % 12; pc = applyAlter(9, pc); break;
        case 11: pc = (root + 17) % 12; pc = applyAlter(11, pc); break;
        case 13: pc = (root + 21) % 12; pc = applyAlter(13, pc); break;
        default: pc = root; break;
    }
    return (pc + 12) % 12;
}

int JazzBalladPianoPlanner::nearestMidiForPc(int pc, int around, int lo, int hi) {
    if (pc < 0) pc = 0;
    around = clampMidi(around);
    // Find candidate in [lo,hi] with matching pc, closest to around.
    int best = -1;
    int bestDist = 999999;
    for (int m = lo; m <= hi; ++m) {
        if ((m % 12 + 12) % 12 != pc) continue;
        const int d = qAbs(m - around);
        if (d < bestDist) { bestDist = d; best = m; }
    }
    if (best >= 0) return best;
    // Fallback: fold
    int m = lo + ((pc - (lo % 12) + 1200) % 12);
    while (m < lo) m += 12;
    while (m > hi) m -= 12;
    return clampMidi(m);
}

int JazzBalladPianoPlanner::bestNearestToPrev(int pc, const QVector<int>& prev, int lo, int hi) {
    if (prev.isEmpty()) {
        const int around = (lo + hi) / 2;
        return nearestMidiForPc(pc, around, lo, hi);
    }
    // Choose around the closest previous note for continuity.
    int around = prev[0];
    int bestD = qAbs(prev[0] - (lo + hi) / 2);
    for (int m : prev) {
        const int d = qAbs(m - (lo + hi) / 2);
        if (d < bestD) { bestD = d; around = m; }
    }
    return nearestMidiForPc(pc, around, lo, hi);
}

void JazzBalladPianoPlanner::sortUnique(QVector<int>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

QVector<int> JazzBalladPianoPlanner::makeRootlessA(const music::ChordSymbol& c) {
    // Type A: 3-5-7-9 (rootless)
    return {pcForDegree(c, 3), pcForDegree(c, 5), pcForDegree(c, 7), pcForDegree(c, 9)};
}

QVector<int> JazzBalladPianoPlanner::makeRootlessB(const music::ChordSymbol& c) {
    // Type B: 7-9-3-5 (rootless)
    return {pcForDegree(c, 7), pcForDegree(c, 9), pcForDegree(c, 3), pcForDegree(c, 5)};
}

QVector<int> JazzBalladPianoPlanner::makeShell(const music::ChordSymbol& c) {
    // Shell: 3-7 (and optionally 9)
    return {pcForDegree(c, 3), pcForDegree(c, 7), pcForDegree(c, 9)};
}

bool JazzBalladPianoPlanner::feasible(const QVector<int>& midiNotes) const {
    virtuoso::constraints::CandidateGesture g;
    g.midiNotes = midiNotes;
    virtuoso::constraints::PerformanceState s;
    const auto r = m_driver.evaluateFeasibility(s, g);
    return r.ok;
}

QVector<int> JazzBalladPianoPlanner::repairToFeasible(QVector<int> midiNotes) const {
    sortUnique(midiNotes);
    // If too many notes or too wide, progressively reduce and fold octaves.
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (feasible(midiNotes)) return midiNotes;

        // Reduce polyphony first.
        if (midiNotes.size() > 4) midiNotes.resize(4);
        if (midiNotes.size() > 3) midiNotes.resize(3);

        // Fold the top note down an octave if span is too big.
        if (midiNotes.size() >= 2) {
            const int span = midiNotes.last() - midiNotes.first();
            if (span > m_driver.constraints().maxSpanSemitones) {
                midiNotes.last() -= 12;
                sortUnique(midiNotes);
                continue;
            }
        }

        // If still not feasible, drop the highest color tone.
        if (midiNotes.size() > 2) {
            midiNotes.removeLast();
            continue;
        }

        // Last resort: 2-note shell.
        return midiNotes;
    }
    return midiNotes;
}

QVector<virtuoso::engine::AgentIntentNote> JazzBalladPianoPlanner::planBeat(const Context& c,
                                                                           int midiChannel,
                                                                           const virtuoso::groove::TimeSignature& ts) {
    QVector<virtuoso::engine::AgentIntentNote> out;

    using virtuoso::groove::GrooveGrid;
    using virtuoso::groove::Rational;

    const bool climaxDense = c.forceClimax && (c.energy >= 0.75);

    // Deterministic hash for this beat.
    const quint32 h = quint32(qHash(QString("%1|%2|%3").arg(c.chordText).arg(c.playbackBarIndex).arg(c.determinismSeed)));

    // Bar-coherent rhythmic planning: choose a small set of syncopated comp hits once per bar.
    ensureBarRhythmPlanned(c);
    QVector<CompHit> hitsThisBeat;
    hitsThisBeat.reserve(4);
    for (const auto& hit : m_barHits) {
        if (hit.beatInBar == c.beatInBar) hitsThisBeat.push_back(hit);
    }
    if (hitsThisBeat.isEmpty() && !climaxDense) return out;

    // Choose voicing family (Chet/Bill ballad: mostly shells, occasional rootless).
    const bool chordHas7 = (c.chord.extension >= 7 || c.chord.seventh != music::SeventhQuality::None);
    const bool dominant = (c.chord.quality == music::ChordQuality::Dominant);
    const bool alt = c.chord.alt || !c.chord.alterations.isEmpty();
    const bool pickA = ((h % 2u) == 0u);
    const bool useRootless = chordHas7 && (!c.preferShells || c.chordIsNew || (c.beatInBar == 3 && ((h % 4u) == 0u)));

    // Build a ballad-appropriate set:
    // - Guide tones when chord has a 7th: 3 & 7
    // - Otherwise: 3 & 5 (avoid inventing a b7 on plain triads)
    // - Add 1–2 colors (9/13 on maj, 9/11 on min, altered tensions only if chart indicates)
    QVector<int> pcs;
    pcs.reserve(5);
    const int pc3 = pcForDegree(c.chord, 3);
    const int pc7 = chordHas7 ? pcForDegree(c.chord, 7) : -1;
    const int pc5 = pcForDegree(c.chord, 5);
    if (pc3 >= 0) pcs.push_back(pc3);
    if (pc7 >= 0) pcs.push_back(pc7);
    else if (pc5 >= 0) pcs.push_back(pc5);

    // Primary color:
    int primaryDeg = 9;
    if (dominant) {
        // Dominant: 9 is safe; alterations (b9/#9) are only used if chart explicitly encodes them.
        primaryDeg = 9;
    } else if (c.chord.quality == music::ChordQuality::Major) {
        // Major ballad default: 9 (avoid natural 11 over major unless chart indicates lydian/#11).
        primaryDeg = 9;
    } else if (c.chord.quality == music::ChordQuality::Minor) {
        // Minor: 9 is safe.
        primaryDeg = 9;
    }
    pcs.push_back(pcForDegree(c.chord, primaryDeg));

    // Secondary color (occasional):
    const int p2 = qBound(0, int(llround(c.addSecondColorProb * 100.0)), 100);
    const bool add2 = (p2 > 0) && (int((h / 7u) % 100u) < p2);
    if (add2) {
        int deg2 = 13;
        if (dominant) deg2 = 13;                      // 13 is a classic dominant color (unless altered by chart)
        else if (c.chord.quality == music::ChordQuality::Major) deg2 = 13; // maj: 13 over 11
        else if (c.chord.quality == music::ChordQuality::Minor) deg2 = 11; // min: 11 is idiomatic
        pcs.push_back(pcForDegree(c.chord, deg2));
    }

    // If we chose rootless, we can swap in Type A/B flavor by optionally including 5th vs extra color.
    if (useRootless) {
        // Type A/B: slight re-bias; keep the set mostly the same, just ensure we have a 5 sometimes.
        if (pickA && ((h % 5u) == 0u)) pcs.push_back(pcForDegree(c.chord, 5));
        if (!pickA && ((h % 6u) == 0u)) pcs.push_back(pcForDegree(c.chord, 5));
    }

    sortUnique(pcs);

    // Map to midi with LH/RH ranges and voice-leading to last voicing.
    // v2: keep a stable “voice index” mapping to reduce random re-voicing each beat.
    QVector<int> midi;
    midi.reserve(pcs.size());

    // LH anchor tones (3rd + 7th), RH colors with ballad spacing.
    for (int idx = 0; idx < pcs.size(); ++idx) {
        const int pc = pcs[idx];
        if (pc < 0) continue;
        const bool isGuide = (pc == pc3 || (pc7 >= 0 && pc == pc7) || (pc7 < 0 && pc == pc5));
        const int lo = isGuide ? c.lhLo : c.rhLo;
        const int hi = isGuide ? c.lhHi : c.rhHi;
        int around = (lo + hi) / 2;
        if (!m_lastVoicing.isEmpty()) {
            const int j = qBound(0, idx, m_lastVoicing.size() - 1);
            around = m_lastVoicing[j];
        }
        const int chosen = nearestMidiForPc(pc, around, lo, hi);
        midi.push_back(chosen);
    }
    sortUnique(midi);

    midi = repairToFeasible(midi);
    if (midi.isEmpty()) return out;

    // Save for voice-leading.
    m_lastVoicing = midi;

    const QString voicingType = useRootless
        ? (pickA ? "Ballad Rootless (A-ish)" : "Ballad Rootless (B-ish)")
        : (chordHas7 ? "Ballad Shell (3-7)" : "Ballad Shell (3-5)");
    const int baseVel = climaxDense ? (c.chordIsNew ? 64 : 58) : (c.chordIsNew ? 50 : 44);

    // Optional high sparkle on beat 4 (very occasional), but only when there's rhythmic space.
    const int pSp = qBound(0, int(llround(c.sparkleProbBeat4 * 100.0)), 100);
    const bool sparkle = (c.beatInBar == 3) && c.userSilence && (pSp > 0) && (int((h / 13u) % 100u) < pSp);
    int sparklePc = -1;
    if (sparkle) {
        // Choose a tasteful color and place it high.
        // IMPORTANT: avoid dissonant natural 11 over major unless chart explicitly implies #11.
        int spPc = -1;
        if (c.chord.quality == music::ChordQuality::Major) {
            const int pc9 = pcForDegree(c.chord, 9);
            const int pc13 = pcForDegree(c.chord, 13);
            // Allow 11 only if explicitly altered (#11) in chart.
            bool hasSharp11 = false;
            for (const auto& a : c.chord.alterations) {
                if (a.degree == 11 && a.delta == 1) { hasSharp11 = true; break; }
            }
            const int pc11 = hasSharp11 ? pcForDegree(c.chord, 11) : -1;
            spPc = ((h % 3u) == 0u && pc13 >= 0) ? pc13 : (pc9 >= 0 ? pc9 : pc11);
        } else if (c.chord.quality == music::ChordQuality::Minor) {
            const int pc9 = pcForDegree(c.chord, 9);
            const int pc11 = pcForDegree(c.chord, 11);
            spPc = ((h % 3u) == 0u && pc11 >= 0) ? pc11 : pc9;
        } else if (c.chord.quality == music::ChordQuality::Dominant) {
            const int pc9 = pcForDegree(c.chord, 9);
            const int pc13 = pcForDegree(c.chord, 13);
            spPc = ((h % 3u) == 0u && pc13 >= 0) ? pc13 : pc9;
        } else {
            spPc = pcForDegree(c.chord, 9);
        }
        if (spPc >= 0) sparklePc = ((spPc % 12) + 12) % 12;
    }

    // Helper: derive a "guide tones only" subset from current voicing.
    auto guideSubset = [&](const QVector<int>& full) -> QVector<int> {
        QVector<int> g;
        g.reserve(2);
        for (int m : full) {
            const int pc = ((m % 12) + 12) % 12;
            if (pc == pc3 || (pc7 >= 0 && pc == pc7) || (pc7 < 0 && pc == pc5)) g.push_back(m);
        }
        sortUnique(g);
        if (g.size() > 2) g.resize(2);
        if (g.isEmpty() && !full.isEmpty()) g.push_back(full.first());
        return g;
    };

    auto renderHit = [&](const CompHit& hit) {
        QVector<int> notes = (hit.density.trimmed().toLower() == "guide") ? guideSubset(midi) : midi;
        if (notes.isEmpty()) return;

        // Session-player feel: treat pianist as two hands.
        // - LH anchors are earlier and can sustain
        // - RH colors/top can be lightly rolled/arpeggiated for floaty/dreamy feel
        std::sort(notes.begin(), notes.end());
        const int topNote = notes.last();
        const int lowNote = notes.first();

        const quint32 hr = quint32(qHash(QString("roll|%1|%2|%3|%4")
                                             .arg(c.chordText)
                                             .arg(c.playbackBarIndex)
                                             .arg(hit.beatInBar)
                                             .arg(c.determinismSeed)));
        const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
        const bool doRoll = !userBusy && (c.energy >= 0.25) && (int(hr % 100u) < qBound(0, int(llround(20.0 + 35.0 * c.energy)), 70));
        const bool doArp = doRoll && (notes.size() >= 3) && (hit.dur >= virtuoso::groove::Rational(1, 8));
        const bool arpUp = ((hr / 3u) % 2u) == 0u;

        auto to16thSub = [&](int sub, int count) -> int {
            if (count <= 1) return 0;
            if (count == 2) return qBound(0, sub * 2, 3);
            if (count == 4) return qBound(0, sub, 3);
            // generic mapping
            const int s = int(llround(double(sub) * 4.0 / double(count)));
            return qBound(0, s, 3);
        };
        const int base16 = to16thSub(hit.sub, hit.count);

        for (int idx = 0; idx < notes.size(); ++idx) {
            const int m = notes[idx];
            const bool isLow = (m == lowNote);
            const bool isTop = (m == topNote);
            const bool isRh = (m > c.lhHi); // heuristic split at LH range ceiling

            int vel = baseVel + hit.velDelta;
            // Expressive shaping: top voice sings; inner voices are softer; LH anchor steady.
            if (isTop) vel += 7;
            else if (!isLow) vel -= 4;
            if (isRh) vel += 1;
            // If arpeggiating upward, slightly crescendo; downward, slightly decrescendo.
            if (doArp) {
                const int order = arpUp ? idx : (notes.size() - 1 - idx);
                vel += arpUp ? qMin(4, order) : -qMin(3, order);
            }
            vel = qBound(1, vel, 127);

            int beat = hit.beatInBar;
            int sub = hit.sub;
            int count = hit.count;

            // Intra-chord arpeggiation/roll: place notes a few 16ths apart (deterministic),
            // with RH slightly later than LH to feel "hands" rather than a MIDI block chord.
            if (doRoll) {
                const int order = arpUp ? idx : (notes.size() - 1 - idx);
                const int step = doArp ? qMin(2, order) : (isRh ? 1 : 0); // 0..2 16ths
                count = 4;
                sub = qMin(3, base16 + step);
            }

            virtuoso::engine::AgentIntentNote n;
            n.agent = "Piano";
            n.channel = midiChannel;
            n.note = clampMidi(m);
            n.baseVelocity = vel;
            n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, beat, sub, count, ts);
            // If RH is rolled late, keep it a bit shorter so it doesn't smear.
            const bool shorten = doRoll && isRh && !isLow && (hit.dur <= virtuoso::groove::Rational(1, 4));
            n.durationWhole = shorten ? qMin(hit.dur, virtuoso::groove::Rational(3, 16)) : hit.dur;
            n.structural = c.chordIsNew;
            n.chord_context = c.chordText;
            n.voicing_type = voicingType + (doRoll ? (doArp ? " + Arpeggiated" : " + RolledHands") : "");
            n.logic_tag = hit.rhythmTag.isEmpty() ? "ballad_comp" : ("ballad_comp|" + hit.rhythmTag);
            n.target_note = isTop ? "Comp (top voice)" : (isLow ? "Comp (LH anchor)" : "Comp (inner)");
            out.push_back(n);
        }
    };

    for (const auto& hit : hitsThisBeat) renderHit(hit);

    // RH top-line motifs (2–3 note micro-phrases with tension->release).
    {
        const bool allowTop = !c.userDensityHigh && !c.userIntensityPeak;
        if (allowTop) {
            // Safe pitch-class pool for dyads (avoid stray dissonance).
            const int pc1 = (c.chord.rootPc >= 0) ? (c.chord.rootPc % 12) : 0;
            const int pc3 = pcForDegree(c.chord, 3);
            const int pc5 = pcForDegree(c.chord, 5);
            const int pc7 = pcForDegree(c.chord, 7);
            const int pc9 = pcForDegree(c.chord, 9);
            const int pc11 = pcForDegree(c.chord, 11);
            const int pc13 = pcForDegree(c.chord, 13);

            QSet<int> safe;
            auto addSafe = [&](int pc) { if (pc >= 0) safe.insert((pc % 12 + 12) % 12); };
            addSafe(pc1);
            addSafe(pc3);
            addSafe(pc5);
            if (pc7 >= 0) addSafe(pc7);
            if (c.chord.quality == music::ChordQuality::Major) {
                addSafe(pc9);
                addSafe(pc13);
                bool hasSharp11 = false;
                for (const auto& a : c.chord.alterations) {
                    if (a.degree == 11 && a.delta == 1) { hasSharp11 = true; break; }
                }
                if (hasSharp11) addSafe(pc11);
            } else if (c.chord.quality == music::ChordQuality::Minor) {
                addSafe(pc9);
                addSafe(pc11);
                if (c.chord.extension >= 13) addSafe(pc13);
            } else if (c.chord.quality == music::ChordQuality::Dominant) {
                addSafe(pc9);
                addSafe(pc13);
                for (const auto& a : c.chord.alterations) {
                    if (a.degree == 11) addSafe(pc11);
                }
            } else {
                addSafe(pc9);
            }

            // Pick a dyad partner that is consonant (avoid seconds + tritones).
            auto chooseDyadLowMidi = [&](int topPc, int mTop, int lo, int hi) -> int {
                const int t = ((topPc % 12) + 12) % 12;
                int bestMidi = -1;
                int bestScore = 999999;
                for (int pc : safe) {
                    if (pc == t) continue;
                    int m = nearestMidiForPc(pc, mTop - 5, lo, hi);
                    if (m < 0) continue;
                    while (m >= mTop && (m - 12) >= lo) m -= 12;
                    while ((mTop - m) > 12 && (m + 12) <= hi) m += 12;
                    if (m >= mTop) continue;
                    const int interval = mTop - m; // semitones
                    // Hard reject: seconds and tritone and major 7.
                    if (interval <= 2) continue;
                    if (interval == 6) continue;
                    if (interval >= 11) continue;
                    // Score preference: 3rds/6ths highest, then 5ths/4ths.
                    int s = 0;
                    if (interval == 3 || interval == 4) s += 0;
                    else if (interval == 8 || interval == 9) s += 1;
                    else if (interval == 7 || interval == 5) s += 3;
                    else if (interval == 10) s += 7;
                    else s += 12;
                    // Prefer the lower note not to be too low (keep dyads in a sweet mid-high zone).
                    const int mid = (lo + hi) / 2;
                    s += qAbs(mid - m) / 3;
                    if (s < bestScore) { bestScore = s; bestMidi = m; }
                }
                return bestMidi;
            };

            for (const auto& th : m_barTopHits) {
                if (th.beatInBar != c.beatInBar) continue;
                const int pc = th.pc;
                if (pc < 0) continue;
                // Range: mid->high (avoid only very top), but allow reaching sparkleHi occasionally.
                const int lo = c.rhLo;
                const int hi = c.sparkleHi;
                int around = (c.rhLo + c.rhHi) / 2 + 6;
                if (m_lastTopMidi >= 0) around = m_lastTopMidi;
                around = qBound(lo + 4, around, hi - 4);

                const int mTop = nearestMidiForPc(pc, around, lo, hi);
                if (mTop < 0) continue;

                // Prefer consonant dyads (no seconds/tritones).
                const int mLow = chooseDyadLowMidi(pc, mTop, lo, hi);

                if (th.resolve) m_lastTopMidi = mTop;

                const auto pos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, th.beatInBar, th.sub, th.count, ts);

                if (mLow >= 0) {
                    // Lower dyad tone
                    virtuoso::engine::AgentIntentNote a;
                    a.agent = "Piano";
                    a.channel = midiChannel;
                    a.note = clampMidi(mLow);
                    a.baseVelocity = qBound(1, baseVel - 16 + th.velDelta, 127);
                    a.startPos = pos;
                    a.durationWhole = th.dur;
                    a.structural = c.chordIsNew;
                    a.chord_context = c.chordText;
                    a.voicing_type = voicingType + " + TopDyad";
                    a.logic_tag = th.resolve ? ("ballad_topline|resolve|" + th.tag + "|dyad") : ("ballad_topline|tension|" + th.tag + "|dyad");
                    a.target_note = th.resolve ? "RH resolve (dyad low)" : "RH tension (dyad low)";
                    out.push_back(a);
                }

                // Upper dyad tone (always)
                virtuoso::engine::AgentIntentNote b;
                b.agent = "Piano";
                b.channel = midiChannel;
                b.note = clampMidi(mTop);
                b.baseVelocity = qBound(1, baseVel - 12 + th.velDelta + 3, 127);
                b.startPos = pos;
                b.durationWhole = th.dur;
                b.structural = c.chordIsNew;
                b.chord_context = c.chordText;
                b.voicing_type = voicingType + " + TopDyad";
                b.logic_tag = th.resolve ? ("ballad_topline|resolve|" + th.tag + "|dyad") : ("ballad_topline|tension|" + th.tag + "|dyad");
                b.target_note = th.resolve ? "RH resolve (dyad high)" : "RH tension (dyad high)";
                out.push_back(b);
            }
        }
    }

    if (sparklePc >= 0) {
        // Sparkle as a small harmony (dyad), and not only the very top register.
        const int lo = c.rhLo;
        const int hi = c.sparkleHi;
        int around = (c.rhHi + c.sparkleLo) / 2;
        if (m_lastTopMidi >= 0) around = qBound(c.rhHi - 4, m_lastTopMidi + 2, hi - 3);
        around = qBound(lo + 6, around, hi - 4);

        // Rebuild a minimal safe set for dyad partner selection.
        QSet<int> safe;
        auto addSafe = [&](int pc) { if (pc >= 0) safe.insert((pc % 12 + 12) % 12); };
        addSafe((c.chord.rootPc >= 0) ? (c.chord.rootPc % 12) : 0);
        addSafe(pcForDegree(c.chord, 3));
        addSafe(pcForDegree(c.chord, 5));
        addSafe(pcForDegree(c.chord, 7));
        addSafe(pcForDegree(c.chord, 9));
        addSafe(pcForDegree(c.chord, 13));
        if (c.chord.quality == music::ChordQuality::Minor) addSafe(pcForDegree(c.chord, 11));
        if (c.chord.quality == music::ChordQuality::Major) {
            for (const auto& a : c.chord.alterations) {
                if (a.degree == 11 && a.delta == 1) { addSafe(pcForDegree(c.chord, 11)); break; }
            }
        }

        auto chooseDyadLowMidi = [&](int topPc, int mTop, int lo, int hi) -> int {
            const int t = ((topPc % 12) + 12) % 12;
            int bestMidi = -1;
            int bestScore = 999999;
            for (int pc : safe) {
                if (pc == t) continue;
                int m = nearestMidiForPc(pc, mTop - 5, lo, hi);
                if (m < 0) continue;
                while (m >= mTop && (m - 12) >= lo) m -= 12;
                while ((mTop - m) > 12 && (m + 12) <= hi) m += 12;
                if (m >= mTop) continue;
                const int interval = mTop - m;
                if (interval <= 2) continue;
                if (interval == 6) continue;
                if (interval >= 11) continue;
                int s = 0;
                if (interval == 3 || interval == 4) s += 0;
                else if (interval == 8 || interval == 9) s += 1;
                else if (interval == 7 || interval == 5) s += 3;
                else if (interval == 10) s += 7;
                else s += 12;
                const int mid = (lo + hi) / 2;
                s += qAbs(mid - m) / 3;
                if (s < bestScore) { bestScore = s; bestMidi = m; }
            }
            return bestMidi;
        };

        const int mTop = nearestMidiForPc(sparklePc, around, lo, hi);
        const int mLow = chooseDyadLowMidi(sparklePc, mTop, lo, hi);

        const auto pos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 0, 1, ts);
        if (mLow >= 0) {
            virtuoso::engine::AgentIntentNote a;
            a.agent = "Piano";
            a.channel = midiChannel;
            a.note = clampMidi(mLow);
            a.baseVelocity = qBound(1, baseVel - 14, 127);
            a.startPos = pos;
            a.durationWhole = Rational(1, ts.den * 2);
            a.structural = false;
            a.chord_context = c.chordText;
            a.voicing_type = voicingType + " + SparkleDyad";
            a.logic_tag = "ballad_sparkle|dyad";
            a.target_note = "Sparkle (dyad low)";
            out.push_back(a);
        }
        virtuoso::engine::AgentIntentNote b;
        b.agent = "Piano";
        b.channel = midiChannel;
        b.note = clampMidi(mTop);
        b.baseVelocity = qBound(1, baseVel - 10, 127);
        b.startPos = pos;
        b.durationWhole = Rational(1, ts.den * 2);
        b.structural = false;
        b.chord_context = c.chordText;
        b.voicing_type = voicingType + " + SparkleDyad";
        b.logic_tag = "ballad_sparkle|dyad";
        b.target_note = "Sparkle (dyad high)";
        out.push_back(b);
    }

    // Silence-response fill: if user is silent, optionally add a tiny answering dyad on the upbeat of beat 4.
    if (c.userSilence && c.beatInBar == 3 && !m_lastVoicing.isEmpty()) {
        const quint32 hf = quint32(qHash(QString("%1|%2|%3|fill").arg(c.chordText).arg(c.playbackBarIndex).arg(c.determinismSeed)));
        if ((hf % 100u) < 18u) {
            // Tasteful answer as a small harmony (dyad), not a single ping.
            QSet<int> safe;
            auto addSafe = [&](int pc) { if (pc >= 0) safe.insert((pc % 12 + 12) % 12); };
            addSafe((c.chord.rootPc >= 0) ? (c.chord.rootPc % 12) : 0);
            addSafe(pcForDegree(c.chord, 3));
            addSafe(pcForDegree(c.chord, 5));
            addSafe(pcForDegree(c.chord, 7));
            addSafe(pcForDegree(c.chord, 9));
            addSafe(pcForDegree(c.chord, 13));
            if (c.chord.quality == music::ChordQuality::Minor) addSafe(pcForDegree(c.chord, 11));
            if (c.chord.quality == music::ChordQuality::Major) {
                for (const auto& a : c.chord.alterations) {
                    if (a.degree == 11 && a.delta == 1) { addSafe(pcForDegree(c.chord, 11)); break; }
                }
            }

            auto chooseDyadLowMidi = [&](int topPc, int mTop, int lo, int hi) -> int {
                const int t = ((topPc % 12) + 12) % 12;
                int bestMidi = -1;
                int bestScore = 999999;
                for (int pc : safe) {
                    if (pc == t) continue;
                    int m = nearestMidiForPc(pc, mTop - 5, lo, hi);
                    if (m < 0) continue;
                    while (m >= mTop && (m - 12) >= lo) m -= 12;
                    while ((mTop - m) > 12 && (m + 12) <= hi) m += 12;
                    if (m >= mTop) continue;
                    const int interval = mTop - m;
                    if (interval <= 2) continue;
                    if (interval == 6) continue;
                    if (interval >= 11) continue;
                    int s = 0;
                    if (interval == 3 || interval == 4) s += 0;
                    else if (interval == 8 || interval == 9) s += 1;
                    else if (interval == 7 || interval == 5) s += 3;
                    else if (interval == 10) s += 7;
                    else s += 12;
                    const int mid = (lo + hi) / 2;
                    s += qAbs(mid - m) / 3;
                    if (s < bestScore) { bestScore = s; bestMidi = m; }
                }
                return bestMidi;
            };

            const int pc9 = pcForDegree(c.chord, 9);
            const int around = m_lastVoicing.last();
            const int mTop = nearestMidiForPc(pc9, around + 7, c.rhLo, c.rhHi);
            const int mLow = chooseDyadLowMidi(pc9, mTop, c.rhLo, c.rhHi);

            const auto pos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, 3, /*sub*/1, /*count*/2, ts); // and-of-4
            if (mLow >= 0) {
                virtuoso::engine::AgentIntentNote a;
                a.agent = "Piano";
                a.channel = midiChannel;
                a.note = clampMidi(mLow);
                a.baseVelocity = qMax(1, baseVel - 18);
                a.startPos = pos;
                a.durationWhole = Rational(1, 8);
                a.structural = false;
                a.chord_context = c.chordText;
                a.voicing_type = voicingType + " + FillDyad";
                a.logic_tag = "ballad_silence_fill|dyad";
                a.target_note = "Answer fill (dyad low)";
                out.push_back(a);
            }
            virtuoso::engine::AgentIntentNote b;
            b.agent = "Piano";
            b.channel = midiChannel;
            b.note = clampMidi(mTop);
            b.baseVelocity = qMax(1, baseVel - 14);
            b.startPos = pos;
            b.durationWhole = Rational(1, 8);
            b.structural = false;
            b.chord_context = c.chordText;
            b.voicing_type = voicingType + " + FillDyad";
            b.logic_tag = "ballad_silence_fill|dyad";
            b.target_note = "Answer fill (dyad high)";
            out.push_back(b);
        }
    }
    return out;
}

void JazzBalladPianoPlanner::ensureBarRhythmPlanned(const Context& c) {
    const int bar = qMax(0, c.playbackBarIndex);
    if (bar == m_lastRhythmBar) return;
    m_lastRhythmBar = bar;
    m_barHits = chooseBarCompRhythm(c);
    ensureMotifBlockPlanned(c);
    m_barTopHits = chooseBarTopLine(c);
}

void JazzBalladPianoPlanner::ensureMotifBlockPlanned(const Context& c) {
    const int bar = qMax(0, c.playbackBarIndex);
    const int blockStart = (bar / 2) * 2;
    if (blockStart == m_motifBlockStartBar) return;
    m_motifBlockStartBar = blockStart;
    buildMotifBlockTemplates(c);
}

void JazzBalladPianoPlanner::buildMotifBlockTemplates(const Context& c) {
    m_motifA.clear();
    m_motifB.clear();

    const int bar = qMax(0, c.playbackBarIndex);
    const int block = bar / 2;
    const quint32 h = quint32(qHash(QString("pno_motif2|%1|%2|%3")
                                        .arg(c.chordText)
                                        .arg(block)
                                        .arg(c.determinismSeed)));
    const double e = qBound(0.0, c.energy, 1.0);
    const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
    const bool allow = !userBusy && (c.userSilence || e >= 0.45);
    if (!allow) return;

    auto addT = [&](QVector<TopTemplateHit>& v,
                    int beat, int sub, int count,
                    virtuoso::groove::Rational dur,
                    int velDelta,
                    int degree,
                    int neighborDir,
                    bool resolve,
                    QString tag) {
        TopTemplateHit t;
        t.beatInBar = qBound(0, beat, 3);
        t.sub = qMax(0, sub);
        t.count = qMax(1, count);
        if (t.sub >= t.count) t.sub = t.count - 1;
        t.dur = dur;
        t.velDelta = velDelta;
        t.degree = degree;
        t.neighborDir = neighborDir;
        t.resolve = resolve;
        t.tag = std::move(tag);
        v.push_back(t);
    };

    // Choose a degree center for this 2-bar motif.
    int deg = 9;
    if (c.chord.quality == music::ChordQuality::Major) deg = ((h % 3u) == 0u) ? 13 : 9;
    else if (c.chord.quality == music::ChordQuality::Minor) deg = ((h % 3u) == 0u) ? 11 : 9;
    else if (c.chord.quality == music::ChordQuality::Dominant) deg = ((h % 3u) == 0u) ? 13 : 9;

    const int variant = int(h % 4u);
    const int dir = (((h / 7u) % 2u) == 0u) ? -1 : +1;

    // Bar A: statement (late, floaty)
    if (variant == 0) {
        addT(m_motifA, 2, 1, 2, {1, 16}, -16, deg, dir, false, "neighbor_and3");
        addT(m_motifA, 3, 0, 1, {1, 8}, -10, deg, 0, true, "resolve4");
    } else if (variant == 1) {
        addT(m_motifA, 2, 1, 2, {1, 16}, -18, deg, -dir, false, "enclosure_a");
        addT(m_motifA, 3, 0, 1, {1, 16}, -18, deg, +dir, false, "enclosure_b");
        addT(m_motifA, 3, 1, 2, {1, 8}, -10, deg, 0, true, "enclosure_resolve");
    } else if (variant == 2) {
        int deg2 = deg;
        if (deg == 9) deg2 = (c.chord.quality == music::ChordQuality::Major) ? 13 : (c.chord.quality == music::ChordQuality::Minor ? 11 : 13);
        addT(m_motifA, 3, 0, 1, {1, 16}, -14, deg, 0, false, "step_a");
        addT(m_motifA, 3, 1, 2, {1, 8}, -10, deg2, 0, true, "step_b");
    } else {
        addT(m_motifA, 3, 0, 1, {1, 8}, -12, deg, 0, true, "answer4");
    }

    // Bar B: echo/sequence (same contour, slightly earlier sometimes)
    if (!m_motifA.isEmpty()) {
        for (const auto& t : m_motifA) {
            TopTemplateHit u = t;
            if (int((h / 11u) % 100u) < qBound(0, int(llround(35.0 + 25.0 * e)), 80)) {
                u.beatInBar = qMax(0, u.beatInBar - 1);
                if (u.beatInBar == 0 && u.count == 2 && u.sub > 0) u.sub = 0;
            }
            u.velDelta = qBound(-24, u.velDelta - 2, 6);
            u.tag = "echo_" + u.tag;
            m_motifB.push_back(u);
        }
        // Sequence: sometimes swap resolution degree (9<->13, 9<->11) on bar B.
        if (int((h / 13u) % 100u) < 35) {
            for (auto& u : m_motifB) {
                if (!u.resolve) continue;
                if (c.chord.quality == music::ChordQuality::Minor) u.degree = (u.degree == 9) ? 11 : 9;
                else u.degree = (u.degree == 9) ? 13 : 9;
                u.tag += "_seq";
            }
        }
    }
}

QVector<JazzBalladPianoPlanner::TopHit> JazzBalladPianoPlanner::realizeTopTemplate(
    const Context& c, const QVector<TopTemplateHit>& tmpl) const {
    QVector<TopHit> out;
    out.reserve(tmpl.size());

    const int pc1 = (c.chord.rootPc >= 0) ? (c.chord.rootPc % 12) : 0;
    const int pc3 = pcForDegree(c.chord, 3);
    const int pc5 = pcForDegree(c.chord, 5);
    const int pc7 = pcForDegree(c.chord, 7);
    const int pc9 = pcForDegree(c.chord, 9);
    const int pc11 = pcForDegree(c.chord, 11);
    const int pc13 = pcForDegree(c.chord, 13);

    QSet<int> safe;
    auto addSafe = [&](int pc) { if (pc >= 0) safe.insert((pc % 12 + 12) % 12); };
    addSafe(pc1);
    addSafe(pc3);
    addSafe(pc5);
    if (pc7 >= 0) addSafe(pc7);
    if (c.chord.quality == music::ChordQuality::Major) {
        addSafe(pc9);
        addSafe(pc13);
        bool hasSharp11 = false;
        for (const auto& a : c.chord.alterations) {
            if (a.degree == 11 && a.delta == 1) { hasSharp11 = true; break; }
        }
        if (hasSharp11) addSafe(pc11);
    } else if (c.chord.quality == music::ChordQuality::Minor) {
        addSafe(pc9);
        addSafe(pc11);
        if (c.chord.extension >= 13) addSafe(pc13);
    } else if (c.chord.quality == music::ChordQuality::Dominant) {
        addSafe(pc9);
        addSafe(pc13);
        for (const auto& a : c.chord.alterations) {
            if (a.degree == 11) addSafe(pc11);
        }
    } else {
        addSafe(pc9);
    }

    auto pickDegreePc = [&](int degree) -> int {
        int pc = -1;
        switch (degree) {
            case 1: pc = pc1; break;
            case 3: pc = pc3; break;
            case 5: pc = pc5; break;
            case 7: pc = pc7; break;
            case 9: pc = pc9; break;
            case 11: pc = pc11; break;
            case 13: pc = pc13; break;
            default: pc = pc9; break;
        }
        if (pc < 0) pc = pc9;
        const int p = (pc % 12 + 12) % 12;
        if (safe.contains(p)) return p;
        // fallback to a safe color
        const int p9 = (pc9 % 12 + 12) % 12;
        const int p13 = (pc13 % 12 + 12) % 12;
        const int p3 = (pc3 % 12 + 12) % 12;
        if (safe.contains(p9)) return p9;
        if (safe.contains(p13)) return p13;
        if (safe.contains(p3)) return p3;
        return p;
    };

    auto nearestOtherSafePc = [&](int aroundPc, int preferDir) -> int {
        int best = -1;
        int bestDist = 999;
        for (int pc : safe) {
            if (pc == aroundPc) continue;
            int d = pc - aroundPc;
            while (d > 6) d -= 12;
            while (d < -6) d += 12;
            const int dist = qAbs(d) * 10 + ((preferDir < 0 && d > 0) || (preferDir > 0 && d < 0) ? 3 : 0);
            if (dist < bestDist) { bestDist = dist; best = pc; }
        }
        return best;
    };

    for (const auto& t : tmpl) {
        const int targetPc = pickDegreePc(t.degree);
        int pc = targetPc;
        if (t.neighborDir != 0) {
            const int nb = nearestOtherSafePc(targetPc, t.neighborDir);
            if (nb >= 0) pc = nb;
        }
        TopHit o;
        o.beatInBar = t.beatInBar;
        o.sub = t.sub;
        o.count = t.count;
        o.dur = t.dur;
        o.velDelta = t.velDelta;
        o.pc = pc;
        o.resolve = t.resolve;
        o.tag = t.tag;
        out.push_back(o);
    }
    return out;
}

QVector<JazzBalladPianoPlanner::TopHit> JazzBalladPianoPlanner::chooseBarTopLine(const Context& c) const {
    const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
    const double e = qBound(0.0, c.energy, 1.0);
    if (userBusy) return {};
    if (!c.userSilence && e < 0.42) return {};
    const int bar = qMax(0, c.playbackBarIndex);
    const bool odd = (bar % 2) == 1;
    const auto& tmpl = odd ? m_motifB : m_motifA;
    return realizeTopTemplate(c, tmpl);
}

QVector<JazzBalladPianoPlanner::CompHit> JazzBalladPianoPlanner::chooseBarCompRhythm(const Context& c) const {
    QVector<CompHit> out;
    out.reserve(6);

    const quint32 h = quint32(qHash(QString("pno_rhy|%1|%2|%3").arg(c.chordText).arg(c.playbackBarIndex).arg(c.determinismSeed)));
    const double e = qBound(0.0, c.energy, 1.0);
    const bool climax = c.forceClimax && (e >= 0.75);
    const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
    const bool phraseEnd = ((qMax(1, c.playbackBarIndex) % 4) == 3);

    // Complexity proxy from energy, reduced if user is busy (make space).
    double complexity = e;
    if (userBusy) complexity *= 0.55;
    if (c.userSilence) complexity = qMin(1.0, complexity + 0.10);

    auto add = [&](int beat, int sub, int count, virtuoso::groove::Rational dur, int velDelta, QString density, QString tag) {
        CompHit hit;
        hit.beatInBar = qBound(0, beat, 3);
        hit.sub = qMax(0, sub);
        hit.count = qMax(1, count);
        hit.dur = dur;
        hit.velDelta = velDelta;
        hit.density = density;
        hit.rhythmTag = std::move(tag);
        out.push_back(hit);
    };

    // Sustain shaping: session players vary holds (stabs vs pads) depending on space/energy.
    // Deterministic per bar, so lookahead + audition always match.
    const int holdRoll = int((h / 29u) % 100u); // 0..99
    auto shortDur = [&]() -> virtuoso::groove::Rational { return userBusy ? virtuoso::groove::Rational(1, 16) : virtuoso::groove::Rational(1, 8); };
    auto medDur = [&]() -> virtuoso::groove::Rational { return virtuoso::groove::Rational(1, 4); };  // 1 beat
    auto longDur = [&]() -> virtuoso::groove::Rational { return virtuoso::groove::Rational(3, 8); }; // dotted quarter (~1.5 beats)
    auto padDur = [&]() -> virtuoso::groove::Rational { return virtuoso::groove::Rational(1, 2); };  // 2 beats

    auto chooseHold = [&](const QString& tag, bool canPad, bool canLong) -> virtuoso::groove::Rational {
        // Base: short stabs when user is busy; otherwise medium.
        virtuoso::groove::Rational d = userBusy ? shortDur() : medDur();
        // Let chords ring more in ballads, especially when user is silent.
        const bool isDownbeatLike = tag.contains("arrival") || tag.contains("touch1") || tag.contains("charleston_1");
        auto barDur = [&]() -> virtuoso::groove::Rational { return virtuoso::groove::Rational(3, 4); }; // ~3 beats
        if (canLong && !userBusy && c.userSilence && (holdRoll < 40)) d = longDur();
        if (canPad && !userBusy && c.userSilence && (holdRoll < 26)) d = padDur();
        if (isDownbeatLike && canPad && !userBusy && c.userSilence && (holdRoll < 10)) d = barDur();
        // In higher energy, shorten slightly (more motion); in very low energy, lengthen slightly.
        if (!userBusy && e >= 0.65 && d == medDur() && (holdRoll < 35)) d = shortDur();
        if (!userBusy && e <= 0.30 && d == medDur() && (holdRoll < 35)) d = longDur();
        return d;
    };

    // --- Base feel: jazz ballad comping is sparse but syncopated. ---
    // Patterns are bar-coherent and deterministic via hash.
    const int variant = int(h % 7u); // 0..6

    if (climax) {
        // Denser, but still musical: quarters + occasional upbeat pushes.
        add(0, 0, 1, chooseHold("climax_q1", /*canPad*/false, /*canLong*/false), +4, "full", "climax_q1");
        add(1, ((h / 3u) % 2u) ? 1 : 0, 2, shortDur(), 0, "guide", "climax_push2");
        add(2, 0, 1, chooseHold("climax_q3", /*canPad*/false, /*canLong*/false), +2, "full", "climax_q3");
        add(3, 1, 2, shortDur(), -6, "guide", phraseEnd ? "climax_push4_end" : "climax_push4");
        return out;
    }

    // Non-climax: choose a session-player-ish bar rhythm.
    switch (variant) {
        case 6: // Arrival on 1 + 4 (good for chord changes; avoids constant avoidance of beat 1)
            add(0, 0, 1, chooseHold("arrival1", /*canPad*/true, /*canLong*/true), -2, "guide", "arrival1");
            add(3, 0, 1, chooseHold("backbeat4_short", /*canPad*/true, /*canLong*/true), 0, "full", phraseEnd ? "only4_end" : "only4");
            break;
        case 0: // Classic 2&4, but light and short (breathing)
            add(1, 0, 1, shortDur(), -2, "guide", "backbeat2_short");
            add(3, 0, 1, chooseHold("backbeat4_short", /*canPad*/true, /*canLong*/true), 0, "full", "backbeat4_short");
            break;
        case 1: // Delayed 2 (laid-back) + 4
            add(1, 1, 2, shortDur(), -4, "guide", "delay2");
            add(3, 0, 1, chooseHold("backbeat4_short", /*canPad*/true, /*canLong*/true), 0, "full", "backbeat4_short");
            break;
        case 2: // Charleston-ish: beat 1 + and-of-2
            if (!c.userSilence && !c.chordIsNew && (int((h / 11u) % 100u) < int(llround(c.skipBeat2ProbStable * 100.0)))) {
                // leave space; only the push
                add(1, 1, 2, shortDur(), -4, "guide", "charleston_push_only");
            } else {
                add(0, 0, 1, chooseHold("charleston_1", /*canPad*/false, /*canLong*/true), -6, "guide", "charleston_1");
                add(1, 1, 2, chooseHold("charleston_and2", /*canPad*/true, /*canLong*/true), 0, "full", "charleston_and2");
            }
            break;
        case 3: // Anticipate 4 (and-of-4) + maybe 2
            if (complexity > 0.35) add(1, 0, 1, shortDur(), -2, "guide", "light2");
            // Push on and-of-4 often wants to ring into the barline a bit.
            add(3, 1, 2, chooseHold("push4", /*canPad*/true, /*canLong*/true), -6, "full", phraseEnd ? "push4_end" : "push4");
            break;
        case 4: // 2 only (super sparse, vocalist)
            add(1, 0, 1, chooseHold("only2", /*canPad*/false, /*canLong*/true), -4, "guide", "only2");
            break;
        case 5: // 4 only + tiny answer on and-of-3 if user silence
            add(3, 0, 1, chooseHold("only4", /*canPad*/true, /*canLong*/true), -4, "full", "only4");
            if (c.userSilence && complexity > 0.30) add(2, 1, 2, virtuoso::groove::Rational(1, 16), -10, "guide", "answer_and3");
            break;
        default: // syncopated 2 (and-of-2) + 4 (and-of-4)
            add(1, 1, 2, shortDur(), -6, "guide", "and2");
            add(3, 1, 2, chooseHold("and4", /*canPad*/true, /*canLong*/true), -8, "full", phraseEnd ? "and4_end" : "and4");
            break;
    }

    // Chord arrivals: sometimes hit on beat 1 (or and-of-1) so we don't always avoid the downbeat.
    // This is especially important for chart changes and keeps it from sounding "student-ish".
    if (c.chordIsNew && !userBusy) {
        const int pArr = qBound(0, int(llround(18.0 + 30.0 * complexity)), 60);
        if (int((h / 5u) % 100u) < pArr) {
            const bool onBeat = int((h / 7u) % 100u) < 70;
            add(0, onBeat ? 0 : 1, onBeat ? 1 : 2,
                chooseHold("arrival", /*canPad*/true, /*canLong*/true),
                +1, "guide", onBeat ? "arrival1" : "arrival_and1");
        }
    }

    // Also allow a *very soft* beat-1 touch occasionally even when harmony is stable,
    // to avoid feeling like comping is "missing the downbeat" forever.
    if (!c.chordIsNew && !userBusy && complexity > 0.40) {
        const int pTouch = qBound(0, int(llround(6.0 + 18.0 * complexity)), 28);
        if (int((h / 31u) % 100u) < pTouch) {
            add(0, 0, 1, chooseHold("touch1", /*canPad*/false, /*canLong*/true), -8, "guide", "touch1");
        }
    }

    // Occasional extra jab (and-of-1 or and-of-3) when there is space and energy.
    // Keep it rare; we're aiming for beauty over busy-ness.
    if (!userBusy && complexity > 0.65 && (int((h / 17u) % 100u) < 10)) {
        const bool on1 = ((h / 19u) % 2u) == 0u;
        add(on1 ? 0 : 2, 1, 2, virtuoso::groove::Rational(1, 16), -10, "guide", on1 ? "jab_and1" : "jab_and3");
    }

    // Phrase end: slightly more likely to add a light push into next bar.
    if (phraseEnd && !userBusy && (int((h / 23u) % 100u) < 28)) {
        add(3, 1, 2, chooseHold("phrase_end_push", /*canPad*/true, /*canLong*/true), -8, "guide", "phrase_end_push");
    }

    // If user is very active, bias to single backbeat (beat 4) to stay out of the way.
    if (userBusy) {
        QVector<CompHit> filtered;
        for (const auto& hit : out) {
            if (hit.beatInBar == 3 && hit.sub == 0) filtered.push_back(hit);
        }
        if (!filtered.isEmpty()) return filtered;
        // fallback: any beat-4 event
        for (const auto& hit : out) {
            if (hit.beatInBar == 3) filtered.push_back(hit);
        }
        if (!filtered.isEmpty()) return filtered;
    }

    return out;
}

} // namespace playback

