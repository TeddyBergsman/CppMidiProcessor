#include "virtuoso/vocab/VocabularyRegistry.h"

#include "virtuoso/util/StableHash.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

#include <algorithm>

namespace virtuoso::vocab {
namespace {

static bool jsonGetBool(const QJsonObject& o, const char* k, bool def) {
    const auto v = o.value(QString::fromUtf8(k));
    if (v.isBool()) return v.toBool();
    return def;
}

static double jsonGetDouble(const QJsonObject& o, const char* k, double def) {
    const auto v = o.value(QString::fromUtf8(k));
    if (v.isDouble()) return v.toDouble();
    return def;
}

static int jsonGetInt(const QJsonObject& o, const char* k, int def) {
    const auto v = o.value(QString::fromUtf8(k));
    if (v.isDouble()) return v.toInt();
    return def;
}

static QString jsonGetString(const QJsonObject& o, const char* k, const QString& def = QString()) {
    const auto v = o.value(QString::fromUtf8(k));
    if (v.isString()) return v.toString();
    return def;
}

static QVector<int> jsonGetIntArray(const QJsonObject& o, const char* k) {
    QVector<int> out;
    const auto v = o.value(QString::fromUtf8(k));
    if (!v.isArray()) return out;
    const auto a = v.toArray();
    out.reserve(a.size());
    for (const auto& e : a) {
        if (!e.isDouble()) continue;
        out.push_back(e.toInt());
    }
    return out;
}

static QVector<QString> jsonGetStringArray(const QJsonObject& o, const char* k) {
    QVector<QString> out;
    const auto v = o.value(QString::fromUtf8(k));
    if (!v.isArray()) return out;
    const auto a = v.toArray();
    out.reserve(a.size());
    for (const auto& e : a) {
        if (!e.isString()) continue;
        const QString s = e.toString().trimmed();
        if (!s.isEmpty()) out.push_back(s);
    }
    return out;
}

static VocabularyRegistry::DrumArticulation parseDrumArticulation(const QString& s) {
    const QString k = s.trimmed().toLower();
    if (k == "ride_bell") return VocabularyRegistry::DrumArticulation::RideBell;
    if (k == "snare_swish") return VocabularyRegistry::DrumArticulation::SnareSwish;
    if (k == "brush_short") return VocabularyRegistry::DrumArticulation::BrushShort;
    return VocabularyRegistry::DrumArticulation::RideHit;
}

static bool functionMatches(const QVector<QString>& allowed, const QString& func) {
    if (allowed.isEmpty()) return true;
    const QString f = func.trimmed();
    if (f.isEmpty()) return true;
    for (const auto& a : allowed) {
        if (a.trimmed().compare(f, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

} // namespace

quint32 VocabularyRegistry::fnv1a32(const QByteArray& bytes) {
    // Canonical hash across the app (do not use qHash for determinism).
    return virtuoso::util::StableHash::fnv1a32(bytes);
}

bool VocabularyRegistry::energyMatches(double e, double minE, double maxE) {
    if (minE > maxE) std::swap(minE, maxE);
    return (e >= minE) && (e <= maxE);
}

int VocabularyRegistry::clampBeat(int beatInBar) {
    return (beatInBar < 0) ? 0 : beatInBar;
}

template <typename TPattern, typename TChoice, typename TMakeChoiceFn>
TChoice VocabularyRegistry::chooseWeighted(const QVector<TPattern>& patterns,
                                          quint32 pickHash,
                                          const TMakeChoiceFn& makeChoice) {
    TChoice out{};
    if (patterns.isEmpty()) return out;
    // Weighted pick by mapping hash into [0, sumWeights).
    double sum = 0.0;
    for (const auto& p : patterns) sum += (p.weight > 0.0 ? p.weight : 0.0);
    if (sum <= 0.0) return out;

    // Deterministic unit in [0,1)
    const double u = double((pickHash >> 8) & 0x00FF'FFFFu) / double(0x0100'0000u);
    const double r = u * sum;

    double acc = 0.0;
    for (const auto& p : patterns) {
        const double w = (p.weight > 0.0 ? p.weight : 0.0);
        acc += w;
        if (r <= acc) return makeChoice(p);
    }
    // Fallback to last.
    return makeChoice(patterns.last());
}

bool VocabularyRegistry::loadFromResourcePath(const QString& resourcePath, QString* outError) {
    QFile f(resourcePath);
    if (!f.open(QIODevice::ReadOnly)) {
        m_lastError = QString("Failed to open vocab resource '%1'").arg(resourcePath);
        if (outError) *outError = m_lastError;
        m_loaded = false;
        return false;
    }
    return loadFromJsonBytes(f.readAll(), outError);
}

bool VocabularyRegistry::loadFromJsonBytes(const QByteArray& json, QString* outError) {
    m_lastError.clear();
    m_loaded = false;
    m_piano.clear();
    m_bass.clear();
    m_drums.clear();
    m_pianoPhrases.clear();
    m_pianoTopLines.clear();
    m_pianoGestures.clear();
    m_pianoPedals.clear();
    m_bassPhrases.clear();
    m_drumsPhrases.clear();

    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        m_lastError = QString("Invalid vocab JSON: %1").arg(pe.errorString());
        if (outError) *outError = m_lastError;
        return false;
    }
    const auto root = doc.object();

    auto parsePiano = [&]() {
        const auto arrV = root.value("piano");
        if (!arrV.isArray()) return;
        const auto arr = arrV.toArray();
        for (const auto& v : arr) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            PianoBeatPattern p;
            p.id = jsonGetString(o, "id");
            if (p.id.trimmed().isEmpty()) continue;
            p.beats = jsonGetIntArray(o, "beats");
            p.minEnergy = jsonGetDouble(o, "minEnergy", 0.0);
            p.maxEnergy = jsonGetDouble(o, "maxEnergy", 1.0);
            p.weight = jsonGetDouble(o, "weight", 1.0);
            p.chordIsNewOnly = jsonGetBool(o, "chordIsNewOnly", false);
            p.stableOnly = jsonGetBool(o, "stableOnly", false);
            p.allowWhenUserSilence = jsonGetBool(o, "allowWhenUserSilence", true);
            p.chordFunctions = jsonGetStringArray(o, "functions");
            p.notes = jsonGetString(o, "notes");

            const auto hitsV = o.value("hits");
            if (hitsV.isArray()) {
                const auto hitsA = hitsV.toArray();
                for (const auto& hv : hitsA) {
                    if (!hv.isObject()) continue;
                    const auto ho = hv.toObject();
                    PianoHit h;
                    h.sub = jsonGetInt(ho, "sub", 0);
                    h.count = jsonGetInt(ho, "count", 1);
                    h.dur_num = jsonGetInt(ho, "dur_num", 1);
                    h.dur_den = jsonGetInt(ho, "dur_den", 4);
                    h.vel_delta = jsonGetInt(ho, "vel_delta", 0);
                    h.density = jsonGetString(ho, "density", "full");
                    p.hits.push_back(h);
                }
            }
            if (p.hits.isEmpty()) continue;
            m_piano.push_back(p);
        }
    };

    auto parsePianoPhrases = [&]() {
        const auto arrV = root.value("piano_phrases");
        if (!arrV.isArray()) return;
        const auto arr = arrV.toArray();
        for (const auto& v : arr) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            PianoPhrasePattern p;
            p.id = jsonGetString(o, "id");
            if (p.id.trimmed().isEmpty()) continue;
            p.phraseBars = qMax(1, jsonGetInt(o, "phraseBars", 4));
            p.minEnergy = jsonGetDouble(o, "minEnergy", 0.0);
            p.maxEnergy = jsonGetDouble(o, "maxEnergy", 1.0);
            p.weight = jsonGetDouble(o, "weight", 1.0);
            p.allowWhenUserSilence = jsonGetBool(o, "allowWhenUserSilence", true);
            p.chordFunctions = jsonGetStringArray(o, "functions");
            p.notes = jsonGetString(o, "notes");

            const auto hitsV = o.value("hits");
            if (hitsV.isArray()) {
                const auto hitsA = hitsV.toArray();
                for (const auto& hv : hitsA) {
                    if (!hv.isObject()) continue;
                    const auto ho = hv.toObject();
                    PianoPhraseHit ph;
                    ph.barOffset = jsonGetInt(ho, "bar", 0);
                    ph.beatInBar = jsonGetInt(ho, "beat", 0);
                    PianoHit h;
                    h.sub = jsonGetInt(ho, "sub", 0);
                    h.count = jsonGetInt(ho, "count", 1);
                    h.dur_num = jsonGetInt(ho, "dur_num", 1);
                    h.dur_den = jsonGetInt(ho, "dur_den", 4);
                    h.vel_delta = jsonGetInt(ho, "vel_delta", 0);
                    h.density = jsonGetString(ho, "density", "full");
                    ph.hit = h;
                    p.hits.push_back(ph);
                }
            }
            if (p.hits.isEmpty()) continue;
            m_pianoPhrases.push_back(p);
        }
    };

    auto parsePianoTopLines = [&]() {
        const auto arrV = root.value("piano_topline");
        if (!arrV.isArray()) return;
        const auto arr = arrV.toArray();
        for (const auto& v : arr) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            PianoTopLinePattern p;
            p.id = jsonGetString(o, "id");
            if (p.id.trimmed().isEmpty()) continue;
            p.phraseBars = qMax(1, jsonGetInt(o, "phraseBars", 4));
            p.minEnergy = jsonGetDouble(o, "minEnergy", 0.0);
            p.maxEnergy = jsonGetDouble(o, "maxEnergy", 1.0);
            p.weight = jsonGetDouble(o, "weight", 1.0);
            p.allowWhenUserSilence = jsonGetBool(o, "allowWhenUserSilence", true);
            p.chordFunctions = jsonGetStringArray(o, "functions");
            p.notes = jsonGetString(o, "notes");

            const auto hitsV = o.value("hits");
            if (hitsV.isArray()) {
                const auto hitsA = hitsV.toArray();
                for (const auto& hv : hitsA) {
                    if (!hv.isObject()) continue;
                    const auto ho = hv.toObject();
                    PianoTopLineHit th;
                    th.barOffset = jsonGetInt(ho, "bar", 0);
                    th.beatInBar = jsonGetInt(ho, "beat", 0);
                    th.sub = jsonGetInt(ho, "sub", 0);
                    th.count = jsonGetInt(ho, "count", 1);
                    th.dur_num = jsonGetInt(ho, "dur_num", 1);
                    th.dur_den = jsonGetInt(ho, "dur_den", 8);
                    th.vel_delta = jsonGetInt(ho, "vel_delta", -10);
                    th.degree = jsonGetInt(ho, "degree", 9);
                    th.neighborDir = jsonGetInt(ho, "neighborDir", 0);
                    th.resolve = jsonGetBool(ho, "resolve", false);
                    th.tag = jsonGetString(ho, "tag");
                    p.hits.push_back(th);
                }
            }
            if (p.hits.isEmpty()) continue;
            m_pianoTopLines.push_back(p);
        }
    };

    auto parsePianoGestures = [&]() {
        const auto arrV = root.value("piano_gestures");
        if (!arrV.isArray()) return;
        const auto arr = arrV.toArray();
        for (const auto& v : arr) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            PianoGesturePattern p;
            p.id = jsonGetString(o, "id");
            if (p.id.trimmed().isEmpty()) continue;
            p.minEnergy = jsonGetDouble(o, "minEnergy", 0.0);
            p.maxEnergy = jsonGetDouble(o, "maxEnergy", 1.0);
            p.weight = jsonGetDouble(o, "weight", 1.0);
            p.cadenceOnly = jsonGetBool(o, "cadenceOnly", false);
            p.chordIsNewOnly = jsonGetBool(o, "chordIsNewOnly", false);
            p.allowWhenUserSilence = jsonGetBool(o, "allowWhenUserSilence", true);
            p.minNoteCount = qMax(1, jsonGetInt(o, "minNoteCount", 2));
            p.maxNoteCount = qMax(p.minNoteCount, jsonGetInt(o, "maxNoteCount", 10));
            p.maxBpm = qMax(30, jsonGetInt(o, "maxBpm", 999));
            p.kind = jsonGetString(o, "kind", "none");
            p.style = jsonGetString(o, "style");
            p.spreadMs = jsonGetInt(o, "spreadMs", 0);
            p.notes = jsonGetString(o, "notes");
            m_pianoGestures.push_back(p);
        }
    };

    auto parsePianoPedals = [&]() {
        const auto arrV = root.value("piano_pedals");
        if (!arrV.isArray()) return;
        const auto arr = arrV.toArray();
        for (const auto& v : arr) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            PianoPedalPattern p;
            p.id = jsonGetString(o, "id");
            if (p.id.trimmed().isEmpty()) continue;
            p.minEnergy = jsonGetDouble(o, "minEnergy", 0.0);
            p.maxEnergy = jsonGetDouble(o, "maxEnergy", 1.0);
            p.weight = jsonGetDouble(o, "weight", 1.0);
            p.allowWhenUserSilence = jsonGetBool(o, "allowWhenUserSilence", true);
            p.defaultState = jsonGetString(o, "defaultState", "half");
            p.repedalOnNewChord = jsonGetBool(o, "repedalOnNewChord", false);
            p.repedalProbPct = qBound(0, jsonGetInt(o, "repedalProbPct", 50), 100);
            p.clearBeforeChange = jsonGetBool(o, "clearBeforeChange", true);
            p.clearSub = qMax(0, jsonGetInt(o, "clearSub", 3));
            p.clearCount = qMax(1, jsonGetInt(o, "clearCount", 4));
            p.notes = jsonGetString(o, "notes");
            m_pianoPedals.push_back(p);
        }
    };

    auto parseBass = [&]() {
        const auto arrV = root.value("bass");
        if (!arrV.isArray()) return;
        const auto arr = arrV.toArray();
        for (const auto& v : arr) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            BassBeatPattern p;
            p.id = jsonGetString(o, "id");
            if (p.id.trimmed().isEmpty()) continue;
            p.beats = jsonGetIntArray(o, "beats");
            p.minEnergy = jsonGetDouble(o, "minEnergy", 0.0);
            p.maxEnergy = jsonGetDouble(o, "maxEnergy", 1.0);
            p.weight = jsonGetDouble(o, "weight", 1.0);
            p.chordIsNewOnly = jsonGetBool(o, "chordIsNewOnly", false);
            p.stableOnly = jsonGetBool(o, "stableOnly", false);
            p.nextChangesOnly = jsonGetBool(o, "nextChangesOnly", false);
            p.forbidWhenUserDenseOrPeak = jsonGetBool(o, "forbidWhenUserDenseOrPeak", false);
            p.sub = jsonGetInt(o, "sub", 0);
            p.count = jsonGetInt(o, "count", 1);
            p.dur_num = jsonGetInt(o, "dur_num", 1);
            p.dur_den = jsonGetInt(o, "dur_den", 4);
            p.vel_delta = jsonGetInt(o, "vel_delta", 0);
            p.notes = jsonGetString(o, "notes");

            const QString action = jsonGetString(o, "action", "none").trimmed().toLower();
            if (action == "rest") p.action = BassAction::Rest;
            else if (action == "root") p.action = BassAction::Root;
            else if (action == "fifth") p.action = BassAction::Fifth;
            else if (action == "third") p.action = BassAction::Third;
            else if (action == "approach_to_next") p.action = BassAction::ApproachToNext;
            else if (action == "pickup_to_next") p.action = BassAction::PickupToNext;
            else p.action = BassAction::None;

            if (p.action == BassAction::None) continue;
            m_bass.push_back(p);
        }
    };

    auto parseBassPhrases = [&]() {
        const auto arrV = root.value("bass_phrases");
        if (!arrV.isArray()) return;
        const auto arr = arrV.toArray();
        for (const auto& v : arr) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            BassPhrasePattern p;
            p.id = jsonGetString(o, "id");
            if (p.id.trimmed().isEmpty()) continue;
            p.phraseBars = qMax(1, jsonGetInt(o, "phraseBars", 4));
            p.minEnergy = jsonGetDouble(o, "minEnergy", 0.0);
            p.maxEnergy = jsonGetDouble(o, "maxEnergy", 1.0);
            p.weight = jsonGetDouble(o, "weight", 1.0);
            p.forbidWhenUserDenseOrPeak = jsonGetBool(o, "forbidWhenUserDenseOrPeak", false);
            p.notes = jsonGetString(o, "notes");

            const auto hitsV = o.value("hits");
            if (hitsV.isArray()) {
                const auto hitsA = hitsV.toArray();
                for (const auto& hv : hitsA) {
                    if (!hv.isObject()) continue;
                    const auto ho = hv.toObject();
                    BassPhraseHit bh;
                    bh.barOffset = jsonGetInt(ho, "bar", 0);
                    bh.beatInBar = jsonGetInt(ho, "beat", 0);
                    bh.sub = jsonGetInt(ho, "sub", 0);
                    bh.count = jsonGetInt(ho, "count", 1);
                    bh.dur_num = jsonGetInt(ho, "dur_num", 1);
                    bh.dur_den = jsonGetInt(ho, "dur_den", 4);
                    bh.vel_delta = jsonGetInt(ho, "vel_delta", 0);
                    bh.notes = jsonGetString(ho, "notes");

                    const QString action = jsonGetString(ho, "action", "none").trimmed().toLower();
                    if (action == "rest") bh.action = BassAction::Rest;
                    else if (action == "root") bh.action = BassAction::Root;
                    else if (action == "fifth") bh.action = BassAction::Fifth;
                    else if (action == "third") bh.action = BassAction::Third;
                    else if (action == "approach_to_next") bh.action = BassAction::ApproachToNext;
                    else if (action == "pickup_to_next") bh.action = BassAction::PickupToNext;
                    else bh.action = BassAction::None;

                    if (bh.action == BassAction::None) continue;
                    p.hits.push_back(bh);
                }
            }
            if (p.hits.isEmpty()) continue;
            m_bassPhrases.push_back(p);
        }
    };

    auto parseDrums = [&]() {
        const auto arrV = root.value("drums");
        if (!arrV.isArray()) return;
        const auto arr = arrV.toArray();
        for (const auto& v : arr) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            DrumsBeatPattern p;
            p.id = jsonGetString(o, "id");
            if (p.id.trimmed().isEmpty()) continue;
            p.beats = jsonGetIntArray(o, "beats");
            p.minEnergy = jsonGetDouble(o, "minEnergy", 0.0);
            p.maxEnergy = jsonGetDouble(o, "maxEnergy", 1.0);
            p.weight = jsonGetDouble(o, "weight", 1.0);
            p.intensityPeakOnly = jsonGetBool(o, "intensityPeakOnly", false);
            p.notes = jsonGetString(o, "notes");

            const auto hitsV = o.value("hits");
            if (hitsV.isArray()) {
                const auto hitsA = hitsV.toArray();
                for (const auto& hv : hitsA) {
                    if (!hv.isObject()) continue;
                    const auto ho = hv.toObject();
                    DrumHit h;
                    h.articulation = parseDrumArticulation(jsonGetString(ho, "articulation", "ride_hit"));
                    h.sub = jsonGetInt(ho, "sub", 0);
                    h.count = jsonGetInt(ho, "count", 1);
                    h.dur_num = jsonGetInt(ho, "dur_num", 1);
                    h.dur_den = jsonGetInt(ho, "dur_den", 16);
                    h.vel_delta = jsonGetInt(ho, "vel_delta", 0);
                    p.hits.push_back(h);
                }
            }
            if (p.hits.isEmpty()) continue;
            m_drums.push_back(p);
        }
    };

    auto parseDrumsPhrases = [&]() {
        const auto arrV = root.value("drums_phrases");
        if (!arrV.isArray()) return;
        const auto arr = arrV.toArray();
        for (const auto& v : arr) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            DrumsPhrasePattern p;
            p.id = jsonGetString(o, "id");
            if (p.id.trimmed().isEmpty()) continue;
            p.phraseBars = qMax(1, jsonGetInt(o, "phraseBars", 4));
            p.minEnergy = jsonGetDouble(o, "minEnergy", 0.0);
            p.maxEnergy = jsonGetDouble(o, "maxEnergy", 1.0);
            p.weight = jsonGetDouble(o, "weight", 1.0);
            p.intensityPeakOnly = jsonGetBool(o, "intensityPeakOnly", false);
            p.notes = jsonGetString(o, "notes");

            const auto hitsV = o.value("hits");
            if (hitsV.isArray()) {
                const auto hitsA = hitsV.toArray();
                for (const auto& hv : hitsA) {
                    if (!hv.isObject()) continue;
                    const auto ho = hv.toObject();
                    DrumsPhraseHit dh;
                    dh.barOffset = jsonGetInt(ho, "bar", 0);
                    dh.beatInBar = jsonGetInt(ho, "beat", 0);
                    DrumHit h;
                    h.articulation = parseDrumArticulation(jsonGetString(ho, "articulation", "ride_hit"));
                    h.sub = jsonGetInt(ho, "sub", 0);
                    h.count = jsonGetInt(ho, "count", 1);
                    h.dur_num = jsonGetInt(ho, "dur_num", 1);
                    h.dur_den = jsonGetInt(ho, "dur_den", 16);
                    h.vel_delta = jsonGetInt(ho, "vel_delta", 0);
                    dh.hit = h;
                    p.hits.push_back(dh);
                }
            }
            if (p.hits.isEmpty()) continue;
            m_drumsPhrases.push_back(p);
        }
    };

    parsePiano();
    parsePianoPhrases();
    parsePianoTopLines();
    parsePianoGestures();
    parsePianoPedals();
    parseBass();
    parseBassPhrases();
    parseDrums();
    parseDrumsPhrases();

    if (m_piano.isEmpty() && m_bass.isEmpty() && m_drums.isEmpty() &&
        m_pianoPhrases.isEmpty() && m_pianoTopLines.isEmpty() && m_pianoGestures.isEmpty() && m_pianoPedals.isEmpty() &&
        m_bassPhrases.isEmpty() && m_drumsPhrases.isEmpty()) {
        m_lastError = "Vocab JSON parsed but contained no usable patterns (piano/bass/drums were empty).";
        if (outError) *outError = m_lastError;
        return false;
    }

    m_loaded = true;
    return true;
}

VocabularyRegistry::PianoBeatChoice VocabularyRegistry::choosePianoBeat(const PianoBeatQuery& q) const {
    PianoBeatChoice out;
    if (!m_loaded) return out;
    if (!(q.ts.num == 4 && q.ts.den == 4)) return out;
    const int beat = clampBeat(q.beatInBar);
    const double e = qBound(0.0, q.energy, 1.0);

    QVector<PianoBeatPattern> cands;
    cands.reserve(16);
    for (const auto& p : m_piano) {
        if (!p.beats.contains(beat)) continue;
        if (!energyMatches(e, p.minEnergy, p.maxEnergy)) continue;
        if (p.chordIsNewOnly && !q.chordIsNew) continue;
        if (p.stableOnly && q.chordIsNew) continue;
        if (!p.allowWhenUserSilence && q.userSilence) continue;
        if (!functionMatches(p.chordFunctions, q.chordFunction)) continue;
        cands.push_back(p);
    }
    const quint32 h = fnv1a32(QString("%1|piano|%2|%3|%4|%5|%6")
                                  .arg(q.chordText)
                                  .arg(q.playbackBarIndex)
                                  .arg(beat)
                                  .arg(int(q.chordIsNew))
                                  .arg(q.chordFunction)
                                  .arg(q.determinismSeed)
                                  .toUtf8());
    return chooseWeighted<PianoBeatPattern, PianoBeatChoice>(cands, h, [](const PianoBeatPattern& p) {
        PianoBeatChoice c;
        c.id = p.id;
        c.hits = p.hits;
        c.notes = p.notes;
        return c;
    });
}

VocabularyRegistry::BassBeatChoice VocabularyRegistry::chooseBassBeat(const BassBeatQuery& q) const {
    BassBeatChoice out;
    if (!m_loaded) return out;
    if (!(q.ts.num == 4 && q.ts.den == 4)) return out;
    const int beat = clampBeat(q.beatInBar);
    const double e = qBound(0.0, q.energy, 1.0);

    QVector<BassBeatPattern> cands;
    cands.reserve(16);
    for (const auto& p : m_bass) {
        if (!p.beats.contains(beat)) continue;
        if (!energyMatches(e, p.minEnergy, p.maxEnergy)) continue;
        if (p.chordIsNewOnly && !q.chordIsNew) continue;
        if (p.stableOnly && q.chordIsNew) continue;
        if (p.nextChangesOnly && !(q.hasNextChord && q.nextChanges)) continue;
        if (p.forbidWhenUserDenseOrPeak && q.userDenseOrPeak) continue;
        cands.push_back(p);
    }

    const quint32 h = fnv1a32(QString("%1|bass|%2|%3|%4|%5|%6|%7")
                                  .arg(q.chordText)
                                  .arg(q.playbackBarIndex)
                                  .arg(beat)
                                  .arg(int(q.chordIsNew))
                                  .arg(int(q.hasNextChord))
                                  .arg(int(q.nextChanges))
                                  .arg(q.determinismSeed)
                                  .toUtf8());
    return chooseWeighted<BassBeatPattern, BassBeatChoice>(cands, h, [](const BassBeatPattern& p) {
        BassBeatChoice c;
        c.id = p.id;
        c.action = p.action;
        c.sub = p.sub;
        c.count = p.count;
        c.dur_num = p.dur_num;
        c.dur_den = p.dur_den;
        c.vel_delta = p.vel_delta;
        c.notes = p.notes;
        return c;
    });
}

VocabularyRegistry::DrumsBeatChoice VocabularyRegistry::chooseDrumsBeat(const DrumsBeatQuery& q) const {
    DrumsBeatChoice out;
    if (!m_loaded) return out;
    if (!(q.ts.num == 4 && q.ts.den == 4)) return out;
    const int beat = clampBeat(q.beatInBar);
    const double e = qBound(0.0, q.energy, 1.0);

    QVector<DrumsBeatPattern> cands;
    cands.reserve(16);
    for (const auto& p : m_drums) {
        if (!p.beats.contains(beat)) continue;
        if (!energyMatches(e, p.minEnergy, p.maxEnergy)) continue;
        if (p.intensityPeakOnly && !q.intensityPeak) continue;
        cands.push_back(p);
    }

    const quint32 h = fnv1a32(QString("drums|%1|%2|%3|%4")
                                  .arg(q.playbackBarIndex)
                                  .arg(beat)
                                  .arg(int(q.intensityPeak))
                                  .arg(q.determinismSeed)
                                  .toUtf8());
    return chooseWeighted<DrumsBeatPattern, DrumsBeatChoice>(cands, h, [](const DrumsBeatPattern& p) {
        DrumsBeatChoice c;
        c.id = p.id;
        c.hits = p.hits;
        c.notes = p.notes;
        return c;
    });
}

VocabularyRegistry::PianoPhraseChoice VocabularyRegistry::choosePianoPhrase(const PianoPhraseQuery& q) const {
    PianoPhraseChoice out;
    if (!m_loaded) return out;
    if (!(q.ts.num == 4 && q.ts.den == 4)) return out;
    const double e = qBound(0.0, q.energy, 1.0);
    const int pb = qMax(1, q.phraseBars);

    QVector<PianoPhrasePattern> cands;
    cands.reserve(16);
    for (const auto& p : m_pianoPhrases) {
        // Allow modular matching: pattern's phraseBars should evenly divide query's phraseBars.
        // E.g., 4-bar patterns work within 4-bar or 8-bar phrases.
        const int patternBars = qMax(1, p.phraseBars);
        if (pb % patternBars != 0) continue;
        if (!energyMatches(e, p.minEnergy, p.maxEnergy)) continue;
        if (!p.allowWhenUserSilence && q.userSilence) continue;
        if (!functionMatches(p.chordFunctions, q.chordFunction)) continue;
        cands.push_back(p);
    }

    // For modular matching, use the sub-phrase index based on playback bar.
    // This ensures deterministic selection even when 4-bar patterns are used in 8-bar phrases.
    const int subPhraseLen = cands.isEmpty() ? pb : qMax(1, cands.first().phraseBars);
    const int phraseIndex = (q.playbackBarIndex >= 0) ? (q.playbackBarIndex / subPhraseLen) : 0;
    const quint32 h = fnv1a32(QString("%1|piano_phrase|%2|%3|%4|%5")
                                  .arg(q.chordText)
                                  .arg(phraseIndex)
                                  .arg(int(q.chordIsNew))
                                  .arg(q.chordFunction)
                                  .arg(q.determinismSeed)
                                  .toUtf8());
    return chooseWeighted<PianoPhrasePattern, PianoPhraseChoice>(cands, h, [](const PianoPhrasePattern& p) {
        PianoPhraseChoice c;
        c.id = p.id;
        c.phraseBars = p.phraseBars;
        c.hits = p.hits;
        c.notes = p.notes;
        return c;
    });
}

VocabularyRegistry::BassPhraseChoice VocabularyRegistry::chooseBassPhrase(const BassPhraseQuery& q) const {
    BassPhraseChoice out;
    if (!m_loaded) return out;
    if (!(q.ts.num == 4 && q.ts.den == 4)) return out;
    const double e = qBound(0.0, q.energy, 1.0);
    const int pb = qMax(1, q.phraseBars);

    QVector<BassPhrasePattern> cands;
    cands.reserve(16);
    for (const auto& p : m_bassPhrases) {
        // Allow modular matching: pattern's phraseBars should evenly divide query's phraseBars.
        const int patternBars = qMax(1, p.phraseBars);
        if (pb % patternBars != 0) continue;
        if (!energyMatches(e, p.minEnergy, p.maxEnergy)) continue;
        if (p.forbidWhenUserDenseOrPeak && q.userDenseOrPeak) continue;
        cands.push_back(p);
    }
    const int subPhraseLen = cands.isEmpty() ? pb : qMax(1, cands.first().phraseBars);
    const int phraseIndex = (q.playbackBarIndex >= 0) ? (q.playbackBarIndex / subPhraseLen) : 0;
    const quint32 h = fnv1a32(QString("%1|bass_phrase|%2|%3|%4|%5")
                                  .arg(q.chordText)
                                  .arg(phraseIndex)
                                  .arg(int(q.chordIsNew))
                                  .arg(int(q.nextChanges))
                                  .arg(q.determinismSeed)
                                  .toUtf8());
    return chooseWeighted<BassPhrasePattern, BassPhraseChoice>(cands, h, [](const BassPhrasePattern& p) {
        BassPhraseChoice c;
        c.id = p.id;
        c.phraseBars = p.phraseBars;
        c.hits = p.hits;
        c.notes = p.notes;
        return c;
    });
}

VocabularyRegistry::DrumsPhraseChoice VocabularyRegistry::chooseDrumsPhrase(const DrumsPhraseQuery& q) const {
    DrumsPhraseChoice out;
    if (!m_loaded) return out;
    if (!(q.ts.num == 4 && q.ts.den == 4)) return out;
    const double e = qBound(0.0, q.energy, 1.0);
    const int pb = qMax(1, q.phraseBars);

    QVector<DrumsPhrasePattern> cands;
    cands.reserve(16);
    for (const auto& p : m_drumsPhrases) {
        // Allow modular matching: pattern's phraseBars should evenly divide query's phraseBars.
        const int patternBars = qMax(1, p.phraseBars);
        if (pb % patternBars != 0) continue;
        if (!energyMatches(e, p.minEnergy, p.maxEnergy)) continue;
        if (p.intensityPeakOnly && !q.intensityPeak) continue;
        cands.push_back(p);
    }
    const int subPhraseLen = cands.isEmpty() ? pb : qMax(1, cands.first().phraseBars);
    const int phraseIndex = (q.playbackBarIndex >= 0) ? (q.playbackBarIndex / subPhraseLen) : 0;
    const quint32 h = fnv1a32(QString("drums_phrase|%1|%2|%3")
                                  .arg(phraseIndex)
                                  .arg(int(q.intensityPeak))
                                  .arg(q.determinismSeed)
                                  .toUtf8());
    return chooseWeighted<DrumsPhrasePattern, DrumsPhraseChoice>(cands, h, [](const DrumsPhrasePattern& p) {
        DrumsPhraseChoice c;
        c.id = p.id;
        c.phraseBars = p.phraseBars;
        c.hits = p.hits;
        c.notes = p.notes;
        return c;
    });
}

VocabularyRegistry::PianoTopLineChoice VocabularyRegistry::choosePianoTopLine(const PianoTopLineQuery& q) const {
    PianoTopLineChoice out;
    if (!m_loaded) return out;
    if (!(q.ts.num == 4 && q.ts.den == 4)) return out;
    const double e = qBound(0.0, q.energy, 1.0);
    const int pb = qMax(1, q.phraseBars);

    QVector<PianoTopLinePattern> cands;
    cands.reserve(16);
    for (const auto& p : m_pianoTopLines) {
        if (p.phraseBars != pb) continue;
        if (!energyMatches(e, p.minEnergy, p.maxEnergy)) continue;
        if (!p.allowWhenUserSilence && q.userSilence) continue;
        if (!functionMatches(p.chordFunctions, q.chordFunction)) continue;
        cands.push_back(p);
    }
    const int phraseIndex = (q.playbackBarIndex >= 0) ? (q.playbackBarIndex / pb) : 0;
    const quint32 h = fnv1a32(QString("%1|piano_topline|%2|%3|%4|%5|%6")
                                  .arg(q.chordText)
                                  .arg(phraseIndex)
                                  .arg(int(q.chordIsNew))
                                  .arg(q.chordFunction)
                                  .arg(int(llround(q.rhythmicComplexity * 100.0)))
                                  .arg(q.determinismSeed)
                                  .toUtf8());
    return chooseWeighted<PianoTopLinePattern, PianoTopLineChoice>(cands, h, [](const PianoTopLinePattern& p) {
        PianoTopLineChoice c;
        c.id = p.id;
        c.phraseBars = p.phraseBars;
        c.hits = p.hits;
        c.notes = p.notes;
        return c;
    });
}

VocabularyRegistry::PianoGestureChoice VocabularyRegistry::choosePianoGesture(const PianoGestureQuery& q) const {
    PianoGestureChoice out;
    if (!m_loaded) return out;
    if (!(q.ts.num == 4 && q.ts.den == 4)) return out;
    const double e = qBound(0.0, q.energy, 1.0);

    QVector<PianoGesturePattern> cands;
    cands.reserve(16);
    for (const auto& p : m_pianoGestures) {
        if (!energyMatches(e, p.minEnergy, p.maxEnergy)) continue;
        if (p.cadenceOnly && !q.cadence) continue;
        if (p.chordIsNewOnly && !q.chordIsNew) continue;
        if (!p.allowWhenUserSilence && q.userSilence) continue;
        if (q.noteCount < p.minNoteCount || q.noteCount > p.maxNoteCount) continue;
        if (q.bpm > p.maxBpm) continue;
        cands.push_back(p);
    }
    const quint32 h = fnv1a32(QString("%1|piano_gesture|%2|%3|%4|%5|%6|%7")
                                  .arg(q.chordText)
                                  .arg(q.playbackBarIndex)
                                  .arg(q.beatInBar)
                                  .arg(int(q.cadence))
                                  .arg(q.noteCount)
                                  .arg(int(llround(q.energy * 100.0)))
                                  .arg(q.determinismSeed)
                                  .toUtf8());
    return chooseWeighted<PianoGesturePattern, PianoGestureChoice>(cands, h, [](const PianoGesturePattern& p) {
        PianoGestureChoice c;
        c.id = p.id;
        c.kind = p.kind;
        c.style = p.style;
        c.spreadMs = p.spreadMs;
        c.notes = p.notes;
        return c;
    });
}

VocabularyRegistry::PianoPedalChoice VocabularyRegistry::choosePianoPedal(const PianoPedalQuery& q) const {
    PianoPedalChoice out;
    if (!m_loaded) return out;
    if (!(q.ts.num == 4 && q.ts.den == 4)) return out;
    const double e = qBound(0.0, q.energy, 1.0);

    QVector<PianoPedalPattern> cands;
    cands.reserve(16);
    for (const auto& p : m_pianoPedals) {
        if (!energyMatches(e, p.minEnergy, p.maxEnergy)) continue;
        if (!p.allowWhenUserSilence && q.userSilence) continue;
        cands.push_back(p);
    }
    const quint32 h = fnv1a32(QString("%1|piano_pedal|%2|%3|%4|%5|%6")
                                  .arg(q.chordText)
                                  .arg(q.playbackBarIndex)
                                  .arg(int(q.chordIsNew))
                                  .arg(int(q.nextChanges))
                                  .arg(q.beatsUntilChordChange)
                                  .arg(q.determinismSeed)
                                  .toUtf8());
    return chooseWeighted<PianoPedalPattern, PianoPedalChoice>(cands, h, [](const PianoPedalPattern& p) {
        PianoPedalChoice c;
        c.id = p.id;
        c.defaultState = p.defaultState;
        c.repedalOnNewChord = p.repedalOnNewChord;
        c.repedalProbPct = p.repedalProbPct;
        c.clearBeforeChange = p.clearBeforeChange;
        c.clearSub = p.clearSub;
        c.clearCount = p.clearCount;
        c.notes = p.notes;
        return c;
    });
}

QVector<VocabularyRegistry::PianoHit> VocabularyRegistry::pianoPhraseHitsForBeat(const PianoPhraseQuery& q,
                                                                                 QString* outPhraseId,
                                                                                 QString* outPhraseNotes) const {
    QVector<PianoHit> out;
    const auto ch = choosePianoPhrase(q);
    if (outPhraseId) *outPhraseId = ch.id;
    if (outPhraseNotes) *outPhraseNotes = ch.notes;
    if (ch.id.isEmpty()) return out;
    const int pb = qMax(1, ch.phraseBars);
    const int barInPhrase = (q.playbackBarIndex >= 0) ? (q.playbackBarIndex % pb) : 0;
    for (const auto& h : ch.hits) {
        if (h.barOffset == barInPhrase && h.beatInBar == q.beatInBar) out.push_back(h.hit);
    }

    return out;
}

QVector<VocabularyRegistry::BassPhraseHit> VocabularyRegistry::bassPhraseHitsForBeat(const BassPhraseQuery& q,
                                                                                     QString* outPhraseId,
                                                                                     QString* outPhraseNotes) const {
    QVector<BassPhraseHit> out;
    const auto ch = chooseBassPhrase(q);
    if (outPhraseId) *outPhraseId = ch.id;
    if (outPhraseNotes) *outPhraseNotes = ch.notes;
    if (ch.id.isEmpty()) return out;
    const int pb = qMax(1, ch.phraseBars);
    const int barInPhrase = (q.playbackBarIndex >= 0) ? (q.playbackBarIndex % pb) : 0;
    for (const auto& h : ch.hits) {
        if (h.barOffset == barInPhrase && h.beatInBar == q.beatInBar) out.push_back(h);
    }
    return out;
}

QVector<VocabularyRegistry::DrumHit> VocabularyRegistry::drumsPhraseHitsForBeat(const DrumsPhraseQuery& q,
                                                                                QString* outPhraseId,
                                                                                QString* outPhraseNotes) const {
    QVector<DrumHit> out;
    const auto ch = chooseDrumsPhrase(q);
    if (outPhraseId) *outPhraseId = ch.id;
    if (outPhraseNotes) *outPhraseNotes = ch.notes;
    if (ch.id.isEmpty()) return out;
    const int pb = qMax(1, ch.phraseBars);
    const int barInPhrase = (q.playbackBarIndex >= 0) ? (q.playbackBarIndex % pb) : 0;
    for (const auto& h : ch.hits) {
        if (h.barOffset == barInPhrase && h.beatInBar == q.beatInBar) out.push_back(h.hit);
    }
    return out;
}

QVector<VocabularyRegistry::PianoPatternDef> VocabularyRegistry::pianoPatterns() const {
    QVector<PianoPatternDef> out;
    out.reserve(m_piano.size());
    for (const auto& p : m_piano) {
        PianoPatternDef d;
        d.id = p.id;
        d.beats = p.beats;
        d.minEnergy = p.minEnergy;
        d.maxEnergy = p.maxEnergy;
        d.weight = p.weight;
        d.chordIsNewOnly = p.chordIsNewOnly;
        d.stableOnly = p.stableOnly;
        d.allowWhenUserSilence = p.allowWhenUserSilence;
        d.chordFunctions = p.chordFunctions;
        d.hits = p.hits;
        d.notes = p.notes;
        out.push_back(d);
    }
    return out;
}

QVector<VocabularyRegistry::BassPatternDef> VocabularyRegistry::bassPatterns() const {
    QVector<BassPatternDef> out;
    out.reserve(m_bass.size());
    for (const auto& p : m_bass) {
        BassPatternDef d;
        d.id = p.id;
        d.beats = p.beats;
        d.minEnergy = p.minEnergy;
        d.maxEnergy = p.maxEnergy;
        d.weight = p.weight;
        d.chordIsNewOnly = p.chordIsNewOnly;
        d.stableOnly = p.stableOnly;
        d.nextChangesOnly = p.nextChangesOnly;
        d.forbidWhenUserDenseOrPeak = p.forbidWhenUserDenseOrPeak;
        d.action = p.action;
        d.sub = p.sub;
        d.count = p.count;
        d.dur_num = p.dur_num;
        d.dur_den = p.dur_den;
        d.vel_delta = p.vel_delta;
        d.notes = p.notes;
        out.push_back(d);
    }
    return out;
}

QVector<VocabularyRegistry::DrumsPatternDef> VocabularyRegistry::drumsPatterns() const {
    QVector<DrumsPatternDef> out;
    out.reserve(m_drums.size());
    for (const auto& p : m_drums) {
        DrumsPatternDef d;
        d.id = p.id;
        d.beats = p.beats;
        d.minEnergy = p.minEnergy;
        d.maxEnergy = p.maxEnergy;
        d.weight = p.weight;
        d.intensityPeakOnly = p.intensityPeakOnly;
        d.hits = p.hits;
        d.notes = p.notes;
        out.push_back(d);
    }
    return out;
}

QVector<VocabularyRegistry::PianoPhraseChoice> VocabularyRegistry::pianoPhrasePatterns() const {
    QVector<PianoPhraseChoice> out;
    out.reserve(m_pianoPhrases.size());
    for (const auto& p : m_pianoPhrases) {
        PianoPhraseChoice c;
        c.id = p.id;
        c.phraseBars = p.phraseBars;
        c.hits = p.hits;
        c.notes = p.notes;
        out.push_back(c);
    }
    return out;
}

QVector<VocabularyRegistry::PianoTopLinePatternDef> VocabularyRegistry::pianoTopLinePatterns() const {
    QVector<PianoTopLinePatternDef> out;
    out.reserve(m_pianoTopLines.size());
    for (const auto& p : m_pianoTopLines) {
        PianoTopLinePatternDef d;
        d.id = p.id;
        d.phraseBars = p.phraseBars;
        d.minEnergy = p.minEnergy;
        d.maxEnergy = p.maxEnergy;
        d.weight = p.weight;
        d.allowWhenUserSilence = p.allowWhenUserSilence;
        d.chordFunctions = p.chordFunctions;
        d.hits = p.hits;
        d.notes = p.notes;
        out.push_back(d);
    }
    return out;
}

QVector<VocabularyRegistry::PianoGesturePatternDef> VocabularyRegistry::pianoGesturePatterns() const {
    QVector<PianoGesturePatternDef> out;
    out.reserve(m_pianoGestures.size());
    for (const auto& p : m_pianoGestures) {
        PianoGesturePatternDef d;
        d.id = p.id;
        d.minEnergy = p.minEnergy;
        d.maxEnergy = p.maxEnergy;
        d.weight = p.weight;
        d.cadenceOnly = p.cadenceOnly;
        d.chordIsNewOnly = p.chordIsNewOnly;
        d.allowWhenUserSilence = p.allowWhenUserSilence;
        d.minNoteCount = p.minNoteCount;
        d.maxNoteCount = p.maxNoteCount;
        d.maxBpm = p.maxBpm;
        d.kind = p.kind;
        d.style = p.style;
        d.spreadMs = p.spreadMs;
        d.notes = p.notes;
        out.push_back(d);
    }
    return out;
}

QVector<VocabularyRegistry::PianoPedalPatternDef> VocabularyRegistry::pianoPedalPatterns() const {
    QVector<PianoPedalPatternDef> out;
    out.reserve(m_pianoPedals.size());
    for (const auto& p : m_pianoPedals) {
        PianoPedalPatternDef d;
        d.id = p.id;
        d.minEnergy = p.minEnergy;
        d.maxEnergy = p.maxEnergy;
        d.weight = p.weight;
        d.allowWhenUserSilence = p.allowWhenUserSilence;
        d.defaultState = p.defaultState;
        d.repedalOnNewChord = p.repedalOnNewChord;
        d.repedalProbPct = p.repedalProbPct;
        d.clearBeforeChange = p.clearBeforeChange;
        d.clearSub = p.clearSub;
        d.clearCount = p.clearCount;
        d.notes = p.notes;
        out.push_back(d);
    }
    return out;
}

QVector<VocabularyRegistry::BassPhraseChoice> VocabularyRegistry::bassPhrasePatterns() const {
    QVector<BassPhraseChoice> out;
    out.reserve(m_bassPhrases.size());
    for (const auto& p : m_bassPhrases) {
        BassPhraseChoice c;
        c.id = p.id;
        c.phraseBars = p.phraseBars;
        c.hits = p.hits;
        c.notes = p.notes;
        out.push_back(c);
    }
    return out;
}

QVector<VocabularyRegistry::DrumsPhraseChoice> VocabularyRegistry::drumsPhrasePatterns() const {
    QVector<DrumsPhraseChoice> out;
    out.reserve(m_drumsPhrases.size());
    for (const auto& p : m_drumsPhrases) {
        DrumsPhraseChoice c;
        c.id = p.id;
        c.phraseBars = p.phraseBars;
        c.hits = p.hits;
        c.notes = p.notes;
        out.push_back(c);
    }
    return out;
}

} // namespace virtuoso::vocab

