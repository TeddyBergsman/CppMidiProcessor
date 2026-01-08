#include "virtuoso/control/PerformanceWeightsV2.h"

#include <QtGlobal>

namespace virtuoso::control {

static double c01(double x) { return qBound(0.0, x, 1.0); }

void PerformanceWeightsV2::clamp01() {
    density = c01(density);
    rhythm = c01(rhythm);
    emotion = c01(emotion);
    intensity = c01(intensity);
    dynamism = c01(dynamism);
    creativity = c01(creativity);
    tension = c01(tension);
    interactivity = c01(interactivity);
    variability = c01(variability);
    warmth = c01(warmth);
}

QJsonObject PerformanceWeightsV2::toJson() const {
    QJsonObject o;
    o.insert("density", density);
    o.insert("rhythm", rhythm);
    o.insert("emotion", emotion);
    o.insert("intensity", intensity);
    o.insert("dynamism", dynamism);
    o.insert("creativity", creativity);
    o.insert("tension", tension);
    o.insert("interactivity", interactivity);
    o.insert("variability", variability);
    o.insert("warmth", warmth);
    return o;
}

PerformanceWeightsV2 PerformanceWeightsV2::fromJson(const QJsonObject& o) {
    PerformanceWeightsV2 w;
    w.density = o.value("density").toDouble(w.density);
    w.rhythm = o.value("rhythm").toDouble(w.rhythm);
    w.emotion = o.value("emotion").toDouble(w.emotion);
    w.intensity = o.value("intensity").toDouble(w.intensity);
    w.dynamism = o.value("dynamism").toDouble(w.dynamism);
    w.creativity = o.value("creativity").toDouble(w.creativity);
    w.tension = o.value("tension").toDouble(w.tension);
    w.interactivity = o.value("interactivity").toDouble(w.interactivity);
    w.variability = o.value("variability").toDouble(w.variability);
    w.warmth = o.value("warmth").toDouble(w.warmth);
    w.clamp01();
    return w;
}

} // namespace virtuoso::control

