#include "music/Pitch.h"

#include <QChar>

namespace music {
namespace {

static int letterToPc(QChar letter) {
    switch (letter.toUpper().unicode()) {
    case 'C': return 0;
    case 'D': return 2;
    case 'E': return 4;
    case 'F': return 5;
    case 'G': return 7;
    case 'A': return 9;
    case 'B': return 11;
    default:  return -1;
    }
}

static bool isAsciiAccidental(QChar c) { return c == 'b' || c == '#'; }
static bool isUnicodeAccidental(QChar c) { return c == QChar(0x266D) || c == QChar(0x266F); } // ♭ ♯

} // namespace

bool parsePitchClass(QString token, int& pcOut) {
    token = token.trimmed();
    if (token.isEmpty()) return false;

    token.replace(QChar(0x266D), 'b'); // ♭
    token.replace(QChar(0x266F), '#'); // ♯

    const QChar rootLetter = token[0];
    const int base = letterToPc(rootLetter);
    if (base < 0) return false;

    int acc = 0;
    for (int i = 1; i < token.size(); ++i) {
        const QChar c = token[i];
        if (c == 'b') acc -= 1;
        else if (c == '#') acc += 1;
        else break;
    }

    pcOut = normalizePc(base + acc);
    return true;
}

QString spellPitchClass(int pc, bool preferFlats) {
    pc = normalizePc(pc);
    static const char* kSharps[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    static const char* kFlats[12]  = {"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};
    return preferFlats ? QString::fromLatin1(kFlats[pc]) : QString::fromLatin1(kSharps[pc]);
}

} // namespace music

