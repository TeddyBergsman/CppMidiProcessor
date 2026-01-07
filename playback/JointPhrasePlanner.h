#pragma once

#include <QVector>

#include "playback/AgentCoordinator.h"
#include "playback/StoryState.h"

namespace playback {

// Phrase-level planner: chooses a sequence of joint (Drums/Bass/Piano) "style IDs"
// across a 4–8 bar window, using a lightweight beam search.
class JointPhrasePlanner final {
public:
    struct Inputs {
        AgentCoordinator::Inputs in;
        int startStep = 0;
        int steps = 16;          // beat-steps to plan (typically phraseBars * beatsPerBar)
        int beamWidth = 6;
    };

    static QVector<StoryState::JointStepChoice> plan(const Inputs& p);
};

} // namespace playback

