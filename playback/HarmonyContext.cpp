#include "playback/HarmonyContext.h"

#include "virtuoso/theory/ScaleSuggester.h"

#include <QtGlobal>
#include <algorithm>

namespace playback {
namespace {

static const chart::Cell* cellForFlattenedIndexLocal(const chart::ChartModel& model, int cellIndex) {
    if (cellIndex < 0) return nullptr;
    QVector<const chart::Bar*> bars;
    for (const auto& line : model.lines) {
        for (const auto& bar : line.bars) bars.push_back(&bar);
    }
    const int barIndex = cellIndex / 4;
    const int cellInBar = cellIndex % 4;
    if (barIndex < 0 || barIndex >= bars.size()) return nullptr;
    const auto* bar = bars[barIndex];
    if (!bar) return nullptr;
    if (cellInBar < 0 || cellInBar >= bar->cells.size()) return nullptr;
    return &bar->cells[cellInBar];
}

} // namespace

void HarmonyContext::resetRuntimeState() {
    m_lastChord = music::ChordSymbol{};
    m_hasLastChord = false;
}

QVector<const chart::Bar*> HarmonyContext::flattenBarsFrom(const chart::ChartModel& model) {
    QVector<const chart::Bar*> bars;
    for (const auto& line : model.lines) {
        for (const auto& bar : line.bars) bars.push_back(&bar);
    }
    return bars;
}

int HarmonyContext::normalizePc(int pc) {
    int v = pc % 12;
    if (v < 0) v += 12;
    return v;
}

QString HarmonyContext::pcName(int pc) {
    static const char* names[] = {"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};
    return names[normalizePc(pc)];
}

bool HarmonyContext::sameChordKey(const music::ChordSymbol& a, const music::ChordSymbol& b) {
    if (!(a.rootPc == b.rootPc && a.bassPc == b.bassPc && a.quality == b.quality && a.seventh == b.seventh &&
          a.extension == b.extension && a.alt == b.alt)) {
        return false;
    }
    // Alterations materially change harmony (e.g. C7 -> C7b9) and must count as a "new chord".
    if (a.alterations.size() != b.alterations.size()) return false;
    auto norm = [](const music::Alteration& x) {
        // degree, delta, add are the semantic identity.
        return std::tuple<int, int, bool>(x.degree, x.delta, x.add);
    };
    QVector<std::tuple<int, int, bool>> aa;
    QVector<std::tuple<int, int, bool>> bb;
    aa.reserve(a.alterations.size());
    bb.reserve(b.alterations.size());
    for (const auto& x : a.alterations) aa.push_back(norm(x));
    for (const auto& x : b.alterations) bb.push_back(norm(x));
    std::sort(aa.begin(), aa.end());
    std::sort(bb.begin(), bb.end());
    return aa == bb;
}

QString HarmonyContext::ontologyChordKeyFor(const music::ChordSymbol& c) {
    using music::ChordQuality;
    using music::SeventhQuality;
    if (c.noChord || c.placeholder) return {};
    if (c.quality == ChordQuality::Dominant) {
        if (c.alt) return "7alt";
        bool hasB9 = false, hasSharp9 = false, hasB13 = false, hasSharp11 = false;
        for (const auto& a : c.alterations) {
            if (a.degree == 9 && a.delta < 0) hasB9 = true;
            if (a.degree == 9 && a.delta > 0) hasSharp9 = true;
            if (a.degree == 13 && a.delta < 0) hasB13 = true;
            if (a.degree == 11 && a.delta > 0) hasSharp11 = true;
        }
        if (hasB9 && hasSharp9) return "7b9#9";
        if (hasB9 && hasB13) return "7b9b13";
        if (hasSharp9 && hasB13) return "7#9b13";
        if (hasB9) return "7b9";
        if (hasSharp9) return "7#9";
        if (hasB13) return "7b13";
        if (c.extension >= 13 && hasSharp11) return "13#11";
        if (c.extension >= 13) return "13";
        if (c.extension >= 11) return "11";
        if (c.extension >= 9) return "9";
        if (c.seventh != SeventhQuality::None || c.extension >= 7) return "7";
        return "7";
    }
    if (c.quality == ChordQuality::HalfDiminished) return "m7b5";
    if (c.quality == ChordQuality::Diminished) {
        if (c.seventh == SeventhQuality::Dim7) return "dim7";
        return (c.extension >= 7) ? "dim7" : "dim";
    }
    if (c.quality == ChordQuality::Minor) {
        if (c.seventh == SeventhQuality::Major7) {
            if (c.extension >= 13) return "minmaj13";
            if (c.extension >= 11) return "minmaj11";
            if (c.extension >= 9) return "minmaj9";
            return "min_maj7";
        }
        if (c.extension >= 13) return "min13";
        if (c.extension >= 11) return "min11";
        if (c.extension >= 9) return "min9";
        if (c.seventh != SeventhQuality::None || c.extension >= 7) return "min7";
        return "min";
    }
    if (c.quality == ChordQuality::Major) {
        bool hasSharp11 = false;
        for (const auto& a : c.alterations) {
            if (a.degree == 11 && a.delta > 0) hasSharp11 = true;
        }
        if (c.extension >= 13 && hasSharp11) return "maj13#11";
        if (c.extension >= 13) return "maj13";
        if (c.extension >= 11) return "maj11";
        if (c.extension >= 9 && hasSharp11) return "maj9#11";
        if (c.extension >= 9) return "maj9";
        if (c.seventh == SeventhQuality::Major7 || c.extension >= 7) return "maj7";
        if (c.extension >= 6) return "6";
        return "maj";
    }
    if (c.quality == ChordQuality::Sus2) return "sus2";
    if (c.quality == ChordQuality::Sus4) {
        if (c.extension >= 13) return "13sus4";
        if (c.extension >= 9) return "9sus4";
        if (c.seventh == SeventhQuality::Minor7 || c.extension >= 7) return "7sus4";
        return "sus4";
    }
    if (c.quality == ChordQuality::Augmented) {
        if (c.seventh == SeventhQuality::Minor7 || c.extension >= 7) return "aug7";
        return "aug";
    }
    if (c.quality == ChordQuality::Power5) return "5";
    return {};
}

const virtuoso::ontology::ChordDef* HarmonyContext::chordDefForSymbol(const music::ChordSymbol& c) const {
    if (!m_ont) return nullptr;
    const QString key = ontologyChordKeyFor(c);
    if (key.isEmpty()) return nullptr;
    return m_ont->chord(key);
}

QSet<int> HarmonyContext::pitchClassesForChordDef(int rootPc, const virtuoso::ontology::ChordDef& chord) {
    QSet<int> pcs;
    const int r = normalizePc(rootPc);
    pcs.insert(r);
    for (int iv : chord.intervals) pcs.insert(normalizePc(r + iv));
    return pcs;
}

virtuoso::theory::KeyMode HarmonyContext::keyModeForScaleKey(const QString& k) {
    const QString s = k.toLower();
    if (s == "aeolian" || s == "harmonic_minor" || s == "melodic_minor") return virtuoso::theory::KeyMode::Minor;
    return virtuoso::theory::KeyMode::Major;
}

void HarmonyContext::estimateGlobalKeyByScale(const QVector<music::ChordSymbol>& chords, int fallbackPc) {
    m_keyPcGuess = normalizePc(fallbackPc);
    m_keyScaleKey.clear();
    m_keyScaleName.clear();
    m_keyMode = virtuoso::theory::KeyMode::Major;
    m_hasKeyPcGuess = false;
    if (!m_ont || chords.isEmpty()) return;

    QSet<int> pcs;
    pcs.reserve(24);
    for (const auto& c : chords) {
        if (c.noChord || c.placeholder || c.rootPc < 0) continue;
        const auto* def = chordDefForSymbol(c);
        if (!def) continue;
        const auto chordPcs = pitchClassesForChordDef(c.rootPc, *def);
        for (int pc : chordPcs) pcs.insert(pc);
    }
    if (pcs.isEmpty()) return;

    const auto sug = virtuoso::theory::suggestScalesForPitchClasses(*m_ont, pcs, 10);
    if (sug.isEmpty()) return;
    const auto& best = sug.first();
    m_keyPcGuess = normalizePc(best.bestTranspose);
    m_keyScaleKey = best.key;
    m_keyScaleName = best.name;
    m_keyMode = keyModeForScaleKey(best.key);
    m_hasKeyPcGuess = true;
}

QVector<LocalKeyEstimate> HarmonyContext::estimateLocalKeysByBar(const QVector<const chart::Bar*>& bars,
                                                                int windowBars,
                                                                int fallbackTonicPc,
                                                                const QString& fallbackScaleKey,
                                                                const QString& fallbackScaleName,
                                                                virtuoso::theory::KeyMode fallbackMode) const {
    QVector<LocalKeyEstimate> out;
    out.resize(bars.size());
    if (!m_ont || bars.isEmpty()) return out;
    windowBars = qMax(1, windowBars);

    for (int i = 0; i < bars.size(); ++i) {
        QSet<int> pcs;
        pcs.reserve(24);
        QVector<music::ChordSymbol> chords;
        chords.reserve(windowBars * 2);

        const int end = qMin(bars.size(), i + windowBars);
        for (int b = i; b < end; ++b) {
            const auto* bar = bars[b];
            if (!bar) continue;
            for (const auto& cell : bar->cells) {
                const QString t = cell.chord.trimmed();
                if (t.isEmpty()) continue;
                music::ChordSymbol parsed;
                if (!music::parseChordSymbol(t, parsed)) continue;
                if (parsed.placeholder || parsed.noChord || parsed.rootPc < 0) continue;
                chords.push_back(parsed);
                const auto* def = chordDefForSymbol(parsed);
                if (!def) continue;
                const auto chordPcs = pitchClassesForChordDef(parsed.rootPc, *def);
                for (int pc : chordPcs) pcs.insert(pc);
            }
        }

        LocalKeyEstimate lk;
        lk.tonicPc = fallbackTonicPc;
        lk.scaleKey = fallbackScaleKey;
        lk.scaleName = fallbackScaleName;
        lk.mode = fallbackMode;
        lk.score = 0.0;
        lk.coverage = 0.0;

        if (!pcs.isEmpty()) {
            const auto sug = virtuoso::theory::suggestScalesForPitchClasses(*m_ont, pcs, 6);
            if (!sug.isEmpty()) {
                const auto& best = sug.first();
                lk.tonicPc = normalizePc(best.bestTranspose);
                lk.scaleKey = best.key;
                lk.scaleName = best.name;
                lk.mode = keyModeForScaleKey(best.key);
                lk.score = best.score;
                lk.coverage = best.coverage;
            }
        }
        out[i] = lk;
    }
    return out;
}

LocalKeyEstimate HarmonyContext::estimateLocalKeyWindow(const chart::ChartModel& model, int barIndex, int windowBars) const {
    const QVector<const chart::Bar*> bars = flattenBarsFrom(model);
    if (bars.isEmpty()) return LocalKeyEstimate{m_keyPcGuess, m_keyScaleKey, m_keyScaleName, m_keyMode, 0.0, 0.0};
    barIndex = qBound(0, barIndex, bars.size() - 1);
    windowBars = qMax(1, windowBars);

    // Reuse the same logic as estimateLocalKeysByBar, but for a single start index.
    QSet<int> pcs;
    pcs.reserve(24);
    const int end = qMin(bars.size(), barIndex + windowBars);
    for (int b = barIndex; b < end; ++b) {
        const auto* bar = bars[b];
        if (!bar) continue;
        for (const auto& cell : bar->cells) {
            const QString t = cell.chord.trimmed();
            if (t.isEmpty()) continue;
            music::ChordSymbol parsed;
            if (!music::parseChordSymbol(t, parsed)) continue;
            if (parsed.placeholder || parsed.noChord || parsed.rootPc < 0) continue;
            const auto* def = chordDefForSymbol(parsed);
            if (!def) continue;
            const auto chordPcs = pitchClassesForChordDef(parsed.rootPc, *def);
            for (int pc : chordPcs) pcs.insert(pc);
        }
    }

    LocalKeyEstimate lk;
    lk.tonicPc = m_keyPcGuess;
    lk.scaleKey = m_keyScaleKey;
    lk.scaleName = m_keyScaleName;
    lk.mode = m_keyMode;
    lk.score = 0.0;
    lk.coverage = 0.0;

    if (!pcs.isEmpty() && m_ont) {
        const auto sug = virtuoso::theory::suggestScalesForPitchClasses(*m_ont, pcs, 6);
        if (!sug.isEmpty()) {
            const auto& best = sug.first();
            lk.tonicPc = normalizePc(best.bestTranspose);
            lk.scaleKey = best.key;
            lk.scaleName = best.name;
            lk.mode = keyModeForScaleKey(best.key);
            lk.score = best.score;
            lk.coverage = best.coverage;
        }
    }
    return lk;
}

HarmonyContext::ScaleChoice HarmonyContext::chooseScaleForChord(int keyPc,
                                                virtuoso::theory::KeyMode keyMode,
                                                const music::ChordSymbol& chordSym,
                                                const virtuoso::ontology::ChordDef& chordDef,
                                                QString* outRoman,
                                                QString* outFunction) const {
    ScaleChoice out;
    if (!m_ont) return out;
    const QSet<int> pcs = pitchClassesForChordDef(chordSym.rootPc, chordDef);
    const auto sugg = virtuoso::theory::suggestScalesForPitchClasses(*m_ont, pcs, 12);
    if (sugg.isEmpty()) return out;
    const auto h = virtuoso::theory::analyzeChordInKey(keyPc, keyMode, chordSym.rootPc, chordDef);
    if (outRoman) *outRoman = h.roman;
    if (outFunction) *outFunction = h.function;

    struct Sc { virtuoso::theory::ScaleSuggestion s; double score = 0.0; };
    QVector<Sc> ranked;
    ranked.reserve(sugg.size());
    const QString chordKey = ontologyChordKeyFor(chordSym);
    const QVector<QString> hints = virtuoso::theory::explicitHintScalesForContext(QString(), chordKey);
    for (const auto& s : sugg) {
        double bonus = 0.0;
        if (normalizePc(s.bestTranspose) == normalizePc(chordSym.rootPc)) bonus += 0.6;
        const QString name = s.name.toLower();
        if (h.function == "Dominant") {
            if (name.contains("altered") || name.contains("lydian dominant") || name.contains("mixolydian") || name.contains("half-whole")) bonus += 0.35;
        } else if (h.function == "Subdominant") {
            if (name.contains("dorian") || name.contains("lydian") || name.contains("phrygian")) bonus += 0.25;
        } else if (h.function == "Tonic") {
            if (name.contains("ionian") || name.contains("major") || name.contains("lydian")) bonus += 0.25;
        }
        for (int i = 0; i < hints.size(); ++i) {
            if (s.key == hints[i]) bonus += (0.45 - 0.08 * double(i));
        }
        ranked.push_back({s, s.score + bonus});
    }
    std::sort(ranked.begin(), ranked.end(), [](const Sc& a, const Sc& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.s.name < b.s.name;
    });
    const auto& best = ranked.first().s;
    out.key = best.key;
    out.name = best.name;
    out.transposePc = best.bestTranspose;
    out.display = QString("%1 (%2)").arg(best.name).arg(pcName(best.bestTranspose));
    return out;
}

music::ChordSymbol HarmonyContext::parseCellChordNoState(const chart::ChartModel& model,
                                                        int cellIndex,
                                                        const music::ChordSymbol& fallback,
                                                        bool* outIsExplicit) const {
    if (outIsExplicit) *outIsExplicit = false;
    const chart::Cell* c = cellForFlattenedIndexLocal(model, cellIndex);
    if (!c) return fallback;
    const QString t = c->chord.trimmed();
    if (t.isEmpty()) return fallback;
    music::ChordSymbol parsed;
    if (!music::parseChordSymbol(t, parsed)) return fallback;
    if (parsed.placeholder) return fallback;
    if (outIsExplicit) *outIsExplicit = true;
    return parsed;
}

bool HarmonyContext::chordForCellIndex(const chart::ChartModel& model, int cellIndex, music::ChordSymbol& outChord, bool& isNewChord) {
    isNewChord = false;
    const chart::Cell* c = cellForFlattenedIndexLocal(model, cellIndex);
    if (!c) return false;

    const QString t = c->chord.trimmed();
    if (t.isEmpty()) {
        if (m_hasLastChord) { outChord = m_lastChord; return true; }
        return false;
    }

    music::ChordSymbol parsed;
    if (!music::parseChordSymbol(t, parsed)) {
        if (m_hasLastChord) { outChord = m_lastChord; return true; }
        return false;
    }
    if (parsed.placeholder) {
        if (m_hasLastChord) { outChord = m_lastChord; return true; }
        return false;
    }

    outChord = parsed;
    if (!m_hasLastChord) isNewChord = true;
    else isNewChord = !sameChordKey(outChord, m_lastChord);
    m_lastChord = outChord;
    m_hasLastChord = true;
    return true;
}

void HarmonyContext::rebuildFromModel(const chart::ChartModel& model) {
    // Estimate a global key center + scale (major/minor/modal) from the chart,
    // and compute a per-bar local key (sliding window) for modulation detection.
    QVector<music::ChordSymbol> chords;
    chords.reserve(128);
    int fallbackPc = 0;
    bool haveFallback = false;

    const QVector<const chart::Bar*> bars = flattenBarsFrom(model);
    for (const auto* bar : bars) {
        if (!bar) continue;
        for (const auto& cell : bar->cells) {
            const QString t = cell.chord.trimmed();
            if (t.isEmpty()) continue;
            music::ChordSymbol parsed;
            if (!music::parseChordSymbol(t, parsed)) continue;
            if (parsed.placeholder || parsed.noChord || parsed.rootPc < 0) continue;
            chords.push_back(parsed);
            if (!haveFallback) { fallbackPc = parsed.rootPc; haveFallback = true; }
        }
    }

    if (!chords.isEmpty()) {
        estimateGlobalKeyByScale(chords, fallbackPc);
        if (m_keyScaleKey.trimmed().isEmpty()) {
            // Keep old major-key heuristic available as fallback for now.
            // NOTE: This fallback is only used when scale suggestion returns empty (rare).
            m_keyPcGuess = normalizePc(fallbackPc);
            m_keyScaleKey = "ionian";
            m_keyScaleName = "Ionian (Major)";
            m_keyMode = virtuoso::theory::KeyMode::Major;
            m_hasKeyPcGuess = true;
        }
    } else {
        m_keyPcGuess = 0;
        m_keyScaleKey.clear();
        m_keyScaleName.clear();
        m_keyMode = virtuoso::theory::KeyMode::Major;
        m_hasKeyPcGuess = false;
    }

    m_localKeysByBar = estimateLocalKeysByBar(bars,
                                              /*windowBars=*/8,
                                              m_keyPcGuess,
                                              m_keyScaleKey,
                                              m_keyScaleName,
                                              m_keyMode);
}

} // namespace playback

