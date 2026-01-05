#pragma once

#include <QWidget>
#include <QSet>
#include <QHash>

class GuitarFretboardWidget final : public QWidget {
    Q_OBJECT
public:
    explicit GuitarFretboardWidget(QWidget* parent = nullptr);

    void setHighlightedPitchClasses(QSet<int> pcs);
    const QSet<int>& highlightedPitchClasses() const { return m_pcs; }

    // Optional: render the root pitch class distinctly.
    void setRootPitchClass(int pc); // -1 disables
    int rootPitchClass() const { return m_rootPc; }

    // Optional: pitch-class -> degree label (e.g. "1", "3", "b7")
    void setDegreeLabels(QHash<int, QString> labels);

    // Optional: highlight specific MIDI notes (e.g. currently sounding notes).
    void setActiveMidiNotes(QSet<int> midis);

    void setFretCount(int frets); // default 24
    int fretCount() const { return m_frets; }

signals:
    void noteClicked(int midiNote);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    static QString pcName(int pc);
    static bool isBlackPc(int pc);

    int midiAtPoint(const QPoint& pos) const; // -1 if none
    QString tooltipForMidi(int midi) const;

    QSet<int> m_pcs;
    QHash<int, QString> m_degreeForPc;
    QSet<int> m_activeMidis;
    int m_rootPc = -1;
    int m_frets = 24;
    int m_lastTooltipMidi = -999;
};

