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

    // Classic ballad default: sparse, consonant, beautiful.
    {
        PianoProfile p = baseEnabledTrue("Classic Ballad (Default)");
        p.feelStyle = PianoFeelStyle::Ballad;
        p.lhMinMidiNote = 36; p.lhMaxMidiNote = 72;
        p.rhMinMidiNote = 60; p.rhMaxMidiNote = 100;
        p.fillMinMidiNote = 64; p.fillMaxMidiNote = 108;
        p.compDensity = 0.46;
        p.anticipationProb = 0.0;
        p.syncopationProb = 0.0;
        p.restProb = 0.22;
        p.baseVelocity = 60;
        p.velocityVariance = 10;
        p.accentDownbeat = 1.22;
        p.accentBackbeat = 0.92;
        p.laidBackMs = 9;
        p.microJitterMs = 2;
        p.driftMaxMs = 8;
        p.driftRate = 0.10;
        p.rootlessProb = 1.00;
        p.tensionProb = 0.35;
        p.quartalProb = 0.0;
        p.clusterProb = 0.0;
        p.drop2Prob = 0.0;
        p.voiceLeadingStrength = 0.92;
        p.repetitionPenalty = 0.18;
        p.maxHandLeap = 7;
        p.fillProbPhraseEnd = 0.0;
        p.fillProbAnyBeat = 0.0;
        p.fillMaxNotes = 0;
        p.pedalEnabled = true;
        p.pedalReleaseOnChordChange = true;
        p.pedalMinHoldMs = 420;
        p.pedalMaxHoldMs = 1400;
        p.pedalChangeProb = 0.95;
        out.push_back({"classic_ballad_default", p.name, p});
    }

    // Bill Evans (Ballad): slightly more color, still gentle (but no licks by default).
    {
        PianoProfile p = baseEnabledTrue("Bill Evans (Ballad)");
        p.feelStyle = PianoFeelStyle::Ballad;
        p.lhMinMidiNote = 38; p.lhMaxMidiNote = 74;
        p.rhMinMidiNote = 62; p.rhMaxMidiNote = 102;
        p.fillMinMidiNote = 66; p.fillMaxMidiNote = 108;
        p.compDensity = 0.40;
        p.anticipationProb = 0.0;
        p.syncopationProb = 0.0;
        p.restProb = 0.26;
        p.baseVelocity = 58;
        p.velocityVariance = 10;
        p.laidBackMs = 10;
        p.microJitterMs = 3;
        p.driftMaxMs = 10;
        p.driftRate = 0.12;
        p.rootlessProb = 1.00;
        p.tensionProb = 0.45;
        p.quartalProb = 0.0;
        p.clusterProb = 0.0;
        p.drop2Prob = 0.10;
        p.voiceLeadingStrength = 0.85;
        p.repetitionPenalty = 0.35;
        p.fillProbPhraseEnd = 0.0;
        p.fillProbAnyBeat = 0.0;
        p.fillMaxNotes = 0;
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
        p.lhMinMidiNote = 36; p.lhMaxMidiNote = 70;
        p.rhMinMidiNote = 58; p.rhMaxMidiNote = 98;
        p.fillMinMidiNote = 62; p.fillMaxMidiNote = 104;
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
        p.lhMinMidiNote = 36; p.lhMaxMidiNote = 72;
        p.rhMinMidiNote = 60; p.rhMaxMidiNote = 104;
        p.fillMinMidiNote = 64; p.fillMaxMidiNote = 110;
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

