#include "virtuoso/theory/TheoryEvent.h"

#include <QJsonDocument>

namespace virtuoso::theory {

QJsonObject TheoryEvent::toJsonObject() const {
    QJsonObject o;
    o.insert("agent", agent);
    o.insert("timestamp", timestamp);
    o.insert("chord_context", chord_context);
    o.insert("scale_used", scale_used);
    o.insert("voicing_type", voicing_type);
    o.insert("logic_tag", logic_tag);
    o.insert("target_note", target_note);
    o.insert("dynamic_marking", dynamic_marking);

    // Optional groove explainability fields (only emitted when present/non-default).
    if (!groove_template.trimmed().isEmpty()) o.insert("groove_template", groove_template);
    if (!grid_pos.trimmed().isEmpty()) o.insert("grid_pos", grid_pos);
    if (timing_offset_ms != 0) o.insert("timing_offset_ms", timing_offset_ms);
    if (velocity_adjustment != 0) o.insert("velocity_adjustment", velocity_adjustment);
    if (humanize_seed != 0u) o.insert("humanize_seed", int(humanize_seed));
    return o;
}

QString TheoryEvent::toJsonString(bool compact) const {
    const QJsonDocument doc(toJsonObject());
    return QString::fromUtf8(doc.toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented));
}

} // namespace virtuoso::theory

