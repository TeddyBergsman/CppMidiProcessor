#pragma once

#include <QVector>
#include <QString>
#include <QtGlobal>

namespace playback {

// Persistent long-horizon "story" state shared across agents.
// This is intentionally lightweight: it provides 4–8 bar continuity without requiring a full tree search.
struct StoryState final {
    struct RegisterArc {
        int startCenterMidi = 60;
        int endCenterMidi = 60;

        int centerAtBar(int barInPhrase, int phraseBars) const {
            if (phraseBars <= 1) return startCenterMidi;
            const double t = qBound(0.0, double(barInPhrase) / double(phraseBars - 1), 1.0);
            const double v = double(startCenterMidi) + (double(endCenterMidi) - double(startCenterMidi)) * t;
            return qBound(0, int(llround(v)), 127);
        }
    };

    // Phrase tracking (in playback bars, not chart bars)
    int phraseStartBar = -1;
    int phraseBars = 4;

    // Register arcs (centers) per agent
    RegisterArc bassArc{45, 45};
    RegisterArc pianoArc{72, 72};

    // Last observed centers (used as next phrase anchors)
    int lastBassCenterMidi = 45;
    int lastPianoCenterMidi = 72;

    // Phrase-level joint plan (beam-search output). One entry per beat-step.
    struct JointStepChoice {
        int stepIndex = -1; // absolute beat-step index
        QString bassId;
        QString pianoId;
        QString drumsId;
        QString costTag; // optional debug string
    };
    int planStartStep = -1;
    int planSteps = 0;
    QVector<JointStepChoice> plan;

    void reset() {
        phraseStartBar = -1;
        phraseBars = 4;
        bassArc = RegisterArc{45, 45};
        pianoArc = RegisterArc{72, 72};
        lastBassCenterMidi = 45;
        lastPianoCenterMidi = 72;
        planStartStep = -1;
        planSteps = 0;
        plan.clear();
    }
};

} // namespace playback

