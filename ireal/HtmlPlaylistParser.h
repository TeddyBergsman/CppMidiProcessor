#pragma once

#include <QString>

#include "ireal/IRealTypes.h"

namespace ireal {

// Parses iReal Pro-exported .html playlists, extracting irealb:// or irealbook:// links.
class HtmlPlaylistParser {
public:
    // Parse the first playlist link found in the file.
    // Throws no exceptions; on failure returns empty playlist (name empty, songs empty).
    static Playlist parseFile(const QString& htmlPath);
};

} // namespace ireal

