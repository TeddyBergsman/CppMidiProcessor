#include "music/BassProfile.h"

#include <QSettings>
#include <QtGlobal>

namespace music {
namespace {

static int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }
static double clampD(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }

static int readInt(QSettings& s, const QString& k, int def) { return s.value(k, def).toInt(); }
static double readD(QSettings& s, const QString& k, double def) { return s.value(k, def).toDouble(); }
static bool readB(QSettings& s, const QString& k, bool def) { return s.value(k, def).toBool(); }
static QString readS(QSettings& s, const QString& k, const QString& def) { return s.value(k, def).toString(); }
static quint32 readU32(QSettings& s, const QString& k, quint32 def) { return (quint32)s.value(k, (qulonglong)def).toULongLong(); }

} // namespace

BassProfile defaultBassProfile() {
    return BassProfile{};
}

BassProfile loadBassProfile(QSettings& settings, const QString& prefix) {
    BassProfile p = defaultBassProfile();
    const QString base = prefix;

    p.version = readInt(settings, base + "/version", p.version);
    p.name = readS(settings, base + "/name", p.name);

    p.enabled = readB(settings, base + "/enabled", p.enabled);
    p.midiChannel = clampInt(readInt(settings, base + "/midiChannel", p.midiChannel), 1, 16);

    p.minMidiNote = clampInt(readInt(settings, base + "/minMidiNote", p.minMidiNote), 0, 127);
    p.maxMidiNote = clampInt(readInt(settings, base + "/maxMidiNote", p.maxMidiNote), 0, 127);
    if (p.minMidiNote > p.maxMidiNote) std::swap(p.minMidiNote, p.maxMidiNote);

    p.registerCenterMidi = clampInt(readInt(settings, base + "/registerCenterMidi", p.registerCenterMidi), 0, 127);
    p.registerRange = clampInt(readInt(settings, base + "/registerRange", p.registerRange), 0, 60);
    p.maxLeap = clampInt(readInt(settings, base + "/maxLeap", p.maxLeap), 0, 24);
    // transposeSemitones deprecated (previously used for +12 output shift).
    // Bass output shift is now an internal constant in the playback engine so keyswitch/FX notes are never transposed.
    (void)readInt(settings, base + "/transposeSemitones", 12);

    p.honorSlashBass = readB(settings, base + "/honorSlashBass", p.honorSlashBass);
    p.slashBassProb = clampD(readD(settings, base + "/slashBassProb", p.slashBassProb), 0.0, 1.0);
    p.treatMaj6AsMaj7 = readB(settings, base + "/treatMaj6AsMaj7", p.treatMaj6AsMaj7);

    // VST articulations / FX
    p.ampleNoteNameOffsetSemitones = clampInt(readInt(settings, base + "/ampleNoteNameOffsetSemitones", p.ampleNoteNameOffsetSemitones), -24, 24);
    p.artSustainAccent = readB(settings, base + "/artSustainAccent", p.artSustainAccent);
    p.artNaturalHarmonic = readB(settings, base + "/artNaturalHarmonic", p.artNaturalHarmonic);
    p.artPalmMute = readB(settings, base + "/artPalmMute", p.artPalmMute);
    p.artSlideInOut = readB(settings, base + "/artSlideInOut", p.artSlideInOut);
    p.artLegatoSlide = readB(settings, base + "/artLegatoSlide", p.artLegatoSlide);
    p.artHammerPull = readB(settings, base + "/artHammerPull", p.artHammerPull);

    p.fxHitRimMute = readB(settings, base + "/fxHitRimMute", p.fxHitRimMute);
    p.fxHitTopPalmMute = readB(settings, base + "/fxHitTopPalmMute", p.fxHitTopPalmMute);
    p.fxHitTopFingerMute = readB(settings, base + "/fxHitTopFingerMute", p.fxHitTopFingerMute);
    p.fxHitTopOpen = readB(settings, base + "/fxHitTopOpen", p.fxHitTopOpen);
    p.fxHitRimOpen = readB(settings, base + "/fxHitRimOpen", p.fxHitRimOpen);
    p.fxScratch = readB(settings, base + "/fxScratch", p.fxScratch);
    p.fxBreath = readB(settings, base + "/fxBreath", p.fxBreath);
    p.fxSingleStringSlap = readB(settings, base + "/fxSingleStringSlap", p.fxSingleStringSlap);
    p.fxLeftHandSlapNoise = readB(settings, base + "/fxLeftHandSlapNoise", p.fxLeftHandSlapNoise);
    p.fxRightHandSlapNoise = readB(settings, base + "/fxRightHandSlapNoise", p.fxRightHandSlapNoise);
    p.fxSlideTurn4 = readB(settings, base + "/fxSlideTurn4", p.fxSlideTurn4);
    p.fxSlideTurn3 = readB(settings, base + "/fxSlideTurn3", p.fxSlideTurn3);
    p.fxSlideDown4 = readB(settings, base + "/fxSlideDown4", p.fxSlideDown4);
    p.fxSlideDown3 = readB(settings, base + "/fxSlideDown3", p.fxSlideDown3);

    p.swingAmount = clampD(readD(settings, base + "/swingAmount", p.swingAmount), 0.0, 1.0);
    p.swingRatio = clampD(readD(settings, base + "/swingRatio", p.swingRatio), 1.2, 4.0);
    p.microJitterMs = clampInt(readInt(settings, base + "/microJitterMs", p.microJitterMs), 0, 50);
    p.laidBackMs = clampInt(readInt(settings, base + "/laidBackMs", p.laidBackMs), -50, 50);
    p.pushMs = clampInt(readInt(settings, base + "/pushMs", p.pushMs), -50, 50);
    p.driftMaxMs = clampInt(readInt(settings, base + "/driftMaxMs", p.driftMaxMs), 0, 80);
    p.driftRate = clampD(readD(settings, base + "/driftRate", p.driftRate), 0.0, 1.0);
    p.attackVarianceMs = clampInt(readInt(settings, base + "/attackVarianceMs", p.attackVarianceMs), 0, 40);
    p.noteLengthMs = clampInt(readInt(settings, base + "/noteLengthMs", p.noteLengthMs), 0, 2000);
    p.gatePct = clampD(readD(settings, base + "/gatePct", p.gatePct), 0.05, 1.0);
    p.humanizeSeed = readU32(settings, base + "/humanizeSeed", p.humanizeSeed);
    if (p.humanizeSeed == 0) p.humanizeSeed = 1;

    p.baseVelocity = clampInt(readInt(settings, base + "/baseVelocity", p.baseVelocity), 1, 127);
    p.velocityVariance = clampInt(readInt(settings, base + "/velocityVariance", p.velocityVariance), 0, 64);
    p.accentBeat1 = clampD(readD(settings, base + "/accentBeat1", p.accentBeat1), 0.1, 2.0);
    p.accentBeat2 = clampD(readD(settings, base + "/accentBeat2", p.accentBeat2), 0.1, 2.0);
    p.accentBeat3 = clampD(readD(settings, base + "/accentBeat3", p.accentBeat3), 0.1, 2.0);
    p.accentBeat4 = clampD(readD(settings, base + "/accentBeat4", p.accentBeat4), 0.1, 2.0);
    p.phraseContourStrength = clampD(readD(settings, base + "/phraseContourStrength", p.phraseContourStrength), 0.0, 1.0);
    p.phraseArcStrength = clampD(readD(settings, base + "/phraseArcStrength", p.phraseArcStrength), 0.0, 1.0);
    p.sectionArcStrength = clampD(readD(settings, base + "/sectionArcStrength", p.sectionArcStrength), 0.0, 1.0);

    p.chromaticism = clampD(readD(settings, base + "/chromaticism", p.chromaticism), 0.0, 1.0);
    p.leapPenalty = clampD(readD(settings, base + "/leapPenalty", p.leapPenalty), 0.0, 1.0);
    p.repetitionPenalty = clampD(readD(settings, base + "/repetitionPenalty", p.repetitionPenalty), 0.0, 1.0);

    p.intensityBase = clampD(readD(settings, base + "/intensityBase", p.intensityBase), 0.0, 1.0);
    p.intensityVariance = clampD(readD(settings, base + "/intensityVariance", p.intensityVariance), 0.0, 1.0);
    p.evolutionRate = clampD(readD(settings, base + "/evolutionRate", p.evolutionRate), 0.0, 1.0);
    p.sectionRampStrength = clampD(readD(settings, base + "/sectionRampStrength", p.sectionRampStrength), 0.0, 1.0);
    p.phraseLengthBars = clampInt(readInt(settings, base + "/phraseLengthBars", p.phraseLengthBars), 1, 16);
    p.twoFeelPhraseProb = clampD(readD(settings, base + "/twoFeelPhraseProb", p.twoFeelPhraseProb), 0.0, 1.0);
    p.brokenTimePhraseProb = clampD(readD(settings, base + "/brokenTimePhraseProb", p.brokenTimePhraseProb), 0.0, 1.0);
    p.restProb = clampD(readD(settings, base + "/restProb", p.restProb), 0.0, 1.0);
    p.tieProb = clampD(readD(settings, base + "/tieProb", p.tieProb), 0.0, 1.0);

    p.ghostNoteProb = clampD(readD(settings, base + "/ghostNoteProb", p.ghostNoteProb), 0.0, 1.0);
    p.ghostVelocity = clampInt(readInt(settings, base + "/ghostVelocity", p.ghostVelocity), 1, 60);
    p.ghostGatePct = clampD(readD(settings, base + "/ghostGatePct", p.ghostGatePct), 0.05, 0.8);
    p.pickup8thProb = clampD(readD(settings, base + "/pickup8thProb", p.pickup8thProb), 0.0, 1.0);
    p.fillProbPhraseEnd = clampD(readD(settings, base + "/fillProbPhraseEnd", p.fillProbPhraseEnd), 0.0, 1.0);
    p.syncopationProb = clampD(readD(settings, base + "/syncopationProb", p.syncopationProb), 0.0, 1.0);
    p.twoBeatRunProb = clampD(readD(settings, base + "/twoBeatRunProb", p.twoBeatRunProb), 0.0, 1.0);
    p.enclosureProb = clampD(readD(settings, base + "/enclosureProb", p.enclosureProb), 0.0, 1.0);
    p.sectionIntroRestraint = clampD(readD(settings, base + "/sectionIntroRestraint", p.sectionIntroRestraint), 0.0, 1.0);
    p.motifProb = clampD(readD(settings, base + "/motifProb", p.motifProb), 0.0, 1.0);
    p.motifStrength = clampD(readD(settings, base + "/motifStrength", p.motifStrength), 0.0, 1.0);
    p.motifVariation = clampD(readD(settings, base + "/motifVariation", p.motifVariation), 0.0, 1.0);

    p.wRoot = clampD(readD(settings, base + "/wRoot", p.wRoot), 0.0, 3.0);
    p.wThird = clampD(readD(settings, base + "/wThird", p.wThird), 0.0, 3.0);
    p.wFifth = clampD(readD(settings, base + "/wFifth", p.wFifth), 0.0, 3.0);
    p.wSeventh = clampD(readD(settings, base + "/wSeventh", p.wSeventh), 0.0, 3.0);

    p.wApproachChromatic = clampD(readD(settings, base + "/wApproachChromatic", p.wApproachChromatic), 0.0, 1.0);
    p.wApproachDiatonic = clampD(readD(settings, base + "/wApproachDiatonic", p.wApproachDiatonic), 0.0, 1.0);
    p.wApproachEnclosure = clampD(readD(settings, base + "/wApproachEnclosure", p.wApproachEnclosure), 0.0, 1.0);

    return p;
}

void saveBassProfile(QSettings& settings, const QString& prefix, const BassProfile& p) {
    const QString base = prefix;

    settings.setValue(base + "/version", p.version);
    settings.setValue(base + "/name", p.name);

    settings.setValue(base + "/enabled", p.enabled);
    settings.setValue(base + "/midiChannel", p.midiChannel);

    settings.setValue(base + "/minMidiNote", p.minMidiNote);
    settings.setValue(base + "/maxMidiNote", p.maxMidiNote);
    settings.setValue(base + "/registerCenterMidi", p.registerCenterMidi);
    settings.setValue(base + "/registerRange", p.registerRange);
    settings.setValue(base + "/maxLeap", p.maxLeap);
    // transposeSemitones deprecated; no longer saved.

    settings.setValue(base + "/honorSlashBass", p.honorSlashBass);
    settings.setValue(base + "/slashBassProb", p.slashBassProb);
    settings.setValue(base + "/treatMaj6AsMaj7", p.treatMaj6AsMaj7);

    // VST articulations / FX
    settings.setValue(base + "/ampleNoteNameOffsetSemitones", p.ampleNoteNameOffsetSemitones);
    settings.setValue(base + "/artSustainAccent", p.artSustainAccent);
    settings.setValue(base + "/artNaturalHarmonic", p.artNaturalHarmonic);
    settings.setValue(base + "/artPalmMute", p.artPalmMute);
    settings.setValue(base + "/artSlideInOut", p.artSlideInOut);
    settings.setValue(base + "/artLegatoSlide", p.artLegatoSlide);
    settings.setValue(base + "/artHammerPull", p.artHammerPull);

    settings.setValue(base + "/fxHitRimMute", p.fxHitRimMute);
    settings.setValue(base + "/fxHitTopPalmMute", p.fxHitTopPalmMute);
    settings.setValue(base + "/fxHitTopFingerMute", p.fxHitTopFingerMute);
    settings.setValue(base + "/fxHitTopOpen", p.fxHitTopOpen);
    settings.setValue(base + "/fxHitRimOpen", p.fxHitRimOpen);
    settings.setValue(base + "/fxScratch", p.fxScratch);
    settings.setValue(base + "/fxBreath", p.fxBreath);
    settings.setValue(base + "/fxSingleStringSlap", p.fxSingleStringSlap);
    settings.setValue(base + "/fxLeftHandSlapNoise", p.fxLeftHandSlapNoise);
    settings.setValue(base + "/fxRightHandSlapNoise", p.fxRightHandSlapNoise);
    settings.setValue(base + "/fxSlideTurn4", p.fxSlideTurn4);
    settings.setValue(base + "/fxSlideTurn3", p.fxSlideTurn3);
    settings.setValue(base + "/fxSlideDown4", p.fxSlideDown4);
    settings.setValue(base + "/fxSlideDown3", p.fxSlideDown3);

    settings.setValue(base + "/swingAmount", p.swingAmount);
    settings.setValue(base + "/swingRatio", p.swingRatio);
    settings.setValue(base + "/microJitterMs", p.microJitterMs);
    settings.setValue(base + "/laidBackMs", p.laidBackMs);
    settings.setValue(base + "/pushMs", p.pushMs);
    settings.setValue(base + "/driftMaxMs", p.driftMaxMs);
    settings.setValue(base + "/driftRate", p.driftRate);
    settings.setValue(base + "/attackVarianceMs", p.attackVarianceMs);
    settings.setValue(base + "/noteLengthMs", p.noteLengthMs);
    settings.setValue(base + "/gatePct", p.gatePct);
    settings.setValue(base + "/humanizeSeed", (qulonglong)p.humanizeSeed);

    settings.setValue(base + "/baseVelocity", p.baseVelocity);
    settings.setValue(base + "/velocityVariance", p.velocityVariance);
    settings.setValue(base + "/accentBeat1", p.accentBeat1);
    settings.setValue(base + "/accentBeat2", p.accentBeat2);
    settings.setValue(base + "/accentBeat3", p.accentBeat3);
    settings.setValue(base + "/accentBeat4", p.accentBeat4);
    settings.setValue(base + "/phraseContourStrength", p.phraseContourStrength);
    settings.setValue(base + "/phraseArcStrength", p.phraseArcStrength);
    settings.setValue(base + "/sectionArcStrength", p.sectionArcStrength);

    settings.setValue(base + "/chromaticism", p.chromaticism);
    settings.setValue(base + "/leapPenalty", p.leapPenalty);
    settings.setValue(base + "/repetitionPenalty", p.repetitionPenalty);

    settings.setValue(base + "/intensityBase", p.intensityBase);
    settings.setValue(base + "/intensityVariance", p.intensityVariance);
    settings.setValue(base + "/evolutionRate", p.evolutionRate);
    settings.setValue(base + "/sectionRampStrength", p.sectionRampStrength);
    settings.setValue(base + "/phraseLengthBars", p.phraseLengthBars);
    settings.setValue(base + "/twoFeelPhraseProb", p.twoFeelPhraseProb);
    settings.setValue(base + "/brokenTimePhraseProb", p.brokenTimePhraseProb);
    settings.setValue(base + "/restProb", p.restProb);
    settings.setValue(base + "/tieProb", p.tieProb);

    settings.setValue(base + "/ghostNoteProb", p.ghostNoteProb);
    settings.setValue(base + "/ghostVelocity", p.ghostVelocity);
    settings.setValue(base + "/ghostGatePct", p.ghostGatePct);
    settings.setValue(base + "/pickup8thProb", p.pickup8thProb);
    settings.setValue(base + "/fillProbPhraseEnd", p.fillProbPhraseEnd);
    settings.setValue(base + "/syncopationProb", p.syncopationProb);
    settings.setValue(base + "/twoBeatRunProb", p.twoBeatRunProb);
    settings.setValue(base + "/enclosureProb", p.enclosureProb);
    settings.setValue(base + "/sectionIntroRestraint", p.sectionIntroRestraint);
    settings.setValue(base + "/motifProb", p.motifProb);
    settings.setValue(base + "/motifStrength", p.motifStrength);
    settings.setValue(base + "/motifVariation", p.motifVariation);

    settings.setValue(base + "/wRoot", p.wRoot);
    settings.setValue(base + "/wThird", p.wThird);
    settings.setValue(base + "/wFifth", p.wFifth);
    settings.setValue(base + "/wSeventh", p.wSeventh);

    settings.setValue(base + "/wApproachChromatic", p.wApproachChromatic);
    settings.setValue(base + "/wApproachDiatonic", p.wApproachDiatonic);
    settings.setValue(base + "/wApproachEnclosure", p.wApproachEnclosure);
}

} // namespace music

