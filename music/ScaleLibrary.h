#pragma once

#include <QString>
#include <QVector>

#include "music/ChordSymbol.h"

namespace music {

enum class ScaleType {
    Ionian,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Aeolian,
    Locrian,

    MelodicMinor,
    Altered,          // Super-locrian (melodic minor mode 7)
    LydianDominant,   // melodic minor mode 4
    DorianB2,         // melodic minor mode 2
    LocrianNat2,      // melodic minor mode 6

    HarmonicMinor,

    DiminishedWH,
    DiminishedHW,
    WholeTone,

    MajorPentatonic,
    MinorPentatonic,
    Blues,
};

struct Scale {
    ScaleType type;
    QString name;
    QVector<int> intervals; // semitone offsets from tonic (0..11)
};

class ScaleLibrary {
public:
    static const Scale& get(ScaleType type);

    // Best-effort suggested scale types for a chord (initial heuristics).
    static QVector<ScaleType> suggestForChord(const ChordSymbol& chord);
};

} // namespace music

