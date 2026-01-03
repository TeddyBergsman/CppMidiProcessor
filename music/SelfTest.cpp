#include "music/SelfTest.h"

#include "music/ChordDictionary.h"
#include "music/ChordSymbol.h"
#include "music/Pitch.h"

#include <QtGlobal>
#include <QSet>

namespace music {
namespace {

static QSet<int> toSet(const QVector<int>& pcs) {
    QSet<int> s;
    for (int v : pcs) s.insert(normalizePc(v));
    return s;
}

static void expect(bool cond, const char* msg) {
    if (!cond) {
        qWarning("music self-test FAILED: %s", msg);
    }
}

static void testChord(const QString& txt, int rootPc, ChordQuality q, SeventhQuality sev, int ext = 0) {
    ChordSymbol c;
    const bool ok = parseChordSymbol(txt, c);
    expect(ok, "parseChordSymbol returned false");
    if (c.noChord || c.placeholder) return;
    expect(c.rootPc == rootPc, "root pitch-class mismatch");
    expect(c.quality == q, "quality mismatch");
    if (sev != SeventhQuality::None) expect(c.seventh == sev, "seventh mismatch");
    if (ext != 0) expect(c.extension == ext, "extension mismatch");
}

} // namespace

void runMusicSelfTests() {
#ifndef NDEBUG
    // Pitch parsing
    {
        int pc = -1;
        expect(parsePitchClass("E♭", pc) && pc == 3, "parsePitchClass E♭");
        expect(parsePitchClass("F#", pc) && pc == 6, "parsePitchClass F#");
        expect(parsePitchClass("Bb", pc) && pc == 10, "parsePitchClass Bb");
    }

    // Chord parsing (iReal glyphs + plain forms)
    testChord("F–7", 5, ChordQuality::Minor, SeventhQuality::Minor7, 7);
    testChord("Bø7", 11, ChordQuality::HalfDiminished, SeventhQuality::Minor7, 7);
    testChord("CΔ7", 0, ChordQuality::Major, SeventhQuality::Major7, 7);
    // Contains a 9th alteration, so highest extension is 9.
    testChord("E♭7#9", 3, ChordQuality::Dominant, SeventhQuality::Minor7, 9);
    testChord("Emaj7", 4, ChordQuality::Major, SeventhQuality::Major7, 7);
    testChord("Bm7", 11, ChordQuality::Minor, SeventhQuality::Minor7, 7);
    testChord("G7alt", 7, ChordQuality::Dominant, SeventhQuality::Minor7, 7);
    {
        ChordSymbol c;
        expect(parseChordSymbol("C/E", c), "slash chord parse");
        expect(c.rootPc == 0 && c.bassPc == 4, "slash chord bass pc");
    }
    {
        ChordSymbol c;
        expect(parseChordSymbol("N.C.", c) && c.noChord, "N.C. parse");
        expect(parseChordSymbol("x", c) && c.placeholder, "x placeholder parse");
    }

    // Chord dictionary sanity
    {
        ChordSymbol c;
        expect(parseChordSymbol("Bø7", c), "Bø7 parse for dictionary");
        const auto basics = toSet(ChordDictionary::basicTones(c));
        // Bø7 = B, D, F, A (pcs 11,2,5,9)
        expect(basics.contains(11) && basics.contains(2) && basics.contains(5) && basics.contains(9), "Bø7 basic tones set");
    }
#endif
}

} // namespace music

