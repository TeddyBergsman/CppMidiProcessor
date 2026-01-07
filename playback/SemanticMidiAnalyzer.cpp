#include "playback/SemanticMidiAnalyzer.h"

#include "music/ChordSymbol.h"

#include <limits>
#include <QtMath>

namespace playback {

void SemanticMidiAnalyzer::reset() {
    m_noteOnTimesMs.clear();
    m_recentPitchClasses.clear();
    m_lastGuitarVelocity = 0;
    m_lastCc2 = 0;
    m_lastGuitarNoteOnMs = -1;
    m_lastActivityMs = -1;
    m_lastVoiceMidi = -1;
    m_lastVoiceNoteOnMs = -1;
    m_registerEma = 60;
    m_allowedPcs.clear();
    m_guitarActive.fill(false);
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

void SemanticMidiAnalyzer::ingestGuitarNoteOn(int midiNote, int velocity, qint64 timestampMs) {
    midiNote = clampMidi(midiNote);
    velocity = qBound(0, velocity, 127);

    if (m_guitarActive[midiNote]) {
        // Duplicate NOTE_ON while key is held: update last velocity,
        // but do NOT count this as a new attack for density.
        m_lastGuitarVelocity = velocity;
        return;
    }
    m_guitarActive[midiNote] = true;
    m_lastGuitarVelocity = velocity;
    m_lastGuitarNoteOnMs = timestampMs;
    m_lastActivityMs = timestampMs;

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

void SemanticMidiAnalyzer::ingestGuitarNoteOff(int midiNote, qint64) {
    midiNote = clampMidi(midiNote);
    m_guitarActive[midiNote] = false;
}

void SemanticMidiAnalyzer::ingestCc2(int value, qint64 timestampMs) {
    value = qBound(0, value, 127);
    m_lastCc2 = value;
    if (value >= qMax(0, m_s.cc2ActivityFloor)) {
        // Vocal energy counts as "activity" (prevents SILENCE) and can trigger intensity peak.
        m_lastActivityMs = timestampMs;
    }
}

void SemanticMidiAnalyzer::ingestVoiceNoteOn(int midiNote, int velocity, qint64 timestampMs) {
    Q_UNUSED(velocity);
    midiNote = clampMidi(midiNote);
    m_lastVoiceMidi = midiNote;
    m_lastVoiceNoteOnMs = timestampMs;
    // Vocal notes do NOT affect density, but they do count as "activity" (we're hearing a melody).
    m_lastActivityMs = timestampMs;
}

void SemanticMidiAnalyzer::ingestVoiceNoteOff(int midiNote, qint64) {
    Q_UNUSED(midiNote);
}

SemanticMidiAnalyzer::IntentState SemanticMidiAnalyzer::compute(qint64 nowMs) const {
    IntentState out;
    out.lastGuitarVelocity = m_lastGuitarVelocity;
    out.lastCc2 = m_lastCc2;
    out.registerCenterMidi = m_registerEma;
    out.msSinceLastGuitarNoteOn = (m_lastGuitarNoteOnMs < 0) ? std::numeric_limits<qint64>::max() : (nowMs - m_lastGuitarNoteOnMs);
    out.msSinceLastActivity = (m_lastActivityMs < 0) ? std::numeric_limits<qint64>::max() : (nowMs - m_lastActivityMs);
    out.lastVoiceMidi = m_lastVoiceMidi;
    out.msSinceLastVoiceNoteOn = (m_lastVoiceNoteOnMs < 0) ? std::numeric_limits<qint64>::max() : (nowMs - m_lastVoiceNoteOnMs);

    const int winMs = qMax(1, m_s.densityWindowMs);
    // IMPORTANT: decay density over time even if no new notes arrive.
    // m_noteOnTimesMs is append-only and time-ordered.
    const qint64 cutoff = nowMs - qMax(1, m_s.densityWindowMs);
    int recentCount = 0;
    for (int i = m_noteOnTimesMs.size() - 1; i >= 0; --i) {
        if (m_noteOnTimesMs[i] < cutoff) break;
        recentCount++;
    }
    out.notesPerSec = double(recentCount) * (1000.0 / double(winMs));

    out.silence = (out.msSinceLastActivity >= qint64(qMax(1, m_s.silenceMs)));
    out.densityHigh = (out.notesPerSec >= m_s.densityHighNotesPerSec) && !out.silence;
    out.registerHigh = (out.registerCenterMidi >= m_s.registerHighCenterMidi) && !out.silence;
    out.intensityPeak = (!out.silence) && (out.lastCc2 >= m_s.intensityPeakCc2);

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

    // --- Phrase-level interaction heuristics (stateless) ---
    // Approximate "silence onset" without relying on previous-state memory:
    // If we're just barely beyond the silence threshold, consider it an onset.
    const qint64 silMs = qint64(qMax(1, m_s.silenceMs));
    out.silenceOnset = out.silence && (out.msSinceLastActivity >= silMs) && (out.msSinceLastActivity <= silMs + 260);

    // "Question ended" heuristic:
    // - user just went silent
    // - AND they were musically active shortly before (density window still has notes, or CC2 was elevated recently)
    const bool wasActiveRecently = (out.notesPerSec >= (0.45 * m_s.densityHighNotesPerSec))
        || (out.msSinceLastGuitarNoteOn <= qint64(900))
        || (out.lastCc2 >= qMax(0, m_s.intensityPeakCc2 - 18));
    out.questionEnded = out.silenceOnset && wasActiveRecently;

    return out;
}

} // namespace playback

