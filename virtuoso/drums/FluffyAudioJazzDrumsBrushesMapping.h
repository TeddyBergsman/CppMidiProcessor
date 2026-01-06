#pragma once

#include <QString>
#include <QVector>

namespace virtuoso::drums {

// Minimal programmatic access to FluffyAudio "Jazz Drums - Brushes" mapping.
// Source-of-truth text is stored alongside as JSON:
//   `virtuoso/drums/FluffyAudioJazzDrumsBrushesMapping.json`
//
// NOTE: This is intentionally small (MVP-focused). We can expand it into a full kit/limb model later.
struct FluffyAudioBrushesNote {
    int midi = 0;                 // 0..127
    const char* noteName = "";    // e.g. "G0"
    const char* articulation = "";// human label
    int holdMsForFullSample = 0;  // 0 => no guidance / short hit
};

QVector<FluffyAudioBrushesNote> fluffyAudioJazzDrumsBrushesNotes();

// Common MVP notes (from the mapping).
namespace fluffy_brushes {
// IMPORTANT: This library uses a note-name convention where C2 == MIDI 48 (i.e., +12 vs "C4=60" naming).
static constexpr int kKickLooseNormal_G0 = 31;
static constexpr int kKickLooseSforzando_A0 = 33;
static constexpr int kSnareRightHand_D1 = 38;
static constexpr int kSnareBrushing_E3 = 64; // hold ~4000ms for full sample (per mapping)
static constexpr int kBrushCircleTwoHands_Fs3 = 66; // looping (full sample ~6000ms)
static constexpr int kBrushShortRightHand_Gs3 = 68; // hold ~1000ms for full sample
static constexpr int kRideSwish2_E2 = 52; // hold ~4000ms for full sample
} // namespace fluffy_brushes

} // namespace virtuoso::drums

