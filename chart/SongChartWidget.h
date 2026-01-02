#pragma once

#include <QAbstractScrollArea>
#include <QVector>

#include "chart/ChartModel.h"

namespace chart {

class SongChartWidget : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit SongChartWidget(QWidget* parent = nullptr);

    void setChartModel(const ChartModel& model);
    void clear();

public slots:
    // Highlights a flattened cell index (0..bars*4-1).
    void setCurrentCellIndex(int cellIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuildLayout();
    void ensureCellVisible(int cellIndex);

    ChartModel m_model;
    bool m_hasModel = false;

    // Layout metrics
    int m_margin = 12;
    int m_sectionGutter = 70; // room for section + time signature
    int m_lineHeight = 110;
    int m_barHeight = 88;

    // Flattened cell rects in content coordinates (not viewport coords)
    QVector<QRect> m_cellRects;
    int m_currentCell = -1;
};

} // namespace chart

