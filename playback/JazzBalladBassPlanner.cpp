#include "playback/JazzBalladBassPlanner.h"

#include "virtuoso/util/StableHash.h"
#include "virtuoso/solver/CspSolver.h"

#include <QtGlobal>
#include <limits>

namespace playback {

JazzBalladBassPlanner::JazzBalladBassPlanner() {
    reset();
}

void JazzBalladBassPlanner::reset() {
    m_state.ints.insert("lastFret", -1);
    m_state.ints.insert("lastString", -1);
    m_lastMidi = -1;
    m_walkPosBlockStartBar = -1;
    m_walkPosMidi = -1;
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

bool JazzBalladBassPlanner::feasibleOrRepair(int& midi) {
    midi = clampMidi(midi);
    // Try a few octave shifts to satisfy fret constraints.
    for (int k = 0; k < 5; ++k) {
        virtuoso::constraints::CandidateGesture g;
        g.midiNotes = {midi};
        const auto r = m_driver.evaluateFeasibility(m_state, g);
        if (r.ok) {
            for (auto it = r.stateUpdates.begin(); it != r.stateUpdates.end(); ++it) {
                m_state.ints.insert(it.key(), it.value());
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
    m_state.ints.insert("lastString", -1);
    virtuoso::constraints::CandidateGesture g;
    g.midiNotes = {midi};
    const auto r2 = m_driver.evaluateFeasibility(m_state, g);
    if (r2.ok) {
        for (auto it = r2.stateUpdates.begin(); it != r2.stateUpdates.end(); ++it) {
            m_state.ints.insert(it.key(), it.value());
        }
        m_lastMidi = midi;
        return true;
    }
    return false;
}

int JazzBalladBassPlanner::chooseApproachMidiWithConstraints(int nextRootMidi, QString* outChoiceId) const {
    // Two candidates: chromatic below / above.
    QVector<virtuoso::solver::Candidate<int>> cands;
    cands.push_back({"below", nextRootMidi - 1});
    cands.push_back({"above", nextRootMidi + 1});

    virtuoso::solver::DecisionTrace trace;
    const int bestIdx = virtuoso::solver::CspSolver::chooseMinCost(cands, [&](const auto& cand) {
        virtuoso::solver::EvalResult er;
        const int midi = clampMidi(cand.value);
        virtuoso::constraints::CandidateGesture g;
        g.midiNotes = {midi};
        const auto fr = m_driver.evaluateFeasibility(m_state, g);
        if (!fr.ok) {
            er.ok = false;
            er.reasons = fr.reasons;
            return er;
        }

        // Cost: prefer smaller leap + more playable fingering.
        double s = fr.cost;
        if (m_lastMidi >= 0) s += 0.04 * double(qAbs(midi - m_lastMidi));
        er.ok = true;
        er.cost = s;
        er.reasons = fr.reasons;
        return er;
    }, &trace);

    if (outChoiceId) *outChoiceId = (trace.chosenIndex >= 0) ? trace.chosenId : QString();
    if (bestIdx >= 0) return cands[bestIdx].value;
    // Fallback (should be rare): below.
    if (outChoiceId) *outChoiceId = "below_fallback";
    return nextRootMidi - 1;
}

QVector<virtuoso::engine::AgentIntentNote> JazzBalladBassPlanner::planBeat(const Context& c,
                                                                          int midiChannel,
                                                                          const virtuoso::groove::TimeSignature& ts) {
    QVector<virtuoso::engine::AgentIntentNote> out;

    using virtuoso::groove::GrooveGrid;
    using virtuoso::groove::Rational;

    const double progress01 = qBound(0.0, double(qMax(0, c.playbackBarIndex)) / 24.0, 1.0);
    const bool userBusy = (c.userDensityHigh || c.userIntensityPeak);
    const bool climaxWalk = c.forceClimax && (c.energy >= 0.75);
    // Phrase cadence + Virt: allow walking texture later in the song, and at cadence moments.
    const bool solverWalk = (!userBusy && ((progress01 >= 0.35 && c.rhythmicComplexity >= 0.85) || (c.cadence01 >= 0.80 && c.rhythmicComplexity >= 0.70)));
    const bool doWalk = climaxWalk || solverWalk;

    // Two-feel foundation on beats 1 and 3, plus optional pickup on beat 4 when approaching.
    // In Climax / later-song high-complexity: switch to a light walking feel to make the change audible.
    if (doWalk) {
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
    const bool canUsePhrase = (!doWalk && m_vocab && m_vocab->isLoaded() && (ts.num == 4) && (ts.den == 4));
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
    const bool useVocab = (!doWalk && m_vocab && m_vocab->isLoaded() && (ts.num == 4) && (ts.den == 4));
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

    if (doWalk) {
        // Walking grammar v1 (still simple, but much more "session" than MVP):
        // - Strong beats favor clear chord tones
        // - Weak beats can use passing tones (esp. dominants/cadences)
        // - Beat 4 approaches into next bar when harmony changes
        const int thirdIv =
            (c.chord.quality == music::ChordQuality::Minor ||
             c.chord.quality == music::ChordQuality::HalfDiminished ||
             c.chord.quality == music::ChordQuality::Diminished)
                ? 3
                : 4;
        const int pc3 = (rootPc + thirdIv) % 12;
        const int pc5 = (rootPc + 7) % 12;
        const int pc6 = (rootPc + 9) % 12;

        const int lo = 40, hi = 57;
        // 2-bar position lock: keep the bassist in a consistent register area.
        const int posBlock = (qMax(0, c.playbackBarIndex) / 2) * 2;
        if (posBlock != m_walkPosBlockStartBar) {
            m_walkPosBlockStartBar = posBlock;
            const int rootMidi = pcToBassMidiInRange(rootPc, lo, hi);
            // Anchor near last if possible, else near mid range.
            int anchor = (m_lastMidi >= 0) ? qBound(lo, m_lastMidi, hi) : qBound(lo, 47, hi);
            // Pull anchor toward root (but not too jumpy).
            if (qAbs(rootMidi - anchor) > 7) anchor = (rootMidi < anchor) ? (anchor - 5) : (anchor + 5);
            m_walkPosMidi = qBound(lo, anchor, hi);
        }

        auto pickMidiNearLast = [&](int pc, int lo2, int hi2) -> int {
            int m = pcToBassMidiInRange(pc, lo, hi);
            const int anchor = (m_walkPosMidi >= 0) ? m_walkPosMidi : ((lo + hi) / 2);
            const int ref = (m_lastMidi >= 0) ? m_lastMidi : anchor;
            int best = m;
            int bestD = qAbs(m - ref);
            for (int k = -2; k <= 2; ++k) {
                const int cand = m + 12 * k;
                if (cand < lo || cand > hi) continue;
                const int d = qAbs(cand - ref) + (qAbs(cand - anchor) / 3);
                if (d < bestD) { best = cand; bestD = d; }
            }
            return best;
        };

        auto chooseApproachTo = [&](int targetMidi) -> int {
            // Prefer half-step approaches; at cadence/dominant allow occasional whole-step.
            const bool spicy = (c.chordFunction == "Dominant") || (c.cadence01 >= 0.55);
            const quint32 h = virtuoso::util::StableHash::fnv1a32(QString("bwalk_app|%1|%2|%3")
                                                                      .arg(c.chordText)
                                                                      .arg(c.playbackBarIndex)
                                                                      .arg(c.determinismSeed)
                                                                      .toUtf8());
            const int step = (spicy && c.harmonicRisk >= 0.55 && int(h % 100u) < 35) ? 2 : 1;
            const int below = targetMidi - step;
            const int above = targetMidi + step;
            const int dBelow = (m_lastMidi >= 0) ? qAbs(below - m_lastMidi) : qAbs(step);
            const int dAbove = (m_lastMidi >= 0) ? qAbs(above - m_lastMidi) : qAbs(step);
            // Bias to "below" slightly (classic bass approach), but still pick the smaller leap.
            if (dBelow + 1 <= dAbove) return below;
            return above;
        };

        // Beat-by-beat choice (pitch-class domain) with voice-leading hints.
        if (c.beatInBar == 0) {
            chosenPc = rootPc;
            logic = "Bass:walk_v1 root";
        } else if (c.beatInBar == 1) {
            // Weak beat: choose chord tone, occasionally passing tone on dominants/cadences.
            struct Cand { QString id; int pc = 0; bool passing = false; };
            QVector<Cand> cands;
            cands.reserve(6);
            cands.push_back({"third", pc3, false});
            cands.push_back({"fifth", pc5, false});
            cands.push_back({"root", rootPc, false});
            if (c.harmonicRisk >= 0.65 && (c.chordFunction == "Tonic" || c.chordFunction == "Subdominant")) cands.push_back({"sixth", pc6, false});
            if (!userBusy && (c.chordFunction == "Dominant" || c.cadence01 >= 0.55)) {
                // A tiny chromatic passing tone is very idiomatic in walking lines.
                cands.push_back({"pass_up", (rootPc + 1) % 12, true});
                cands.push_back({"pass_dn", (rootPc + 11) % 12, true});
            }
            const int last = m_lastMidi;
            auto score = [&](const Cand& k) -> double {
                const int m = pickMidiNearLast(k.pc, lo, hi);
                double s = 0.0;
                if (last >= 0) s += 0.020 * double(qAbs(m - last));
                // Dominant function: prefer 3rd.
                if (c.chordFunction == "Dominant" && k.id == "third") s -= 0.25;
                // Passing tones only when we want motion.
                if (k.passing) s += (c.rhythmicComplexity >= 0.65 ? 0.10 : 0.60);
                // Avoid sitting on root too much.
                if (k.id == "root") s += 0.10;
                // Deterministic tiny tiebreak.
                s += (double(virtuoso::util::StableHash::fnv1a32(k.id.toUtf8())) / double(std::numeric_limits<uint>::max())) * 1e-6;
                return s;
            };
            const Cand* best = nullptr;
            double bestS = 1e9;
            for (const auto& k : cands) {
                const double sc = score(k);
                if (sc < bestS) { bestS = sc; best = &k; }
            }
            chosenPc = best ? best->pc : pc5;
            logic = best ? ("Bass:walk_v1 " + best->id) : "Bass:walk_v1 chord";
        } else if (c.beatInBar == 2) {
            // Strong beat: stable support tone; drift toward next root when cadence is strong.
            const bool wantMove = nextChanges || (c.cadence01 >= 0.55);
            struct Cand { QString id; int pc = 0; };
            QVector<Cand> cands;
            cands.reserve(4);
            cands.push_back({"fifth", pc5});
            cands.push_back({"third", pc3});
            if (c.harmonicRisk >= 0.60) cands.push_back({"sixth", pc6});
            if (wantMove && nextRootPc >= 0) cands.push_back({"nextRoot", nextRootPc});
            const int last = m_lastMidi;
            auto score = [&](const Cand& k) -> double {
                const int m = pickMidiNearLast(k.pc, lo, hi);
                double s = 0.0;
                if (last >= 0) s += 0.015 * double(qAbs(m - last));
                if (k.id == "nextRoot" && !wantMove) s += 0.6;
                if (k.id == "nextRoot" && wantMove) s -= 0.10;
                if (c.chordFunction == "Dominant" && k.id == "third") s -= 0.10;
                s += (double(virtuoso::util::StableHash::fnv1a32(k.id.toUtf8())) / double(std::numeric_limits<uint>::max())) * 1e-6;
                return s;
            };
            const Cand* best = nullptr;
            double bestS = 1e9;
            for (const auto& k : cands) {
                const double sc = score(k);
                if (sc < bestS) { bestS = sc; best = &k; }
            }
            chosenPc = best ? best->pc : pc5;
            logic = best ? ("Bass:walk_v1 " + best->id) : "Bass:walk_v1 support";
        } else { // beat 3 (0-based) => beat 4
            // Cadence / chord-change: approach into next bar.
            if (nextChanges) {
                const int nextRootMidi = pickMidiNearLast(nextRootPc, lo, hi);
                // Enclosure option (dominants/cadence): two 8ths on beat 4 -> and-of-4.
                const bool wantEnclosure = !userBusy && c.allowApproachFromAbove &&
                                           (c.chordFunction == "Dominant" || c.cadence01 >= 0.75) &&
                                           (c.rhythmicComplexity >= 0.70);
                const quint32 he = virtuoso::util::StableHash::fnv1a32(QString("bwalk_enc|%1|%2|%3")
                                                                           .arg(c.chordText)
                                                                           .arg(c.playbackBarIndex)
                                                                           .arg(c.determinismSeed)
                                                                           .toUtf8());
                if (wantEnclosure && int(he % 100u) < int(llround(25.0 + 55.0 * qBound(0.0, c.cadence01, 1.0)))) {
                    int a = nextRootMidi + 1;
                    int b = nextRootMidi - 1;
                    while (a > hi) a -= 12;
                    while (a < lo) a += 12;
                    while (b > hi) b -= 12;
                    while (b < lo) b += 12;
                    const bool upFirst = (int((he / 7u) % 2u) == 0u);
                    int first = upFirst ? a : b;
                    int second = upFirst ? b : a;
                    int r1 = first;
                    int r2 = second;
                    if (feasibleOrRepair(r1) && feasibleOrRepair(r2)) {
                        virtuoso::engine::AgentIntentNote n1;
                        n1.agent = "Bass";
                        n1.channel = midiChannel;
                        n1.note = r1;
                        n1.baseVelocity = 56;
                        n1.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, 0, 1, ts);
                        n1.durationWhole = Rational(1, 8);
                        n1.structural = false;
                        n1.chord_context = c.chordText;
                        n1.logic_tag = "walk_v1_enclosure";
                        n1.target_note = "Walk enclosure (beat4)";
                        out.push_back(n1);

                        virtuoso::engine::AgentIntentNote n2 = n1;
                        n2.note = r2;
                        n2.baseVelocity = 50;
                        n2.startPos = GrooveGrid::fromBarBeatTuplet(c.playbackBarIndex, c.beatInBar, /*sub*/1, /*count*/2, ts);
                        n2.durationWhole = Rational(1, 8);
                        n2.target_note = "Walk enclosure (and4)";
                        out.push_back(n2);
                        return out;
                    }
                }

                int approachMidi = c.allowApproachFromAbove ? chooseApproachTo(nextRootMidi) : (nextRootMidi - 1);
                while (approachMidi < lo) approachMidi += 12;
                while (approachMidi > hi) approachMidi -= 12;
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
                    n.logic_tag = "walk_v1_approach";
                    n.target_note = "Walk approach";
                    out.push_back(n);
                    return out;
                }
            }
            // If not changing, resolve toward root/third to make barline feel intentional.
            chosenPc = (c.cadence01 >= 0.55 || c.phraseEndBar) ? rootPc : pc3;
            logic = "Bass:walk_v1 resolve";
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
                QString appChoice;
                int approachMidi = c.allowApproachFromAbove ? chooseApproachMidiWithConstraints(nextRootMidi, &appChoice) : (nextRootMidi - 1);
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
                    n.logic_tag = QString("VocabPhrase:Bass:%1").arg(phraseId)
                        + (appChoice.trimmed().isEmpty() ? QString() : (QString("|csp_app=") + appChoice));
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
                QString appChoice;
                int approachMidi = c.allowApproachFromAbove ? chooseApproachMidiWithConstraints(nextRootMidi, &appChoice) : (nextRootMidi - 1);
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
                    n.logic_tag = QString("Vocab:Bass:%1").arg(vocabChoice.id)
                        + (appChoice.trimmed().isEmpty() ? QString() : (QString("|csp_app=") + appChoice));
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
            // Stage 3: candidate + cost selection for beat-3 support (non-vocab).
            // Keep the old "Chet space" behavior but modulate by Virtuosity weights.
            const bool stableHarmony = !nextChanges && !c.chordIsNew;
            if (stableHarmony) {
                const quint32 hStable = virtuoso::util::StableHash::fnv1a32(QString("%1|%2|%3|b3")
                                                                                .arg(c.chordText)
                                                                                .arg(c.playbackBarIndex)
                                                                                .arg(c.determinismSeed)
                                                                                .toUtf8());
                // If interaction is low or user is busy, omit more; otherwise omit less as song progresses.
                const double omit = qBound(0.0,
                                           c.skipBeat3ProbStable
                                               + 0.20 * (c.interaction < 0.35 ? 1.0 : 0.0)
                                               + 0.15 * (userBusy ? 1.0 : 0.0)
                                               - 0.20 * progress01
                                               - 0.25 * c.rhythmicComplexity,
                                           0.95);
                const int p = qBound(0, int(llround(omit * 100.0)), 100);
                if (p > 0 && int(hStable % 100u) < p) return out;
            }

            const int thirdIv =
                (c.chord.quality == music::ChordQuality::Minor ||
                 c.chord.quality == music::ChordQuality::HalfDiminished ||
                 c.chord.quality == music::ChordQuality::Diminished)
                    ? 3
                    : 4;
            const int pc3 = (rootPc + thirdIv) % 12;
            const int pc5 = (rootPc + 7) % 12;
            const int pc6 = (rootPc + 9) % 12;

            struct Cand { QString id; int pc = 0; bool rest = false; bool approach = false; };
            QVector<Cand> cands;
            cands.reserve(6);
            cands.push_back({"fifth", pc5, false, false});
            cands.push_back({"third", pc3, false, false});
            cands.push_back({"root", rootPc, false, false});
            if (c.harmonicRisk >= 0.70 && (c.chordFunction == "Tonic" || c.chordFunction == "Subdominant")) cands.push_back({"sixth", pc6, false, false});
            if (nextChanges) cands.push_back({"approach", rootPc, false, true});
            if (!c.userSilence && (userBusy || c.interaction < 0.35)) cands.push_back({"rest", rootPc, true, false});

            auto score = [&](const Cand& k) -> double {
                double s = 0.0;
                if (k.rest) {
                    // Rest is good when user is busy / interaction low; otherwise we want support.
                    s += (c.userSilence ? 1.0 : 0.2);
                    s += (c.interaction < 0.35 ? 0.0 : 0.7);
                    return s;
                }
                if (k.approach) {
                    // Prefer approaches on dominant function and later in song / higher rhythmicComplexity.
                    double want = (nextChanges ? 1.0 : 0.0) * (0.18 + 0.70 * c.rhythmicComplexity + 0.25 * progress01);
                    if (c.chordFunction == "Dominant") want = qMin(1.0, want + 0.15);
                    s += (1.0 - want) * 0.9;
                }
                // Stability: prefer 5th/3rd.
                if (k.id == "fifth") s -= 0.18;
                if (k.id == "third") s -= 0.10;
                if (k.id == "root") s += 0.12;
                if (c.chordFunction == "Dominant" && k.id == "third") s -= 0.18;
                // Voice-leading cost (smaller leaps).
                const int m = pcToBassMidiInRange(k.pc, 40, 57);
                if (m_lastMidi >= 0) s += 0.012 * double(qAbs(m - m_lastMidi));
                // Avoid color when user is busy.
                if (userBusy && k.id == "sixth") s += 0.9;
                s += (double(virtuoso::util::StableHash::fnv1a32(k.id.toUtf8())) / double(std::numeric_limits<uint>::max())) * 1e-6;
                return s;
            };

            const Cand* best = nullptr;
            double bestS = 1e9;
            for (const auto& k : cands) {
                const double sc = score(k);
                if (sc < bestS) { bestS = sc; best = &k; }
            }
            if (best && best->rest) return out;
            if (best && best->approach && nextChanges) {
                const int nextRootMidi = pcToBassMidiInRange(nextRootPc, /*lo*/40, /*hi*/57);
                QString appChoice;
                int approachMidi = c.allowApproachFromAbove ? chooseApproachMidiWithConstraints(nextRootMidi, &appChoice) : (nextRootMidi - 1);
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
                    n.durationWhole = Rational(1, ts.den);
                    n.structural = false;
                    n.chord_context = c.chordText;
                    n.logic_tag = QString("bass_solver_approach") + (appChoice.trimmed().isEmpty() ? QString() : (QString("|csp_app=") + appChoice));
                    n.target_note = QString("Approach -> next root pc=%1").arg(nextRootPc);
                    out.push_back(n);
                    return out;
                }
            }
            if (best) { chosenPc = best->pc; logic = "Bass: solver " + best->id; }
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
                QString appChoice;
                int approachMidi = c.allowApproachFromAbove ? chooseApproachMidiWithConstraints(nextRootMidi, &appChoice) : (nextRootMidi - 1);
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
                n.logic_tag = QString("VocabPhrase:Bass:%1").arg(phraseId)
                    + (appChoice.trimmed().isEmpty() ? QString() : (QString("|csp_app=") + appChoice));
                n.target_note = ph.notes.isEmpty() ? phraseNotes : ph.notes;
                out.push_back(n);
                return out;
            }
        }

        if (haveVocab && vocabChoice.action == virtuoso::vocab::VocabularyRegistry::BassAction::PickupToNext) {
            const int nextRootMidi = pcToBassMidiInRange(nextRootPc, /*lo*/40, /*hi*/57);
            QString appChoice;
            int approachMidi = c.allowApproachFromAbove ? chooseApproachMidiWithConstraints(nextRootMidi, &appChoice) : (nextRootMidi - 1);
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
            n.logic_tag = QString("Vocab:Bass:%1").arg(vocabChoice.id)
                + (appChoice.trimmed().isEmpty() ? QString() : (QString("|csp_app=") + appChoice));
            n.target_note = vocabChoice.notes.isEmpty()
                                ? QString("Pickup -> next root pc=%1").arg(nextRootPc)
                                : vocabChoice.notes;
            out.push_back(n);
            return out;
        }

        // Deterministic probability, slightly higher at phrase ends (4-bar phrasing).
        const bool phraseEnd = ((c.playbackBarIndex % 4) == 3);
        const quint32 hApp = virtuoso::util::StableHash::fnv1a32(QString("%1|%2|%3|app4")
                                                                     .arg(c.chordText)
                                                                     .arg(c.playbackBarIndex)
                                                                     .arg(c.determinismSeed)
                                                                     .toUtf8());
        // Make Virt rhythmicComplexity audibly affect pickup frequency.
        // (Higher complexity => more pickups/approaches; darker tone does not affect bass much here.)
        const double baseP = qBound(0.0, (c.approachProbBeat3 * 0.45) * (0.35 + 0.95 * qBound(0.0, c.rhythmicComplexity, 1.0)), 1.0);
        const double p = phraseEnd ? qMin(1.0, baseP + 0.18) : baseP;
        if (int(hApp % 100u) >= int(llround(p * 100.0))) return out;

        const int nextRootMidi = pcToBassMidiInRange(nextRootPc, /*lo*/40, /*hi*/57);
        QString appChoice;
        int approachMidi = c.allowApproachFromAbove ? chooseApproachMidiWithConstraints(nextRootMidi, &appChoice) : (nextRootMidi - 1);
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
        n.logic_tag = QString("two_feel_pickup") + (appChoice.trimmed().isEmpty() ? QString() : (QString("|csp_app=") + appChoice));
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
    const int baseVel = doWalk ? 58 : ((c.beatInBar == 0) ? 56 : 50);
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
            const quint32 hLen = virtuoso::util::StableHash::fnv1a32(QString("%1|%2|%3|len")
                                                                         .arg(c.chordText)
                                                                         .arg(c.playbackBarIndex)
                                                                         .arg(c.determinismSeed)
                                                                         .toUtf8());
            const bool longHold = stable && ((hLen % 4u) == 0u);
            // Walk articulation: slightly legato when stepwise, otherwise normal quarter.
            if (doWalk) {
                const bool stepwise = (m_lastMidi >= 0) && (qAbs(n.note - m_lastMidi) <= 2);
                n.durationWhole = Rational(1, ts.den) + (stepwise && !userBusy ? Rational(1, 32) : Rational(0, 1));
            } else {
                n.durationWhole = longHold ? Rational(ts.num, ts.den) : Rational(2, ts.den);
            }
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
            if (doWalk) {
                const bool stepwise = (m_lastMidi >= 0) && (qAbs(n.note - m_lastMidi) <= 2);
                n.durationWhole = Rational(1, ts.den) + (stepwise && !userBusy ? Rational(1, 32) : Rational(0, 1));
            } else {
                n.durationWhole = Rational(2, ts.den);
            }
        }
    }
    n.structural = c.chordIsNew || (c.beatInBar == 0);
    n.chord_context = c.chordText;
    if (havePhraseHits) n.logic_tag = QString("VocabPhrase:Bass:%1").arg(phraseId);
    else if (haveVocab) n.logic_tag = QString("Vocab:Bass:%1").arg(vocabChoice.id);
    else n.logic_tag = doWalk ? "walk" : ((c.beatInBar == 0) ? "two_feel_root" : "two_feel_fifth");
    n.target_note = logic;
    out.push_back(n);
    return out;
}

} // namespace playback

