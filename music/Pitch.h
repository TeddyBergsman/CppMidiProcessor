#pragma once

#include <QString>

namespace music {

// Normalized pitch class: 0=C, 1=C#/Db, ... 11=B.
inline int normalizePc(int pc) {
    pc %= 12;
    if (pc < 0) pc += 12;
    return pc;
}

// Parse a pitch name like "C", "Eb", "F#", "B♭", "C♯" into a pitch class.
// Accepts ASCII and Unicode accidentals (♭/♯).
// Returns true on success.
bool parsePitchClass(QString token, int& pcOut);

// Spells a pitch class using either flats or sharps.
// Returns a short name like "Eb" or "D#".
QString spellPitchClass(int pc, bool preferFlats);

} // namespace music

