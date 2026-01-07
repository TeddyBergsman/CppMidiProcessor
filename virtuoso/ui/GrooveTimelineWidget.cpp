#include "virtuoso/ui/GrooveTimelineWidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QtMath>

namespace virtuoso::ui {
namespace {
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

GrooveTimelineWidget::GrooveTimelineWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(220);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(12, 12, 12));
    setPalette(pal);
}

void GrooveTimelineWidget::setTempoAndSignature(int bpm, int tsNum, int tsDen) {
    m_bpm = clampi(bpm, 30, 300);
    m_tsNum = clampi(tsNum, 1, 32);
    m_tsDen = (tsDen <= 0) ? 4 : tsDen;
    update();
}

void GrooveTimelineWidget::setPreviewBars(int bars) {
    m_previewBars = clampi(bars, 1, 64);
    update();
}

void GrooveTimelineWidget::setSubdivision(int subdivPerBeat) {
    m_subdivPerBeat = clampi(subdivPerBeat, 1, 12);
    update();
}

void GrooveTimelineWidget::setLanes(const QStringList& lanes) {
    m_lanes = lanes;
    update();
}

void GrooveTimelineWidget::setEvents(const QVector<LaneEvent>& events) {
    m_events = events;
    update();
}

void GrooveTimelineWidget::setPlayheadMs(qint64 ms) {
    m_playheadMs = ms;
    update();
}

qint64 GrooveTimelineWidget::totalMs() const {
    const double quarterMs = 60000.0 / double(qMax(1, m_bpm));
    const double beatMs = quarterMs * (4.0 / double(qMax(1, m_tsDen)));
    const double barMs = beatMs * double(qMax(1, m_tsNum));
    return qint64(llround(barMs * double(qMax(1, m_previewBars))));
}

double GrooveTimelineWidget::xForMs(qint64 ms) const {
    const qint64 tot = qMax<qint64>(1, totalMs());
    const int leftPad = 90;
    const int rightPad = 12;
    const int w = qMax(1, width() - leftPad - rightPad);
    const double t = double(qBound<qint64>(0, ms, tot)) / double(tot);
    return double(leftPad) + t * double(w);
}

QRect GrooveTimelineWidget::laneRect(int laneIndex) const {
    const int topPad = 18;
    const int bottomPad = 10;
    const int leftPad = 90;
    const int rightPad = 12;
    const int lanes = qMax(1, m_lanes.size());
    const int h = qMax(1, height() - topPad - bottomPad);
    const int laneH = h / lanes;
    const int y = topPad + laneIndex * laneH;
    return QRect(leftPad, y, qMax(1, width() - leftPad - rightPad), laneH);
}

int GrooveTimelineWidget::laneIndexForY(int y) const {
    if (m_lanes.isEmpty()) return -1;
    const int topPad = 18;
    const int bottomPad = 10;
    const int h = qMax(1, height() - topPad - bottomPad);
    const int laneH = qMax(1, h / m_lanes.size());
    const int idx = (y - topPad) / laneH;
    if (idx < 0 || idx >= m_lanes.size()) return -1;
    return idx;
}

int GrooveTimelineWidget::subRowCountForLane(const QString& lane) const {
    const QString l = lane.trimmed().toLower();
    if (l == "rhythm") return 1;
    if (l == "pedal") return 1;
    if (l == "articulation") return 1;
    if (l == "keyswitch") return 1;
    if (l == "articulationstate") return 1;
    if (l == "fx") return 1;
    if (l == "bass") return 4;
    if (l == "drums") return 6;
    if (l == "piano") return 8;
    return 6;
}

int GrooveTimelineWidget::subRowIndexForEvent(const QString& lane, int note) const {
    const int rows = qMax(1, subRowCountForLane(lane));
    const QString l = lane.trimmed().toLower();
    if (l == "rhythm") return 0;
    if (l == "pedal") return 0;
    if (l == "articulation") return 0;
    if (l == "keyswitch") return 0;
    if (l == "articulationstate") return 0;
    if (l == "fx") return 0;
    // Pitch mapping: spread notes across subrows so overlapping notes remain visible.
    if (l == "piano") {
        // map ~C3..C6
        const int lo = 48, hi = 96;
        const int n = clampi(note, lo, hi);
        const double t = double(n - lo) / double(qMax(1, hi - lo));
        return clampi(int((1.0 - t) * double(rows - 1)), 0, rows - 1);
    }
    if (l == "bass") {
        // map ~E1..E3
        const int lo = 28, hi = 52;
        const int n = clampi(note, lo, hi);
        const double t = double(n - lo) / double(qMax(1, hi - lo));
        return clampi(int((1.0 - t) * double(rows - 1)), 0, rows - 1);
    }
    if (l == "drums") {
        // Stable-ish spread by midi note.
        return clampi((note < 0 ? 0 : note) % rows, 0, rows - 1);
    }
    return clampi((note < 0 ? 0 : note) % rows, 0, rows - 1);
}

QRectF GrooveTimelineWidget::eventRect(int laneIndex, const LaneEvent& ev) const {
    const QRect lr = laneRect(laneIndex);
    const double x1 = xForMs(ev.onMs);
    const double x2 = xForMs(ev.offMs);
    const double w = qMax(6.0, x2 - x1);

    const int rows = qMax(1, subRowCountForLane(ev.lane));
    const int row = subRowIndexForEvent(ev.lane, ev.note);
    const double rowH = double(lr.height() - 12) / double(rows);
    const double y = double(lr.y() + 6) + rowH * double(row);
    const double h = qMax(6.0, rowH - 2.0);
    return QRectF(x1, y, w, h);
}

int GrooveTimelineWidget::hitTestEventIndex(const QPoint& p) const {
    const int li = laneIndexForY(p.y());
    if (li < 0) return -1;
    const QString lane = m_lanes.value(li);
    const double x = p.x();
    for (int i = 0; i < m_events.size(); ++i) {
        const auto& ev = m_events[i];
        if (ev.lane != lane) continue;
        const QRectF r = eventRect(li, ev).adjusted(-2, -2, +2, +2);
        if (r.contains(QPointF(p))) return i;
    }
    Q_UNUSED(x);
    return -1;
}

void GrooveTimelineWidget::mousePressEvent(QMouseEvent* e) {
    const int idx = hitTestEventIndex(e->pos());
    if (idx >= 0 && idx < m_events.size()) {
        const auto& ev = m_events[idx];
        emit eventClicked(ev.lane, ev.note, ev.velocity, ev.label);
    }
    QWidget::mousePressEvent(e);
}

void GrooveTimelineWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int lanes = qMax(1, m_lanes.size());
    const qint64 totMs = totalMs();
    const double quarterMs = 60000.0 / double(qMax(1, m_bpm));
    const double beatMs = quarterMs * (4.0 / double(qMax(1, m_tsDen)));
    const double barMs = beatMs * double(qMax(1, m_tsNum));

    // Background + lane separators
    p.fillRect(rect(), QColor(12, 12, 12));
    p.setPen(QPen(QColor(40, 40, 40), 1));
    for (int li = 0; li < lanes; ++li) {
        const QRect lr = laneRect(li);
        p.drawRect(lr.adjusted(0, 0, -1, -1));
        // Lane label
        p.setPen(QColor(220, 220, 220));
        p.drawText(QRect(8, lr.y(), 78, lr.height()), Qt::AlignVCenter | Qt::AlignLeft, m_lanes.value(li));
        p.setPen(QPen(QColor(40, 40, 40), 1));
    }

    // Grid lines (bars/beats/subdiv)
    const int leftPad = 90;
    const int topPad = 18;
    const int bottomPad = 10;
    const int plotH = qMax(1, height() - topPad - bottomPad);

    auto drawV = [&](qint64 ms, const QColor& c, int w, const QString& label) {
        const double x = xForMs(ms);
        p.setPen(QPen(c, w));
        p.drawLine(QPointF(x, topPad), QPointF(x, topPad + plotH));
        if (!label.isEmpty()) {
            p.setPen(QColor(170, 170, 170));
            p.drawText(QRectF(x + 3, 2, 80, 14), label);
        }
    };

    // bars
    for (int b = 0; b <= m_previewBars; ++b) {
        const qint64 ms = qint64(llround(double(b) * barMs));
        drawV(ms, QColor(80, 80, 80), 2, QString("bar %1").arg(b + 1));
    }
    // beats + subdivisions
    const int beatsTotal = m_previewBars * qMax(1, m_tsNum);
    for (int bi = 0; bi <= beatsTotal; ++bi) {
        const qint64 ms = qint64(llround(double(bi) * beatMs));
        if (bi % qMax(1, m_tsNum) == 0) continue; // already bar
        drawV(ms, QColor(55, 55, 55), 1, QString());
        // subdivs inside beat
        for (int s = 1; s < m_subdivPerBeat; ++s) {
            const double subMs = beatMs * (double(s) / double(m_subdivPerBeat));
            const qint64 sm = qint64(llround(double(bi) * beatMs + subMs));
            drawV(sm, QColor(32, 32, 32), 1, QString());
        }
    }

    // Events
    for (const auto& ev : m_events) {
        const int li = m_lanes.indexOf(ev.lane);
        if (li < 0) continue;
        QRectF r = eventRect(li, ev);

        // Color by lane
        QColor fill(70, 120, 220, 180);
        if (ev.lane.compare("Drums", Qt::CaseInsensitive) == 0) fill = QColor(200, 140, 70, 190);
        if (ev.lane.compare("Bass", Qt::CaseInsensitive) == 0) fill = QColor(80, 200, 130, 190);
        if (ev.lane.compare("Piano", Qt::CaseInsensitive) == 0) fill = QColor(120, 160, 240, 190);
        if (ev.lane.compare("Rhythm", Qt::CaseInsensitive) == 0) fill = QColor(180, 180, 180, 120);
        if (ev.lane.compare("Pedal", Qt::CaseInsensitive) == 0) fill = QColor(230, 200, 70, 170);
        if (ev.lane.compare("Articulation", Qt::CaseInsensitive) == 0) fill = QColor(210, 120, 220, 160);
        if (ev.lane.compare("KeySwitch", Qt::CaseInsensitive) == 0) fill = QColor(155, 95, 210, 160);
        if (ev.lane.compare("ArticulationState", Qt::CaseInsensitive) == 0) fill = QColor(110, 85, 170, 140);
        if (ev.lane.compare("FX", Qt::CaseInsensitive) == 0) fill = QColor(240, 150, 70, 160);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(r, 3, 3);

        // High-contrast labels (elide to fit). This is critical for usability.
        QFont f = p.font();
        f.setPointSizeF(qMax(8.0, f.pointSizeF() - 1.0));
        p.setFont(f);
        const QString raw = ev.label.isEmpty()
            ? QString("n%1 v%2").arg(ev.note).arg(ev.velocity)
            : ev.label;
        const QString text = p.fontMetrics().elidedText(raw, Qt::ElideRight, int(r.width()) - 8);

        // Shadow + white text for readability on all fills.
        p.setPen(QColor(0, 0, 0, 160));
        p.drawText(r.adjusted(5, 1, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
        p.setPen(QColor(245, 245, 245));
        p.drawText(r.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    }

    // Playhead
    if (m_playheadMs >= 0) {
        const double x = xForMs(m_playheadMs);
        p.setPen(QPen(QColor(255, 70, 70), 2));
        p.drawLine(QPointF(x, topPad), QPointF(x, topPad + plotH));
    }

    // bottom time label
    p.setPen(QColor(140, 140, 140));
    p.drawText(QRect(leftPad, height() - 16, width() - leftPad, 14),
               Qt::AlignLeft | Qt::AlignVCenter,
               QString("Tempo=%1  TimeSig=%2/%3  Subdiv=%4/beat  Total=%5ms")
                   .arg(m_bpm).arg(m_tsNum).arg(m_tsDen).arg(m_subdivPerBeat).arg(totMs));
}

} // namespace virtuoso::ui

