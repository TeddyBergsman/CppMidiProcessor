#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>

namespace virtuoso::constraints {

// A candidate "gesture" to evaluate. Stage 1: just a set of MIDI notes.
struct CandidateGesture {
    QVector<int> midiNotes; // absolute MIDI pitches
};

// A minimal state container that can be extended without breaking interfaces.
// Drivers can store instrument-specific information in keyed fields.
struct PerformanceState {
    QVector<int> heldNotes;      // currently sounding notes (optional use)
    QHash<QString, int> ints;    // e.g. "lastFret", "lastString"
};

struct FeasibilityResult {
    bool ok = true;
    double cost = 0.0;           // lower is better
    QStringList reasons;         // explainable constraint outcomes

    // Optional state updates the caller may apply after choosing this candidate.
    // This avoids brittle parsing of reasons strings to maintain continuity state.
    QHash<QString, int> stateUpdates;
};

} // namespace virtuoso::constraints

