#include "ireal/HtmlPlaylistParser.h"

#include <QFile>
#include <QRegularExpression>
#include <QUrl>

#include "ireal/IRealbCodec.h"

namespace ireal {
namespace {

static QString percentDecode(const QString& s) {
    // iReal exports percent-encoded URLs inside HTML.
    // Use QByteArray path to correctly decode %xx sequences.
    return QUrl::fromPercentEncoding(s.toUtf8());
}

static QString extractFirstIRealHref(const QString& html) {
    // Matches href="irealb://...." or href="irealbook://...."
    // iReal export uses double quotes.
    static const QRegularExpression re(R"(href\s*=\s*\"(ireal(?:b|book)://[^\"]+)\")",
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(html);
    if (!m.hasMatch()) return {};
    return m.captured(1);
}

static QVector<QString> splitKeepEmpty(const QString& s, const QString& sep) {
    return s.split(sep, Qt::KeepEmptyParts);
}

static bool parseIrealbSongRecord(const QString& record, Song& outSong) {
    // irealb record format (irealpro variant) is 10 '='-separated fields.
    const QVector<QString> fields = splitKeepEmpty(record, "=");
    if (fields.size() != 10) return false;

    outSong.title = fields[0];
    outSong.composer = fields[1];
    // fields[2] is unused "a2"
    outSong.style = fields[3];
    outSong.key = fields[4];

    // fields[5] actual_key (sometimes empty or numeric)
    bool okKey = false;
    outSong.actualKey = fields[5].isEmpty() ? 0 : fields[5].toInt(&okKey);
    if (!okKey && !fields[5].isEmpty()) outSong.actualKey = 0;

    const QString rawTokens = fields[6];
    outSong.progression = deobfuscateIRealbTokens(rawTokens);

    outSong.actualStyle = fields[7];
    bool okTempo = false;
    outSong.actualTempoBpm = fields[8].toInt(&okTempo);
    if (!okTempo) outSong.actualTempoBpm = 0;
    bool okRep = false;
    outSong.actualRepeats = fields[9].toInt(&okRep);
    if (!okRep) outSong.actualRepeats = 0;

    return true;
}

static bool parseIrealbookSongRecord(const QString& record, Song& outSong) {
    // irealbook record format is 6 '='-separated fields.
    const QVector<QString> fields = splitKeepEmpty(record, "=");
    if (fields.size() != 6) return false;

    outSong.title = fields[0];
    outSong.composer = fields[1];
    outSong.style = fields[2];
    QString a3 = fields[3]; // often "n"
    outSong.key = fields[4];
    outSong.progression = fields[5];

    // Some irealbook exports swap key and a3 (reference behavior):
    if (outSong.key == "n") {
        outSong.key = a3;
        // a3 becomes "n" (unused in our struct)
    }

    return true;
}

static Playlist parseIRealUriToPlaylist(const QString& uriDecoded) {
    Playlist pl;
    if (uriDecoded.startsWith("irealb://", Qt::CaseInsensitive)) {
        QString data = uriDecoded.mid(QString("irealb://").size());
        const QVector<QString> parts = splitKeepEmpty(data, "===");
        if (parts.isEmpty()) return pl;

        // Last part is playlist name if there are multiple songs.
        QVector<QString> songRecords = parts;
        if (parts.size() > 1) {
            pl.name = songRecords.takeLast();
        }

        for (const QString& rec : songRecords) {
            Song s;
            if (parseIrealbSongRecord(rec, s)) {
                pl.songs.push_back(s);
            }
        }
        return pl;
    }

    if (uriDecoded.startsWith("irealbook://", Qt::CaseInsensitive)) {
        QString data = uriDecoded.mid(QString("irealbook://").size());

        // irealbook playlists are not delimited by ===; they are a long '=' stream.
        QVector<QString> fields = splitKeepEmpty(data, "=");
        QVector<QString> songRecords;

        while (fields.size() >= 6) {
            // join the next 6 fields with '=' (preserving empties)
            QString rec = fields[0];
            for (int i = 1; i < 6; ++i) rec += "=" + fields[i];
            songRecords.push_back(rec);
            fields.erase(fields.begin(), fields.begin() + 6);
        }

        if (!fields.isEmpty()) {
            // Remaining single field is playlist name (may be empty)
            pl.name = fields[0];
        }

        for (const QString& rec : songRecords) {
            Song s;
            if (parseIrealbookSongRecord(rec, s)) {
                pl.songs.push_back(s);
            }
        }
        return pl;
    }

    return pl;
}

} // namespace

Playlist HtmlPlaylistParser::parseFile(const QString& htmlPath) {
    QFile f(htmlPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString html = QString::fromUtf8(f.readAll());
    const QString href = extractFirstIRealHref(html);
    if (href.isEmpty()) return {};

    const QString decoded = percentDecode(href);
    return parseIRealUriToPlaylist(decoded);
}

} // namespace ireal

