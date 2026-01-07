#pragma once

#include <QHash>
#include <QVector>

#include "chart/ChartModel.h"

namespace playback {

// TransportTimeline: chart flattening + repeat / D.C./D.S. expansion, and utilities
// for mapping beat-steps to chart cells.
//
// This is an extracted component from VirtuosoBalladMvpPlaybackEngine to keep transport
// concerns separate from harmony/interaction/agent logic.
class TransportTimeline final {
public:
    void setModel(const chart::ChartModel* model) { m_model = model; }

    // Rebuilds the playback sequence (cell indices) based on the current model.
    void rebuild();

    const QVector<int>& sequence() const { return m_sequence; }

    QVector<const chart::Bar*> flattenBars() const;
    const chart::Cell* cellForFlattenedIndex(int cellIndex) const;

private:
    static QVector<int> buildPlaybackSequenceFrom(const chart::ChartModel& model);

    const chart::ChartModel* m_model = nullptr; // not owned
    QVector<int> m_sequence;
};

} // namespace playback

