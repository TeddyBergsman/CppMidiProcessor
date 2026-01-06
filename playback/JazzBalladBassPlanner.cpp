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

    using virtuoso::groove::GrooveGrid;
    using virtuoso::groove::Rational;

    const bool climaxWalk = c.forceClimax && (c.energy >= 0.75);

    // Two-feel foundation on beats 1 and 3, plus optional pickup on beat 4 when approaching.
    // In Climax: switch to a light walking feel (quarter notes) to make the change audible.
    if (climaxWalk) {
        if (!(c.beatInBar >= 0 && c.beatInBar <= 3)) return out;
    } else {
        if (!(c.beatInBar == 0 || c.beatInBar == 2 || c.beatInBar == 3)) return out;
    }

    const int rootPc = (c.chord.bassPc >= 0) ? c.chord.bassPc : c.chord.rootPc;
    if (rootPc < 0) return out;

    // Determine next-chord root (for approach into bar starts).
    int nextRootPc = -1;
    if (c.hasNextChord) {
        nextRootPc = (c.nextChord.bassPc >= 0) ? c.nextChord.bassPc : c.nextChord.rootPc;
    }
    const bool nextChanges = (nextRootPc >= 0 && nextRootPc != rootPc);

    // Phrase-level rhythm vocab (primary "when to play" driver in non-walking mode).
    QVector<virtuoso::vocab::VocabularyRegistry::BassPhraseHit> phraseHits;
    QString phraseId, phraseNotes;
    const bool canUsePhrase = (!climaxWalk && m_vocab && m_vocab->isLoaded() && (ts.num == 4) && (ts.den == 4));
    if (canUsePhrase) {
        virtuoso::vocab::VocabularyRegistry::BassPhraseQuery pq;
        pq.ts = ts;
        pq.playbackBarIndex = c.playbackBarIndex;
        pq.beatInBar = c.beatInBar;
        pq.chordText = c.chordText;
        pq.chordIsNew = c.chordIsNew;
        pq.hasNextChord = c.hasNextChord;
        pq.nextChanges = nextChanges;
        pq.userDenseOrPeak = (c.userDensityHigh || c.userIntensityPeak);
        pq.energy = c.energy;
        pq.determinismSeed = c.determinismSeed;
        pq.phraseBars = 4;
        phraseHits = m_vocab->bassPhraseHitsForBeat(pq, &phraseId, &phraseNotes);
    }
    const bool havePhraseHits = !phraseHits.isEmpty() && !phraseId.isEmpty();

    // Vocab (optional): beat-scoped bass device selection (only in non-walking two-feel mode).
    virtuoso::vocab::VocabularyRegistry::BassBeatChoice vocabChoice;
    const bool useVocab = (!climaxWalk && m_vocab && m_vocab->isLoaded() && (ts.num == 4) && (ts.den == 4));
    if (useVocab) {
        virtuoso::vocab::VocabularyRegistry::BassBeatQuery q;
        q.ts = ts;
        q.playbackBarIndex = c.playbackBarIndex;
        q.beatInBar = c.beatInBar;
        q.chordText = c.chordText;
        q.chordIsNew = c.chordIsNew;
        q.hasNextChord = c.hasNextChord;
        q.nextChanges = nextChanges;
        q.userDenseOrPeak = (c.userDensityHigh || c.userIntensityPeak);
        q.energy = c.energy;
        q.determinismSeed = c.determinismSeed;
        vocabChoice = m_vocab->chooseBassBeat(q);
    }
    const bool haveVocab = (!vocabChoice.id.isEmpty() && vocabChoice.action != virtuoso::vocab::VocabularyRegistry::BassAction::None);

    // Base note selection:
    // - Beat 1: root (clear foundation)
    // - Beat 3: if next chord changes on the next bar, approach next root; else play 5th.
    int chosenPc = rootPc;
    QString logic;

    if (climaxWalk) {
        // Very simple walking grammar (MVP):
        // beat1: root, beat2: 5th or 3rd, beat3: approach/chord tone, beat4: approach into next bar if changing.
        const int pc3 = (rootPc + 4) % 12;
        const int pc5 = (rootPc + 7) % 12;
        if (c.beatInBar == 0) { chosenPc = rootPc; logic = "Bass:walk root"; }
        else if (c.beatInBar == 1) {
            chosenPc = ((qHash(QString("%1|%2|w2").arg(c.chordText).arg(c.playbackBarIndex)) % 2u) == 0u) ? pc5 : pc3;
            logic = "Bass:walk chord tone";
        } else if (c.beatInBar == 2) {
            chosenPc = pc5;
            logic = "Bass:walk support";
        } else { // beat 3 (0-based) => beat 4
            if (nextChanges) {
                const int nextRootMidi = pcToBassMidiInRange(nextRootPc, /*lo*/40, /*hi*/57);
                int approachMidi = c.allowApproachFromAbove ? chooseApproachMidi(nextRootMidi, m_lastMidi) : (nextRootMidi - 1);
                while (approachMidi < 40) approachMidi += 12;
                while (approachMidi > 57) approachMidi -= 12;
                int repaired = approachMidi;
                if (feasibleOrRepair(repaired)) {
                    virtuoso::engine::AgentIntentNote n;
                    n.agent = "Bass";
                    n.channel = midiChannel;
                    n.note = repaired;
                    n.baseVelocity = 58;
                    n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 0, 1, ts);
                    n.durationWhole = Rational(1, ts.den);
                    n.structural = false;
                    n.chord_context = c.chordText;
                    n.logic_tag = "walk_approach";
                    n.target_note = "Walk approach";
                    out.push_back(n);
                    return out;
                }
            }
            chosenPc = pc3;
            logic = "Bass:walk resolve";
        }
    } else if (c.beatInBar == 0) {
        if (havePhraseHits) {
            const auto& ph = phraseHits.first();
            if (ph.action == virtuoso::vocab::VocabularyRegistry::BassAction::Rest) return out;
            chosenPc = rootPc;
            logic = phraseNotes.isEmpty() ? QString("Phrase bass") : phraseNotes;
        }
        if (haveVocab) {
            if (vocabChoice.action == virtuoso::vocab::VocabularyRegistry::BassAction::Rest) return out;
            chosenPc = rootPc;
            logic = QString("Vocab: %1").arg(vocabChoice.notes);
        } else {
            chosenPc = rootPc;
            logic = "Bass: two-feel root";
        }
    } else if (c.beatInBar == 2) {
        if (havePhraseHits) {
            const auto& ph = phraseHits.first();
            using BA = virtuoso::vocab::VocabularyRegistry::BassAction;
            if (ph.action == BA::Rest) return out;
            if (ph.action == BA::ApproachToNext && nextChanges) {
                const int nextRootMidi = pcToBassMidiInRange(nextRootPc, /*lo*/40, /*hi*/57);
                int approachMidi = c.allowApproachFromAbove ? chooseApproachMidi(nextRootMidi, m_lastMidi) : (nextRootMidi - 1);
                while (approachMidi < 40) approachMidi += 12;
                while (approachMidi > 57) approachMidi -= 12;
                int repaired = approachMidi;
                if (feasibleOrRepair(repaired)) {
                    virtuoso::engine::AgentIntentNote n;
                    n.agent = "Bass";
                    n.channel = midiChannel;
                    n.note = repaired;
                    n.baseVelocity = qBound(1, 50 + ph.vel_delta, 127);
                    n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, ph.sub, ph.count, ts);
                    n.durationWhole = Rational(qMax(1, ph.dur_num), qMax(1, ph.dur_den));
                    n.structural = false;
                    n.chord_context = c.chordText;
                    n.logic_tag = QString("VocabPhrase:Bass:%1").arg(phraseId);
                    n.target_note = ph.notes.isEmpty() ? phraseNotes : ph.notes;
                    out.push_back(n);
                    return out;
                }
            }
            if (ph.action == BA::Third) {
                const int thirdIv =
                    (c.chord.quality == music::ChordQuality::Minor ||
                     c.chord.quality == music::ChordQuality::HalfDiminished ||
                     c.chord.quality == music::ChordQuality::Diminished)
                        ? 3
                        : 4;
                chosenPc = (rootPc + thirdIv) % 12;
            } else if (ph.action == BA::Root) {
                chosenPc = rootPc;
            } else {
                chosenPc = (rootPc + 7) % 12;
            }
            logic = phraseNotes.isEmpty() ? QString("Phrase bass") : phraseNotes;
        }
        if (haveVocab) {
            using BA = virtuoso::vocab::VocabularyRegistry::BassAction;
            if (vocabChoice.action == BA::Rest) return out;
            if (vocabChoice.action == BA::ApproachToNext && nextChanges) {
                const int nextRootMidi = pcToBassMidiInRange(nextRootPc, /*lo*/40, /*hi*/57);
                int approachMidi = c.allowApproachFromAbove ? chooseApproachMidi(nextRootMidi, m_lastMidi) : (nextRootMidi - 1);
                while (approachMidi < 40) approachMidi += 12;
                while (approachMidi > 57) approachMidi -= 12;
                int repaired = approachMidi;
                if (feasibleOrRepair(repaired)) {
                    virtuoso::engine::AgentIntentNote n;
                    n.agent = "Bass";
                    n.channel = midiChannel;
                    n.note = repaired;
                    n.baseVelocity = qBound(1, 50 + vocabChoice.vel_delta, 127);
                    n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 0, 1, ts);
                    n.durationWhole = Rational(qMax(1, vocabChoice.dur_num), qMax(1, vocabChoice.dur_den));
                    n.structural = false;
                    n.chord_context = c.chordText;
                    n.logic_tag = QString("Vocab:Bass:%1").arg(vocabChoice.id);
                    n.target_note = vocabChoice.notes.isEmpty()
                                        ? QString("Approach -> next root pc=%1").arg(nextRootPc)
                                        : vocabChoice.notes;
                    out.push_back(n);
                    return out;
                }
            }
            if (vocabChoice.action == BA::Third) {
                const int thirdIv =
                    (c.chord.quality == music::ChordQuality::Minor ||
                     c.chord.quality == music::ChordQuality::HalfDiminished ||
                     c.chord.quality == music::ChordQuality::Diminished)
                        ? 3
                        : 4;
                chosenPc = (rootPc + thirdIv) % 12;
            } else if (vocabChoice.action == BA::Root) {
                chosenPc = rootPc;
            } else {
                chosenPc = (rootPc + 7) % 12;
            }
            logic = QString("Vocab: %1").arg(vocabChoice.notes);
        } else {
            // Chet-style space: on stable harmony, sometimes omit beat 3 entirely.
            const bool stableHarmony = !nextChanges && !c.chordIsNew;
            if (stableHarmony) {
                const quint32 hStable = quint32(qHash(QString("%1|%2|%3|b3")
                                                     .arg(c.chordText)
                                                     .arg(c.playbackBarIndex)
                                                     .arg(c.determinismSeed)));
                const int p = qBound(0, int(llround(c.skipBeat3ProbStable * 100.0)), 100);
                if (p > 0 && int(hStable % 100u) < p) {
                    return out;
                }
            }

            if (nextChanges) {
                // Approach tone into the next bar's chord (probabilistic but deterministic).
                const quint32 hApp = quint32(qHash(QString("%1|%2|%3|app")
                                                   .arg(c.chordText)
                                                   .arg(c.playbackBarIndex)
                                                   .arg(c.determinismSeed)));
                const int p = qBound(0, int(llround(c.approachProbBeat3 * 100.0)), 100);
                const bool doApproach = (p > 0) && (int(hApp % 100u) < p);
                if (doApproach) {
                    const int nextRootMidi = pcToBassMidiInRange(nextRootPc, /*lo*/40, /*hi*/57);
                    int approachMidi = c.allowApproachFromAbove ? chooseApproachMidi(nextRootMidi, m_lastMidi) : (nextRootMidi - 1);
                    // Fold approach toward our bass range.
                    while (approachMidi < 40) approachMidi += 12;
                    while (approachMidi > 57) approachMidi -= 12;
                    int repaired = approachMidi;
                    if (feasibleOrRepair(repaired)) {
                        virtuoso::engine::AgentIntentNote n;
                        n.agent = "Bass";
                        n.channel = midiChannel;
                        n.note = repaired;
                        n.baseVelocity = 50;
                        n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 0, 1, ts);
                        n.durationWhole = Rational(1, ts.den); // 1 beat (beat 3) approach tone
                        n.structural = false;
                        n.chord_context = c.chordText;
                        n.logic_tag = "two_feel_approach";
                        n.target_note = QString("Approach -> next root pc=%1").arg(nextRootPc);
                        out.push_back(n);
                        return out;
                    }
                }
            }

            // 5th (or root fallback) when not approaching.
            const int fifthPc = (rootPc + 7) % 12;
            chosenPc = fifthPc;
            logic = "Bass: two-feel fifth";
        }
    } else {
        // Beat 4 pickup: only when the harmony is changing into next bar.
        if (!nextChanges) return out;

        // If the user is extremely dense/intense, avoid extra pickups (keep foundation).
        if (c.userDensityHigh || c.userIntensityPeak) return out;

        if (havePhraseHits) {
            const auto& ph = phraseHits.first();
            if (ph.action == virtuoso::vocab::VocabularyRegistry::BassAction::PickupToNext) {
                const int nextRootMidi = pcToBassMidiInRange(nextRootPc, /*lo*/40, /*hi*/57);
                int approachMidi = c.allowApproachFromAbove ? chooseApproachMidi(nextRootMidi, m_lastMidi) : (nextRootMidi - 1);
                while (approachMidi < 40) approachMidi += 12;
                while (approachMidi > 57) approachMidi -= 12;
                int repaired = approachMidi;
                if (!feasibleOrRepair(repaired)) return out;

                virtuoso::engine::AgentIntentNote n;
                n.agent = "Bass";
                n.channel = midiChannel;
                n.note = repaired;
                n.baseVelocity = qBound(1, 46 + ph.vel_delta, 127);
                n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, ph.sub, ph.count, ts);
                n.durationWhole = Rational(qMax(1, ph.dur_num), qMax(1, ph.dur_den));
                n.structural = false;
                n.chord_context = c.chordText;
                n.logic_tag = QString("VocabPhrase:Bass:%1").arg(phraseId);
                n.target_note = ph.notes.isEmpty() ? phraseNotes : ph.notes;
                out.push_back(n);
                return out;
            }
        }

        if (haveVocab && vocabChoice.action == virtuoso::vocab::VocabularyRegistry::BassAction::PickupToNext) {
            const int nextRootMidi = pcToBassMidiInRange(nextRootPc, /*lo*/40, /*hi*/57);
            int approachMidi = c.allowApproachFromAbove ? chooseApproachMidi(nextRootMidi, m_lastMidi) : (nextRootMidi - 1);
            while (approachMidi < 40) approachMidi += 12;
            while (approachMidi > 57) approachMidi -= 12;
            int repaired = approachMidi;
            if (!feasibleOrRepair(repaired)) return out;

            virtuoso::engine::AgentIntentNote n;
            n.agent = "Bass";
            n.channel = midiChannel;
            n.note = repaired;
            n.baseVelocity = qBound(1, 46 + vocabChoice.vel_delta, 127);
            n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar,
                                                      vocabChoice.sub, vocabChoice.count, ts);
            n.durationWhole = Rational(qMax(1, vocabChoice.dur_num), qMax(1, vocabChoice.dur_den));
            n.structural = false;
            n.chord_context = c.chordText;
            n.logic_tag = QString("Vocab:Bass:%1").arg(vocabChoice.id);
            n.target_note = vocabChoice.notes.isEmpty()
                                ? QString("Pickup -> next root pc=%1").arg(nextRootPc)
                                : vocabChoice.notes;
            out.push_back(n);
            return out;
        }

        // Deterministic probability, slightly higher at phrase ends (4-bar phrasing).
        const bool phraseEnd = ((c.playbackBarIndex % 4) == 3);
        const quint32 hApp = quint32(qHash(QString("%1|%2|%3|app4")
                                           .arg(c.chordText)
                                           .arg(c.playbackBarIndex)
                                           .arg(c.determinismSeed)));
        const double baseP = qBound(0.0, c.approachProbBeat3 * 0.65, 1.0);
        const double p = phraseEnd ? qMin(1.0, baseP + 0.18) : baseP;
        if (int(hApp % 100u) >= int(llround(p * 100.0))) return out;

        const int nextRootMidi = pcToBassMidiInRange(nextRootPc, /*lo*/40, /*hi*/57);
        int approachMidi = c.allowApproachFromAbove ? chooseApproachMidi(nextRootMidi, m_lastMidi) : (nextRootMidi - 1);
        while (approachMidi < 40) approachMidi += 12;
        while (approachMidi > 57) approachMidi -= 12;
        int repaired = approachMidi;
        if (!feasibleOrRepair(repaired)) return out;

        virtuoso::engine::AgentIntentNote n;
        n.agent = "Bass";
        n.channel = midiChannel;
        n.note = repaired;
        n.baseVelocity = 46;
        // Place on the upbeat 8th of beat 4 ("and of 4") as a pickup.
        n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, /*sub*/1, /*count*/2, ts);
        n.durationWhole = Rational(1, 8);
        n.structural = false;
        n.chord_context = c.chordText;
        n.logic_tag = "two_feel_pickup";
        n.target_note = QString("Pickup -> next root pc=%1").arg(nextRootPc);
        out.push_back(n);
        return out;
    }

    int midi = pcToBassMidiInRange(chosenPc, /*lo*/40, /*hi*/57);
    int repaired = midi;
    if (!feasibleOrRepair(repaired)) return out;

    virtuoso::engine::AgentIntentNote n;
    n.agent = "Bass";
    n.channel = midiChannel;
    n.note = repaired;
    const int baseVel = climaxWalk ? 58 : ((c.beatInBar == 0) ? 56 : 50);
    const int phraseVelDelta = havePhraseHits ? phraseHits.first().vel_delta : 0;
    n.baseVelocity = qBound(1, baseVel + (havePhraseHits ? phraseVelDelta : (haveVocab ? vocabChoice.vel_delta : 0)), 127);
    if (c.beatInBar == 0) {
        n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 0, 1, ts);
        if (havePhraseHits && phraseHits.first().action == virtuoso::vocab::VocabularyRegistry::BassAction::Root) {
            const auto& ph = phraseHits.first();
            n.durationWhole = Rational(qMax(1, ph.dur_num), qMax(1, ph.dur_den));
        } else if (haveVocab && vocabChoice.action == virtuoso::vocab::VocabularyRegistry::BassAction::Root) {
            n.durationWhole = Rational(qMax(1, vocabChoice.dur_num), qMax(1, vocabChoice.dur_den));
        } else {
            // On stable harmony, occasionally hold the root for the whole bar (Chet ballad vibe).
            const bool stable = (!c.hasNextChord) || ((c.nextChord.rootPc == c.chord.rootPc) && !c.chordIsNew);
            const quint32 hLen = quint32(qHash(QString("%1|%2|%3|len")
                                               .arg(c.chordText)
                                               .arg(c.playbackBarIndex)
                                               .arg(c.determinismSeed)));
            const bool longHold = stable && ((hLen % 4u) == 0u);
            n.durationWhole = climaxWalk ? Rational(1, ts.den) : (longHold ? Rational(ts.num, ts.den) : Rational(2, ts.den));
        }
    } else {
        n.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 0, 1, ts);
        if (havePhraseHits && (phraseHits.first().action == virtuoso::vocab::VocabularyRegistry::BassAction::Fifth ||
                               phraseHits.first().action == virtuoso::vocab::VocabularyRegistry::BassAction::Third ||
                               phraseHits.first().action == virtuoso::vocab::VocabularyRegistry::BassAction::Root)) {
            const auto& ph = phraseHits.first();
            n.durationWhole = Rational(qMax(1, ph.dur_num), qMax(1, ph.dur_den));
        } else if (haveVocab && (vocabChoice.action == virtuoso::vocab::VocabularyRegistry::BassAction::Fifth ||
                          vocabChoice.action == virtuoso::vocab::VocabularyRegistry::BassAction::Third ||
                          vocabChoice.action == virtuoso::vocab::VocabularyRegistry::BassAction::Root)) {
            n.durationWhole = Rational(qMax(1, vocabChoice.dur_num), qMax(1, vocabChoice.dur_den));
        } else {
            n.durationWhole = climaxWalk ? Rational(1, ts.den) : Rational(2, ts.den);
        }
    }
    n.structural = c.chordIsNew || (c.beatInBar == 0);
    n.chord_context = c.chordText;
    if (havePhraseHits) n.logic_tag = QString("VocabPhrase:Bass:%1").arg(phraseId);
    else if (haveVocab) n.logic_tag = QString("Vocab:Bass:%1").arg(vocabChoice.id);
    else n.logic_tag = climaxWalk ? "walk" : ((c.beatInBar == 0) ? "two_feel_root" : "two_feel_fifth");
    n.target_note = logic;
    out.push_back(n);
    return out;
}

} // namespace playback

