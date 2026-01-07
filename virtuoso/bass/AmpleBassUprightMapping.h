#pragma once

#include <QVector>

namespace virtuoso::bass {

// Minimal programmatic access to Ample Sound "Ample Bass Upright" articulation + FX mapping.
// Source-of-truth text is stored alongside as JSON:
//   `virtuoso/bass/AmpleBassUprightMapping.json`
//
// NOTE: This app uses a note-name convention where C2 == MIDI 48 (+12 vs common naming).
struct AmpleUprightMappingNote {
    int midi = 0;               // 0..127
    const char* noteName = "";  // e.g. "C0"
    const char* label = "";     // human label
};

QVector<AmpleUprightMappingNote> ampleBassUprightKeyswitches();
QVector<AmpleUprightMappingNote> ampleBassUprightFxNotes();

namespace ample_upright {
// Keyswitches (C2==48 convention => C0==24).
static constexpr int kKeyswitch_SustainAccent_C0 = 24;
static constexpr int kKeyswitch_NaturalHarmonic_Cs0 = 25;
static constexpr int kKeyswitch_PalmMute_D0 = 26;
static constexpr int kKeyswitch_SlideInOut_Ds0 = 27;
static constexpr int kKeyswitch_LegatoSlide_E0 = 28;
static constexpr int kKeyswitch_HammerPull_F0 = 29;

// FX Sound Group (subset from manual screenshot; C2==48 convention).
static constexpr int kFx_HitRimMute_Fs4 = 78;
static constexpr int kFx_HitTopPalmMute_G4 = 79;
static constexpr int kFx_HitTopFingerMute_Gs4 = 80;
static constexpr int kFx_HitTopOpen_A4 = 81;
static constexpr int kFx_HitRimOpen_As4 = 82;
static constexpr int kFx_Scratch_F5 = 89;
static constexpr int kFx_Breath_Fs5 = 90;
static constexpr int kFx_SingleStringSlap_G5 = 91;
static constexpr int kFx_LeftHandSlapNoise_Gs5 = 92;
static constexpr int kFx_RightHandSlapNoise_A5 = 93;
static constexpr int kFx_SlideTurn4_As5 = 94;
static constexpr int kFx_SlideTurn3_B5 = 95;
static constexpr int kFx_SlideDown4_C6 = 96;
static constexpr int kFx_SlideDown3_Cs6 = 97;
} // namespace ample_upright

} // namespace virtuoso::bass

