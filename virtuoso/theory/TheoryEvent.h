#pragma once

#include <QString>
#include <QJsonObject>

namespace virtuoso::theory {

// Stage 1: explainable "glass box" event model (subset of full spec).
// This is intentionally verbose and stringly-typed early; later it can be
// tightened with enums and normalized timestamp representations.
struct TheoryEvent {
    QString agent;            // e.g. "Piano"
    QString timestamp;        // e.g. "12.3.1.0"
    QString chord_context;    // e.g. "G7alt"
    QString scale_used;       // e.g. "Ab Melodic Minor (7th Mode)"
    QString voicing_type;     // e.g. "UST bVI (Eb Major Triad)"
    QString logic_tag;        // e.g. "Tritone Substitution Response"
    QString target_note;      // e.g. "B (3rd of Cmaj7)"
    QString dynamic_marking;  // e.g. "mf"

    QJsonObject toJsonObject() const;
    QString toJsonString(bool compact = true) const;
};

} // namespace virtuoso::theory

