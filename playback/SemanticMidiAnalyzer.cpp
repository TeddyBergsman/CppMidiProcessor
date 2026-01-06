#include "playback/SemanticMidiAnalyzer.h"

#include "music/ChordSymbol.h"

#include <limits>
#include <QtMath>

namespace playback {

void SemanticMidiAnalyzer::reset() {
    m_noteOnTimesMs.clear();
    m_recentPitchClasses.clear();
    m_lastVelocity = 0;
    m_lastNoteOnMs = -1;
    m_registerEma = 60;
    m_allowedPcs.clear();
}

static int thirdIntervalForQuality(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::Minor:
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished: return 3;
        case music::ChordQuality::Sus2: return 2;
        case music::ChordQuality::Sus4: return 5;
        default: return 4;
    }
}

static int fifthIntervalForQuality(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished: return 6;
        case music::ChordQuality::Augmented: return 8;
        default: return 7;
    }
}

static int seventhIntervalFor(const music::ChordSymbol& c) {
    if (c.seventh == music::SeventhQuality::Major7) return 11;
    if (c.seventh == music::SeventhQuality::Dim7) return 9;
    if (c.seventh == music::SeventhQuality::Minor7) return 10;
    if (c.extension >= 7) return 10;
    return -1;
}

QSet<int> SemanticMidiAnalyzer::allowedPitchClassesForChord(const music::ChordSymbol& c) {
    QSet<int> pcs;
    const int root = (c.rootPc >= 0) ? c.rootPc : 0;
    const int bass = (c.bassPc >= 0) ? c.bassPc : root;

    auto pc = [&](int semi) -> int { return (root + semi + 1200) % 12; };
    auto applyAlter = [&](int degree, int basePc) -> int {
        for (const auto& a : c.alterations) {
            if (a.degree != degree) continue;
            return (basePc + a.delta + 1200) % 12;
        }
        return basePc;
    };

    // Always allow bass/root.
    pcs.insert((bass + 12) % 12);
    pcs.insert((root + 12) % 12);

    // Core chord tones + common extensions.
    const int pc3 = pc(thirdIntervalForQuality(c.quality));
    const int pc5 = applyAlter(5, pc(fifthIntervalForQuality(c.quality)));
    pcs.insert(pc3);
    pcs.insert(pc5);

    const int sev = seventhIntervalFor(c);
    if (sev >= 0) pcs.insert(pc(sev));

    // Extensions (if present/likely): 9/11/13 + alterations.
    pcs.insert(applyAlter(9, pc(14)));
    pcs.insert(applyAlter(11, pc(17)));
    pcs.insert(applyAlter(13, pc(21)));

    // If explicitly noChord, leave empty (caller will treat outside as false).
    if (c.noChord) pcs.clear();
    return pcs;
}

void SemanticMidiAnalyzer::setChordContext(const music::ChordSymbol& chord) {
    m_allowedPcs.clear();
    m_allowedPcs = allowedPitchClassesForChord(chord);
}

void SemanticMidiAnalyzer::ingestNoteOn(Source, int midiNote, int velocity, qint64 timestampMs) {
    midiNote = clampMidi(midiNote);
    velocity = qBound(0, velocity, 127);
    m_lastVelocity = velocity;
    m_lastNoteOnMs = timestampMs;

    // Register center (EMA): bias toward recent notes but stable enough for comping decisions.
    // alpha chosen empirically for responsiveness without jitter.
    constexpr double alpha = 0.20;
    m_registerEma = int(llround((1.0 - alpha) * double(m_registerEma) + alpha * double(midiNote)));

    // Density window
    m_noteOnTimesMs.push_back(timestampMs);
    const qint64 cutoff = timestampMs - qMax(1, m_s.densityWindowMs);
    while (!m_noteOnTimesMs.isEmpty() && m_noteOnTimesMs.front() < cutoff) {
        m_noteOnTimesMs.pop_front();
    }

    // Outside window (pitch classes)
    m_recentPitchClasses.push_back(midiNote % 12);
    while (m_recentPitchClasses.size() > qMax(1, m_s.outsideWindowNotes)) {
        m_recentPitchClasses.pop_front();
    }
}

void SemanticMidiAnalyzer::ingestNoteOff(Source, int midiNote, qint64) {
    Q_UNUSED(midiNote);
}

SemanticMidiAnalyzer::IntentState SemanticMidiAnalyzer::compute(qint64 nowMs) const {
    IntentState out;
    out.lastVelocity = m_lastVelocity;
    out.registerCenterMidi = m_registerEma;
    out.msSinceLastNoteOn = (m_lastNoteOnMs < 0) ? std::numeric_limits<qint64>::max() : (nowMs - m_lastNoteOnMs);

    const int winMs = qMax(1, m_s.densityWindowMs);
    out.notesPerSec = double(m_noteOnTimesMs.size()) * (1000.0 / double(winMs));

    out.silence = (out.msSinceLastNoteOn >= qint64(qMax(1, m_s.silenceMs)));
    out.densityHigh = (out.notesPerSec >= m_s.densityHighNotesPerSec) && !out.silence;
    out.registerHigh = (out.registerCenterMidi >= m_s.registerHighCenterMidi) && !out.silence;
    out.intensityPeak = (!out.silence) &&
                        (out.lastVelocity >= m_s.intensityPeakVelocity) &&
                        (out.notesPerSec >= m_s.intensityPeakNotesPerSec);

    // Playing outside: compare recent pitch classes to allowed chord set.
    // If no chord context, do not assert "outside".
    if (!m_allowedPcs.isEmpty() && !m_recentPitchClasses.isEmpty()) {
        int outCount = 0;
        for (int pc : m_recentPitchClasses) {
            if (!m_allowedPcs.contains(pc)) outCount++;
        }
        out.outsideRatio = double(outCount) / double(m_recentPitchClasses.size());
        out.playingOutside = (!out.silence) && (out.outsideRatio >= m_s.outsideRatioThreshold);
    } else {
        out.outsideRatio = 0.0;
        out.playingOutside = false;
    }

    return out;
}

} // namespace playback

