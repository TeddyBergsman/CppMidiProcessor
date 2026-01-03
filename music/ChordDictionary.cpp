#include "music/ChordDictionary.h"

#include "music/Pitch.h"

#include <algorithm>

namespace music {
namespace {

static int intervalForDegree(int degree) {
    // Natural extensions relative to major scale degrees:
    // 5 -> 7, 9 -> 14, 11 -> 17, 13 -> 21 (reduce mod 12 later).
    switch (degree) {
    case 5:  return 7;
    case 9:  return 14;
    case 11: return 17;
    case 13: return 21;
    default: return 0;
    }
}

static void uniqueSort(QVector<int>& pcs) {
    for (int& v : pcs) v = normalizePc(v);
    std::sort(pcs.begin(), pcs.end());
    pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
}

static int seventhInterval(const ChordSymbol& c) {
    switch (c.seventh) {
    case SeventhQuality::Major7: return 11;
    case SeventhQuality::Minor7: return 10;
    case SeventhQuality::Dim7: return 9;
    case SeventhQuality::None: default: return 0;
    }
}

} // namespace

QVector<int> ChordDictionary::chordPitchClasses(const ChordSymbol& chord) {
    QVector<int> intervals;
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return {};

    // Triad skeleton
    switch (chord.quality) {
    case ChordQuality::Major:
    case ChordQuality::Dominant:
        intervals = {0, 4, 7};
        break;
    case ChordQuality::Minor:
        intervals = {0, 3, 7};
        break;
    case ChordQuality::HalfDiminished:
        intervals = {0, 3, 6};
        break;
    case ChordQuality::Diminished:
        intervals = {0, 3, 6};
        break;
    case ChordQuality::Augmented:
        intervals = {0, 4, 8};
        break;
    case ChordQuality::Sus2:
        intervals = {0, 2, 7};
        break;
    case ChordQuality::Sus4:
        intervals = {0, 5, 7};
        break;
    case ChordQuality::Power5:
        intervals = {0, 7};
        break;
    case ChordQuality::Unknown:
    default:
        intervals = {0, 4, 7};
        break;
    }

    // 6/7/9/11/13 extensions
    if (chord.extension >= 6) {
        // 6th (always major 6th for common symbols, even in minor)
        intervals.push_back(9);
    }

    if (chord.extension >= 7) {
        const int sev = seventhInterval(chord);
        if (sev != 0) intervals.push_back(sev);
    }

    if (chord.extension >= 9) {
        intervals.push_back(14); // 9
    }
    if (chord.extension >= 11) {
        intervals.push_back(17); // 11
    }
    if (chord.extension >= 13) {
        intervals.push_back(21); // 13
    }

    // "alt" implies at least b9/#9 and b5/#5 in practice; we keep it minimal.
    if (chord.alt && chord.extension >= 7) {
        intervals.push_back(13); // b9
        intervals.push_back(15); // #9
        intervals.push_back(6);  // b5/#11
        intervals.push_back(8);  // #5/b13
    }

    // Alterations and adds
    for (const auto& a : chord.alterations) {
        if (a.degree == 0) continue;
        const int base = intervalForDegree(a.degree);
        if (base == 0) continue;
        const int iv = base + a.delta;
        intervals.push_back(iv);
    }

    // Resolve to absolute pitch classes.
    QVector<int> pcs;
    pcs.reserve(intervals.size());
    for (int iv : intervals) pcs.push_back(normalizePc(chord.rootPc + iv));
    uniqueSort(pcs);
    return pcs;
}

QVector<int> ChordDictionary::basicTones(const ChordSymbol& chord) {
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return {};

    QVector<int> intervals;
    intervals.push_back(0);

    switch (chord.quality) {
    case ChordQuality::Minor:
    case ChordQuality::HalfDiminished:
    case ChordQuality::Diminished:
        intervals.push_back(3);
        break;
    case ChordQuality::Sus2:
        intervals.push_back(2);
        break;
    case ChordQuality::Sus4:
        intervals.push_back(5);
        break;
    case ChordQuality::Power5:
        break;
    default:
        intervals.push_back(4);
        break;
    }

    // Fifth
    switch (chord.quality) {
    case ChordQuality::HalfDiminished:
    case ChordQuality::Diminished:
        intervals.push_back(6);
        break;
    case ChordQuality::Augmented:
        intervals.push_back(8);
        break;
    case ChordQuality::Power5:
    default:
        intervals.push_back(7);
        break;
    }

    if (chord.extension >= 7 || chord.seventh != SeventhQuality::None) {
        const int sev = seventhInterval(chord);
        if (sev != 0) intervals.push_back(sev);
    }

    QVector<int> pcs;
    pcs.reserve(intervals.size());
    for (int iv : intervals) pcs.push_back(normalizePc(chord.rootPc + iv));
    uniqueSort(pcs);
    return pcs;
}

int ChordDictionary::bassRootPc(const ChordSymbol& chord) {
    if (chord.bassPc >= 0) return chord.bassPc;
    return chord.rootPc;
}

} // namespace music

