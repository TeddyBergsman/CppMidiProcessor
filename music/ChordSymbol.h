#pragma once

#include <QString>
#include <QVector>

namespace music {

enum class ChordQuality {
    Unknown = 0,
    Major,
    Minor,
    Dominant,
    HalfDiminished,
    Diminished,
    Augmented,
    Sus2,
    Sus4,
    Power5,
};

enum class SeventhQuality {
    None = 0,
    Minor7,
    Major7,
    Dim7,
};

struct Alteration {
    // degree is one of 5, 9, 11, 13.
    int degree = 0;
    // delta semitones relative to the "natural" extension degree:
    //  - b9 => -1, #9 => +1, etc.
    int delta = 0;
    // whether it's an "add" (add9) rather than an extension (9/11/13).
    bool add = false;
};

struct ChordSymbol {
    QString originalText;

    bool placeholder = false; // "x"
    bool noChord = false;     // "N.C."

    int rootPc = -1; // 0..11
    int bassPc = -1; // optional slash-bass, 0..11

    ChordQuality quality = ChordQuality::Unknown;
    SeventhQuality seventh = SeventhQuality::None;

    // Highest extension present (0, 6, 7, 9, 11, 13)
    int extension = 0;
    bool alt = false; // "alt"

    QVector<Alteration> alterations;
};

// Normalizes a chord string into a parser-friendly ASCII-ish form.
// - Converts ♭/♯ → b/#, Δ → "maj", en-dash minor marker → "m"
// - Keeps ø/° (half-diminished/diminished) for parsing
QString normalizeChordText(QString chordText);

// Parses a chord string as displayed in the chart grid into a structured chord symbol.
// Returns true if it looks like a chord or special token (x, N.C.).
// Returns false if it cannot be parsed (root not recognized).
bool parseChordSymbol(const QString& chordText, ChordSymbol& out);

} // namespace music

