#include "virtuoso/ui/PianoKeyboardWidget.h"

#include <QPainter>
#include <QtGlobal>

PianoKeyboardWidget::PianoKeyboardWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PianoKeyboardWidget::setHighlightedPitchClasses(QSet<int> pcs) {
    m_pcs = std::move(pcs);
    update();
}

void PianoKeyboardWidget::setRootPitchClass(int pc) {
    m_rootPc = normalizePc(pc);
    update();
}

void PianoKeyboardWidget::setRange(int minMidi, int maxMidi) {
    if (minMidi > maxMidi) std::swap(minMidi, maxMidi);
    m_minMidi = qBound(21, minMidi, 108); // 88-key range A0(21) .. C8(108)
    m_maxMidi = qBound(21, maxMidi, 108);
    update();
}

int PianoKeyboardWidget::normalizePc(int pc) {
    int v = pc % 12;
    if (v < 0) v += 12;
    return v;
}

bool PianoKeyboardWidget::isBlackPc(int pc) {
    switch (normalizePc(pc)) {
    case 1: case 3: case 6: case 8: case 10: return true;
    default: return false;
    }
}

void PianoKeyboardWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().window());

    const QRect r = rect().adjusted(8, 24, -8, -8);

    // Count white keys in range.
    int whiteCount = 0;
    for (int midi = m_minMidi; midi <= m_maxMidi; ++midi) {
        if (!isBlackPc(midi)) ++whiteCount;
    }
    if (whiteCount <= 0) return;

    const double whiteW = double(r.width()) / double(whiteCount);
    const double whiteH = double(r.height());
    const double blackW = whiteW * 0.62;
    const double blackH = whiteH * 0.62;

    // First pass: draw white keys and remember their x positions.
    struct KeyGeom { int midi; bool black; double x; };
    QVector<KeyGeom> keys;
    keys.reserve(m_maxMidi - m_minMidi + 1);

    double x = r.left();
    for (int midi = m_minMidi; midi <= m_maxMidi; ++midi) {
        const bool black = isBlackPc(midi);
        if (!black) {
            keys.push_back({midi, false, x});
            x += whiteW;
        } else {
            // black key x will be placed relative to previous white key.
            keys.push_back({midi, true, 0.0});
        }
    }

    // Draw white keys.
    p.setPen(QPen(QColor(40, 40, 40, 200), 1));
    for (const auto& k : keys) {
        if (k.black) continue;
        const int pc = normalizePc(k.midi);
        QColor fill = QColor(245, 245, 245);
        if (m_pcs.contains(pc)) {
            fill = (m_rootPc >= 0 && pc == m_rootPc) ? QColor(255, 190, 90) : QColor(120, 200, 255);
        }
        p.setBrush(fill);
        p.drawRect(QRectF(k.x, r.top(), whiteW, whiteH));
    }

    // Second pass: draw black keys on top.
    // Find x position: center black key between adjacent whites.
    // Simple heuristic: black key goes at (prevWhiteX + whiteW - blackW/2).
    double lastWhiteX = r.left();
    bool haveLastWhite = false;
    for (auto& k : keys) {
        if (!k.black) {
            lastWhiteX = k.x;
            haveLastWhite = true;
            continue;
        }
        if (!haveLastWhite) continue;
        k.x = lastWhiteX + whiteW - blackW * 0.5;
    }

    p.setPen(QPen(QColor(10, 10, 10, 220), 1));
    for (const auto& k : keys) {
        if (!k.black) continue;
        const int pc = normalizePc(k.midi);
        QColor fill = QColor(20, 20, 20);
        if (m_pcs.contains(pc)) {
            fill = (m_rootPc >= 0 && pc == m_rootPc) ? QColor(220, 130, 40) : QColor(60, 150, 255);
        }
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(k.x, r.top(), blackW, blackH), 2, 2);
    }

    // Label
    p.setPen(palette().text().color());
    p.setFont(QFont(p.font().family(), 10, QFont::DemiBold));
    p.drawText(rect().adjusted(12, 6, -12, -6),
               Qt::AlignTop | Qt::AlignLeft,
               QString("Piano (%1–%2)").arg(m_minMidi).arg(m_maxMidi));
}

