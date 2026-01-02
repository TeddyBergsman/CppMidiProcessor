#pragma once

#include "ireal/IRealTypes.h"

#include <QString>
#include <QVector>

namespace ireal {

// Parses iReal Pro-exported HTML playlists.
// - Extracts all href="irealb://..." and href="irealbook://..." links
// - Decodes percent-encoding
// - For irealb:// records: deobfuscates the token string into Song::progression
// - Returns one or more playlists (most exports contain exactly one)
QVector<Playlist> parseIRealHtmlPlaylists(const QString& html);

} // namespace ireal

