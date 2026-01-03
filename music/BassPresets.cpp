#include "music/BassPresets.h"

namespace music {
namespace {

static BassProfile baseEnabledFalse(QString name) {
    BassProfile p = defaultBassProfile();
    p.name = std::move(name);
    // Presets should not force-enable; keep song toggle authoritative.
    p.enabled = false;
    return p;
}

} // namespace

QVector<BassPreset> BassPresets::all() {
    QVector<BassPreset> out;
    out.reserve(8);

    // A sane modern “pro walking” baseline.
    {
        BassProfile p = baseEnabledFalse("Modern Walking (Default)");
        p.swingAmount = 0.55;
        p.swingRatio = 2.2;
        p.laidBackMs = 4;
        p.microJitterMs = 3;
        p.baseVelocity = 86;
        p.velocityVariance = 10;
        p.chromaticism = 0.55;
        p.ghostNoteProb = 0.18;
        p.pickup8thProb = 0.20;
        p.enclosureProb = 0.20;
        p.twoBeatRunProb = 0.18;
        p.fillProbPhraseEnd = 0.28;
        p.syncopationProb = 0.06;
        p.intensityBase = 0.55;
        p.intensityVariance = 0.30;
        p.evolutionRate = 0.18;
        p.sectionRampStrength = 0.25;
        p.phraseLengthBars = 4;
        out.push_back({"default_modern", p.name, p});
    }

    // Ray Brown-ish: very time-forward, authoritative quarter note, fewer busy fills, strong beat 1.
    {
        BassProfile p = baseEnabledFalse("Ray Brown (Straight-Ahead)");
        p.swingAmount = 0.50;
        p.swingRatio = 2.0;
        p.laidBackMs = 2;
        p.microJitterMs = 2;
        p.baseVelocity = 92;
        p.velocityVariance = 8;
        p.accentBeat1 = 1.10;
        p.accentBeat3 = 0.95;
        p.chromaticism = 0.45;
        p.ghostNoteProb = 0.10;
        p.pickup8thProb = 0.12;
        p.enclosureProb = 0.10;
        p.twoBeatRunProb = 0.08;
        p.fillProbPhraseEnd = 0.16;
        p.syncopationProb = 0.03;
        p.intensityBase = 0.50;
        p.intensityVariance = 0.20;
        p.evolutionRate = 0.14;
        p.sectionRampStrength = 0.18;
        out.push_back({"ray_brown", p.name, p});
    }

    // Paul Chambers-ish: more chromatic approaches and enclosures, more pickups, still tight time.
    {
        BassProfile p = baseEnabledFalse("Paul Chambers (Bop)");
        p.swingAmount = 0.60;
        p.swingRatio = 2.4;
        p.laidBackMs = 3;
        p.microJitterMs = 3;
        p.baseVelocity = 84;
        p.velocityVariance = 12;
        p.chromaticism = 0.70;
        p.ghostNoteProb = 0.16;
        p.pickup8thProb = 0.26;
        p.enclosureProb = 0.28;
        p.twoBeatRunProb = 0.22;
        p.fillProbPhraseEnd = 0.34;
        p.syncopationProb = 0.07;
        p.intensityBase = 0.62;
        p.intensityVariance = 0.35;
        p.evolutionRate = 0.22;
        p.sectionRampStrength = 0.30;
        out.push_back({"paul_chambers", p.name, p});
    }

    // Ron Carter-ish: more space and shape, less constant busyness, more guide-tone focus.
    {
        BassProfile p = baseEnabledFalse("Ron Carter (Modern, Spacious)");
        p.swingAmount = 0.45;
        p.swingRatio = 2.1;
        p.laidBackMs = 6;
        p.microJitterMs = 2;
        p.baseVelocity = 80;
        p.velocityVariance = 10;
        p.chromaticism = 0.45;
        p.ghostNoteProb = 0.12;
        p.pickup8thProb = 0.10;
        p.enclosureProb = 0.14;
        p.twoBeatRunProb = 0.08;
        p.fillProbPhraseEnd = 0.18;
        p.syncopationProb = 0.05;
        p.intensityBase = 0.45;
        p.intensityVariance = 0.25;
        p.evolutionRate = 0.16;
        p.sectionRampStrength = 0.22;
        p.sectionIntroRestraint = 0.70;
        // favor guide-tone motion
        p.wRoot = 0.85;
        p.wThird = 0.95;
        p.wSeventh = 1.05;
        out.push_back({"ron_carter", p.name, p});
    }

    // Jamerson-ish (Motown): not truly “walking”, but emulate busier syncopation + ghosted articulation.
    {
        BassProfile p = baseEnabledFalse("James Jamerson (Motown-ish)");
        p.swingAmount = 0.10;
        p.swingRatio = 2.0;
        p.laidBackMs = 0;
        p.pushMs = 2;
        p.microJitterMs = 3;
        p.baseVelocity = 94;
        p.velocityVariance = 14;
        p.chromaticism = 0.55;
        p.ghostNoteProb = 0.26;
        p.ghostVelocity = 22;
        p.pickup8thProb = 0.32;
        p.enclosureProb = 0.18;
        p.twoBeatRunProb = 0.10;
        p.fillProbPhraseEnd = 0.30;
        p.syncopationProb = 0.16;
        p.gatePct = 0.75;
        p.intensityBase = 0.70;
        p.intensityVariance = 0.30;
        p.evolutionRate = 0.24;
        p.sectionRampStrength = 0.18;
        p.phraseLengthBars = 2;
        out.push_back({"jamerson", p.name, p});
    }

    return out;
}

bool BassPresets::getById(const QString& id, BassPreset& out) {
    for (const auto& p : all()) {
        if (p.id == id) { out = p; return true; }
    }
    return false;
}

bool BassPresets::getByName(const QString& name, BassPreset& out) {
    const QString needle = name.trimmed();
    if (needle.isEmpty()) return false;
    for (const auto& p : all()) {
        if (p.name.compare(needle, Qt::CaseInsensitive) == 0) { out = p; return true; }
    }
    return false;
}

} // namespace music

