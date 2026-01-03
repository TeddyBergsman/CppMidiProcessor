#pragma once

#include <QVector>

#include "music/ChordSymbol.h"

namespace music {

// Utilities to expand a ChordSymbol to pitch-class intervals / chord tones.
class ChordDictionary {
public:
    // Returns pitch classes for chord tones (including extensions/alterations) as absolute pitch classes (0..11).
    static QVector<int> chordPitchClasses(const ChordSymbol& chord);

    // Returns "basic" chord tones useful for bass: root, 3rd, 5th, 7th (as pitch classes).
    // Missing tones are omitted.
    static QVector<int> basicTones(const ChordSymbol& chord);

    // Returns the root pitch class for bass purposes (slash bass if present, else root).
    static int bassRootPc(const ChordSymbol& chord);
};

} // namespace music

