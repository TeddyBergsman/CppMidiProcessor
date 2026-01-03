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
    // Used to display Roman numeral chord functions (e.g. ii–V–I) relative to the key.
    void setKeyCenter(const QString& keyCenter);

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
    int m_sectionGutter = 36; // room for section + time signature (tighter like iReal)
    int m_lineHeight = 96;
    int m_barHeight = 88;

    // Flattened cell rects in content coordinates (not viewport coords)
    QVector<QRect> m_cellRects;
    int m_currentCell = -1;

    // Key center string like "Eb major" (drives Roman numeral display).
    QString m_keyCenter = "Eb major";
};

} // namespace chart

