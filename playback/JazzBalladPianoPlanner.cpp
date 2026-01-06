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
    int sparkleMidi = -1;
    if (sparkle) {
        // Choose a tasteful color (9 or 11) and place it high.
        const int spPc = pcForDegree(c.chord, ((h % 2u) == 0u) ? 9 : 11);
        sparkleMidi = nearestMidiForPc(spPc, /*around*/(c.sparkleLo + c.sparkleHi) / 2, c.sparkleLo, c.sparkleHi);
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
        const QVector<int> notes = (hit.density.trimmed().toLower() == "guide") ? guideSubset(midi) : midi;
        for (int m : notes) {
            virtuoso::engine::AgentIntentNote n;
            n.agent = "Piano";
            n.channel = midiChannel;
            n.note = clampMidi(m);
            n.baseVelocity = qBound(1, baseVel + hit.velDelta, 127);
            n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, hit.beatInBar, hit.sub, hit.count, ts);
            n.durationWhole = hit.dur;
            n.structural = c.chordIsNew;
            n.chord_context = c.chordText;
            n.voicing_type = voicingType;
            n.logic_tag = hit.rhythmTag.isEmpty() ? "ballad_comp" : ("ballad_comp|" + hit.rhythmTag);
            n.target_note = "Comp";
            out.push_back(n);
        }
    };

    for (const auto& hit : hitsThisBeat) renderHit(hit);

    if (sparkleMidi >= 0) {
        virtuoso::engine::AgentIntentNote n;
        n.agent = "Piano";
        n.channel = midiChannel;
        n.note = clampMidi(sparkleMidi);
        n.baseVelocity = baseVel - 6;
        n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 0, 1, ts);
        n.durationWhole = Rational(1, ts.den * 2); // short sparkle
        n.structural = false;
        n.chord_context = c.chordText;
        n.voicing_type = voicingType + " + Sparkle";
        n.logic_tag = "ballad_sparkle";
        n.target_note = "High sparkle";
        out.push_back(n);
    }

    // Silence-response fill: if user is silent, optionally add a tiny answering dyad on the upbeat of beat 4.
    if (c.userSilence && c.beatInBar == 3 && !m_lastVoicing.isEmpty()) {
        const quint32 hf = quint32(qHash(QString("%1|%2|%3|fill").arg(c.chordText).arg(c.playbackBarIndex).arg(c.determinismSeed)));
        if ((hf % 100u) < 18u) {
            // Choose a simple color (9) near the last RH cluster.
            const int pc9 = pcForDegree(c.chord, 9);
            const int around = m_lastVoicing.last();
            const int m1 = nearestMidiForPc(pc9, around + 7, c.rhLo, c.rhHi);
            virtuoso::engine::AgentIntentNote f;
            f.agent = "Piano";
            f.channel = midiChannel;
            f.note = clampMidi(m1);
            f.baseVelocity = qMax(1, baseVel - 12);
            f.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, 3, /*sub*/1, /*count*/2, ts); // and-of-4
            f.durationWhole = Rational(1, 16);
            f.structural = false;
            f.chord_context = c.chordText;
            f.voicing_type = voicingType + " + Fill";
            f.logic_tag = "ballad_silence_fill";
            f.target_note = "Answer fill";
            out.push_back(f);
        }
    }
    return out;
}

void JazzBalladPianoPlanner::ensureBarRhythmPlanned(const Context& c) {
    const int bar = qMax(0, c.playbackBarIndex);
    if (bar == m_lastRhythmBar) return;
    m_lastRhythmBar = bar;
    m_barHits = chooseBarCompRhythm(c);
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
        Q_UNUSED(tag);
        // Base: short stabs when user is busy; otherwise medium.
        virtuoso::groove::Rational d = userBusy ? shortDur() : medDur();
        if (canLong && !userBusy && c.userSilence && (holdRoll < 28)) d = longDur();
        if (canPad && !userBusy && c.userSilence && (holdRoll < 14)) d = padDur();
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

    // Occasional extra jab (and-of-1 or and-of-3) when there is space and energy.
    if (!userBusy && complexity > 0.55 && (int((h / 17u) % 100u) < 22)) {
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

