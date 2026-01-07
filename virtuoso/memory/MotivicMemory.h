#pragma once

#include <QHash>
#include <QVector>
#include <QString>

#include "virtuoso/engine/VirtuosoEngine.h"

namespace virtuoso::memory {

// Shared ring-buffer for recent musical intents across agents.
// This is intentionally small: it provides enough history for simple repetition/variation
// and counterpoint checks.
class MotivicMemory final {
public:
    struct Entry {
        QString agent;
        int midi = -1;
        // Beat position (optional; used for rhythm displacement later)
        virtuoso::groove::GridPos pos;
    };

    explicit MotivicMemory(int maxEntriesPerAgent = 256) : m_max(maxEntriesPerAgent) {}

    void clear() { m_byAgent.clear(); }

    void push(const virtuoso::engine::AgentIntentNote& n) {
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

    QVector<Entry> recent(const QString& agent, int maxN = 8) const {
        const auto it = m_byAgent.find(agent);
        if (it == m_byAgent.end()) return {};
        const auto& v = it.value();
        const int n = qMax(0, qMin(maxN, v.size()));
        QVector<Entry> out;
        out.reserve(n);
        for (int i = v.size() - n; i < v.size(); ++i) out.push_back(v[i]);
        return out;
    }

    int lastMidi(const QString& agent) const {
        const auto it = m_byAgent.find(agent);
        if (it == m_byAgent.end() || it.value().isEmpty()) return -1;
        return it.value().last().midi;
    }

    int prevMidi(const QString& agent) const {
        const auto it = m_byAgent.find(agent);
        if (it == m_byAgent.end() || it.value().size() < 2) return -1;
        return it.value()[it.value().size() - 2].midi;
    }

private:
    int m_max = 256;
    QHash<QString, QVector<Entry>> m_byAgent;
};

} // namespace virtuoso::memory

