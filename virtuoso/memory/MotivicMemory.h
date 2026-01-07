#pragma once

#include <QHash>
#include <QVector>
#include <QString>

#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/groove/GrooveGrid.h"

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

    void clear();
    void push(const virtuoso::engine::AgentIntentNote& n);

    // Recent raw entries (last maxN, regardless of bars).
    QVector<Entry> recent(const QString& agent, int maxN = 8) const;

    // Recent entries restricted to a rolling bar window (inferred from the last-seen barIndex per agent).
    QVector<Entry> recentInBars(const QString& agent, int bars, int maxN = 16) const;

    // Convenience: recent pitch-class motif (0..11) for an agent over the last `bars` bars.
    QVector<int> recentPitchMotif(const QString& agent, int bars, int maxN = 16) const;

    // Convenience: recent rhythm motif as a 16th-grid bitmask across the bar.
    // slotsPerBeat=4 => 16ths. Returns up to 64 slots (supports up to 16/4=4 beats? Actually ts.num*slotsPerBeat must be <=64).
    quint64 recentRhythmMotifMask16(const QString& agent,
                                   int bars,
                                   const virtuoso::groove::TimeSignature& ts,
                                   int slotsPerBeat = 4,
                                   int maxN = 64) const;

    int lastMidi(const QString& agent) const;
    int prevMidi(const QString& agent) const;

private:
    static quint64 mask16ForEntries(const QVector<Entry>& entries,
                                   const virtuoso::groove::TimeSignature& ts,
                                   int slotsPerBeat);

    int m_max = 256;
    QHash<QString, QVector<Entry>> m_byAgent;
};

} // namespace virtuoso::memory

