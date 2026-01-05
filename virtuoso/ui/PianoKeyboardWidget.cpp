#include "virtuoso/ui/PianoKeyboardWidget.h"

#include <QPainter>
#include <QtGlobal>
#include <QMouseEvent>
#include <QToolTip>

PianoKeyboardWidget::PianoKeyboardWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void PianoKeyboardWidget::setHighlightedPitchClasses(QSet<int> pcs) {
    m_pcs = std::move(pcs);
    update();
}

void PianoKeyboardWidget::setRootPitchClass(int pc) {
    m_rootPc = normalizePc(pc);
    update();
}

void PianoKeyboardWidget::setDegreeLabels(QHash<int, QString> labels) {
    QHash<int, QString> norm;
    for (auto it = labels.begin(); it != labels.end(); ++it) {
        norm.insert(normalizePc(it.key()), it.value());
    }
    m_degreeForPc = std::move(norm);
    update();
}

void PianoKeyboardWidget::setActiveMidiNotes(QSet<int> midis) {
    m_activeMidis = std::move(midis);
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

QVector<PianoKeyboardWidget::KeyRect> PianoKeyboardWidget::buildKeyRects(const QRect& area) const {
    QVector<KeyRect> out;
    if (area.width() <= 0 || area.height() <= 0) return out;

    // Count white keys in range.
    int whiteCount = 0;
    for (int midi = m_minMidi; midi <= m_maxMidi; ++midi) {
        if (!isBlackPc(midi)) ++whiteCount;
    }
    if (whiteCount <= 0) return out;

    const double whiteW = double(area.width()) / double(whiteCount);
    const double whiteH = double(area.height());
    const double blackW = whiteW * 0.62;
    const double blackH = whiteH * 0.62;

    // First create white keys (with x), and placeholders for black keys.
    double x = area.left();
    double lastWhiteX = area.left();
    bool haveLastWhite = false;

    for (int midi = m_minMidi; midi <= m_maxMidi; ++midi) {
        const bool black = isBlackPc(midi);
        if (!black) {
            QRectF rect(x, area.top(), whiteW, whiteH);
            out.push_back({midi, false, rect});
            lastWhiteX = x;
            haveLastWhite = true;
            x += whiteW;
        } else {
            if (!haveLastWhite) continue;
            const double bx = lastWhiteX + whiteW - blackW * 0.5;
            QRectF rect(bx, area.top(), blackW, blackH);
            out.push_back({midi, true, rect});
        }
    }
    return out;
}

int PianoKeyboardWidget::midiAtPoint(const QPoint& pos) const {
    const QRect area = rect().adjusted(8, 24, -8, -8);
    if (!area.contains(pos)) return -1;
    const auto keys = buildKeyRects(area);
    // Check black keys first (top layer), then whites.
    for (const auto& k : keys) {
        if (k.black && k.rect.contains(pos)) return k.midi;
    }
    for (const auto& k : keys) {
        if (!k.black && k.rect.contains(pos)) return k.midi;
    }
    return -1;
}

QString PianoKeyboardWidget::tooltipForMidi(int midi) const {
    if (midi < 0 || midi > 127) return {};
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    const int pc = normalizePc(midi);
    const int oct = midi / 12 - 1;
    QString t = QString("%1%2").arg(names[pc]).arg(oct);
    if (m_degreeForPc.contains(pc)) t += QString("  (deg %1)").arg(m_degreeForPc.value(pc));
    return t;
}

void PianoKeyboardWidget::mouseMoveEvent(QMouseEvent* event) {
    const int midi = midiAtPoint(event->pos());
    if (midi == m_lastTooltipMidi) return;
    m_lastTooltipMidi = midi;
    const QString tip = tooltipForMidi(midi);
    if (tip.isEmpty()) {
        QToolTip::hideText();
        return;
    }
    QToolTip::showText(event->globalPosition().toPoint(), tip, this);
}

void PianoKeyboardWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int midi = midiAtPoint(event->pos());
    if (midi < 0) return;
    emit noteClicked(midi);
}

void PianoKeyboardWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().window());

    const QRect r = rect().adjusted(8, 24, -8, -8);

    const auto keys = buildKeyRects(r);
    if (keys.isEmpty()) return;

    // Draw white keys.
    p.setPen(QPen(QColor(40, 40, 40, 200), 1));
    for (const auto& k : keys) {
        if (k.black) continue;
        const int pc = normalizePc(k.midi);
        const bool isActiveMidi = m_activeMidis.contains(k.midi);
        QColor fill = QColor(245, 245, 245);
        if (m_pcs.contains(pc)) {
            fill = (m_rootPc >= 0 && pc == m_rootPc) ? QColor(255, 190, 90) : QColor(120, 200, 255);
        }
        // Active notes are indicated via the white outline ring below; don't recolor
        // out-of-set notes to avoid confusion (it can look like a wrong highlight).
        p.setBrush(fill);
        p.drawRect(k.rect);

        const QString deg = m_degreeForPc.value(pc);
        if (!deg.isEmpty() && m_pcs.contains(pc)) {
            p.setPen(QColor(20, 20, 20, 220));
            QFont fnt = p.font();
            fnt.setPointSize(8);
            fnt.setBold(true);
            p.setFont(fnt);
            QRectF tr = k.rect.adjusted(0, 2, 0, -2);
            tr.setHeight(14);
            p.drawText(tr, Qt::AlignCenter, deg);
        }

        if (isActiveMidi) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 255, 255, 220), 2));
            p.drawRoundedRect(k.rect.adjusted(1, 1, -1, -1), 2, 2);
            p.setPen(QPen(QColor(40, 40, 40, 200), 1));
        }
    }

    p.setPen(QPen(QColor(10, 10, 10, 220), 1));
    for (const auto& k : keys) {
        if (!k.black) continue;
        const int pc = normalizePc(k.midi);
        const bool isActiveMidi = m_activeMidis.contains(k.midi);
        QColor fill = QColor(20, 20, 20);
        if (m_pcs.contains(pc)) {
            fill = (m_rootPc >= 0 && pc == m_rootPc) ? QColor(220, 130, 40) : QColor(60, 150, 255);
        }
        // Active notes are indicated via the white outline ring below; don't recolor
        // out-of-set notes to avoid confusion (it can look like a wrong highlight).
        p.setBrush(fill);
        p.drawRoundedRect(k.rect, 2, 2);

        const QString deg = m_degreeForPc.value(pc);
        if (!deg.isEmpty() && m_pcs.contains(pc)) {
            p.setPen(QColor(240, 240, 240, 220));
            QFont fnt = p.font();
            fnt.setPointSize(8);
            fnt.setBold(true);
            p.setFont(fnt);
            QRectF tr = k.rect.adjusted(0, 2, 0, -2);
            tr.setHeight(14);
            p.drawText(tr, Qt::AlignCenter, deg);
            p.setPen(QPen(QColor(10, 10, 10, 220), 1));
        }

        if (isActiveMidi) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 255, 255, 220), 2));
            p.drawRoundedRect(k.rect.adjusted(1, 1, -1, -1), 2, 2);
            p.setPen(QPen(QColor(10, 10, 10, 220), 1));
        }
    }

    // Label
    p.setPen(palette().text().color());
    p.setFont(QFont(p.font().family(), 10, QFont::DemiBold));
    p.drawText(rect().adjusted(12, 6, -12, -6),
               Qt::AlignTop | Qt::AlignLeft,
               QString("Piano (%1–%2)").arg(m_minMidi).arg(m_maxMidi));
}

