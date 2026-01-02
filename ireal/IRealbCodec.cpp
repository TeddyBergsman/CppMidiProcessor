#include "ireal/IRealbCodec.h"

#include <QString>

namespace ireal {
namespace {

static QString hussle(const QString& in) {
    // Implements the symmetric 50-character shuffling used by iReal Pro token strings.
    // The transformation is its own inverse.
    QString string = in;
    QString result;
    result.reserve(in.size());

    while (string.size() > 50) {
        const QString segment = string.left(50);
        string.remove(0, 50);

        if (string.size() < 2) {
            result += segment;
            continue;
        }

        // Equivalent to the reference:
        // reverse(substr(45,5)) + substr(5,5) + reverse(substr(26,14)) + substr(24,2)
        // + reverse(substr(10,14)) + substr(40,5) + reverse(substr(0,5))
        auto rev = [](const QString& s) {
            QString r = s;
            std::reverse(r.begin(), r.end());
            return r;
        };

        result += rev(segment.mid(45, 5));
        result += segment.mid(5, 5);
        result += rev(segment.mid(26, 14));
        result += segment.mid(24, 2);
        result += rev(segment.mid(10, 14));
        result += segment.mid(40, 5);
        result += rev(segment.mid(0, 5));
    }

    result += string;
    return result;
}

} // namespace

QString deobfuscateIRealbTokens(const QString& rawTokenString) {
    static const QString kMagic = "1r34LbKcu7";
    if (!rawTokenString.startsWith(kMagic)) {
        return rawTokenString; // best-effort: already deobfuscated or unsupported variant
    }

    QString t = rawTokenString.mid(kMagic.size());
    t = hussle(t);

    // NOTE: order is important (matches reference).
    t.replace("XyQ", "   ");
    t.replace("LZ", " |");
    t.replace("Kcl", "| x");

    return t;
}

} // namespace ireal

