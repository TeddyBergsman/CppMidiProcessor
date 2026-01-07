#pragma once

#include <QSet>
#include <QString>
#include <QVector>

#include "music/ChordSymbol.h"
#include "virtuoso/control/VirtuosityMatrix.h"
#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/groove/GrooveGrid.h"

namespace virtuoso::solver {

struct CostWeights {
    double harmony = 1.0;
    double voiceLeading = 1.0;
    double rhythm = 1.0;
    double interaction = 1.0;
};

// Spec-aligned cost components (lower is better).
struct CostBreakdown {
    double harmonicStability = 0.0;
    double voiceLeadingDistance = 0.0;
    double rhythmicInterest = 0.0;
    double interactionFactor = 0.0;

    double total(const CostWeights& w) const {
        return (harmonicStability * w.harmony)
            + (voiceLeadingDistance * w.voiceLeading)
            + (rhythmicInterest * w.rhythm)
            + (interactionFactor * w.interaction);
    }

    QString shortTag(const CostWeights& w) const;
};

CostWeights weightsFromVirtuosity(const virtuoso::control::VirtuosityMatrix& v);

// Shared harmonic "allowed pitch class" set for a chord symbol (used for stability scoring).
QSet<int> allowedPitchClassesForChord(const music::ChordSymbol& c);

// Simple harmonic stability penalty:
// - counts non-allowed pitch classes in notes (0..127)
// - normalizes by note count (returns 0 when empty)
double harmonicOutsidePenalty01(const QVector<virtuoso::engine::AgentIntentNote>& notes,
                                const music::ChordSymbol& chord);

// Rhythmic interest proxy:
// - count offbeat attacks (subdivisions != beat start) and syncopation (odd beats)
// - normalize by note count (returns 0 when empty)
double rhythmicInterestPenalty01(const QVector<virtuoso::engine::AgentIntentNote>& notes,
                                 const virtuoso::groove::TimeSignature& ts);

// Voice-leading proxy:
// - compare mean MIDI to previous center target (abs semitones / 12)
double voiceLeadingPenalty(const QVector<virtuoso::engine::AgentIntentNote>& notes,
                           int prevCenterMidi);

} // namespace virtuoso::solver

