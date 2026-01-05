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
    return o;
}

QString TheoryEvent::toJsonString(bool compact) const {
    const QJsonDocument doc(toJsonObject());
    return QString::fromUtf8(doc.toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented));
}

} // namespace virtuoso::theory

