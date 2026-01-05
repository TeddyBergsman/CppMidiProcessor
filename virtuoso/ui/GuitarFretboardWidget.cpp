#include "virtuoso/ui/GuitarFretboardWidget.h"

#include <QPainter>
#include <QtMath>
#include <QMouseEvent>
#include <QToolTip>

namespace {
static int normalizePc(int pc) {
    int v = pc % 12;
    if (v < 0) v += 12;
    return v;
}
} // namespace

GuitarFretboardWidget::GuitarFretboardWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void GuitarFretboardWidget::setHighlightedPitchClasses(QSet<int> pcs) {
    m_pcs = std::move(pcs);
    update();
}

void GuitarFretboardWidget::setRootPitchClass(int pc) {
    m_rootPc = normalizePc(pc);
    update();
}

void GuitarFretboardWidget::setDegreeLabels(QHash<int, QString> labels) {
    // normalize keys to 0..11
    QHash<int, QString> norm;
    for (auto it = labels.begin(); it != labels.end(); ++it) {
        norm.insert(normalizePc(it.key()), it.value());
    }
    m_degreeForPc = std::move(norm);
    update();
}

void GuitarFretboardWidget::setFretCount(int frets) {
    m_frets = qMax(1, frets);
    update();
}

QString GuitarFretboardWidget::pcName(int pc) {
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return names[normalizePc(pc)];
}

bool GuitarFretboardWidget::isBlackPc(int pc) {
    switch (normalizePc(pc)) {
    case 1: case 3: case 6: case 8: case 10: return true;
    default: return false;
    }
}

int GuitarFretboardWidget::midiAtPoint(const QPoint& pos) const {
    const QRect r = rect().adjusted(8, 8, -8, -8);
    const QRect board = r.adjusted(0, 0, 0, 0);
    if (!board.contains(pos)) return -1;

    const int strings = 6;
    const int frets = m_frets;
    const double fretW = double(board.width()) / double(frets + 1);
    const double stringH = double(board.height()) / double(strings - 1);

    const double fx = (double(pos.x()) - double(board.left())) / fretW;
    const int fret = qBound(0, int(std::floor(fx)), frets);

    const double sy = (double(pos.y()) - double(board.top())) / stringH;
    const int stringIndex = qBound(0, int(std::round(sy)), strings - 1);

    // Standard guitar tuning (MIDI), drawn TOP→BOTTOM (right-handed view):
    // E4 B3 G3 D3 A2 E2
    const int openMidi[6] = {64, 59, 55, 50, 45, 40};
    return openMidi[stringIndex] + fret;
}

QString GuitarFretboardWidget::tooltipForMidi(int midi) const {
    if (midi < 0 || midi > 127) return {};
    const int pc = normalizePc(midi);
    const int oct = midi / 12 - 1;
    QString t = QString("%1%2").arg(pcName(pc)).arg(oct);
    if (m_degreeForPc.contains(pc)) {
        t += QString("  (deg %1)").arg(m_degreeForPc.value(pc));
    }
    return t;
}

void GuitarFretboardWidget::mouseMoveEvent(QMouseEvent* event) {
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

void GuitarFretboardWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int midi = midiAtPoint(event->pos());
    if (midi < 0) return;
    emit noteClicked(midi);
}

void GuitarFretboardWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect().adjusted(8, 8, -8, -8);
    p.fillRect(rect(), palette().window());

    // Fretboard background
    QColor wood(44, 28, 18);
    p.setPen(Qt::NoPen);
    p.setBrush(wood);
    p.drawRoundedRect(r, 8, 8);

    // Geometry
    const int strings = 6;
    const int frets = m_frets;
    const double fretW = double(r.width()) / double(frets + 1);
    const double stringH = double(r.height()) / double(strings - 1);

    // Fret lines
    p.setPen(QPen(QColor(200, 200, 200, 120), 1));
    for (int f = 0; f <= frets; ++f) {
        const int x = int(r.left() + std::round(double(f) * fretW));
        p.drawLine(x, r.top(), x, r.bottom());
    }

    // Nut
    p.setPen(QPen(QColor(240, 240, 240, 200), 4));
    p.drawLine(r.left(), r.top(), r.left(), r.bottom());

    // String lines (thicker for low strings)
    for (int s = 0; s < strings; ++s) {
        const int y = int(r.top() + std::round(double(s) * stringH));
        // Right-handed visual order: high E at top, low E at bottom => thickness increases with s.
        const int w = int(1 + s * 0.5);
        p.setPen(QPen(QColor(230, 230, 230, 170), w));
        p.drawLine(r.left(), y, r.right(), y);
    }

    // Inlay dots
    const QSet<int> dotFrets = QSet<int>({3,5,7,9,12,15,17,19,21,24});
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 70));
    for (int f : dotFrets) {
        if (f < 1 || f > frets) continue;
        const double xCenter = r.left() + (double(f) - 0.5) * fretW;
        if (f == 12 || f == 24) {
            const double y1 = r.top() + r.height() * 0.35;
            const double y2 = r.top() + r.height() * 0.65;
            p.drawEllipse(QPointF(xCenter, y1), 6, 6);
            p.drawEllipse(QPointF(xCenter, y2), 6, 6);
        } else {
            p.drawEllipse(QPointF(xCenter, r.center().y()), 6, 6);
        }
    }

    // Standard guitar tuning (MIDI), drawn TOP→BOTTOM (right-handed view):
    // E4 B3 G3 D3 A2 E2  (high E on top, low E on bottom)
    const int openMidi[6] = {64, 59, 55, 50, 45, 40};

    // Highlight dots for selected pitch classes across the neck.
    for (int s = 0; s < strings; ++s) {
        const int y = int(r.top() + std::round(double(s) * stringH));
        for (int f = 0; f <= frets; ++f) {
            const int midi = openMidi[s] + f;
            const int pc = normalizePc(midi);
            if (!m_pcs.contains(pc)) continue;

            const double xCenter = r.left() + (double(f) + 0.5) * fretW;
            const bool isRoot = (m_rootPc >= 0 && pc == m_rootPc);
            const QColor fill = isRoot
                ? QColor(255, 170, 60, 235)
                : (isBlackPc(pc) ? QColor(60, 160, 255, 220) : QColor(80, 200, 255, 220));
            p.setBrush(fill);
            p.setPen(QPen(QColor(10, 10, 10, 160), 1));
            p.drawEllipse(QPointF(xCenter, y), 10, 10);

            const QString deg = m_degreeForPc.value(pc);
            if (!deg.isEmpty()) {
                p.setPen(QColor(10, 10, 10, 220));
                QFont fnt = p.font();
                fnt.setPointSize(8);
                fnt.setBold(true);
                p.setFont(fnt);
                p.drawText(QRectF(xCenter - 9, y - 8, 18, 16), Qt::AlignCenter, deg);
            }
        }
    }

    // Label
    p.setPen(QColor(255, 255, 255, 200));
    p.setFont(QFont(p.font().family(), 10, QFont::DemiBold));
    p.drawText(rect().adjusted(12, 8, -12, -8), Qt::AlignTop | Qt::AlignLeft, "Guitar (6-string, 24-fret)");
}

