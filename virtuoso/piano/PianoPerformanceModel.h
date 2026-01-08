#pragma once

#include <QString>
#include <QVector>

#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/piano/PianoPerformancePlan.h"

namespace virtuoso::piano {

// PianoPerformanceModel v1:
// - Owns the "performance plan" representation (notes+pedal+gestures)
// - In this initial integration step, it can infer performance metadata from legacy AgentIntentNotes/CC64 intents.
// Later milestones move generation of pedal/gesture/topline into this model.
class PianoPerformanceModel final {
public:
    struct LegacyCc64Intent {
        int value = 0; // 0..127
        groove::GridPos startPos;
        bool structural = false;
        QString logicTag;
    };

    // Infer a performance plan from already-realized piano note intents and CC64 actions.
    // This is used to keep behavior stable while we refactor the generator to be action-first.
    static PianoPerformancePlan inferFromLegacy(const QVector<virtuoso::engine::AgentIntentNote>& notes,
                                                const QVector<LegacyCc64Intent>& cc64);
};

} // namespace virtuoso::piano

