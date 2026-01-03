#include "chart/SongChartWidget.h"

#include <QPainter>
#include <QScrollBar>
#include <QHash>

namespace chart {
namespace {

enum class BarlineStyle { Normal, Double, RepeatStart, RepeatEnd, Final };

// Single source of truth for chord sizing (no dynamic scaling).
static constexpr int kChordRootPointSize = 20;

static void drawBarline(QPainter& p, int x, int y, int h, BarlineStyle style) {
    switch (style) {
        case BarlineStyle::Normal: {
            QPen pen(QColor(240, 240, 240));
            pen.setWidthF(2.2);
            p.setPen(pen);
            p.drawLine(x, y, x, y + h);
            break;
        }
        case BarlineStyle::Double: {
            QPen pen(QColor(240, 240, 240));
            pen.setWidthF(2.2);
            p.setPen(pen);
            p.drawLine(x - 2, y, x - 2, y + h);
            p.drawLine(x + 2, y, x + 2, y + h);
            break;
        }
        case BarlineStyle::RepeatStart: {
            QPen thick(QColor(240, 240, 240));
            thick.setWidthF(4.0);
            p.setPen(thick);
            p.drawLine(x, y, x, y + h);
            QPen thin(QColor(240, 240, 240));
            thin.setWidthF(2.0);
            p.setPen(thin);
            p.drawLine(x + 6, y, x + 6, y + h);
            p.setBrush(QColor(240, 240, 240));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(x + 12, y + h * 0.35), 2.4, 2.4);
            p.drawEllipse(QPointF(x + 12, y + h * 0.65), 2.4, 2.4);
            break;
        }
        case BarlineStyle::RepeatEnd: {
            QPen thin(QColor(240, 240, 240));
            thin.setWidthF(2.0);
            p.setPen(thin);
            p.drawLine(x - 6, y, x - 6, y + h);
            QPen thick(QColor(240, 240, 240));
            thick.setWidthF(4.0);
            p.setPen(thick);
            p.drawLine(x, y, x, y + h);
            p.setBrush(QColor(240, 240, 240));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(x - 12, y + h * 0.35), 2.4, 2.4);
            p.drawEllipse(QPointF(x - 12, y + h * 0.65), 2.4, 2.4);
            break;
        }
        case BarlineStyle::Final: {
            QPen thin(QColor(240, 240, 240));
            thin.setWidthF(2.0);
            p.setPen(thin);
            p.drawLine(x - 5, y, x - 5, y + h);
            QPen thick(QColor(240, 240, 240));
            thick.setWidthF(5.0);
            p.setPen(thick);
            p.drawLine(x + 1, y, x + 1, y + h);
            break;
        }
    }
}

static void drawRepeatCellMark(QPainter& p, const QRect& r) {
    // iReal-style “repeat measure” mark (diagonal slash with two dots).
    const QPointF a(r.left() + r.width() * 0.40, r.top() + r.height() * 0.70);
    const QPointF b(r.left() + r.width() * 0.60, r.top() + r.height() * 0.30);
    QPen pen(QColor(240, 240, 240));
    pen.setWidthF(3.0);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawLine(a, b);
    p.setBrush(QColor(240, 240, 240));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(r.left() + r.width() * 0.42, r.top() + r.height() * 0.35), 3.0, 3.0);
    p.drawEllipse(QPointF(r.left() + r.width() * 0.58, r.top() + r.height() * 0.65), 3.0, 3.0);
}

static void drawChordPretty(QPainter& p, const QRect& cellRect, const QString& chordText) {
    QString t = chordText.trimmed();
    if (t.isEmpty()) return;

    // Repeat/placeholder cell: render like iReal's repeat mark (best-effort)
    if (t == "x") {
        drawRepeatCellMark(p, cellRect);
        return;
    }

    // Split slash chords
    QString main = t;
    QString bass;
    const int slash = t.indexOf('/');
    if (slash >= 0) {
        main = t.left(slash);
        bass = t.mid(slash + 1);
    }

    // Extract parenthetical alternatives, e.g. Ao7(Bb7sus)
    QString paren;
    const int lp = main.indexOf('(');
    const int rp = main.lastIndexOf(')');
    if (lp >= 0 && rp > lp) {
        paren = main.mid(lp, rp - lp + 1);
        main = main.left(lp);
    }

    // Parse root letter + accidental
    QString root;
    QString accidental;
    QString rest;
    if (!main.isEmpty() && main[0].isLetter()) {
        root = main.left(1);
        int pos = 1;
        if (pos < main.size() && (main[pos] == QChar(0x266D) || main[pos] == QChar(0x266F))) { // ♭/♯
            accidental = main.mid(pos, 1);
            pos += 1;
        }
        rest = main.mid(pos);
    } else {
        rest = main;
    }

    QFont rootFont = p.font();
    QFont supFont = rootFont;
    QFont accFont = rootFont;
    QFont bassFont = rootFont;
    QFont parenFont = rootFont;

    const int baseRoot = kChordRootPointSize;
    rootFont.setPointSize(baseRoot);
    supFont.setPointSize(std::max(10, int(baseRoot * 0.55)));
    accFont.setPointSize(std::max(10, int(baseRoot * 0.55)));
    bassFont.setPointSize(std::max(10, int(baseRoot * 0.60)));
    parenFont.setPointSize(std::max(9, int(baseRoot * 0.50)));

    rootFont.setBold(true);
    supFont.setBold(true);
    accFont.setBold(true);
    bassFont.setBold(true);
    parenFont.setBold(true);

    const int x0 = cellRect.left() + 10;
    const int y0 = cellRect.top() + 10;

    int x = x0;
    int baseline = y0 + int(rootFont.pointSize() * 1.2);

    // Root
    if (!root.isEmpty()) {
        p.setFont(rootFont);
        p.drawText(QPoint(x, baseline), root);
        x += QFontMetrics(rootFont).horizontalAdvance(root);
    }
    // Accidental (raised)
    if (!accidental.isEmpty()) {
        p.setFont(accFont);
        const int accBase = baseline - int(rootFont.pointSize() * 0.45);
        p.drawText(QPoint(x, accBase), accidental);
        x += QFontMetrics(accFont).horizontalAdvance(accidental);
    }
    // Rest (raised/smaller)
    if (!rest.isEmpty()) {
        p.setFont(supFont);
        const int supBase = baseline - int(rootFont.pointSize() * 0.35);
        p.drawText(QPoint(x + 2, supBase), rest);
        x += QFontMetrics(supFont).horizontalAdvance(rest) + 2;
    }
    // Parenthetical alternative (even smaller)
    if (!paren.isEmpty()) {
        p.setFont(parenFont);
        const int pBase = baseline - int(rootFont.pointSize() * 0.40);
        p.drawText(QPoint(x + 2, pBase), paren);
        x += QFontMetrics(parenFont).horizontalAdvance(paren) + 2;
    }
    // Slash bass (smaller, lower)
    if (!bass.isEmpty()) {
        p.setFont(bassFont);
        const QString slashText = "/" + bass;
        const int bassBase = baseline + int(rootFont.pointSize() * 0.15);
        p.drawText(QPoint(x + 4, bassBase), slashText);
    }
}

} // namespace

SongChartWidget::SongChartWidget(QWidget* parent)
    : QAbstractScrollArea(parent) {
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Base, Qt::black);
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    viewport()->setAutoFillBackground(true);
}

void SongChartWidget::clear() {
    m_model = ChartModel{};
    m_hasModel = false;
    m_cellRects.clear();
    m_currentCell = -1;
    verticalScrollBar()->setValue(0);
    rebuildLayout();
    viewport()->update();
}

void SongChartWidget::setChartModel(const ChartModel& model) {
    m_model = model;
    m_hasModel = true;
    m_currentCell = -1;
    rebuildLayout();
    viewport()->update();
}

void SongChartWidget::setCurrentCellIndex(int cellIndex) {
    if (!m_hasModel) return;
    if (cellIndex < 0 || cellIndex >= m_cellRects.size()) return;
    if (m_currentCell == cellIndex) return;
    m_currentCell = cellIndex;
    ensureCellVisible(cellIndex);
    viewport()->update();
}

void SongChartWidget::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    rebuildLayout();
}

void SongChartWidget::rebuildLayout() {
    m_cellRects.clear();

    const int contentW = viewport()->width();
    const int barsPerLine = 4;
    const int cellsPerBar = 4;

    int y = m_margin;
    for (const auto& line : m_model.lines) {
        int x = m_margin + m_sectionGutter;
        const int usableW = std::max(0, contentW - (m_margin * 2 + m_sectionGutter));
        const int barW = usableW / barsPerLine;
        const int cellW = barW / cellsPerBar;

        // If this line contains a 2nd ending start, right-align the bars so N2 sits under N1.
        bool hasSecondEnding = false;
        for (const auto& b : line.bars) {
            if (b.endingStart == 2) { hasSecondEnding = true; break; }
        }
        const int offsetBars = (hasSecondEnding && line.bars.size() < barsPerLine)
            ? (barsPerLine - int(line.bars.size()))
            : 0;
        const int xOffset = offsetBars * barW;

        const int barsToDraw = std::min<int>(barsPerLine, int(line.bars.size()));
        for (int b = 0; b < barsToDraw; ++b) {
            const int barX = x + xOffset + b * barW;
            for (int c = 0; c < cellsPerBar; ++c) {
                QRect r(barX + c * cellW, y, cellW, m_barHeight);
                m_cellRects.push_back(r);
            }
        }

        y += m_lineHeight;
    }

    const int contentH = y + m_margin;
    verticalScrollBar()->setRange(0, std::max(0, contentH - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
}

void SongChartWidget::ensureCellVisible(int cellIndex) {
    if (cellIndex < 0 || cellIndex >= m_cellRects.size()) return;
    const QRect r = m_cellRects[cellIndex];

    const int y0 = verticalScrollBar()->value();
    const int y1 = y0 + viewport()->height();
    const int top = r.top();
    const int bottom = r.bottom();

    if (top < y0) {
        verticalScrollBar()->setValue(std::max(0, top - m_margin));
    } else if (bottom > y1) {
        verticalScrollBar()->setValue(std::min(verticalScrollBar()->maximum(), bottom - viewport()->height() + m_margin));
    }
}

void SongChartWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, true);

    // background
    p.fillRect(rect(), Qt::black);
    if (!m_hasModel) {
        p.setPen(QColor(120, 120, 120));
        QFont f = p.font();
        f.setPointSize(12);
        p.setFont(f);
        p.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignTop | Qt::AlignLeft,
                   "Open an iReal Pro .html playlist to display a chart.");
        return;
    }

    const int scrollY = verticalScrollBar()->value();
    p.translate(0, -scrollY);

    const int contentW = viewport()->width();
    const int barsPerLine = 4;
    const int cellsPerBar = 4;

    QPen penWhite(QColor(240, 240, 240));
    penWhite.setWidthF(1.2);
    p.setPen(penWhite);

    QFont chordFont = p.font();
    // Keep a single, static chord font size. Individual chords must not scale.
    chordFont.setPointSize(kChordRootPointSize);
    chordFont.setBold(true);
    p.setFont(chordFont);

    int globalCell = 0;
    int y = m_margin;
    bool drewTimeSig = false;
    int endingActive = 0;
    bool endingNumberDrawn = false;
    int endingStartBarX = 0;
    QHash<int, int> endingAnchors; // align N2 under N1

    for (const auto& line : m_model.lines) {
        const int usableW = std::max(0, contentW - (m_margin * 2 + m_sectionGutter));
        const int barW = usableW / barsPerLine;
        const int cellW = barW / cellsPerBar;
        const int x0 = m_margin + m_sectionGutter;

        bool hasSecondEnding = false;
        for (const auto& b : line.bars) {
            if (b.endingStart == 2) { hasSecondEnding = true; break; }
        }
        const int offsetBars = (hasSecondEnding && line.bars.size() < barsPerLine)
            ? (barsPerLine - int(line.bars.size()))
            : 0;
        const int xOffset = offsetBars * barW;

        // Section label
        if (!line.sectionLabel.isEmpty()) {
            QFont secFont = chordFont;
            secFont.setPointSize(18);
            p.setFont(secFont);
            p.drawText(QRect(m_margin, y, m_sectionGutter - 6, m_barHeight),
                       Qt::AlignLeft | Qt::AlignTop, line.sectionLabel);
            p.setFont(chordFont);
        }

        // Time signature (draw once at the first rendered line, iReal-style stacked)
        if (!drewTimeSig) {
            drewTimeSig = true;
            QFont tsFont = chordFont;
            tsFont.setPointSize(22);
            tsFont.setBold(true);
            p.setFont(tsFont);
            const QString top = QString::number(m_model.timeSigNum);
            const QString bot = QString::number(m_model.timeSigDen);
            p.drawText(QRect(m_margin + 22, y + 4, 40, 34), Qt::AlignLeft | Qt::AlignVCenter, top);
            p.drawText(QRect(m_margin + 22, y + 36, 40, 34), Qt::AlignLeft | Qt::AlignVCenter, bot);
            p.setFont(chordFont);
        }

        // Draw bars
        for (int b = 0; b < barsPerLine; ++b) {
            const int barX = x0 + xOffset + b * barW;
            if (b >= line.bars.size()) {
                // Stop drawing when this line has no more bars
                break;
            }

            BarlineStyle leftStyle = BarlineStyle::Normal;
            BarlineStyle rightStyle = BarlineStyle::Normal;
            const Bar& bar = line.bars[b];
            if (bar.barlineLeft.contains('{')) leftStyle = BarlineStyle::RepeatStart;
            else if (bar.barlineLeft.contains('[')) leftStyle = BarlineStyle::Double;
            if (bar.barlineRight.contains('Z')) rightStyle = BarlineStyle::Final;
            else if (bar.barlineRight.contains('}')) rightStyle = BarlineStyle::RepeatEnd;
            else if (bar.barlineRight.contains(']')) rightStyle = BarlineStyle::Double;
            // A trailing explicit '|' token is only present when the token stream encodes an end-of-song double barline ("||").
            // Normal single barlines between measures are drawn implicitly and are NOT stored in barlineRight.
            else if (bar.barlineRight.contains('|')) rightStyle = BarlineStyle::Double;

            // First/second endings bracket rendering (best-effort).
            if (bar.endingStart > 0) {
                endingActive = bar.endingStart;
                endingNumberDrawn = false;
                // Align N2 (and higher) underneath N1 by reusing the same anchor column.
                if (endingActive > 1 && endingAnchors.contains(1)) {
                    endingStartBarX = endingAnchors.value(1);
                } else if (endingAnchors.contains(endingActive)) {
                    endingStartBarX = endingAnchors.value(endingActive);
                } else {
                    endingStartBarX = barX;
                    endingAnchors.insert(endingActive, endingStartBarX);
                }
            }

            drawBarline(p, barX, y, m_barHeight, leftStyle);
            drawBarline(p, barX + barW, y, m_barHeight, rightStyle);

            // Ending bracket segment for this bar (draw over bars while active)
            if (endingActive > 0) {
                const int bracketY = y - 10;
                QPen brPen(QColor(240, 240, 240));
                brPen.setWidthF(2.0);
                p.setPen(brPen);
                // Vertical start only at the first bar of the ending (per line)
                if (!endingNumberDrawn) {
                    p.drawLine(endingStartBarX, bracketY, endingStartBarX, bracketY + 18);
                    QFont f = p.font();
                    f.setPointSize(16);
                    f.setBold(true);
                    p.setFont(f);
                    p.drawText(QRect(endingStartBarX + 6, bracketY - 2, 30, 20),
                               Qt::AlignLeft | Qt::AlignVCenter, QString("%1.").arg(endingActive));
                    p.setFont(chordFont);
                    endingNumberDrawn = true;

                    // If aligned anchor starts left of the actual bar, draw the gap segment.
                    if (endingStartBarX < barX) {
                        p.drawLine(endingStartBarX, bracketY, barX, bracketY);
                    }
                }
                // Horizontal line over this bar
                p.drawLine(barX, bracketY, barX + barW, bracketY);
            }

            // chords (only for existing bars; don't draw padding bars)
            if (b < line.bars.size()) {
                const Bar& bar = line.bars[b];
                for (int c = 0; c < cellsPerBar; ++c) {
                    QRect cellRect(barX + c * cellW, y, cellW, m_barHeight);

                    // highlight current cell
                    if (globalCell == m_currentCell) {
                        p.fillRect(cellRect.adjusted(2, 2, -2, -2), QColor(40, 90, 160));
                    }

                    if (c < bar.cells.size() && !bar.cells[c].chord.isEmpty()) {
                        p.setPen(QColor(240, 240, 240));
                        drawChordPretty(p, cellRect.adjusted(0, 0, -6, -6), bar.cells[c].chord);
                    }

                    globalCell++;
                }

                // Bar annotation like "Fine" (draw near right side of the bar)
                if (!bar.annotation.isEmpty()) {
                    QFont f = p.font();
                    f.setBold(true);
                    f.setPointSize(20);
                    p.setFont(f);
                    p.setPen(QColor(240, 240, 240));
                    QRect annRect(barX + int(barW * 0.55), y + int(m_barHeight * 0.55), int(barW * 0.45) - 8, int(m_barHeight * 0.45));
                    p.drawText(annRect, Qt::AlignRight | Qt::AlignVCenter, bar.annotation);
                    p.setFont(chordFont);
                }

                // Endings: if this bar ends the ending, close it.
                if (bar.endingEnd > 0 && endingActive > 0) {
                    const int bracketY = y - 10;
                    QPen brPen(QColor(240, 240, 240));
                    brPen.setWidthF(2.0);
                    p.setPen(brPen);
                    p.drawLine(barX + barW, bracketY, barX + barW, bracketY + 18);
                    endingActive = 0;
                    endingNumberDrawn = false;
                    endingAnchors.clear();
                }
            } else {
                // Padding bars: do not draw and do not advance cell index,
                // so they don't appear as “extra bars” around endings.
                // (We recompute totalCells based on actual bars elsewhere.)
            }

            // (barline styling is handled above)
        }

        y += m_lineHeight;
    }

    // Footer annotation (e.g. "D.C. al Fine") drawn at bottom-right like iReal.
    if (!m_model.footerText.isEmpty()) {
        QFont f = p.font();
        f.setBold(true);
        f.setPointSize(22);
        p.setFont(f);
        p.setPen(QColor(240, 240, 240));
        const int footerY = y - int(m_lineHeight * 0.35);
        const int usableW = std::max(0, contentW - (m_margin * 2));
        p.drawText(QRect(m_margin, footerY, usableW, 40), Qt::AlignRight | Qt::AlignVCenter, m_model.footerText);
    }
}

} // namespace chart

