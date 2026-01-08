#include "playback/JointCandidateModel.h"

#include <QtGlobal>
#include <limits>

#include "virtuoso/constraints/PianoDriver.h"

namespace playback {

JointCandidateModel::NoteStats JointCandidateModel::statsForNotes(const QVector<virtuoso::engine::AgentIntentNote>& notes) {
    NoteStats s;
    if (notes.isEmpty()) return s;
    qint64 sum = 0;
    s.count = notes.size();
    for (const auto& n : notes) {
        const int m = qBound(0, n.note, 127);
        s.minMidi = qMin(s.minMidi, m);
        s.maxMidi = qMax(s.maxMidi, m);
        sum += m;
    }
    s.meanMidi = double(sum) / double(qMax(1, s.count));
    return s;
}

namespace {

static bool samePos(const virtuoso::groove::GridPos& a, const virtuoso::groove::GridPos& b) {
    return a.barIndex == b.barIndex && (a.withinBarWhole == b.withinBarWhole);
}

struct PianoExtraCosts {
    double pianist = 0.0;
    double pedal = 0.0;
    double topline = 0.0;
};

static PianoExtraCosts pianoExtraCosts(const JazzBalladPianoPlanner::PlannerState& startState,
                                       const JazzBalladPianoPlanner::BeatPlan& plan,
                                       const virtuoso::groove::TimeSignature& ts) {
    PianoExtraCosts out;
    virtuoso::constraints::PianoDriver driver;
    virtuoso::constraints::PerformanceState st = startState.perf;

    // Collect CC64 events (sorted by time).
    struct CcEv { virtuoso::groove::GridPos pos; int value = 0; };
    QVector<CcEv> ccs;
    ccs.reserve(plan.ccs.size());
    for (const auto& ci : plan.ccs) {
        if (ci.cc != 64) continue;
        ccs.push_back({ci.startPos, qBound(0, ci.value, 127)});
    }
    std::sort(ccs.begin(), ccs.end(), [&](const CcEv& a, const CcEv& b) {
        if (a.pos.barIndex != b.pos.barIndex) return a.pos.barIndex < b.pos.barIndex;
        return a.pos.withinBarWhole < b.pos.withinBarWhole;
    });

    // Group notes by startPos.
    struct Group { virtuoso::groove::GridPos pos; QVector<int> notes; bool hasTopLine = false; };
    QVector<Group> groups;
    groups.reserve(8);
    for (const auto& n : plan.notes) {
        if (n.note < 0) continue;
        int idx = -1;
        for (int i = 0; i < groups.size(); ++i) {
            if (samePos(groups[i].pos, n.startPos)) { idx = i; break; }
        }
        if (idx < 0) {
            Group g;
            g.pos = n.startPos;
            groups.push_back(g);
            idx = groups.size() - 1;
        }
        groups[idx].notes.push_back(qBound(0, n.note, 127));
        const QString tn = n.target_note.trimmed().toLower();
        if (tn.contains("topline")) groups[idx].hasTopLine = true;
    }
    std::sort(groups.begin(), groups.end(), [&](const Group& a, const Group& b) {
        if (a.pos.barIndex != b.pos.barIndex) return a.pos.barIndex < b.pos.barIndex;
        return a.pos.withinBarWhole < b.pos.withinBarWhole;
    });

    auto applyCcUpTo = [&](const virtuoso::groove::GridPos& pos) {
        for (const auto& ev : ccs) {
            if (ev.pos.barIndex < pos.barIndex || (ev.pos.barIndex == pos.barIndex && ev.pos.withinBarWhole <= pos.withinBarWhole)) {
                st.ints.insert("cc64", ev.value);
                if (ev.value <= 1) st.heldNotes.clear();
            }
        }
    };

    int pedalChanges = 0;
    int lastCc = st.ints.value("cc64", 0);
    for (const auto& ev : ccs) {
        if (ev.value != lastCc) ++pedalChanges;
        lastCc = ev.value;
    }
    out.pedal += 0.10 * double(qMax(0, pedalChanges - 1));

    // Step through the beat, accumulating feasibility costs.
    for (const auto& g : groups) {
        applyCcUpTo(g.pos);
        QVector<int> ns = g.notes;
        std::sort(ns.begin(), ns.end());
        ns.erase(std::unique(ns.begin(), ns.end()), ns.end());
        if (ns.isEmpty()) continue;
        virtuoso::constraints::CandidateGesture cg;
        cg.midiNotes = ns;
        const auto fr = driver.evaluateFeasibility(st, cg);
        if (!fr.ok) out.pianist += 25.0;
        else out.pianist += 0.12 * fr.cost;

        // Update held notes approximation.
        const int cc = st.ints.value("cc64", 0);
        const bool sustainAny = (cc >= 32);
        if (sustainAny) {
            for (int m : ns) if (!st.heldNotes.contains(m)) st.heldNotes.push_back(m);
        } else {
            st.heldNotes = ns;
        }

        if (g.hasTopLine) out.topline += 0.0;
    }

    // If the beat produced no topline note at all, add a tiny penalty (encourage melodic continuity).
    bool anyTop = false;
    for (const auto& g : groups) if (g.hasTopLine) { anyTop = true; break; }
    if (!anyTop) out.topline += 0.15;

    // Pedal clarity: penalize excessive held notes under sustain.
    const int ccEnd = st.ints.value("cc64", 0);
    if (ccEnd >= 32) {
        const int held = st.heldNotes.size();
        if (held > 14) out.pedal += 0.08 * double(held - 14);
    }

    return out;
}

} // namespace

void JointCandidateModel::generateBassPianoCandidates(const GenerationInputs& in,
                                                      QVector<BassCand>& outBass,
                                                      QVector<PianoCand>& outPiano) {
    outBass.clear();
    outPiano.clear();
    if (!in.bassPlanner || !in.pianoPlanner) return;

    auto planBass = [&](const QString& id, const JazzBalladBassPlanner::Context& ctx) -> BassCand {
        in.bassPlanner->restoreState(in.bassStart);
        BassCand c;
        c.id = id;
        c.ctx = ctx;
        c.plan = in.bassPlanner->planBeatWithActions(ctx, in.chBass, in.ts);
        c.nextState = in.bassPlanner->snapshotState();
        c.st = statsForNotes(c.plan.notes);
        return c;
    };
    auto planPiano = [&](const QString& id, const JazzBalladPianoPlanner::Context& ctx) -> PianoCand {
        in.pianoPlanner->restoreState(in.pianoStart);
        PianoCand c;
        c.id = id;
        c.ctx = ctx;
        c.plan = in.pianoPlanner->planBeatWithActions(ctx, in.chPiano, in.ts);
        c.nextState = in.pianoPlanner->snapshotState();
        c.st = statsForNotes(c.plan.notes);
        const auto extra = pianoExtraCosts(in.pianoStart, c.plan, in.ts);
        c.pianistFeasibilityCost = extra.pianist;
        c.pedalClarityCost = extra.pedal;
        c.topLineContinuityCost = extra.topline;
        return c;
    };

    outBass.reserve(3);
    outBass.push_back(planBass("sparse", in.bcSparse));
    outBass.push_back(planBass("base", in.bcBase));
    outBass.push_back(planBass("rich", in.bcRich));

    outPiano.reserve(3);
    outPiano.push_back(planPiano("sparse", in.pcSparse));
    outPiano.push_back(planPiano("base", in.pcBase));
    outPiano.push_back(planPiano("rich", in.pcRich));

    // Restore to caller-provided start states.
    in.bassPlanner->restoreState(in.bassStart);
    in.pianoPlanner->restoreState(in.pianoStart);
}

double JointCandidateModel::spacingPenalty(const QVector<virtuoso::engine::AgentIntentNote>& bassNotes,
                                          const QVector<virtuoso::engine::AgentIntentNote>& pianoNotes) {
    if (bassNotes.isEmpty() || pianoNotes.isEmpty()) return 0.0;
    int bassHi = 0;
    int pianoLo = 127;
    for (const auto& n : bassNotes) bassHi = qMax(bassHi, qBound(0, n.note, 127));
    for (const auto& n : pianoNotes) pianoLo = qMin(pianoLo, qBound(0, n.note, 127));
    const int spacingMin = 9; // semitones
    if (pianoLo >= bassHi + spacingMin) return 0.0;
    const int overlap = (bassHi + spacingMin) - pianoLo;
    return 6.0 + 0.85 * double(overlap);
}

JointCandidateModel::BestChoice JointCandidateModel::chooseBestCombo(const ScoringInputs& in,
                                                                     const QVector<BassCand>& bass,
                                                                     const QVector<PianoCand>& piano,
                                                                     const QVector<DrumCand>& drums,
                                                                     const QString& plannedBassId,
                                                                     const QString& plannedPianoId,
                                                                     const QString& plannedDrumsId) {
    BestChoice out;
    out.bestCost = std::numeric_limits<double>::infinity();
    if (bass.isEmpty() || piano.isEmpty() || drums.isEmpty()) return out;

    auto comboCost = [&](const QVector<virtuoso::engine::AgentIntentNote>& bassNotes,
                         const QVector<virtuoso::engine::AgentIntentNote>& pianoNotes,
                         const QVector<virtuoso::engine::AgentIntentNote>& drumNotes,
                         const QString& drumId,
                         virtuoso::solver::CostBreakdown* outBd) -> double {
        virtuoso::solver::CostBreakdown bd;

        bd.harmonicStability =
            0.65 * virtuoso::solver::harmonicOutsidePenalty01(bassNotes, in.chord) +
            0.95 * virtuoso::solver::harmonicOutsidePenalty01(pianoNotes, in.chord);

        bd.voiceLeadingDistance =
            0.55 * virtuoso::solver::voiceLeadingPenalty(bassNotes, in.prevBassCenterMidi) +
            0.55 * virtuoso::solver::voiceLeadingPenalty(pianoNotes, in.prevPianoCenterMidi);

        bd.rhythmicInterest =
            0.55 * virtuoso::solver::rhythmicInterestPenalty01(bassNotes, in.ts) +
            0.65 * virtuoso::solver::rhythmicInterestPenalty01(pianoNotes, in.ts) +
            0.20 * virtuoso::solver::rhythmicInterestPenalty01(drumNotes, in.ts);

        const double totalNotes = double(bassNotes.size() + pianoNotes.size()) + 0.35 * double(drumNotes.size());
        const double rc = qBound(0.0, in.virtAvg.rhythmicComplexity, 1.0);
        double target = 2.0 + 4.5 * rc;
        if (in.userSilence) target += qBound(0.0, in.virtAvg.interaction, 1.0) * 2.0;
        if (in.userBusy) target -= 2.5;
        target = qBound(0.0, target, 10.0);
        bd.interactionFactor = 0.55 * qAbs(totalNotes - target);
        if (in.userBusy) bd.interactionFactor += 0.45 * qMax(0.0, totalNotes - 3.0);

        if (in.cadence01 >= 0.80 && in.beatInBar == 0) {
            if (totalNotes <= 0.01) bd.interactionFactor += 6.0;
            else bd.interactionFactor = qMax(0.0, bd.interactionFactor - 0.30 * qMin(4.0, totalNotes));
        }

        if (drumId != "none") {
            const int beatsPerBar = qMax(1, in.ts.num);
            if ((in.phraseSetupBar || in.phraseEndBar) && in.beatInBar == (beatsPerBar - 1) && in.cadence01 >= 0.35) {
                if (drumId == "wet") bd.interactionFactor = qMax(0.0, bd.interactionFactor - 0.55 * qBound(0.0, in.cadence01, 1.0));
                else bd.interactionFactor += 0.65 * qBound(0.0, in.cadence01, 1.0);
            }
            if (in.userBusy && drumId == "wet") bd.interactionFactor += 1.25;
        }

        if (outBd) *outBd = bd;
        return bd.total(in.weights);
    };

    const bool havePlanned = (!plannedBassId.isEmpty() || !plannedPianoId.isEmpty() || !plannedDrumsId.isEmpty());
    if (havePlanned) {
        for (int bi = 0; bi < bass.size(); ++bi) if (bass[bi].id == plannedBassId) out.bestBi = bi;
        for (int pi = 0; pi < piano.size(); ++pi) if (piano[pi].id == plannedPianoId) out.bestPi = pi;
        for (int di = 0; di < drums.size(); ++di) if (drums[di].id == plannedDrumsId) out.bestDi = di;
        (void)comboCost(bass[out.bestBi].plan.notes, piano[out.bestPi].plan.notes, drums[out.bestDi].plan, drums[out.bestDi].id, &out.bestBd);
        out.bestCost = out.bestBd.total(in.weights) + spacingPenalty(bass[out.bestBi].plan.notes, piano[out.bestPi].plan.notes);
        return out;
    }

    out.combos.reserve(bass.size() * piano.size() * drums.size());
    for (int bi = 0; bi < bass.size(); ++bi) {
        for (int pi = 0; pi < piano.size(); ++pi) {
            for (int di = 0; di < drums.size(); ++di) {
                virtuoso::solver::CostBreakdown bd;
                double c = comboCost(bass[bi].plan.notes, piano[pi].plan.notes, drums[di].plan, drums[di].id, &bd);
                c += spacingPenalty(bass[bi].plan.notes, piano[pi].plan.notes);

                const double pianoExtra = piano[pi].pianistFeasibilityCost + piano[pi].pedalClarityCost + piano[pi].topLineContinuityCost;
                c += pianoExtra;

                if (!in.lastBassId.isEmpty() && in.lastBassId != bass[bi].id) c += in.bassSwitchPenalty;
                if (!in.lastPianoId.isEmpty() && in.lastPianoId != piano[pi].id) c += in.pianoSwitchPenalty;
                if (!in.lastDrumsId.isEmpty() && in.lastDrumsId != drums[di].id) c += in.drumsSwitchPenalty;

                if (in.inResponse) {
                    if (drums[di].id == "wet") c -= in.responseWetBonus;
                    if (piano[pi].id == "rich") c -= in.responsePianoRichBonus;
                    if (bass[bi].id == "rich") c -= in.responseBassRichBonus;
                }

                out.combos.push_back({bi, pi, di, bass[bi].id, piano[pi].id, drums[di].id, c, pianoExtra, bd});
                if (c < out.bestCost) {
                    out.bestCost = c;
                    out.bestBd = bd;
                    out.bestBi = bi;
                    out.bestPi = pi;
                    out.bestDi = di;
                }
            }
        }
    }
    return out;
}

} // namespace playback

