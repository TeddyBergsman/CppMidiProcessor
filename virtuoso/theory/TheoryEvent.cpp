#include "virtuoso/theory/TheoryEvent.h"

#include <QJsonDocument>

namespace virtuoso::theory {

QJsonObject TheoryEvent::toJsonObject() const {
    QJsonObject o;
    o.insert("agent", agent);
    o.insert("timestamp", timestamp);
    o.insert("chord_context", chord_context);
    o.insert("scale_used", scale_used);
    if (!key_center.trimmed().isEmpty()) o.insert("key_center", key_center);
    if (!roman.trimmed().isEmpty()) o.insert("roman", roman);
    if (!chord_function.trimmed().isEmpty()) o.insert("chord_function", chord_function);
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

    // Optional event detail fields
    if (channel > 0) o.insert("channel", channel);
    if (note >= 0) o.insert("note", note);
    if (on_ms > 0) o.insert("on_ms", on_ms);
    if (off_ms > 0) o.insert("off_ms", off_ms);
    if (tempo_bpm > 0) o.insert("tempo_bpm", tempo_bpm);
    if (ts_num > 0) o.insert("ts_num", ts_num);
    if (ts_den > 0) o.insert("ts_den", ts_den);
    if (engine_now_ms > 0) o.insert("engine_now_ms", engine_now_ms);

    // Optional interaction fields
    if (!vibe_state.trimmed().isEmpty()) o.insert("vibe_state", vibe_state);
    if (!user_intents.trimmed().isEmpty()) o.insert("user_intents", user_intents);
    if (user_outside_ratio > 0.0) o.insert("user_outside_ratio", user_outside_ratio);
    return o;
}

QString TheoryEvent::toJsonString(bool compact) const {
    const QJsonDocument doc(toJsonObject());
    return QString::fromUtf8(doc.toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented));
}

} // namespace virtuoso::theory

