#include "playback/AutoWeightController.h"

#include <QtGlobal>

namespace playback {

static bool isSec(const QString& s, const QString& want) {
    return s.trimmed().compare(want, Qt::CaseInsensitive) == 0;
}

virtuoso::control::PerformanceWeightsV2 AutoWeightController::compute(const Inputs& in) {
    using virtuoso::control::PerformanceWeightsV2;
    PerformanceWeightsV2 w;

    const QString sec = in.sectionLabel.trimmed();
    const bool intro = isSec(sec, "Intro");
    const bool verse = isSec(sec, "Verse");
    const bool bridge = isSec(sec, "Bridge");
    const bool chorus = isSec(sec, "Chorus");
    const bool outro = isSec(sec, "Outro");

    const double phrasePos01 = (in.phraseBars > 1)
        ? qBound(0.0, double(in.barInPhrase) / double(in.phraseBars - 1), 1.0)
        : 0.0;
    const double cad = qBound(0.0, in.cadence01, 1.0);
    const bool lastRepeat = (in.repeatsTotal > 0) ? (in.repeatIndex >= (in.repeatsTotal - 1)) : false;

    // --- Base by section ---
    // These are intentionally conservative defaults (ballad-friendly).
    if (intro) {
        w.density = 0.18;
        w.rhythm = 0.22;
        w.intensity = 0.30;
        w.dynamism = 0.35;
        w.emotion = 0.55;
        w.creativity = 0.18;
        w.variability = 0.20;
        w.interactivity = 0.45;
        w.tension = 0.25;
        w.warmth = 0.70;
    } else if (bridge) {
        w.density = 0.32;
        w.rhythm = 0.45;
        w.intensity = 0.45;
        w.dynamism = 0.55;
        w.emotion = 0.50;
        w.creativity = 0.45;
        w.variability = 0.45;
        w.interactivity = 0.55;
        w.tension = 0.60;
        w.warmth = 0.60;
    } else if (chorus) {
        w.density = 0.45;
        w.rhythm = 0.40;
        w.intensity = 0.55;
        w.dynamism = 0.70;
        w.emotion = 0.35;
        w.creativity = 0.30;
        w.variability = 0.35;
        w.interactivity = 0.55;
        w.tension = 0.55;
        w.warmth = 0.55;
    } else if (outro) {
        w.density = 0.22;
        w.rhythm = 0.20;
        w.intensity = 0.28;
        w.dynamism = 0.40;
        w.emotion = 0.60;
        w.creativity = 0.18;
        w.variability = 0.15;
        w.interactivity = 0.35;
        w.tension = 0.40;
        w.warmth = 0.75;
    } else if (verse || !sec.isEmpty()) {
        // Default \"supportive\" section.
        w.density = 0.28;
        w.rhythm = 0.30;
        w.intensity = 0.40;
        w.dynamism = 0.50;
        w.emotion = 0.45;
        w.creativity = 0.22;
        w.variability = 0.28;
        w.interactivity = 0.60;
        w.tension = 0.40;
        w.warmth = 0.65;
    }

    // --- Phrase shaping ---
    // Start: more space. End: more tension/release + dynamics.
    w.density = qBound(0.0, w.density + (phrasePos01 - 0.5) * 0.14, 1.0);
    w.dynamism = qBound(0.0, w.dynamism + 0.20 * cad, 1.0);
    w.tension = qBound(0.0, w.tension + 0.25 * cad, 1.0);

    // --- User interaction overrides ---
    if (in.userSilence) {
        // Fill a bit more, but keep intensity controlled.
        w.density = qMin(1.0, w.density + 0.12);
        w.creativity = qMin(1.0, w.creativity + 0.08);
        w.intensity = qMax(0.0, w.intensity - 0.08);
    }
    if (in.userBusy) {
        // Make space. Increase \"interactivity\" (but expressed as restraint), reduce density/rhythm.
        w.density = qMax(0.0, w.density - 0.18);
        w.rhythm = qMax(0.0, w.rhythm - 0.15);
        w.interactivity = qMin(1.0, w.interactivity + 0.10);
    }
    if (in.userIntensityPeak) {
        w.intensity = qMin(1.0, w.intensity + 0.08);
        w.density = qMax(0.0, w.density - 0.06);
    }

    // --- Repeat logic ---
    if (!lastRepeat) {
        // Earlier repeats: allow a bit more variability/creativity to avoid monotony.
        w.variability = qMin(1.0, w.variability + 0.08 * qBound(0.0, double(in.repeatIndex) / 3.0, 1.0));
        w.creativity = qMin(1.0, w.creativity + 0.06 * qBound(0.0, double(in.repeatIndex) / 3.0, 1.0));
    } else {
        // Last repeat: reduce novelty, increase cadence clarity.
        w.variability = qMax(0.0, w.variability - 0.12);
        w.tension = qMin(1.0, w.tension + 0.10);
    }

    w.clamp01();
    return w;
}

} // namespace playback

