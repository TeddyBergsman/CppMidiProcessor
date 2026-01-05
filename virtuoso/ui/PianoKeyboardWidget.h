#pragma once

#include <QWidget>
#include <QSet>

class PianoKeyboardWidget final : public QWidget {
    Q_OBJECT
public:
    explicit PianoKeyboardWidget(QWidget* parent = nullptr);

    // Highlight by pitch class (pc = 0..11)
    void setHighlightedPitchClasses(QSet<int> pcs);
    const QSet<int>& highlightedPitchClasses() const { return m_pcs; }

    // Optional: render the root pitch class distinctly.
    void setRootPitchClass(int pc); // -1 disables
    int rootPitchClass() const { return m_rootPc; }

    // Display range (inclusive). The widget supports full 88 keys, but can display a subset.
    void setRange(int minMidi, int maxMidi);
    int minMidi() const { return m_minMidi; }
    int maxMidi() const { return m_maxMidi; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static int normalizePc(int pc);
    static bool isBlackPc(int pc);

    QSet<int> m_pcs;
    int m_rootPc = -1;
    int m_minMidi = 45; // A2
    int m_maxMidi = 72; // C5
};

