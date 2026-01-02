#pragma once

#include <QString>
#include <QVector>

namespace ireal {

// Parsed representation of one iReal Pro song record (from irealb:// or irealbook://).
struct Song {
    QString title;
    QString composer;
    QString style;
    QString key;          // e.g. "Eb", "G-", "Bb"

    // irealb:// (irealpro variant) extra fields
    QString actualStyle;  // sometimes empty
    int actualTempoBpm = 0;
    int actualRepeats = 0;
    int actualKey = 0;    // semitone index or 0/empty in exports; kept for future

    // The decoded progression/token string (for irealb:// this is deobfuscated).
    // For irealbook:// this is the raw progression string.
    QString progression;
};

struct Playlist {
    QString name;
    QVector<Song> songs;
};

} // namespace ireal

