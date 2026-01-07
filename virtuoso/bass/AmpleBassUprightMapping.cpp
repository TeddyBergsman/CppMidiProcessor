#include "virtuoso/bass/AmpleBassUprightMapping.h"

namespace virtuoso::bass {

QVector<AmpleUprightMappingNote> ampleBassUprightKeyswitches() {
    using namespace ample_upright;
    return {
        {kKeyswitch_SustainAccent_C0, "C0",  "Sustain & Accent (vel<=125 sustain; vel>=126 accent)"},
        {kKeyswitch_NaturalHarmonic_Cs0, "C#0", "Natural Harmonic"},
        {kKeyswitch_PalmMute_D0, "D0", "Palm Mute"},
        {kKeyswitch_SlideInOut_Ds0, "D#0", "Slide In & Slide Out (auto reverts to sustain)"},
        {kKeyswitch_LegatoSlide_E0, "E0", "Legato Slide (Poly Legato)"},
        {kKeyswitch_HammerPull_F0, "F0", "Hammer-On & Pull-Off (Poly Legato)"},
    };
}

QVector<AmpleUprightMappingNote> ampleBassUprightFxNotes() {
    using namespace ample_upright;
    return {
        {kFx_HitRimMute_Fs4, "F#4", "Hit Rim (Mute)"},
        {kFx_HitTopPalmMute_G4, "G4", "Hit Top (Palm Mute)"},
        {kFx_HitTopFingerMute_Gs4, "G#4", "Hit Top (Finger Mute)"},
        {kFx_HitTopOpen_A4, "A4", "Hit Top (Open)"},
        {kFx_HitRimOpen_As4, "A#4", "Hit Rim (Open)"},
        {kFx_Scratch_F5, "F5", "Scratch"},
        {kFx_Breath_Fs5, "F#5", "Breath"},
        {kFx_SingleStringSlap_G5, "G5", "Single String Slap"},
        {kFx_LeftHandSlapNoise_Gs5, "G#5", "Left-Hand Slap Noise"},
        {kFx_RightHandSlapNoise_A5, "A5", "Right-Hand Slap Noise"},
        {kFx_SlideTurn4_As5, "A#5", "Fx Slide Turn 4"},
        {kFx_SlideTurn3_B5, "B5", "Fx Slide Turn 3"},
        {kFx_SlideDown4_C6, "C6", "Fx Slide Down 4"},
        {kFx_SlideDown3_Cs6, "C#6", "Fx Slide Down 3"},
    };
}

} // namespace virtuoso::bass

