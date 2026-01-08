#include "playback/WeightNegotiator.h"

#include <QtGlobal>
#include <QJsonObject>

namespace playback {

static double c01(double x) { return qBound(0.0, x, 1.0); }
static double lerp(double a, double b, double t) { return a + (b - a) * c01(t); }

static virtuoso::control::VirtuosityMatrix mapToVirt(const virtuoso::control::PerformanceWeightsV2& w) {
    virtuoso::control::VirtuosityMatrix v;
    v.harmonicRisk = c01(0.15 + 0.85 * w.creativity);
    v.rhythmicComplexity = c01(0.15 + 0.85 * w.rhythm * (0.70 + 0.60 * w.density));
    v.interaction = c01(0.10 + 0.90 * w.interactivity);
    v.toneDark = c01(w.warmth);
    return v;
}

QJsonObject WeightNegotiator::Output::toJson() const {
    QJsonObject o;
    o.insert("global", global.toJson());
    auto pack = [&](const char* k, const AgentWeights& aw) {
        QJsonObject a;
        a.insert("weights", aw.w.toJson());
        a.insert("virt", aw.virt.toJsonObject());
        o.insert(k, a);
    };
    pack("piano", piano);
    pack("bass", bass);
    pack("drums", drums);
    return o;
}

static void normalize3(double& a, double& b, double& c) {
    const double s = qMax(1e-6, a + b + c);
    a /= s; b /= s; c /= s;
}

WeightNegotiator::Output WeightNegotiator::negotiate(const Inputs& in, State& state, double alpha) {
    Output out;
    out.global = in.global;
    out.global.clamp01();

    // Fully free allocation, but with weak priors so it is not chaotic.
    // We compute per-axis \"shares\" and then map to per-agent weights.
    auto alloc = [&](double piano, double bass, double drums) {
        normalize3(piano, bass, drums);
        return std::array<double, 3>{piano, bass, drums};
    };

    const QString sec = in.sectionLabel.trimmed();
    const bool bridge = (sec.compare("Bridge", Qt::CaseInsensitive) == 0);
    const bool chorus = (sec.compare("Chorus", Qt::CaseInsensitive) == 0);
    const bool intro = (sec.compare("Intro", Qt::CaseInsensitive) == 0);
    const bool outro = (sec.compare("Outro", Qt::CaseInsensitive) == 0);

    // Density: who \"fills\".
    auto aDensity = alloc(
        in.userSilence ? 0.55 : 0.40,
        in.userSilence ? 0.30 : 0.30,
        in.userBusy ? 0.55 : 0.30
    );
    // Rhythm: drums lead.
    auto aRhythm = alloc(0.25, 0.20, 0.55);
    // Intensity: drums+piano.
    auto aIntensity = alloc(0.40, 0.15, 0.45);
    // Dynamism: piano+piano phrasing + drums gestures.
    auto aDyn = alloc(0.45, 0.15, 0.40);
    // Emotion (time feel): piano leads.
    auto aEmo = alloc(0.60, 0.15, 0.25);
    // Creativity: piano leads; bridge increases bass share a bit.
    auto aCreat = alloc(0.65, bridge ? 0.25 : 0.15, 0.20);
    // Tension: piano+drums; chorus/outro shifts to drums for clear setups.
    auto aTension = alloc(outro ? 0.45 : 0.55, 0.15, chorus || outro ? 0.40 : 0.30);
    // Interactivity: respond with space; if user busy, drums/piano should back off (but still \"respond\").
    auto aInteract = alloc(in.userBusy ? 0.35 : 0.45, 0.20, in.userBusy ? 0.45 : 0.35);
    // Variability: piano mostly, but chorus reduces piano variability to stay grounded.
    auto aVar = alloc(chorus ? 0.45 : 0.60, 0.20, 0.20);
    // Warmth: piano leads.
    auto aWarm = alloc(0.55, 0.20, 0.25);

    auto buildAgent = [&](int idx) -> AgentWeights {
        AgentWeights aw;
        aw.w = out.global;
        // Apply axis shares (agent expresses only its portion of the ensemble intent).
        aw.w.density *= (idx == 0 ? aDensity[0] : (idx == 1 ? aDensity[1] : aDensity[2])) * 3.0;
        aw.w.rhythm *= (idx == 0 ? aRhythm[0] : (idx == 1 ? aRhythm[1] : aRhythm[2])) * 3.0;
        aw.w.intensity *= (idx == 0 ? aIntensity[0] : (idx == 1 ? aIntensity[1] : aIntensity[2])) * 3.0;
        aw.w.dynamism *= (idx == 0 ? aDyn[0] : (idx == 1 ? aDyn[1] : aDyn[2])) * 3.0;
        aw.w.emotion *= (idx == 0 ? aEmo[0] : (idx == 1 ? aEmo[1] : aEmo[2])) * 3.0;
        aw.w.creativity *= (idx == 0 ? aCreat[0] : (idx == 1 ? aCreat[1] : aCreat[2])) * 3.0;
        aw.w.tension *= (idx == 0 ? aTension[0] : (idx == 1 ? aTension[1] : aTension[2])) * 3.0;
        aw.w.interactivity *= (idx == 0 ? aInteract[0] : (idx == 1 ? aInteract[1] : aInteract[2])) * 3.0;
        aw.w.variability *= (idx == 0 ? aVar[0] : (idx == 1 ? aVar[1] : aVar[2])) * 3.0;
        aw.w.warmth *= (idx == 0 ? aWarm[0] : (idx == 1 ? aWarm[1] : aWarm[2])) * 3.0;
        aw.w.clamp01();
        aw.virt = mapToVirt(aw.w);
        return aw;
    };

    AgentWeights piano = buildAgent(0);
    AgentWeights bass = buildAgent(1);
    AgentWeights drums = buildAgent(2);

    // Smooth mapped virt weights so allocations don't thrash (EMA).
    auto smoothVirt = [&](virtuoso::control::VirtuosityMatrix& cur,
                          const virtuoso::control::VirtuosityMatrix& tgt) {
        cur.harmonicRisk = lerp(cur.harmonicRisk, tgt.harmonicRisk, alpha);
        cur.rhythmicComplexity = lerp(cur.rhythmicComplexity, tgt.rhythmicComplexity, alpha);
        cur.interaction = lerp(cur.interaction, tgt.interaction, alpha);
        cur.toneDark = lerp(cur.toneDark, tgt.toneDark, alpha);
    };

    if (!state.initialized) {
        state.piano = piano;
        state.bass = bass;
        state.drums = drums;
        state.initialized = true;
    } else {
        smoothVirt(state.piano.virt, piano.virt);
        smoothVirt(state.bass.virt, bass.virt);
        smoothVirt(state.drums.virt, drums.virt);
        state.piano.w = piano.w;
        state.bass.w = bass.w;
        state.drums.w = drums.w;
    }

    out.piano = state.piano;
    out.bass = state.bass;
    out.drums = state.drums;
    return out;
}

} // namespace playback

