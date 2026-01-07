// NOTE: This header was unintentionally emptied at some point; it is required by VirtuosoCore.
#pragma once

#include <QtGlobal>
#include <QString>
#include <QJsonObject>

#include "virtuoso/control/VirtuosityMatrix.h"

namespace virtuoso::theory {

// Explainable event stream emitted by Virtuoso engines (glass-box).
struct TheoryEvent {
    // Optional event kind for non-note actions (e.g. "cc").
    // When empty, consumers may assume "note" semantics.
    QString event_kind;

    QString agent;
    QString timestamp;
    QString chord_context;
    QString scale_used;
    QString key_center;
    QString roman;
    QString chord_function;
    QString voicing_type;
    QString logic_tag;
    QString target_note;
    QString dynamic_marking;

    // Groove explainability fields (optional)
    QString groove_template;
    QString grid_pos;
    qint64 timing_offset_ms = 0;
    int velocity_adjustment = 0;
    quint32 humanize_seed = 0;

    // Event detail (optional): allows UI to build live timelines
    int channel = 0;     // 1..16
    int note = -1;       // 0..127
    int cc = -1;         // 0..127 (when event_kind == "cc")
    int cc_value = -1;   // 0..127 (when event_kind == "cc")
    qint64 on_ms = 0;    // engine clock domain
    qint64 off_ms = 0;   // engine clock domain
    int tempo_bpm = 0;
    int ts_num = 0;
    int ts_den = 0;
    qint64 engine_now_ms = 0; // when this TheoryEvent was emitted (engine clock domain), for live lookahead UIs

    // Interaction / macro-dynamics (optional)
    QString vibe_state;      // e.g. "Simmer", "Build", "Climax", "CoolDown"
    QString user_intents;    // comma-separated flags (MVP): "DENSITY_HIGH,REGISTER_HIGH,..."
    double user_outside_ratio = 0.0;

    // Virtuosity Matrix snapshot (optional; when present, emitted as a nested JSON object).
    bool has_virtuosity = false;
    virtuoso::control::VirtuosityMatrix virtuosity{};

    QJsonObject toJsonObject() const;
    QString toJsonString(bool compact = true) const;
};

} // namespace virtuoso::theory

