#include "virtuoso/vocab/VocabularyRegistry.h"

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

static VocabularyRegistry::DrumArticulation parseDrumArticulation(const QString& s) {
    const QString k = s.trimmed().toLower();
    if (k == "ride_bell") return VocabularyRegistry::DrumArticulation::RideBell;
    if (k == "snare_swish") return VocabularyRegistry::DrumArticulation::SnareSwish;
    if (k == "brush_short") return VocabularyRegistry::DrumArticulation::BrushShort;
    return VocabularyRegistry::DrumArticulation::RideHit;
}

} // namespace

quint32 VocabularyRegistry::fnv1a32(const QByteArray& bytes) {
    quint32 h = 2166136261u;
    for (unsigned char c : bytes) {
        h ^= quint32(c);
        h *= 16777619u;
    }
    return h;
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

    parsePiano();
    parseBass();
    parseDrums();

    if (m_piano.isEmpty() && m_bass.isEmpty() && m_drums.isEmpty()) {
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
        cands.push_back(p);
    }
    const quint32 h = fnv1a32(QString("%1|piano|%2|%3|%4|%5")
                                  .arg(q.chordText)
                                  .arg(q.playbackBarIndex)
                                  .arg(beat)
                                  .arg(int(q.chordIsNew))
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

} // namespace virtuoso::vocab

