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
    // Default: classic, beautiful jazz ballad comping (hotel-bar tasteful).
    PianoProfile p;
    p.name = "Classic Ballad (Default)";
    p.enabled = true;
    p.midiChannel = 4;
    p.feelStyle = PianoFeelStyle::Ballad;

    // Register: this app's piano center is one octave higher than v1 assumptions.
    // Keep LH in the midrange and RH comfortably above it.
    // Expanded downward range so the piano isn't only bright.
    p.lhMinMidiNote = 36;  // C2
    p.lhMaxMidiNote = 72;  // C5
    p.rhMinMidiNote = 60;  // C4
    p.rhMaxMidiNote = 100; // E7

    // Timing: slightly laid back, still professional/tight.
    p.microJitterMs = 2;
    p.laidBackMs = 9;
    p.pushMs = 0;
    p.driftMaxMs = 8;
    p.driftRate = 0.10;

    // Dynamics: warm and restrained.
    p.baseVelocity = 60;
    p.velocityVariance = 10;
    p.accentDownbeat = 1.22;
    p.accentBackbeat = 0.92;

    // Comping: very sparse, mainly 1 & 3 (two-feel).
    p.compDensity = 0.46;
    p.anticipationProb = 0.0;
    p.syncopationProb = 0.0;
    p.restProb = 0.22;

    // Voicing language: shell/rootless, very consonant, smooth voice-leading.
    p.preferRootless = true;
    p.rootlessProb = 1.00;
    p.drop2Prob = 0.0;
    p.quartalProb = 0.0;
    p.clusterProb = 0.0;
    p.tensionProb = 0.35;
    p.avoidRootProb = 0.90;
    p.avoidThirdProb = 0.0;
    p.voiceLeadingStrength = 0.92;
    p.repetitionPenalty = 0.18;
    p.maxHandLeap = 7;

    // Fills: off by default for classic ballad (no distracting licks).
    p.fillProbPhraseEnd = 0.0;
    p.fillProbAnyBeat = 0.0;
    p.phraseLengthBars = 4;
    p.fillMaxNotes = 0;
    p.fillMinMidiNote = 64;
    p.fillMaxMidiNote = 108;

    // Pedal: on by default, refresh on changes (clear harmony).
    p.pedalEnabled = true;
    p.pedalReleaseOnChordChange = true;
    p.pedalMinHoldMs = 420;
    p.pedalMaxHoldMs = 1400;
    p.pedalChangeProb = 0.95;

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

    // v1 -> v2 migration: earlier versions were centered an octave too low.
    if (p.version < 2) {
        auto bump12 = [](int x) -> int { return std::max(0, std::min(127, x + 12)); };
        p.lhMinMidiNote = bump12(p.lhMinMidiNote);
        p.lhMaxMidiNote = bump12(p.lhMaxMidiNote);
        p.rhMinMidiNote = bump12(p.rhMinMidiNote);
        p.rhMaxMidiNote = bump12(p.rhMaxMidiNote);
        p.fillMinMidiNote = bump12(p.fillMinMidiNote);
        p.fillMaxMidiNote = bump12(p.fillMaxMidiNote);
        if (p.lhMinMidiNote > p.lhMaxMidiNote) std::swap(p.lhMinMidiNote, p.lhMaxMidiNote);
        if (p.rhMinMidiNote > p.rhMaxMidiNote) std::swap(p.rhMinMidiNote, p.rhMaxMidiNote);
        if (p.fillMinMidiNote > p.fillMaxMidiNote) std::swap(p.fillMinMidiNote, p.fillMaxMidiNote);
        p.version = 2;
    }

    // v2 -> v3 migration: expand range downward by an octave (warmer, less bright).
    if (p.version < 3) {
        auto down12 = [](int x) -> int { return std::max(0, std::min(127, x - 12)); };
        p.lhMinMidiNote = down12(p.lhMinMidiNote);
        p.rhMinMidiNote = down12(p.rhMinMidiNote);
        p.fillMinMidiNote = down12(p.fillMinMidiNote);
        if (p.lhMinMidiNote > p.lhMaxMidiNote) std::swap(p.lhMinMidiNote, p.lhMaxMidiNote);
        if (p.rhMinMidiNote > p.rhMaxMidiNote) std::swap(p.rhMinMidiNote, p.rhMaxMidiNote);
        if (p.fillMinMidiNote > p.fillMaxMidiNote) std::swap(p.fillMinMidiNote, p.fillMaxMidiNote);
        p.version = 3;
    }

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

