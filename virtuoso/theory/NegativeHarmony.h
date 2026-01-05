#pragma once

namespace virtuoso::theory {

// Negative harmony mirror (pitch-class inversion around tonic axis).
// In C (tonicPc=0): D(2)->Bb(10), E(4)->Ab(8), F(5)->G(7), etc.
int negativeHarmonyMirrorPc(int pc, int tonicPc);

// MIDI helper (keeps octave; mirrors pitch-class around tonicPc).
int negativeHarmonyMirrorMidi(int midi, int tonicPc);

} // namespace virtuoso::theory

