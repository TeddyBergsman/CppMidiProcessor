#include "PitchMonitorWidget.h"

#include <PitchColor.h>
#include <QPainter>
#include <QPaintEvent>
#include <QtMath>
#include <algorithm>

static QColor colorForCentsWithAlpha(double cents, int alpha) {
    QColor c(pitchColorForCents(cents));
    c.setAlpha(alpha);
    return c;
}

static int baseLetterPc(QChar letter) {
    switch (letter.toUpper().unicode()) {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        case 'B': return 11;
        default: return 0;
    }
}

PitchMonitorWidget::PitchMonitorWidget(QWidget* parent)
    : QWidget(parent) {
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(140);

    m_clock.start();

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, [this]() { tick(); });
    m_timer->start();
}

void PitchMonitorWidget::setBpm(int bpm) {
    m_bpm = std::clamp(bpm, 30, 300);
    update();
}

void PitchMonitorWidget::setKeyCenter(const QString& keyCenter) {
    m_keyCenter = keyCenter;
    update();
}

void PitchMonitorWidget::pushSample(QVector<Sample>& stream, int midiNote, double cents,
                                    double& lastAppendSec, int& lastMidi, double& lastCents) {
    const double t = nowSec();
    const double c = std::clamp(cents, -50.0, 50.0);

    // Throttle samples aggressively unless something meaningful changed.
    const bool first = lastAppendSec < 0.0;
    const bool noteChanged = (midiNote != lastMidi);
    const bool centsChanged = std::fabs(c - lastCents) >= 0.6;
    const bool timeOk = first || (t - lastAppendSec) >= m_minAppendIntervalSec;

    if (!timeOk && !noteChanged && !centsChanged) {
        return;
    }

    // Insert an explicit gap when switching between two active notes
    // to avoid tall vertical connector segments.
    if (!first && noteChanged && lastMidi >= 0 && midiNote >= 0) {
        Sample gap;
        gap.tSec = t;
        gap.midiNote = -1;
        gap.cents = 0.0;
        stream.push_back(gap);
    }

    Sample s;
    s.tSec = t;
    s.midiNote = midiNote;
    s.cents = c;
    stream.push_back(s);

    lastAppendSec = t;
    lastMidi = midiNote;
    lastCents = c;

    if (midiNote >= 0) {
        updateVerticalTargetForNote(midiNote);
    }
}

void PitchMonitorWidget::pushGuitar(int midiNote, double cents) {
    pushSample(m_guitar, midiNote, cents, m_lastGuitarAppendSec, m_lastGuitarMidi, m_lastGuitarCents);
}

void PitchMonitorWidget::pushVocal(int midiNote, double cents) {
    pushSample(m_vocal, midiNote, cents, m_lastVocalAppendSec, m_lastVocalMidi, m_lastVocalCents);
}

double PitchMonitorWidget::nowSec() const {
    if (!m_clock.isValid()) return 0.0;
    return m_clock.elapsed() * 0.001;
}

double PitchMonitorWidget::pxPerSecond() const {
    // px/sec = beats/sec * px/beat
    const double beatsPerSec = std::max(1.0, static_cast<double>(m_bpm)) / 60.0;
    return beatsPerSec * static_cast<double>(m_pxPerBeat);
}

double PitchMonitorWidget::midiToY(double midi) const {
    const double h = std::max(1, height());
    const double pxPerSemi = h / std::max(1.0, m_visibleSemis);
    // Higher midi => smaller y (top)
    const double topMidi = m_centerMidi + (m_visibleSemis * 0.5);
    return (topMidi - midi) * pxPerSemi;
}

bool PitchMonitorWidget::preferFlats() const {
    QString k = m_keyCenter.toLower();
    static const QStringList flatKeys = {
        "f major","bb major","b♭ major","eb major","e♭ major","ab major","a♭ major","db major","d♭ major","gb major","g♭ major","cb major","c♭ major"
    };
    for (const QString& fk : flatKeys) {
        if (k == fk) return true;
    }
    if (k.contains("b major") || k.contains(QChar(0x266D))) return true;
    return false;
}

int PitchMonitorWidget::keyRootPitchClass() const {
    // Expects "Eb major" etc; take first token and parse accidental.
    QString root = m_keyCenter.trimmed().split(' ').value(0);
    if (root.isEmpty()) return 0;
    QChar letter = root[0];
    int pc = baseLetterPc(letter);
    if (root.size() >= 2) {
        QChar acc = root[1];
        if (acc == QChar('b') || acc == QChar(0x266D)) pc -= 1;
        if (acc == QChar('#') || acc == QChar(0x266F)) pc += 1;
    }
    pc %= 12;
    if (pc < 0) pc += 12;
    return pc;
}

bool PitchMonitorWidget::isPitchClassInKeyMajorScale(int pitchClass) const {
    // Major scale intervals: 0,2,4,5,7,9,11
    static const int intervals[] = {0,2,4,5,7,9,11};
    const int rootPc = keyRootPitchClass();
    const int pc = ((pitchClass % 12) + 12) % 12;
    for (int iv : intervals) {
        if (((rootPc + iv) % 12) == pc) return true;
    }
    return false;
}

QString PitchMonitorWidget::formatNoteShort(int midiNote) const {
    if (midiNote < 0) return QString();
    int octave = midiNote / 12 - 1;
    int pc = midiNote % 12;
    if (pc < 0) pc += 12;

    // Use the same spelling approach as NoteMonitorWidget (but render ♭/♯).
    static const char* lettersSharp[] = {"C","C","D","D","E","F","F","G","G","A","A","B"};
    static const QChar accSharp[] = {QChar(),QChar(0x266F),QChar(),QChar(0x266F),QChar(),QChar(),QChar(0x266F),QChar(),QChar(0x266F),QChar(),QChar(0x266F),QChar()};
    static const char* lettersFlat[]  = {"C","D","D","E","E","F","G","G","A","A","B","B"};
    static const QChar accFlat[]  = {QChar(),QChar(0x266D),QChar(),QChar(0x266D),QChar(),QChar(),QChar(0x266D),QChar(),QChar(0x266D),QChar(),QChar(0x266D),QChar()};

    const bool useFlat = preferFlats();
    QChar letterOut = useFlat ? QChar(lettersFlat[pc][0]) : QChar(lettersSharp[pc][0]);
    QChar accOut = useFlat ? accFlat[pc] : accSharp[pc];

    if (accOut.isNull()) {
        return QString("%1%2").arg(QString(letterOut)).arg(octave);
    }
    return QString("%1%2%3").arg(QString(letterOut)).arg(QString(accOut)).arg(octave);
}

void PitchMonitorWidget::updateVerticalTargetForNote(int midiNote) {
    const double half = m_visibleSemis * 0.5;
    const double lo = m_targetCenterMidi - half + m_recenterMarginSemis;
    const double hi = m_targetCenterMidi + half - m_recenterMarginSemis;
    const double n = static_cast<double>(midiNote);

    if (n < lo) {
        m_targetCenterMidi = n + (half - m_recenterMarginSemis);
    } else if (n > hi) {
        m_targetCenterMidi = n - (half - m_recenterMarginSemis);
    }
}

void PitchMonitorWidget::pruneOldSamples() {
    const double now = nowSec();
    const double pps = pxPerSecond();
    if (pps <= 1.0) return;

    // Keep a little extra history to avoid popping.
    const double idealWindowSec = (width() / pps) + 0.5;
    const double windowSec = std::min(idealWindowSec, m_maxHistorySec);
    const double minT = now - windowSec;

    auto pruneVec = [&](QVector<Sample>& v) {
        int firstKeep = 0;
        while (firstKeep < v.size() && v[firstKeep].tSec < minT) {
            ++firstKeep;
        }
        if (firstKeep > 0) {
            v.remove(0, firstKeep);
        }
    };

    pruneVec(m_guitar);
    pruneVec(m_vocal);
}

void PitchMonitorWidget::tick() {
    // Smoothly animate center to target to prevent jumpy vertical scrolling.
    const double alpha = 0.18; // smoothing
    m_centerMidi += (m_targetCenterMidi - m_centerMidi) * alpha;

    // Keep-alive sampling so held notes continue to draw even if upstream emits no changes.
    // MidiProcessor intentionally throttles pitch updates; this fills in the visual timeline.
    if (m_lastGuitarMidi >= 0) {
        pushSample(m_guitar, m_lastGuitarMidi, m_lastGuitarCents,
                   m_lastGuitarAppendSec, m_lastGuitarMidi, m_lastGuitarCents);
    }
    if (m_lastVocalMidi >= 0) {
        pushSample(m_vocal, m_lastVocalMidi, m_lastVocalCents,
                   m_lastVocalAppendSec, m_lastVocalMidi, m_lastVocalCents);
    }

    pruneOldSamples();

    // If nothing is active and we have no history, don't repaint constantly.
    // (Still keep the timer for smoothness when active.)
    if (!m_guitar.isEmpty() || !m_vocal.isEmpty()) {
        update();
    }
}

void PitchMonitorWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    pruneOldSamples();
}

void PitchMonitorWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();
    if (w <= 2 || h <= 2) return;

    const double now = nowSec();
    const double pps = pxPerSecond();

    // Determine "current" played notes per stream based on the most recent sample
    // (including note-off). This lets us highlight guitar + vocal simultaneously.
    int guitarNote = -1;
    double guitarCents = 0.0;
    int vocalNote = -1;
    double vocalCents = 0.0;
    auto lastSample = [](const QVector<Sample>& v) -> Sample {
        return v.isEmpty() ? Sample{} : v.last();
    };
    Sample gLast = lastSample(m_guitar);
    Sample vLast = lastSample(m_vocal);
    if (gLast.tSec > 0.0 && (now - gLast.tSec) <= 0.75 && gLast.midiNote >= 0) {
        guitarNote = gLast.midiNote;
        guitarCents = gLast.cents;
    }
    if (vLast.tSec > 0.0 && (now - vLast.tSec) <= 0.75 && vLast.midiNote >= 0) {
        vocalNote = vLast.midiNote;
        vocalCents = vLast.cents;
    }

    // Reserve a label gutter to the right so labels never overlap plotted lines.
    // Make it only as wide as needed for currently visible labels.
    const int labelPad = 4;
    int labelGutterW = 0;
    {
        QFontMetrics fm(p.font());
        const double half = m_visibleSemis * 0.5;
        const int midiTop = static_cast<int>(std::ceil(m_centerMidi + half));
        const int midiBottom = static_cast<int>(std::floor(m_centerMidi - half));
        int maxW = 0;
        for (int m = midiBottom; m <= midiTop; ++m) {
            const int pc = ((m % 12) + 12) % 12;
            const bool inScale = isPitchClassInKeyMajorScale(pc);
            const bool isG = (m == guitarNote);
            const bool isV = (m == vocalNote);
            if (!inScale && !isG && !isV) continue;
            maxW = std::max(maxW, fm.horizontalAdvance(formatNoteShort(m)));
        }
        // Tight fit: just enough room for the text + minimal padding.
        labelGutterW = maxW + (labelPad * 2) + 2;
        // Avoid collapsing completely (keeps a stable layout when there are no labels).
        labelGutterW = std::max(labelGutterW, 24);
        // Cap so it can't steal too much plot space.
        labelGutterW = std::min(labelGutterW, std::max(24, w / 4));
    }

    const QRect plotRect = rect().adjusted(0, 0, -labelGutterW, 0);
    const QRect labelRect = rect().adjusted(plotRect.right() + 1, 0, 0, 0);
    if (plotRect.width() <= 10) return;

    // Draw label gutter background + separator FIRST (so labels draw on top).
    {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(labelRect, Qt::black);
        p.setRenderHint(QPainter::Antialiasing, true);
    }

    // Plot-space mapping helpers
    auto yFromMidi = [&](double midi) -> double {
        const double ph = std::max(1, plotRect.height());
        const double pxPerSemi = ph / std::max(1.0, m_visibleSemis);
        const double topMidi = m_centerMidi + (m_visibleSemis * 0.5);
        return plotRect.top() + (topMidi - midi) * pxPerSemi;
    };
    auto xFromTime = [&](double tSec) -> double {
        const double age = now - tSec;
        return plotRect.right() - (age * pps);
    };

    // --- Grid ---
    {
        p.setRenderHint(QPainter::Antialiasing, false);
        const double half = m_visibleSemis * 0.5;
        const int midiTop = static_cast<int>(std::ceil(m_centerMidi + half));
        const int midiBottom = static_cast<int>(std::floor(m_centerMidi - half));
        const int labelW = labelRect.width() - (labelPad * 2);
        const QRect labelRectTemplate(labelRect.left() + labelPad, 0, std::max(10, labelW), 14);

        for (int m = midiBottom; m <= midiTop; ++m) {
            const double y = yFromMidi(static_cast<double>(m));
            if (y < -1 || y > h + 1) continue;

            const int pc = ((m % 12) + 12) % 12;
            const bool isC = (pc == 0);
            const bool isOctaveLine = isC; // keep C lines stronger as octave references

            QPen pen(isOctaveLine ? QColor(80, 80, 80) : QColor(40, 40, 40));
            pen.setWidth(isOctaveLine ? 2 : 1);
            p.setPen(pen);
            p.drawLine(plotRect.left(), static_cast<int>(y), plotRect.right(), static_cast<int>(y));

            // Note labels: only show notes in the current key's major scale.
            // If we're currently playing a non-scale note, temporarily show it so it can be highlighted.
            const bool inScale = isPitchClassInKeyMajorScale(pc);
            const bool isG = (m == guitarNote);
            const bool isV = (m == vocalNote);
            if (!inScale && !isG && !isV) continue;

            // Non-tonic (non-C within the grid) are lower opacity; the currently played note is highlighted.
            QRect r = labelRectTemplate;
            r.moveTop(static_cast<int>(y) - (r.height() / 2));

            if (isG || isV) {
                // Highlight all currently active notes. If both streams hit the same pitch,
                // draw twice with a 1px offset so both colors are visible.
                if (isG) {
                    QColor hl(pitchColorForCents(guitarCents));
                    hl.setAlpha(255);
                    p.setPen(hl);
                    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, formatNoteShort(m));
                }
                if (isV) {
                    QColor hl(pitchColorForCents(vocalCents));
                    hl.setAlpha(255);
                    p.setPen(hl);
                    QRect r2 = r;
                    if (isG) r2.translate(1, 0);
                    p.drawText(r2, Qt::AlignLeft | Qt::AlignVCenter, formatNoteShort(m));
                }
            } else {
                QColor textColor(160, 160, 160);
                // Dim non-root scale degrees a bit more (C is just a reference; not necessarily tonic for the key).
                textColor.setAlpha(isC ? 200 : 110);
                p.setPen(textColor);
                p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, formatNoteShort(m));
            }
        }
        p.setRenderHint(QPainter::Antialiasing, true);
    }

    // --- Helper to draw stream ---
    auto drawStream = [&](const QVector<Sample>& v, bool dotted, int alpha) {
        if (v.size() < 2) return;
        QPen pen;
        pen.setWidth(2);
        pen.setCosmetic(true); // keep 1px regardless of scaling
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setStyle(dotted ? Qt::DotLine : Qt::SolidLine);

        bool havePrev = false;
        QPointF prevPt;
        Sample prevS;

        for (int i = 0; i < v.size(); ++i) {
            const Sample& s = v[i];
            const double x = xFromTime(s.tSec);
            if (x < -50) continue;      // far left of view
            if (x > plotRect.right() + 10) continue;   // future (shouldn't happen)

            if (s.midiNote < 0) {
                havePrev = false;
                continue;
            }
            const QPointF pt(x, yFromMidi(static_cast<double>(s.midiNote) + (s.cents / 100.0)));

            if (havePrev) {
                // Break long time gaps (prevents diagonal streaks on silence).
                const double dt = s.tSec - prevS.tSec;
                // Also break on large pitch jumps to avoid vertical spikes
                // (expected behavior is "gap" between separate notes).
                const double prevMidi = static_cast<double>(prevS.midiNote) + (prevS.cents / 100.0);
                const double currMidi = static_cast<double>(s.midiNote) + (s.cents / 100.0);
                const double dSemi = std::fabs(currMidi - prevMidi);
                if (dt > 0.25 || dSemi >= 1.25) {
                    havePrev = false;
                } else {
                    pen.setColor(colorForCentsWithAlpha(s.cents, alpha));
                    p.setPen(pen);
                    p.drawLine(prevPt, pt);
                }
            }

            prevPt = pt;
            prevS = s;
            havePrev = true;
        }
    };

    // Guitar: solid, vocal: solid at ~70% opacity
    drawStream(m_guitar, false, 220);
    drawStream(m_vocal, false, 178);

}

