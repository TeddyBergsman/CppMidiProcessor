#include "playback/JazzBalladBassPlanner.h"

#include <QtGlobal>

namespace playback {

JazzBalladBassPlanner::JazzBalladBassPlanner() {
    reset();
}

void JazzBalladBassPlanner::reset() {
    m_state.ints.insert("lastFret", -1);
    m_lastMidi = -1;
}

int JazzBalladBassPlanner::pcToBassMidiInRange(int pc, int lo, int hi) {
    if (pc < 0) pc = 0;
    // Start at C2-ish and fold into range.
    int midi = lo + ((pc - (lo % 12) + 1200) % 12);
    while (midi < lo) midi += 12;
    while (midi > hi) midi -= 12;
    return midi;
}

int JazzBalladBassPlanner::chooseApproachMidi(int nextRootMidi, int lastMidi) {
    if (lastMidi < 0) {
        // Default: approach from below.
        return nextRootMidi - 1;
    }
    // Choose half-step approach direction that reduces leap.
    const int below = nextRootMidi - 1;
    const int above = nextRootMidi + 1;
    const int dBelow = qAbs(below - lastMidi);
    const int dAbove = qAbs(above - lastMidi);
    return (dBelow <= dAbove) ? below : above;
}

int JazzBalladBassPlanner::parseFretFromReasonLine(const QString& s) {
    // BassDriver emits: "OK: note=.. string=.. fret=.. cost=.."
    const int idx = s.indexOf("fret=");
    if (idx < 0) return -1;
    const int start = idx + 5;
    int end = start;
    while (end < s.size() && s[end].isDigit()) end++;
    bool ok = false;
    const int fret = s.mid(start, end - start).toInt(&ok);
    return ok ? fret : -1;
}

bool JazzBalladBassPlanner::feasibleOrRepair(int& midi) {
    midi = clampMidi(midi);
    // Try a few octave shifts to satisfy fret constraints.
    for (int k = 0; k < 5; ++k) {
        virtuoso::constraints::CandidateGesture g;
        g.midiNotes = {midi};
        const auto r = m_driver.evaluateFeasibility(m_state, g);
        if (r.ok) {
            // Update lastFret by parsing the OK message.
            for (const auto& line : r.reasons) {
                const int f = parseFretFromReasonLine(line);
                if (f >= 0) {
                    m_state.ints.insert("lastFret", f);
                    break;
                }
            }
            m_lastMidi = midi;
            return true;
        }
        // Repair: if not playable, move by octave toward the instrument center.
        if (midi < 45) midi += 12;
        else midi -= 12;
    }
    // If we're stuck due to lastFret shift, reset and try once more.
    m_state.ints.insert("lastFret", -1);
    virtuoso::constraints::CandidateGesture g;
    g.midiNotes = {midi};
    const auto r2 = m_driver.evaluateFeasibility(m_state, g);
    if (r2.ok) {
        for (const auto& line : r2.reasons) {
            const int f = parseFretFromReasonLine(line);
            if (f >= 0) { m_state.ints.insert("lastFret", f); break; }
        }
        m_lastMidi = midi;
        return true;
    }
    return false;
}

QVector<virtuoso::engine::AgentIntentNote> JazzBalladBassPlanner::planBeat(const Context& c,
                                                                          int midiChannel,
                                                                          const virtuoso::groove::TimeSignature& ts) {
    QVector<virtuoso::engine::AgentIntentNote> out;

    // Two-feel: beats 1 and 3 only (0 and 2 in 0-based).
    if (!(c.beatInBar == 0 || c.beatInBar == 2)) return out;

    const int rootPc = (c.chord.bassPc >= 0) ? c.chord.bassPc : c.chord.rootPc;
    if (rootPc < 0) return out;

    // Determine next-chord root (for approach into bar starts).
    int nextRootPc = -1;
    if (c.hasNextChord) {
        nextRootPc = (c.nextChord.bassPc >= 0) ? c.nextChord.bassPc : c.nextChord.rootPc;
    }

    // Base note selection:
    // - Beat 1: root (clear foundation)
    // - Beat 3: if next chord changes on the next bar, approach next root; else play 5th.
    int chosenPc = rootPc;
    QString logic;
    if (c.beatInBar == 0) {
        chosenPc = rootPc;
        logic = "Bass: two-feel root";
    } else {
        const bool nextChanges = (nextRootPc >= 0 && nextRootPc != rootPc);
        if (nextChanges) {
            // Approach tone into the next bar's chord.
            const int nextRootMidi = pcToBassMidiInRange(nextRootPc, /*lo*/40, /*hi*/57);
            int approachMidi = chooseApproachMidi(nextRootMidi, m_lastMidi);
            // Fold approach toward our bass range.
            while (approachMidi < 40) approachMidi += 12;
            while (approachMidi > 57) approachMidi -= 12;
            int repaired = approachMidi;
            if (feasibleOrRepair(repaired)) {
                virtuoso::engine::AgentIntentNote n;
                n.agent = "Bass";
                n.channel = midiChannel;
                n.note = repaired;
                n.baseVelocity = 54;
                n.durationWhole = virtuoso::groove::Rational(2, ts.den); // half a bar? (approx) => sustain
                n.structural = c.chordIsNew;
                n.chord_context = c.chordText;
                n.logic_tag = "two_feel_approach";
                n.target_note = QString("Approach -> next root pc=%1").arg(nextRootPc);
                out.push_back(n);
                return out;
            }
        }

        // 5th (or root fallback) when not approaching.
        const int fifthPc = (rootPc + 7) % 12;
        chosenPc = fifthPc;
        logic = "Bass: two-feel fifth";
    }

    int midi = pcToBassMidiInRange(chosenPc, /*lo*/40, /*hi*/57);
    int repaired = midi;
    if (!feasibleOrRepair(repaired)) return out;

    virtuoso::engine::AgentIntentNote n;
    n.agent = "Bass";
    n.channel = midiChannel;
    n.note = repaired;
    n.baseVelocity = (c.beatInBar == 0) ? 58 : 52;
    n.durationWhole = virtuoso::groove::Rational(2, ts.den); // sustain into space (ballad)
    n.structural = c.chordIsNew || (c.beatInBar == 0);
    n.chord_context = c.chordText;
    n.logic_tag = (c.beatInBar == 0) ? "two_feel_root" : "two_feel_fifth";
    n.target_note = logic;
    out.push_back(n);
    return out;
}

} // namespace playback

