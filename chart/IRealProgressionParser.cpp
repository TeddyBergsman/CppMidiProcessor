#include "chart/IRealProgressionParser.h"

#include <QChar>
#include <QRegularExpression>

namespace chart {
namespace {

static bool isBarToken(QChar c) {
    return c == '|' || c == '[' || c == ']' || c == '{' || c == '}' || c == 'Z';
}

static bool isControlToken(QChar c) {
    // iReal decoded token streams use some control characters that are not chords:
    // - 'Y' often appears as a layout control near line boundaries
    return c == 'Y';
}

static bool isChordChar(QChar c) {
    // iReal chords are ASCII-ish with some symbols; we treat “token until space or bar token” as chord.
    return !c.isSpace() && !isBarToken(c) && !isControlToken(c);
}

static void ensureCellCount(Bar& bar, int count) {
    while (bar.cells.size() < count) bar.cells.push_back(Cell{});
}

static void pushBar(Line& line, Bar& bar) {
    // Normalize to 4 cells to keep rendering stable
    ensureCellCount(bar, 4);
    line.bars.push_back(bar);
    bar = Bar{};
}

static void pushLine(ChartModel& model, Line& line) {
    if (line.bars.isEmpty()) return;

    auto barIsVisuallyEmpty = [](const Bar& b) {
        if (b.endingStart > 0 || b.endingEnd > 0) return false;
        if (!b.annotation.trimmed().isEmpty()) return false;
        // Pure barline-only bars (even with repeat/final markers) should not be rendered as standalone measures.
        // In iReal, such markers belong to adjacent bars; a bar with no chord/rest content is effectively padding.
        for (const auto& c : b.cells) {
            if (!c.chord.trimmed().isEmpty()) return false;
        }
        return true;
    };

    // Trim leading/trailing padding bars introduced by token-string alignment.
    while (!line.bars.isEmpty() && barIsVisuallyEmpty(line.bars.first())) {
        line.bars.removeFirst();
    }
    while (!line.bars.isEmpty() && barIsVisuallyEmpty(line.bars.last())) {
        line.bars.removeLast();
    }

    if (!line.bars.isEmpty()) {
        model.lines.push_back(line);
    }
    line = Line{};
}

} // namespace

ChartModel parseIRealProgression(const QString& decodedProgression) {
    ChartModel model;
    Line currentLine;
    Bar currentBar;

    int cellInBar = 0;
    QString pendingSection;

    // Normalize newlines to spaces for the tokenizer.
    QString s = decodedProgression;
    s.replace('\n', ' ');
    s.replace('\r', ' ');
    int i = 0;

    // Default: assume 4 bars per line.
    const int barsPerLine = 4;
    int activeEnding = 0; // N1/N2 currently active ending number

    auto barIsMeaningful = [&](const Bar& bar, int filledCells) -> bool {
        // Only chord/rest/annotation/ending content should create a new bar.
        // Barline-only "bars" are typically padding artifacts in decoded streams.
        if (filledCells > 0) return true;
        if (!bar.annotation.trimmed().isEmpty()) return true;
        if (bar.endingStart > 0 || bar.endingEnd > 0) return true;
        for (const auto& c : bar.cells) {
            if (!c.chord.trimmed().isEmpty()) return true;
        }
        return false;
    };

    auto applyDefaultHarmonicRhythm = [&](Bar& bar) {
        // iReal convention in 4/4: if there are exactly 2 chords in a bar and no explicit spacing,
        // place them on beats 1 and 3 (cells 0 and 2), not 1 and 2.
        if (model.timeSigNum != 4 || model.timeSigDen != 4) return;
        ensureCellCount(bar, 4);

        QVector<int> idx;
        idx.reserve(4);
        for (int k = 0; k < bar.cells.size(); ++k) {
            const QString c = bar.cells[k].chord.trimmed();
            if (!c.isEmpty()) {
                idx.push_back(k);
            }
        }
        if (idx.size() != 2) return;

        // Don't shift repeat placeholders.
        if (bar.cells[idx[0]].chord.trimmed() == "x" || bar.cells[idx[1]].chord.trimmed() == "x") return;

        // If chords are in cells 0 and 1 and cells 2 and 3 are empty, shift second to cell 2.
        if (idx[0] == 0 && idx[1] == 1) {
            if (bar.cells[2].chord.trimmed().isEmpty() && bar.cells[3].chord.trimmed().isEmpty()) {
                bar.cells[2] = bar.cells[1];
                bar.cells[1] = Cell{};
            }
        }
    };

    auto finalizeBar = [&]() {
        if (barIsMeaningful(currentBar, cellInBar)) {
            ensureCellCount(currentBar, cellInBar);
            applyDefaultHarmonicRhythm(currentBar);
            pushBar(currentLine, currentBar);
            cellInBar = 0;
            if (currentLine.bars.size() >= barsPerLine) {
                pushLine(model, currentLine);
            }
        }
    };

    auto barHasChordContent = [&](const Bar& bar, int filledCells) -> bool {
        if (filledCells > 0) return true;
        for (const auto& cell : bar.cells) {
            if (!cell.chord.trimmed().isEmpty()) return true;
        }
        return false;
    };

    auto hasChordAhead = [&](int fromIndex) -> bool {
        // Look ahead for any upcoming chord token before end-of-stream.
        // This is used to decide whether certain barline markers belong to a new (upcoming) bar
        // or are dangling end-markers that should attach to the previous bar.
        int j = fromIndex + 1;
        while (j < s.size()) {
            const QChar cc = s[j];
            if (cc.isSpace() || cc == ',' || isBarToken(cc) || isControlToken(cc)) {
                j += 1;
                continue;
            }
            if (cc == '<') {
                const int close = s.indexOf('>', j + 1);
                if (close > j) { j = close + 1; continue; }
                j += 1;
                continue;
            }
            if (isChordChar(cc)) return true;
            j += 1;
        }
        return false;
    };

    auto lastEmittedBar = [&]() -> Bar* {
        if (!currentLine.bars.isEmpty()) return &currentLine.bars.last();
        if (!model.lines.isEmpty() && !model.lines.last().bars.isEmpty()) return &model.lines.last().bars.last();
        return nullptr;
    };

    while (i < s.size()) {
        const QChar c = s[i];

        // Angle-bracket annotations can contain spaces (e.g. "<D.C. a' Fine>").
        // Treat them as a single token and do NOT consume grid cells.
        if (c == '<') {
            const int close = s.indexOf('>', i + 1);
            if (close > i) {
                QString ann = s.mid(i + 1, close - i - 1).trimmed();
                // Normalize common iReal shorthand
                ann.replace("a'", "al");
                ann.replace("a’", "al");
                ann.replace("a´", "al");
                ann.replace(QRegularExpression("\\s+"), " ");

                if (!ann.isEmpty()) {
                    if (ann.startsWith("D.C.", Qt::CaseInsensitive) || ann.startsWith("D.S.", Qt::CaseInsensitive)) {
                        model.footerText = ann;
                    } else if (ann.compare("Fine", Qt::CaseInsensitive) == 0) {
                        currentBar.annotation = "Fine";
                    } else {
                        currentBar.annotation = ann;
                    }
                }

                i = close + 1;
                continue;
            }
            // If malformed, skip '<'
            i += 1;
            continue;
        }

        // Standalone comma is a layout hint in many exports; ignore it.
        // Chord-list commas are consumed as part of chord tokens below.
        if (c == ',') { i += 1; continue; }

        // Section markers: *A, *B... (ignore lower-case control tokens like *i)
        if (c == '*' && i + 1 < s.size()) {
            const QChar sec = s[i + 1];
            if (sec.isUpper()) {
                // Section markers start a new line in iReal.
                // If we are mid-line or mid-bar, flush first so the section label does not get applied retroactively.
                // IMPORTANT: do NOT flush a bar that only contains a leading barline marker (e.g. "{") before a section.
                // iReal commonly starts with "{*A..." and the "{" belongs to the first bar of section A, not an empty bar.
                if (barHasChordContent(currentBar, cellInBar)) {
                    finalizeBar();
                }
                if (!currentLine.bars.isEmpty()) {
                    pushLine(model, currentLine);
                }
                currentLine.sectionLabel = QString(sec);
                pendingSection.clear();
                i += 2;
                continue;
            }
        }

        // Time signature: T44, T34, T68 etc.
        if (c == 'T' && i + 2 < s.size()) {
            const QChar n1 = s[i + 1];
            const QChar d1 = s[i + 2];
            if (n1.isDigit() && d1.isDigit()) {
                model.timeSigNum = n1.digitValue();
                model.timeSigDen = d1.digitValue();
                i += 3;
                continue;
            }
        }

        // Barlines and structural tokens (do not consume cells)
        if (isBarToken(c)) {
            // Do not apply pending sections mid-line; section markers already force a new line above.
            pendingSection.clear();

            // These tokens imply a bar boundary; apply to current bar if active, otherwise to the next bar.
            if (c == '{' || c == '[') {
                if (!currentBar.cells.isEmpty() || cellInBar > 0) {
                    finalizeBar();
                }
                // Special case: iReal streams can end with a dangling '[' to indicate an end-of-chart double barline.
                // If there is no chord content ahead, attach this marker to the previous real bar instead of creating
                // a barline-only bar (which we intentionally suppress).
                if (c == '[' && currentBar.cells.isEmpty() && cellInBar == 0 && !hasChordAhead(i)) {
                    if (Bar* last = lastEmittedBar()) {
                        last->barlineRight += ']';
                    } else {
                        currentBar.barlineLeft += c;
                    }
                } else {
                    currentBar.barlineLeft += c;
                }
            } else if (c == '}' || c == ']' || c == 'Z') {
                // Closing barline tokens should attach to the last *real* bar, not create a new empty bar.
                // iReal decoded streams often end with a trailing ']' / 'Z' without additional chord content.
                const bool curHasContent = barHasChordContent(currentBar, cellInBar) || !currentBar.annotation.trimmed().isEmpty()
                    || currentBar.endingStart > 0 || currentBar.endingEnd > 0;
                if (!curHasContent) {
                    Bar* last = lastEmittedBar();
                    if (!last) {
                        // Nothing to attach to; fall back to applying to current bar.
                        currentBar.barlineRight += c;
                        finalizeBar();
                        if (activeEnding != 0) activeEnding = 0;
                        i += 1;
                        continue;
                    }
                    // Ending typically closes at a repeat-end or section end.
                    if (activeEnding != 0) {
                        last->endingEnd = activeEnding;
                    }
                    last->barlineRight += c;
                    if (activeEnding != 0) {
                        activeEnding = 0;
                    }
                } else {
                    // Ending typically closes at a repeat-end or section end.
                    if (activeEnding != 0) {
                        currentBar.endingEnd = activeEnding;
                    }
                    currentBar.barlineRight += c;
                    finalizeBar();
                    if (activeEnding != 0) {
                        activeEnding = 0;
                    }
                }
            } else if (c == '|') {
                if (!currentBar.cells.isEmpty() || cellInBar > 0) {
                    finalizeBar();
                } else {
                    // A dangling '|' after a finalized bar belongs to the previous bar's right edge.
                    if (Bar* last = lastEmittedBar()) {
                        last->barlineRight += c;
                    } else {
                        currentBar.barlineLeft += c;
                    }
                }
            }

            i += 1;
            continue;
        }

        // Explicit line breaks / layout controls (do not consume cells)
        if (isControlToken(c)) {
            // Flush current bar and line.
            finalizeBar();
            pushLine(model, currentLine);
            i += 1;
            continue;
        }

        // Spaces: cell boundaries and empty cells.
        if (c.isSpace()) {
            int run = 0;
            while (i < s.size() && s[i].isSpace()) {
                run++;
                i++;
            }

            // Single space separates chord tokens (no cell advance). Runs 2-3 represent empty cells.
            // This matches the common iReal export behavior after deobfuscation:
            // - chords are separated by one space
            // - empty cells are encoded as 2-3 spaces in a row
            if (run >= 2) {
                // each extra “slot” in the grid is one cell; in practice iReal uses 3 spaces for one empty cell,
                // but we keep it tolerant and treat >=2 as advancing one cell, then consume further in chunks of 3.
                int empties = 1;
                if (run >= 5) {
                    // approximate additional empties for long runs
                    empties = 1 + (run - 2) / 3;
                }
                for (int e = 0; e < empties; ++e) {
                    ensureCellCount(currentBar, cellInBar + 1);
                    // leave chord empty
                    cellInBar++;
                    if (cellInBar >= 4) {
                        finalizeBar();
                    }
                }
            }
            continue;
        }

        // Chord token: read until space or bar token
        if (isChordChar(c)) {
            int start = i;
            while (i < s.size() && (isChordChar(s[i]) || s[i] == ',')) i++;
            QString chordToken = s.mid(start, i - start);

            // Apply section label if this is the first thing on a line.
            if (!pendingSection.isEmpty() && currentLine.sectionLabel.isEmpty()) {
                currentLine.sectionLabel = pendingSection;
                pendingSection.clear();
            }

            // Endings: N1 / N2 prefix directly attached to the chord (e.g. N1A-7, N2F6).
            if (chordToken.size() >= 3 && chordToken[0] == 'N' && chordToken[1].isDigit()) {
                const int n = chordToken[1].digitValue();
                if (n > 0) {
                    currentBar.endingStart = n;
                    activeEnding = n;
                }
                chordToken.remove(0, 2);
            }

            // Special token: 's' prefix indicates a list of chords separated by commas that should occupy successive cells.
            // Example from your chart: sEb^,Ab7,G-7,C7,
            auto normalizeChord = [](QString t) -> QString {
                t = t.trimmed();
                // Some exports leave trailing commas on chord tokens; drop them.
                while (t.endsWith(',')) {
                    t.chop(1);
                    t = t.trimmed();
                }
                // Strip iReal control prefixes like *i, *v, *k that precede a chord.
                // Keep section markers (*A, *B...) handled earlier.
                while (t.size() >= 2 && t[0] == '*' && t[1].isLower()) {
                    t.remove(0, 2);
                    t = t.trimmed();
                }
                // Strip additional single-letter layout prefixes found in exports.
                // - 'U' sometimes prefixes the first chord of a bar (e.g. UEb6) as a layout control.
                if (t.size() >= 2 && t[0] == 'U' && t[1].isLetter()) {
                    t.remove(0, 1);
                }
                if (t.startsWith('l') && t.size() > 1) {
                    // 'l' is a layout prefix in iReal streams; strip for display.
                    t.remove(0, 1);
                }
                // A standalone 'l' is a layout control (often appears before a barline as "l|").
                if (t == "l") return QString();

                // Display niceties / typography (match iReal Pro as closely as possible)
                // Accidentals
                // Replace # when used as sharp, and b when used as flat (avoid changing words like 'sus').
                t.replace(QRegularExpression(R"((?<=^[A-G])#)"), QString(QChar(0x266F))); // ♯
                t.replace(QRegularExpression(R"((?<=^[A-G])b)"), QString(QChar(0x266D))); // ♭
                t.replace(QRegularExpression(R"(#(?=\d))"), QString(QChar(0x266F)));      // ♯ (alterations)
                t.replace(QRegularExpression(R"(b(?=\d))"), QString(QChar(0x266D)));      // ♭ (alterations)
                // Major quality
                t.replace('^', QChar(0x0394)); // Δ

                // Half-diminished and diminished symbols: only when used as a chord-quality marker
                // e.g. "Dh7" -> "Dø7", "Ao7" -> "A°7"
                static const QRegularExpression reHalfDim(R"(^([A-G](?:[♭♯])?)h)");
                t.replace(reHalfDim, QStringLiteral("\\1") + QChar(0x00F8)); // ø
                static const QRegularExpression reDim(R"(^([A-G](?:[♭♯])?)o)");
                t.replace(reDim, QStringLiteral("\\1") + QChar(0x00B0)); // °

                // Minor marker: iReal uses a dash-like glyph, not ASCII '-'
                // e.g. "F-7" -> "F–7", "G-" -> "G–"
                static const QRegularExpression reMinor(R"(^([A-G](?:[♭♯])?)-)");
                t.replace(reMinor, QStringLiteral("\\1") + QChar(0x2013)); // –

                return t;
            };

            // Chord-list tokens:
            // - "s" prefix + commas: sBb,Bb7/F,  (fills successive cells)
            // - plain comma-separated chord tokens: Eb6, E°7 (also fills successive cells)
            QString listToken = chordToken;
            if (listToken.size() >= 2 && listToken[0] == 's' && listToken[1].isLetter()) {
                listToken.remove(0, 1);
            }
            const auto listParts = listToken.split(',', Qt::SkipEmptyParts);
            // Filter out layout-only parts (e.g. trailing 'l' in sAb-7,Db7,l)
            QVector<QString> filtered;
            filtered.reserve(listParts.size());
            for (const auto& part : listParts) {
                const QString chord = normalizeChord(part);
                if (chord.isEmpty()) continue;
                filtered.push_back(chord);
            }

            if (filtered.size() > 1) {
                // iReal harmonic rhythm placement heuristics (4/4):
                // If the bar already has one chord in cell 0 and we now have two more chords,
                // they should land on beats 3 and 4 (cells 2 and 3), leaving beat 2 empty.
                if (model.timeSigNum == 4 && model.timeSigDen == 4 && cellInBar == 1 && filtered.size() == 2) {
                    ensureCellCount(currentBar, 2);
                    cellInBar = 2;
                }
                for (const QString& chord : filtered) {
                    ensureCellCount(currentBar, cellInBar + 1);
                    currentBar.cells[cellInBar].chord = chord;
                    currentBar.cells[cellInBar].isPlaceholder = (chord.trimmed() == "x");
                    cellInBar++;
                    if (cellInBar >= 4) {
                        finalizeBar();
                    }
                }
            } else {
                const QString chord = normalizeChord(listToken);
                if (!chord.isEmpty()) {
                    ensureCellCount(currentBar, cellInBar + 1);
                    currentBar.cells[cellInBar].chord = chord;
                    currentBar.cells[cellInBar].isPlaceholder = (chord.trimmed() == "x");
                    cellInBar++;
                    if (cellInBar >= 4) {
                        finalizeBar();
                    }
                }
            }

            // In the iReal grid, each chord token consumes one cell.
            continue;
        }

        // Unknown char: skip
        i += 1;
    }

    // Flush trailing bar/line
    if (barIsMeaningful(currentBar, cellInBar)) {
        ensureCellCount(currentBar, cellInBar);
        applyDefaultHarmonicRhythm(currentBar);
        pushBar(currentLine, currentBar);
    }
    pushLine(model, currentLine);

    return model;
}

} // namespace chart

