#include "music/ChordSymbol.h"

#include "music/Pitch.h"

#include <QRegularExpression>

namespace music {
namespace {

static bool startsWithIgnoreCase(const QString& s, const QString& prefix) {
    return s.startsWith(prefix, Qt::CaseInsensitive);
}

static void stripPrefixIgnoreCase(QString& s, const QString& prefix) {
    if (s.startsWith(prefix, Qt::CaseInsensitive)) {
        s.remove(0, prefix.size());
    }
}

static void consumeSpaces(QString& s) {
    while (!s.isEmpty() && s[0].isSpace()) s.remove(0, 1);
}

static bool parseRootToken(QString& s, int& rootPcOut) {
    consumeSpaces(s);
    if (s.isEmpty()) return false;
    const QChar c0 = s[0].toUpper();
    if (c0 < 'A' || c0 > 'G') return false;

    QString token;
    token += c0;
    int i = 1;
    while (i < s.size() && (s[i] == 'b' || s[i] == '#')) {
        token += s[i];
        i++;
    }
    int pc = -1;
    if (!parsePitchClass(token, pc)) return false;
    rootPcOut = pc;
    s.remove(0, i);
    return true;
}

static void parseSus(QString& s, ChordSymbol& out) {
    if (startsWithIgnoreCase(s, "sus2")) { out.quality = ChordQuality::Sus2; s.remove(0, 4); return; }
    if (startsWithIgnoreCase(s, "sus4")) { out.quality = ChordQuality::Sus4; s.remove(0, 4); return; }
    if (startsWithIgnoreCase(s, "sus"))  { out.quality = ChordQuality::Sus4; s.remove(0, 3); return; }
}

static void parseAlterations(QString s, QVector<Alteration>& altsOut, bool addMode) {
    // Matches b9/#9/b5/#5/#11/b13 etc.
    // We run globally to allow multiple matches.
    static const QRegularExpression re(R"(([b#])\s*(5|9|11|13))", QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        Alteration a;
        a.add = addMode;
        a.delta = (m.captured(1).toLower() == "b") ? -1 : 1;
        a.degree = m.captured(2).toInt();
        altsOut.push_back(a);
    }
}

static int parseHighestExtension(const QString& s) {
    // Prefer 13 > 11 > 9 > 7 > 6.
    static const QRegularExpression re13(R"(13)");
    static const QRegularExpression re11(R"(11)");
    static const QRegularExpression re9(R"((?<!\d)9(?!\d))"); // standalone 9
    static const QRegularExpression re7(R"((?<!\d)7(?!\d))");
    static const QRegularExpression re6(R"((?<!\d)6(?!\d))");
    if (re13.match(s).hasMatch()) return 13;
    if (re11.match(s).hasMatch()) return 11;
    if (re9.match(s).hasMatch()) return 9;
    if (re7.match(s).hasMatch()) return 7;
    if (re6.match(s).hasMatch()) return 6;
    return 0;
}

static void decideSeventh(const QString& sRaw, int ext, ChordSymbol& out) {
    // IMPORTANT: this must look at the *raw* post-root tail, before we strip "maj"/"m" tokens.
    // Otherwise "Cmaj7" turns into tail="7" and would be mis-classified as a dominant (minor7).

    // Diminished seventh (°7)
    if (sRaw.contains("dim7", Qt::CaseInsensitive) || sRaw.contains("o7") || sRaw.contains(QChar(0x00B0)) /*°*/ ) {
        out.seventh = SeventhQuality::Dim7;
        return;
    }

    // Explicit major seventh markers (and maj9/maj11/maj13 imply a major seventh as well).
    const bool hasMajMarker =
        sRaw.contains("maj", Qt::CaseInsensitive) ||
        sRaw.contains("ma7", Qt::CaseInsensitive) ||
        sRaw.contains("maj7", Qt::CaseInsensitive) ||
        sRaw.contains("M7");
    if (hasMajMarker && ext >= 7) {
        out.seventh = SeventhQuality::Major7;
        return;
    }

    // If any 7/9/11/13 present, default to minor seventh unless specified above.
    // BUT: do not infer a 7th for add-chords (Cadd9, C(add11), etc.) or 6/9 chords.
    if (ext >= 7) {
        const bool isAddChord = sRaw.contains("add", Qt::CaseInsensitive);
        if (!isAddChord) out.seventh = SeventhQuality::Minor7;
    }
}

} // namespace

QString normalizeChordText(QString chordText) {
    QString s = chordText.trimmed();
    if (s.isEmpty()) return s;

    // Normalize some iReal typography to parsing-friendly tokens.
    s.replace(QChar(0x266D), 'b'); // ♭
    s.replace(QChar(0x266F), '#'); // ♯

    // Minor marker in your rendered chords is often an en dash (–) after the root.
    // Replace all en dashes with 'm' (minor). This is safe because chord symbols rarely use it otherwise.
    s.replace(QChar(0x2013), 'm'); // –

    // iReal major symbol Δ
    s.replace(QChar(0x0394), "maj"); // Δ
    // iReal legacy '^' major marker (if any)
    s.replace('^', "maj");

    // Common whitespace noise
    s.replace(QRegularExpression("\\s+"), "");

    // iReal often embeds passing/sub chords in parentheses, e.g. "Dø7(C-Δ7/B)".
    // For our current harmony model, we treat the *main* chord as the portion outside parentheses.
    // (The embedded chord can be handled later as an explicit sub-beat harmony model.)
    while (true) {
        const int l = s.indexOf('(');
        if (l < 0) break;
        const int r = s.indexOf(')', l + 1);
        if (r < 0) { s = s.left(l); break; }
        s.remove(l, (r - l) + 1);
    }
    while (true) {
        const int l = s.indexOf('[');
        if (l < 0) break;
        const int r = s.indexOf(']', l + 1);
        if (r < 0) { s = s.left(l); break; }
        s.remove(l, (r - l) + 1);
    }
    // Clean any stray unmatched brackets.
    s.remove(')');
    s.remove('(');
    s.remove(']');
    s.remove('[');
    return s;
}

bool parseChordSymbol(const QString& chordText, ChordSymbol& out) {
    out = ChordSymbol{};
    out.originalText = chordText;

    QString s = normalizeChordText(chordText);
    if (s.isEmpty()) return false;

    // Special tokens
    if (s.compare("x", Qt::CaseInsensitive) == 0) { out.placeholder = true; return true; }
    if (s.compare("NC", Qt::CaseInsensitive) == 0 || s.compare("N.C.", Qt::CaseInsensitive) == 0 || s.compare("N.C", Qt::CaseInsensitive) == 0) {
        out.noChord = true;
        return true;
    }

    // Split slash bass if present.
    // IMPORTANT: only treat '/' as a slash-bass delimiter if the RHS parses as a pitch class.
    // This avoids mis-parsing common chord spellings like "6/9" as an inversion.
    QString pre = s;
    QString slashBass;
    const int slashIdx = s.indexOf('/');
    if (slashIdx >= 0) {
        const QString rhs = s.mid(slashIdx + 1);
        int bassPc = -1;
        if (parsePitchClass(rhs, bassPc)) {
            pre = s.left(slashIdx);
            slashBass = rhs;
            out.bassPc = bassPc;
        }
    }

    QString head = pre;
    if (!parseRootToken(head, out.rootPc)) return false;
    out.quality = ChordQuality::Major; // default triad

    // Preserve the raw tail (post-root) for correct seventh/extension detection.
    const QString tailRaw = head;
    const bool hadMajMarker = head.contains("maj", Qt::CaseInsensitive) || head.contains("M7");
    const bool hadMinorMarker = head.startsWith("m", Qt::CaseInsensitive) || head.startsWith("min", Qt::CaseInsensitive) || head.startsWith("-");

    // (Slash bass already handled above)

    // Quality markers and common aliases.
    // Half-diminished / diminished symbols appear right after root in iReal-pretty rendering.
    if (!head.isEmpty() && head[0] == QChar(0x00F8) /*ø*/) {
        out.quality = ChordQuality::HalfDiminished;
        head.remove(0, 1);
    } else if (!head.isEmpty() && head[0] == QChar(0x00B0) /*°*/) {
        out.quality = ChordQuality::Diminished;
        head.remove(0, 1);
    }

    // Major/minor/aug/dim textual forms
    if (startsWithIgnoreCase(head, "maj")) { out.quality = ChordQuality::Major; stripPrefixIgnoreCase(head, "maj"); }
    else if (startsWithIgnoreCase(head, "min")) { out.quality = ChordQuality::Minor; stripPrefixIgnoreCase(head, "min"); }
    else if (startsWithIgnoreCase(head, "m")) { out.quality = ChordQuality::Minor; head.remove(0, 1); }
    else if (startsWithIgnoreCase(head, "-")) { out.quality = ChordQuality::Minor; head.remove(0, 1); } // just in case

    if (startsWithIgnoreCase(head, "dim")) { out.quality = ChordQuality::Diminished; stripPrefixIgnoreCase(head, "dim"); }
    if (startsWithIgnoreCase(head, "aug")) { out.quality = ChordQuality::Augmented; stripPrefixIgnoreCase(head, "aug"); }
    if (startsWithIgnoreCase(head, "+"))   { out.quality = ChordQuality::Augmented; head.remove(0, 1); }

    // m7b5 is a common ASCII half-diminished form
    if (startsWithIgnoreCase(head, "7b5") && out.quality == ChordQuality::Minor) {
        // Don't special-case; it becomes minor with altered 5 unless explicitly ø.
    }
    if (head.contains("m7b5", Qt::CaseInsensitive) || head.contains("min7b5", Qt::CaseInsensitive) || head.contains("hdim", Qt::CaseInsensitive)) {
        out.quality = ChordQuality::HalfDiminished;
    }

    // Sus chords override basic triad quality
    parseSus(head, out);

    // Power chord
    if (startsWithIgnoreCase(head, "5")) { out.quality = ChordQuality::Power5; }

    // "alt" marker
    if (head.contains("alt", Qt::CaseInsensitive)) out.alt = true;

    // Extensions / sevenths
    out.extension = parseHighestExtension(tailRaw);

    // Special-case 6/9 and 69: treat as a 6-chord with an added 9 (no implied 7).
    // We preserve the 9 as an "add" alteration so downstream voicing code can include it literally.
    {
        static const QRegularExpression reSixNine(R"((?i)(6/9|69|6\(9\)))");
        const bool sixNine = reSixNine.match(tailRaw).hasMatch();
        if (sixNine) {
            out.extension = 6;
            out.seventh = SeventhQuality::None;
            bool haveAdd9 = false;
            for (const auto& a : out.alterations) {
                if (a.add && a.degree == 9 && a.delta == 0) { haveAdd9 = true; break; }
            }
            if (!haveAdd9) {
                Alteration a;
                a.add = true;
                a.degree = 9;
                a.delta = 0;
                out.alterations.push_back(a);
            }
        }
    }

    // Decide 7th. Do NOT infer a 7th for add-chords (add9/add11/add13) unless explicitly present.
    decideSeventh(tailRaw, out.extension, out);

    // If the symbol looks like a plain "C7" (no explicit maj/min and has 7), treat as dominant.
    if (out.extension >= 7 && out.quality == ChordQuality::Major && !hadMajMarker && !hadMinorMarker) {
        // If it was explicitly maj7 we'll keep Major + Major7; otherwise it's dominant.
        if (out.seventh == SeventhQuality::Minor7) out.quality = ChordQuality::Dominant;
    }

    // Alterations: b9/#9 etc; add9 variants.
    parseAlterations(head, out.alterations, /*addMode*/false);
    static const QRegularExpression reAdd(R"(add\s*(9|11|13))", QRegularExpression::CaseInsensitiveOption);
    auto addIt = reAdd.globalMatch(head);
    while (addIt.hasNext()) {
        const auto m = addIt.next();
        Alteration a;
        a.add = true;
        a.delta = 0;
        a.degree = m.captured(1).toInt();
        out.alterations.push_back(a);
    }

    return true;
}

} // namespace music

