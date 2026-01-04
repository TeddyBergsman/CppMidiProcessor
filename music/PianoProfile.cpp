#include "music/PianoProfile.h"

#include <QSettings>
#include <QtGlobal>
#include <algorithm>

namespace music {
namespace {

static int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }
static double clampD(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }

static int toInt(PianoFeelStyle s) { return (s == PianoFeelStyle::Swing) ? 1 : 0; }
static PianoFeelStyle feelFromInt(int v) { return (v == 1) ? PianoFeelStyle::Swing : PianoFeelStyle::Ballad; }

static int readInt(QSettings& s, const QString& k, int def) { return s.value(k, def).toInt(); }
static double readD(QSettings& s, const QString& k, double def) { return s.value(k, def).toDouble(); }
static bool readB(QSettings& s, const QString& k, bool def) { return s.value(k, def).toBool(); }
static QString readS(QSettings& s, const QString& k, const QString& def) { return s.value(k, def).toString(); }
static quint32 readU32(QSettings& s, const QString& k, quint32 def) { return (quint32)s.value(k, (qulonglong)def).toULongLong(); }

} // namespace

PianoProfile defaultPianoProfile() {
    // Default: world-class Bill Evans-ish comping with light fills, tasteful pedal.
    PianoProfile p;
    p.name = "Bill Evans (Default)";
    p.enabled = true;
    p.midiChannel = 4;
    p.feelStyle = PianoFeelStyle::Swing;

    // Timing: slightly laid back, still professional/tight.
    p.microJitterMs = 4;
    p.laidBackMs = 8;
    p.pushMs = 0;
    p.driftMaxMs = 14;
    p.driftRate = 0.18;

    // Dynamics: warm and restrained.
    p.baseVelocity = 62;
    p.velocityVariance = 14;

    // Comping probability: not "every beat", but present.
    p.compDensity = 0.55;
    p.anticipationProb = 0.14;
    p.syncopationProb = 0.18;
    p.restProb = 0.12;

    // Voicing language: rootless, guided voice-leading, occasional quartal/cluster color.
    p.preferRootless = true;
    p.rootlessProb = 0.80;
    p.drop2Prob = 0.35;
    p.quartalProb = 0.18;
    p.clusterProb = 0.10;
    p.tensionProb = 0.75;
    p.avoidRootProb = 0.65;
    p.avoidThirdProb = 0.10;
    p.voiceLeadingStrength = 0.75;
    p.repetitionPenalty = 0.45;
    p.maxHandLeap = 10;

    // Fills: rare outside phrase ends.
    p.fillProbPhraseEnd = 0.22;
    p.fillProbAnyBeat = 0.06;
    p.phraseLengthBars = 4;
    p.fillMaxNotes = 4;

    // Pedal: on by default, refresh on changes.
    p.pedalEnabled = true;
    p.pedalReleaseOnChordChange = true;
    p.pedalMinHoldMs = 180;
    p.pedalMaxHoldMs = 620;
    p.pedalChangeProb = 0.80;

    return p;
}

PianoProfile loadPianoProfile(QSettings& settings, const QString& prefix) {
    PianoProfile p = defaultPianoProfile();
    const QString base = prefix;

    p.version = readInt(settings, base + "/version", p.version);
    p.name = readS(settings, base + "/name", p.name);
    p.feelStyle = feelFromInt(clampInt(readInt(settings, base + "/feelStyle", toInt(p.feelStyle)), 0, 1));

    p.enabled = readB(settings, base + "/enabled", p.enabled);
    p.midiChannel = clampInt(readInt(settings, base + "/midiChannel", p.midiChannel), 1, 16);

    p.lhMinMidiNote = clampInt(readInt(settings, base + "/lhMinMidiNote", p.lhMinMidiNote), 0, 127);
    p.lhMaxMidiNote = clampInt(readInt(settings, base + "/lhMaxMidiNote", p.lhMaxMidiNote), 0, 127);
    if (p.lhMinMidiNote > p.lhMaxMidiNote) std::swap(p.lhMinMidiNote, p.lhMaxMidiNote);

    p.rhMinMidiNote = clampInt(readInt(settings, base + "/rhMinMidiNote", p.rhMinMidiNote), 0, 127);
    p.rhMaxMidiNote = clampInt(readInt(settings, base + "/rhMaxMidiNote", p.rhMaxMidiNote), 0, 127);
    if (p.rhMinMidiNote > p.rhMaxMidiNote) std::swap(p.rhMinMidiNote, p.rhMaxMidiNote);

    // Keep RH above LH by default; if user overlaps, allow it (some voicings cross), but keep sane.
    p.microJitterMs = clampInt(readInt(settings, base + "/microJitterMs", p.microJitterMs), 0, 50);
    p.laidBackMs = clampInt(readInt(settings, base + "/laidBackMs", p.laidBackMs), -60, 60);
    p.pushMs = clampInt(readInt(settings, base + "/pushMs", p.pushMs), -60, 60);
    p.driftMaxMs = clampInt(readInt(settings, base + "/driftMaxMs", p.driftMaxMs), 0, 120);
    p.driftRate = clampD(readD(settings, base + "/driftRate", p.driftRate), 0.0, 1.0);
    p.humanizeSeed = readU32(settings, base + "/humanizeSeed", p.humanizeSeed);
    if (p.humanizeSeed == 0) p.humanizeSeed = 1;

    p.baseVelocity = clampInt(readInt(settings, base + "/baseVelocity", p.baseVelocity), 1, 127);
    p.velocityVariance = clampInt(readInt(settings, base + "/velocityVariance", p.velocityVariance), 0, 64);
    p.accentDownbeat = clampD(readD(settings, base + "/accentDownbeat", p.accentDownbeat), 0.1, 2.0);
    p.accentBackbeat = clampD(readD(settings, base + "/accentBackbeat", p.accentBackbeat), 0.1, 2.0);

    p.compDensity = clampD(readD(settings, base + "/compDensity", p.compDensity), 0.0, 1.0);
    p.anticipationProb = clampD(readD(settings, base + "/anticipationProb", p.anticipationProb), 0.0, 1.0);
    p.syncopationProb = clampD(readD(settings, base + "/syncopationProb", p.syncopationProb), 0.0, 1.0);
    p.restProb = clampD(readD(settings, base + "/restProb", p.restProb), 0.0, 1.0);

    p.preferRootless = readB(settings, base + "/preferRootless", p.preferRootless);
    p.rootlessProb = clampD(readD(settings, base + "/rootlessProb", p.rootlessProb), 0.0, 1.0);
    p.drop2Prob = clampD(readD(settings, base + "/drop2Prob", p.drop2Prob), 0.0, 1.0);
    p.quartalProb = clampD(readD(settings, base + "/quartalProb", p.quartalProb), 0.0, 1.0);
    p.clusterProb = clampD(readD(settings, base + "/clusterProb", p.clusterProb), 0.0, 1.0);
    p.tensionProb = clampD(readD(settings, base + "/tensionProb", p.tensionProb), 0.0, 1.0);
    p.avoidRootProb = clampD(readD(settings, base + "/avoidRootProb", p.avoidRootProb), 0.0, 1.0);
    p.avoidThirdProb = clampD(readD(settings, base + "/avoidThirdProb", p.avoidThirdProb), 0.0, 1.0);

    p.maxHandLeap = clampInt(readInt(settings, base + "/maxHandLeap", p.maxHandLeap), 0, 36);
    p.voiceLeadingStrength = clampD(readD(settings, base + "/voiceLeadingStrength", p.voiceLeadingStrength), 0.0, 1.0);
    p.repetitionPenalty = clampD(readD(settings, base + "/repetitionPenalty", p.repetitionPenalty), 0.0, 1.0);

    p.fillProbPhraseEnd = clampD(readD(settings, base + "/fillProbPhraseEnd", p.fillProbPhraseEnd), 0.0, 1.0);
    p.fillProbAnyBeat = clampD(readD(settings, base + "/fillProbAnyBeat", p.fillProbAnyBeat), 0.0, 1.0);
    p.phraseLengthBars = clampInt(readInt(settings, base + "/phraseLengthBars", p.phraseLengthBars), 1, 16);
    p.fillMaxNotes = clampInt(readInt(settings, base + "/fillMaxNotes", p.fillMaxNotes), 0, 16);
    p.fillMinMidiNote = clampInt(readInt(settings, base + "/fillMinMidiNote", p.fillMinMidiNote), 0, 127);
    p.fillMaxMidiNote = clampInt(readInt(settings, base + "/fillMaxMidiNote", p.fillMaxMidiNote), 0, 127);
    if (p.fillMinMidiNote > p.fillMaxMidiNote) std::swap(p.fillMinMidiNote, p.fillMaxMidiNote);

    p.pedalEnabled = readB(settings, base + "/pedalEnabled", p.pedalEnabled);
    p.pedalReleaseOnChordChange = readB(settings, base + "/pedalReleaseOnChordChange", p.pedalReleaseOnChordChange);
    p.pedalDownValue = clampInt(readInt(settings, base + "/pedalDownValue", p.pedalDownValue), 0, 127);
    p.pedalUpValue = clampInt(readInt(settings, base + "/pedalUpValue", p.pedalUpValue), 0, 127);
    p.pedalMinHoldMs = clampInt(readInt(settings, base + "/pedalMinHoldMs", p.pedalMinHoldMs), 0, 5000);
    p.pedalMaxHoldMs = clampInt(readInt(settings, base + "/pedalMaxHoldMs", p.pedalMaxHoldMs), 0, 8000);
    if (p.pedalMinHoldMs > p.pedalMaxHoldMs) std::swap(p.pedalMinHoldMs, p.pedalMaxHoldMs);
    p.pedalChangeProb = clampD(readD(settings, base + "/pedalChangeProb", p.pedalChangeProb), 0.0, 1.0);

    p.reasoningLogEnabled = readB(settings, base + "/reasoningLogEnabled", p.reasoningLogEnabled);

    return p;
}

void savePianoProfile(QSettings& settings, const QString& prefix, const PianoProfile& p) {
    const QString base = prefix;

    settings.setValue(base + "/version", p.version);
    settings.setValue(base + "/name", p.name);
    settings.setValue(base + "/feelStyle", toInt(p.feelStyle));

    settings.setValue(base + "/enabled", p.enabled);
    settings.setValue(base + "/midiChannel", p.midiChannel);

    settings.setValue(base + "/lhMinMidiNote", p.lhMinMidiNote);
    settings.setValue(base + "/lhMaxMidiNote", p.lhMaxMidiNote);
    settings.setValue(base + "/rhMinMidiNote", p.rhMinMidiNote);
    settings.setValue(base + "/rhMaxMidiNote", p.rhMaxMidiNote);

    settings.setValue(base + "/microJitterMs", p.microJitterMs);
    settings.setValue(base + "/laidBackMs", p.laidBackMs);
    settings.setValue(base + "/pushMs", p.pushMs);
    settings.setValue(base + "/driftMaxMs", p.driftMaxMs);
    settings.setValue(base + "/driftRate", p.driftRate);
    settings.setValue(base + "/humanizeSeed", (qulonglong)p.humanizeSeed);

    settings.setValue(base + "/baseVelocity", p.baseVelocity);
    settings.setValue(base + "/velocityVariance", p.velocityVariance);
    settings.setValue(base + "/accentDownbeat", p.accentDownbeat);
    settings.setValue(base + "/accentBackbeat", p.accentBackbeat);

    settings.setValue(base + "/compDensity", p.compDensity);
    settings.setValue(base + "/anticipationProb", p.anticipationProb);
    settings.setValue(base + "/syncopationProb", p.syncopationProb);
    settings.setValue(base + "/restProb", p.restProb);

    settings.setValue(base + "/preferRootless", p.preferRootless);
    settings.setValue(base + "/rootlessProb", p.rootlessProb);
    settings.setValue(base + "/drop2Prob", p.drop2Prob);
    settings.setValue(base + "/quartalProb", p.quartalProb);
    settings.setValue(base + "/clusterProb", p.clusterProb);
    settings.setValue(base + "/tensionProb", p.tensionProb);
    settings.setValue(base + "/avoidRootProb", p.avoidRootProb);
    settings.setValue(base + "/avoidThirdProb", p.avoidThirdProb);

    settings.setValue(base + "/maxHandLeap", p.maxHandLeap);
    settings.setValue(base + "/voiceLeadingStrength", p.voiceLeadingStrength);
    settings.setValue(base + "/repetitionPenalty", p.repetitionPenalty);

    settings.setValue(base + "/fillProbPhraseEnd", p.fillProbPhraseEnd);
    settings.setValue(base + "/fillProbAnyBeat", p.fillProbAnyBeat);
    settings.setValue(base + "/phraseLengthBars", p.phraseLengthBars);
    settings.setValue(base + "/fillMaxNotes", p.fillMaxNotes);
    settings.setValue(base + "/fillMinMidiNote", p.fillMinMidiNote);
    settings.setValue(base + "/fillMaxMidiNote", p.fillMaxMidiNote);

    settings.setValue(base + "/pedalEnabled", p.pedalEnabled);
    settings.setValue(base + "/pedalReleaseOnChordChange", p.pedalReleaseOnChordChange);
    settings.setValue(base + "/pedalDownValue", p.pedalDownValue);
    settings.setValue(base + "/pedalUpValue", p.pedalUpValue);
    settings.setValue(base + "/pedalMinHoldMs", p.pedalMinHoldMs);
    settings.setValue(base + "/pedalMaxHoldMs", p.pedalMaxHoldMs);
    settings.setValue(base + "/pedalChangeProb", p.pedalChangeProb);

    settings.setValue(base + "/reasoningLogEnabled", p.reasoningLogEnabled);
}

} // namespace music

