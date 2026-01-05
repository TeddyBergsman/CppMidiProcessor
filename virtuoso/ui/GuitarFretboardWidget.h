#pragma once

#include <QWidget>
#include <QSet>

class GuitarFretboardWidget final : public QWidget {
    Q_OBJECT
public:
    explicit GuitarFretboardWidget(QWidget* parent = nullptr);

    void setHighlightedPitchClasses(QSet<int> pcs);
    const QSet<int>& highlightedPitchClasses() const { return m_pcs; }

    // Optional: render the root pitch class distinctly.
    void setRootPitchClass(int pc); // -1 disables
    int rootPitchClass() const { return m_rootPc; }

    void setFretCount(int frets); // default 24
    int fretCount() const { return m_frets; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static QString pcName(int pc);
    static bool isBlackPc(int pc);

    QSet<int> m_pcs;
    int m_rootPc = -1;
    int m_frets = 24;
};

