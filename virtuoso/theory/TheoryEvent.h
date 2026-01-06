// NOTE: This header was unintentionally emptied at some point; it is required by VirtuosoCore.
#pragma once

#include <QtGlobal>
#include <QString>
#include <QJsonObject>

namespace virtuoso::theory {

// Explainable event stream emitted by Virtuoso engines (glass-box).
struct TheoryEvent {
    QString agent;
    QString timestamp;
    QString chord_context;
    QString scale_used;
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

    // Interaction / macro-dynamics (optional)
    QString vibe_state;      // e.g. "Simmer", "Build", "Climax", "CoolDown"
    QString user_intents;    // comma-separated flags (MVP): "DENSITY_HIGH,REGISTER_HIGH,..."
    double user_outside_ratio = 0.0;

    QJsonObject toJsonObject() const;
    QString toJsonString(bool compact = true) const;
};

} // namespace virtuoso::theory

