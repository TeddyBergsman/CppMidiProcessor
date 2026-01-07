#include "virtuoso/memory/MotivicMemory.h"

#include <QtGlobal>

namespace virtuoso::memory {

void MotivicMemory::clear() {
    m_byAgent.clear();
}

void MotivicMemory::push(const virtuoso::engine::AgentIntentNote& n) {
    Entry e;
    e.agent = n.agent;
    e.midi = n.note;
    e.pos = n.startPos;
    auto& v = m_byAgent[e.agent];
    v.push_back(e);
    if (m_max > 0 && v.size() > m_max) {
        v.erase(v.begin(), v.begin() + (v.size() - m_max));
    }
}

QVector<MotivicMemory::Entry> MotivicMemory::recent(const QString& agent, int maxN) const {
    const auto it = m_byAgent.find(agent);
    if (it == m_byAgent.end()) return {};
    const auto& v = it.value();
    const int n = qMax(0, qMin(maxN, v.size()));
    QVector<Entry> out;
    out.reserve(n);
    for (int i = v.size() - n; i < v.size(); ++i) out.push_back(v[i]);
    return out;
}

QVector<MotivicMemory::Entry> MotivicMemory::recentInBars(const QString& agent, int bars, int maxN) const {
    const auto it = m_byAgent.find(agent);
    if (it == m_byAgent.end()) return {};
    const auto& v = it.value();
    if (v.isEmpty()) return {};
    const int lastBar = v.last().pos.barIndex;
    const int barLo = qMax(0, lastBar - qMax(1, bars) + 1);

    QVector<Entry> out;
    out.reserve(qMin(maxN, v.size()));
    for (int i = v.size() - 1; i >= 0; --i) {
        const auto& e = v[i];
        if (e.pos.barIndex < barLo) break;
        out.push_back(e);
        if (out.size() >= maxN) break;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

QVector<int> MotivicMemory::recentPitchMotif(const QString& agent, int bars, int maxN) const {
    const auto ents = recentInBars(agent, bars, maxN);
    QVector<int> pcs;
    pcs.reserve(ents.size());
    for (const auto& e : ents) {
        if (e.midi < 0) continue;
        pcs.push_back((e.midi % 12 + 12) % 12);
    }
    return pcs;
}

quint64 MotivicMemory::mask16ForEntries(const QVector<Entry>& entries,
                                       const virtuoso::groove::TimeSignature& ts,
                                       int slotsPerBeat) {
    const int beatsPerBar = qMax(1, ts.num);
    const int spb = qMax(1, slotsPerBeat);
    const int slotsPerBar = beatsPerBar * spb;
    if (slotsPerBar <= 0 || slotsPerBar > 64) return 0;

    quint64 mask = 0;
    const auto beatWhole = virtuoso::groove::GrooveGrid::beatDurationWhole(ts);
    const auto slotWhole = beatWhole / spb;

    for (const auto& e : entries) {
        int beatInBar = 0;
        virtuoso::groove::Rational withinBeat{0, 1};
        virtuoso::groove::GrooveGrid::splitWithinBar(e.pos, ts, beatInBar, withinBeat);
        // quantize withinBeat to slot index
        // slot = round(withinBeat / slotWhole)
        const double t = withinBeat.toDouble();
        const double u = slotWhole.toDouble();
        int slotInBeat = (u > 0.0) ? int(llround(t / u)) : 0;
        slotInBeat = qBound(0, slotInBeat, spb - 1);
        const int slot = qBound(0, beatInBar * spb + slotInBeat, slotsPerBar - 1);
        mask |= (quint64(1) << quint64(slot));
    }
    return mask;
}

quint64 MotivicMemory::recentRhythmMotifMask16(const QString& agent,
                                              int bars,
                                              const virtuoso::groove::TimeSignature& ts,
                                              int slotsPerBeat,
                                              int maxN) const {
    const auto ents = recentInBars(agent, bars, maxN);
    return mask16ForEntries(ents, ts, slotsPerBeat);
}

int MotivicMemory::lastMidi(const QString& agent) const {
    const auto it = m_byAgent.find(agent);
    if (it == m_byAgent.end() || it.value().isEmpty()) return -1;
    return it.value().last().midi;
}

int MotivicMemory::prevMidi(const QString& agent) const {
    const auto it = m_byAgent.find(agent);
    if (it == m_byAgent.end() || it.value().size() < 2) return -1;
    return it.value()[it.value().size() - 2].midi;
}

} // namespace virtuoso::memory

