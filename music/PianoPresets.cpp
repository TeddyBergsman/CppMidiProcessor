#include "music/PianoPresets.h"

namespace music {
namespace {

static PianoProfile baseEnabledTrue(QString name) {
    PianoProfile p = defaultPianoProfile();
    p.name = std::move(name);
    // Piano is intended to be on by default (world-class pianist as default).
    p.enabled = true;
    return p;
}

} // namespace

QVector<PianoPreset> PianoPresets::all() {
    QVector<PianoPreset> out;
    out.reserve(8);

    // Bill Evans default: warm, rootless, voice-led, light fills.
    {
        PianoProfile p = baseEnabledTrue("Bill Evans (Default)");
        p.feelStyle = PianoFeelStyle::Swing;
        p.compDensity = 0.55;
        p.anticipationProb = 0.14;
        p.syncopationProb = 0.18;
        p.restProb = 0.12;
        p.baseVelocity = 62;
        p.velocityVariance = 14;
        p.laidBackMs = 8;
        p.microJitterMs = 4;
        p.driftMaxMs = 14;
        p.driftRate = 0.18;
        p.rootlessProb = 0.80;
        p.tensionProb = 0.75;
        p.quartalProb = 0.18;
        p.clusterProb = 0.10;
        p.drop2Prob = 0.35;
        p.voiceLeadingStrength = 0.75;
        p.repetitionPenalty = 0.45;
        p.fillProbPhraseEnd = 0.22;
        p.fillProbAnyBeat = 0.06;
        p.pedalEnabled = true;
        p.pedalReleaseOnChordChange = true;
        p.pedalChangeProb = 0.80;
        out.push_back({"bill_evans_default", p.name, p});
    }

    // Bill Evans ballad: sparser, more sustained, less syncopation, more pedal.
    {
        PianoProfile p = baseEnabledTrue("Bill Evans (Ballad)");
        p.feelStyle = PianoFeelStyle::Ballad;
        p.compDensity = 0.34;
        p.anticipationProb = 0.08;
        p.syncopationProb = 0.10;
        p.restProb = 0.22;
        p.baseVelocity = 54;
        p.velocityVariance = 10;
        p.laidBackMs = 10;
        p.microJitterMs = 3;
        p.driftMaxMs = 10;
        p.driftRate = 0.12;
        p.rootlessProb = 0.75;
        p.tensionProb = 0.70;
        p.quartalProb = 0.12;
        p.clusterProb = 0.06;
        p.drop2Prob = 0.30;
        p.voiceLeadingStrength = 0.85;
        p.repetitionPenalty = 0.35;
        p.fillProbPhraseEnd = 0.12;
        p.fillProbAnyBeat = 0.03;
        p.pedalEnabled = true;
        p.pedalReleaseOnChordChange = true;
        p.pedalMinHoldMs = 380;
        p.pedalMaxHoldMs = 1200;
        p.pedalChangeProb = 0.90;
        out.push_back({"bill_evans_ballad", p.name, p});
    }

    // Sparse guide tones: shell voicings, minimal color, very clear harmony.
    {
        PianoProfile p = baseEnabledTrue("Sparse Guide Tones");
        p.feelStyle = PianoFeelStyle::Swing;
        p.compDensity = 0.30;
        p.anticipationProb = 0.08;
        p.syncopationProb = 0.10;
        p.restProb = 0.30;
        p.baseVelocity = 56;
        p.velocityVariance = 10;
        p.preferRootless = true;
        p.rootlessProb = 0.95;
        p.tensionProb = 0.20;
        p.quartalProb = 0.05;
        p.clusterProb = 0.02;
        p.drop2Prob = 0.10;
        p.avoidRootProb = 0.85;
        p.avoidThirdProb = 0.04;
        p.maxHandLeap = 8;
        p.voiceLeadingStrength = 0.92;
        p.repetitionPenalty = 0.55;
        p.fillProbPhraseEnd = 0.06;
        p.fillProbAnyBeat = 0.01;
        p.pedalEnabled = false; // shells are clearer without pedal
        out.push_back({"sparse_guide_tones", p.name, p});
    }

    // Quartal modern: more 4ths and open structures; still voice-led.
    {
        PianoProfile p = baseEnabledTrue("Quartal Modern");
        p.feelStyle = PianoFeelStyle::Swing;
        p.compDensity = 0.60;
        p.anticipationProb = 0.18;
        p.syncopationProb = 0.24;
        p.restProb = 0.10;
        p.baseVelocity = 64;
        p.velocityVariance = 16;
        p.rootlessProb = 0.75;
        p.tensionProb = 0.85;
        p.quartalProb = 0.45;
        p.clusterProb = 0.12;
        p.drop2Prob = 0.20;
        p.voiceLeadingStrength = 0.70;
        p.repetitionPenalty = 0.35;
        p.fillProbPhraseEnd = 0.26;
        p.fillProbAnyBeat = 0.08;
        p.pedalEnabled = true;
        p.pedalReleaseOnChordChange = true;
        p.pedalChangeProb = 0.75;
        out.push_back({"quartal_modern", p.name, p});
    }

    return out;
}

bool PianoPresets::getById(const QString& id, PianoPreset& out) {
    for (const auto& p : all()) {
        if (p.id == id) { out = p; return true; }
    }
    return false;
}

bool PianoPresets::getByName(const QString& name, PianoPreset& out) {
    const QString needle = name.trimmed();
    if (needle.isEmpty()) return false;
    for (const auto& p : all()) {
        if (p.name.compare(needle, Qt::CaseInsensitive) == 0) { out = p; return true; }
    }
    return false;
}

} // namespace music

