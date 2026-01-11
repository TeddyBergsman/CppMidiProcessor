#pragma once

#include <QVector>
#include "music/ChordSymbol.h"

namespace playback {
namespace voicing_utils {

/// Normalize a MIDI note to pitch class 0-11
inline int normalizePc(int midiOrPc) {
    int pc = midiOrPc % 12;
    return (pc < 0) ? pc + 12 : pc;
}

/// Clamp MIDI note to valid range
inline int clampMidi(int m) {
    return qBound(0, m, 127);
}

/// Get pitch class for a given scale degree in a chord
int pcForDegree(const music::ChordSymbol& c, int degree);

/// Get the third interval (in semitones) for a chord quality
int thirdInterval(music::ChordQuality q);

/// Get the fifth interval (in semitones) for a chord quality  
int fifthInterval(music::ChordQuality q);

/// Get the seventh interval (in semitones) for a chord symbol
/// For 6th chords, returns 9 (the 6th interval) as the "color tone"
int seventhInterval(const music::ChordSymbol& c);

/// Returns true if this chord uses a 6th instead of a 7th
bool is6thChord(const music::ChordSymbol& c);

/// Find the nearest MIDI note for a pitch class within bounds
int nearestMidiForPc(int pc, int around, int lo, int hi);

/// Determine what chord degree a pitch class represents
int getDegreeForPc(int pc, const music::ChordSymbol& chord);

/// Calculate voice-leading cost between two voicings
double voiceLeadingCost(const QVector<int>& prev, const QVector<int>& next);

/// Realize pitch classes to MIDI within register with minimal voice movement
QVector<int> realizePcsToMidi(const QVector<int>& pcs, int lo, int hi,
                              const QVector<int>& prevVoicing,
                              int targetTopMidi = -1);

/// Realize a voicing template by stacking intervals (Bill Evans style)
QVector<int> realizeVoicingTemplate(const QVector<int>& degrees,
                                    const music::ChordSymbol& chord,
                                    int bassMidi, int ceiling);

/// Select melodic top note (stepwise preferred, avoid large leaps)
int selectMelodicTopNote(const QVector<int>& candidatePcs, int lo, int hi,
                         int lastTopMidi);

// =============================================================================
// CONSONANCE VALIDATION
// Ensure notes are musically appropriate for the current chord
// =============================================================================

/// Check if a pitch class is a chord tone (1, 3, 5, 7, or valid extension)
bool isChordTone(int pc, const music::ChordSymbol& chord);

/// Check if a pitch class is in the chord's scale (safe passing tone)
bool isScaleTone(int pc, const music::ChordSymbol& chord);

/// Get all valid pitch classes for a chord (chord tones + safe extensions)
QVector<int> getChordTonePcs(const music::ChordSymbol& chord);

/// Get all scale pitch classes for a chord
QVector<int> getScalePcs(const music::ChordSymbol& chord);

/// Validate and correct a MIDI note to be consonant with the chord
/// Returns the nearest consonant MIDI note (prefers chord tones, then scale tones)
int validateToConsonant(int midi, const music::ChordSymbol& chord, int lo, int hi);

/// Validate an entire voicing - returns corrected voicing
QVector<int> validateVoicing(const QVector<int>& midiNotes, 
                             const music::ChordSymbol& chord,
                             int lo, int hi);

} // namespace voicing_utils
} // namespace playback
