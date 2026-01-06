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
            pc = (root + (iv >= 0 ? iv : 10)) % 12;
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

    // Vocab (optional): beat-scoped rhythm patterns.
    virtuoso::vocab::VocabularyRegistry::PianoBeatChoice vocabChoice;
    const bool useVocab = (m_vocab && m_vocab->isLoaded() && (ts.num == 4) && (ts.den == 4));
    if (useVocab) {
        virtuoso::vocab::VocabularyRegistry::PianoBeatQuery q;
        q.ts = ts;
        q.playbackBarIndex = c.playbackBarIndex;
        q.beatInBar = c.beatInBar;
        q.chordText = c.chordText;
        q.chordIsNew = c.chordIsNew;
        q.userSilence = c.userSilence;
        q.energy = c.energy;
        q.determinismSeed = c.determinismSeed;
        vocabChoice = m_vocab->choosePianoBeat(q);
    }
    const bool haveVocabHits = !vocabChoice.hits.isEmpty();

    // Ballad comp default: beats 2 and 4 (1 and 3 in 0-based), with occasional anticipations.
    // In Climax: comp every beat so the difference is audible.
    if (!haveVocabHits) {
        if (climaxDense) {
            if (!(c.beatInBar >= 0 && c.beatInBar <= 3)) return out;
        } else {
            // We allow beat 1 for "and-of-1" anticipations and beat 3 for sparse fills.
            if (!(c.beatInBar == 0 || c.beatInBar == 1 || c.beatInBar == 3)) return out;
        }
    }

    // Deterministic sparse variation: sometimes skip beat 2 if chord is stable (fallback mode).
    const quint32 h = quint32(qHash(QString("%1|%2|%3").arg(c.chordText).arg(c.playbackBarIndex).arg(c.determinismSeed)));
    const bool stable = !c.chordIsNew;
    if (!haveVocabHits) {
        const int pSkip = qBound(0, int(llround(c.skipBeat2ProbStable * 100.0)), 100);
        const bool skip = (stable && (c.beatInBar == 1) && (pSkip > 0) && (int(h % 100u) < pSkip));
        if (skip) return out;
    }

    // Interaction: if user is very dense/intense, piano should mostly comp on beat 4 only (unless forced climax).
    if (!climaxDense && (c.userDensityHigh || c.userIntensityPeak) && (c.beatInBar == 1 || c.beatInBar == 0)) return out;

    // Choose voicing family (Chet/Bill ballad: mostly shells, occasional rootless).
    const bool chordHas7 = (c.chord.extension >= 7 || c.chord.seventh != music::SeventhQuality::None);
    const bool dominant = (c.chord.quality == music::ChordQuality::Dominant);
    const bool alt = c.chord.alt || !c.chord.alterations.isEmpty();
    const bool pickA = ((h % 2u) == 0u);
    const bool useRootless = chordHas7 && (!c.preferShells || c.chordIsNew || (c.beatInBar == 3 && ((h % 4u) == 0u)));

    // Build a ballad-appropriate set: always guide tones, then 1–2 colors.
    QVector<int> pcs;
    pcs.reserve(5);
    const int pc3 = pcForDegree(c.chord, 3);
    const int pc7 = pcForDegree(c.chord, 7);
    pcs.push_back(pc3);
    pcs.push_back(pc7);

    // Primary color:
    int primaryDeg = 9;
    if (dominant && alt) primaryDeg = 9;                 // altered colors handled via alterations/defaults
    else if (c.chord.quality == music::ChordQuality::Major) primaryDeg = ((h % 3u) == 0u) ? 11 : 9; // sometimes #11-ish (if present)
    else if (c.chord.quality == music::ChordQuality::Minor) primaryDeg = ((h % 3u) == 0u) ? 11 : 9;
    pcs.push_back(pcForDegree(c.chord, primaryDeg));

    // Secondary color (occasional):
    const int p2 = qBound(0, int(llround(c.addSecondColorProb * 100.0)), 100);
    const bool add2 = (p2 > 0) && (int((h / 7u) % 100u) < p2);
    if (add2) {
        int deg2 = 13;
        if (dominant && alt) deg2 = 13;
        else if (c.chord.quality == music::ChordQuality::Major) deg2 = 13;
        else if (c.chord.quality == music::ChordQuality::Minor) deg2 = 11;
        pcs.push_back(pcForDegree(c.chord, deg2));
    }

    // If we chose rootless, we can swap in Type A/B flavor by optionally including 5th vs extra color.
    if (useRootless) {
        // Type A/B: slight re-bias; keep the set mostly the same, just ensure we have a 5 sometimes.
        if (pickA && ((h % 5u) == 0u)) pcs.push_back(pcForDegree(c.chord, 5));
        if (!pickA && ((h % 6u) == 0u)) pcs.push_back(pcForDegree(c.chord, 5));
    }

    sortUnique(pcs);

    // Map to midi with simple LH/RH ranges and voice-leading to last voicing.
    QVector<int> midi;
    midi.reserve(pcs.size());

    // LH anchor tones (3rd + 7th), RH colors with ballad spacing.
    for (int pc : pcs) {
        const bool isGuide = (pc == pc3 || pc == pc7);
        const int lo = isGuide ? c.lhLo : c.rhLo;
        const int hi = isGuide ? c.lhHi : c.rhHi;
        const int chosen = bestNearestToPrev(pc, m_lastVoicing, lo, hi);
        midi.push_back(chosen);
    }
    sortUnique(midi);

    midi = repairToFeasible(midi);
    if (midi.isEmpty()) return out;

    // Save for voice-leading.
    m_lastVoicing = midi;

    const QString voicingType = useRootless ? (pickA ? "Ballad Rootless (A-ish)" : "Ballad Rootless (B-ish)") : "Ballad Shell";
    const int baseVel = climaxDense ? (c.chordIsNew ? 64 : 58) : (c.chordIsNew ? 50 : 44);

    // Optional high sparkle on beat 4 (very occasional).
    const int pSp = qBound(0, int(llround(c.sparkleProbBeat4 * 100.0)), 100);
    const bool sparkle = (c.beatInBar == 3) && (pSp > 0) && (int((h / 13u) % 100u) < pSp);
    int sparkleMidi = -1;
    if (sparkle) {
        // Choose a tasteful color (9 or 11) and place it high.
        const int spPc = pcForDegree(c.chord, ((h % 2u) == 0u) ? 9 : 11);
        sparkleMidi = nearestMidiForPc(spPc, /*around*/(c.sparkleLo + c.sparkleHi) / 2, c.sparkleLo, c.sparkleHi);
    }

    // Helper: derive a "guide tones only" subset from current voicing (3rd + 7th).
    auto guideSubset = [&](const QVector<int>& full) -> QVector<int> {
        QVector<int> g;
        g.reserve(2);
        for (int m : full) {
            const int pc = ((m % 12) + 12) % 12;
            if (pc == pc3 || pc == pc7) g.push_back(m);
        }
        sortUnique(g);
        if (g.size() > 2) g.resize(2);
        if (g.isEmpty() && !full.isEmpty()) g.push_back(full.first());
        return g;
    };

    if (haveVocabHits) {
        for (const auto& hit : vocabChoice.hits) {
            const QVector<int> notes = (hit.density.trimmed().toLower() == "guide") ? guideSubset(midi) : midi;
            for (int m : notes) {
                virtuoso::engine::AgentIntentNote n;
                n.agent = "Piano";
                n.channel = midiChannel;
                n.note = clampMidi(m);
                n.baseVelocity = qBound(1, baseVel + hit.vel_delta, 127);
                n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, hit.sub, hit.count, ts);
                n.durationWhole = Rational(qMax(1, hit.dur_num), qMax(1, hit.dur_den));
                n.structural = c.chordIsNew;
                n.chord_context = c.chordText;
                n.voicing_type = voicingType;
                n.logic_tag = QString("Vocab:Piano:%1").arg(vocabChoice.id);
                n.target_note = vocabChoice.notes.isEmpty() ? QString("Vocab comp") : vocabChoice.notes;
                out.push_back(n);
            }
        }
    } else {
        for (int m : midi) {
            virtuoso::engine::AgentIntentNote n;
            n.agent = "Piano";
            n.channel = midiChannel;
            n.note = clampMidi(m);
            n.baseVelocity = baseVel;
            // Timing:
            // - main comp hits on beat starts (beats 2/4)
            // - occasional anticipation on "and of 1" when chord is new
            if (c.beatInBar == 0) {
                // Anticipation: only on chord arrivals, and only if not in silence (avoid stepping on vocal).
                if (!climaxDense) {
                    if (!c.chordIsNew || c.userSilence) continue;
                    if ((h % 5u) != 0u) continue;
                    n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, 0, /*sub*/1, /*count*/2, ts); // and-of-1
                    n.durationWhole = Rational(1, 8);
                    n.baseVelocity = qMax(1, baseVel - 10);
                    n.logic_tag = "ballad_anticipation";
                } else {
                    // In Climax: hit beat 1 normally (no anticipation).
                    n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, 0, 0, 1, ts);
                    n.durationWhole = Rational(1, ts.den);
                    n.logic_tag = "climax_comp";
                }
            } else {
                n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 0, 1, ts);
                n.durationWhole = Rational(1, ts.den); // 1 beat sustain (ballad comp)
            }
            n.structural = c.chordIsNew;
            n.chord_context = c.chordText;
            n.voicing_type = voicingType;
            if (n.logic_tag.isEmpty()) n.logic_tag = "ballad_comp";
            n.target_note = "Guide tones + color";
            out.push_back(n);
        }
    }

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

} // namespace playback

