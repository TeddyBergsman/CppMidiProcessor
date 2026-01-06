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

    // --- Groove explainability (optional) ---
    // These fields are intentionally stringly-typed in Stage 1.
    QString groove_template;  // e.g. "swing_2to1"
    QString grid_pos;         // e.g. "12.3@1/8w" (bar.beat@fraction in whole-notes)
    int timing_offset_ms = 0; // signed ms offset applied to the event time
    int velocity_adjustment = 0; // signed delta from base velocity
    quint32 humanize_seed = 0;    // seed used for determinism (0 means unset)

    QJsonObject toJsonObject() const;
    QString toJsonString(bool compact = true) const;
};

} // namespace virtuoso::theory

