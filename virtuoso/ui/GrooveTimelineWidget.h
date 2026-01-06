#pragma once

#include <QWidget>
#include <QString>
#include <QVector>

namespace virtuoso::ui {

// Visual grid timeline:
// - lanes: instrument rows
// - x-axis: time (bars/beats/subdivision)
// - events drawn at their *humanized* onset times
class GrooveTimelineWidget : public QWidget {
    Q_OBJECT
public:
    struct LaneEvent {
        QString lane;      // e.g. "Drums", "Bass", "Piano"
        int note = 0;
        int velocity = 0;
        qint64 onMs = 0;   // relative to preview start
        qint64 offMs = 0;
        QString label;     // optional (e.g. articulation / voicing)
    };

    explicit GrooveTimelineWidget(QWidget* parent = nullptr);

    void setTempoAndSignature(int bpm, int tsNum, int tsDen);
    void setPreviewBars(int bars);
    void setSubdivision(int subdivPerBeat); // e.g. 2=8ths, 3=triplets, 4=16ths

    void setLanes(const QStringList& lanes); // order top->bottom
    void setEvents(const QVector<LaneEvent>& events);

    void setPlayheadMs(qint64 ms); // relative to preview start (for audition)

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

signals:
    void eventClicked(const QString& lane, int note, int velocity, const QString& label);

private:
    int m_bpm = 60;
    int m_tsNum = 4;
    int m_tsDen = 4;
    int m_previewBars = 4;
    int m_subdivPerBeat = 2;

    QStringList m_lanes;
    QVector<LaneEvent> m_events;
    qint64 m_playheadMs = -1;

    QRect laneRect(int laneIndex) const;
    qint64 totalMs() const;
    double xForMs(qint64 ms) const;
    int laneIndexForY(int y) const;
    int hitTestEventIndex(const QPoint& p) const;

    int subRowCountForLane(const QString& lane) const;
    int subRowIndexForEvent(const QString& lane, int note) const;
    QRectF eventRect(int laneIndex, const LaneEvent& ev) const;
};

} // namespace virtuoso::ui

