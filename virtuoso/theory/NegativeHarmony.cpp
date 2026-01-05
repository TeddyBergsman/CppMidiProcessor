#include "virtuoso/theory/NegativeHarmony.h"

namespace virtuoso::theory {
namespace {
static int normPc(int pc) {
    int v = pc % 12;
    if (v < 0) v += 12;
    return v;
}
} // namespace

int negativeHarmonyMirrorPc(int pc, int tonicPc) {
    const int p = normPc(pc);
    const int t = normPc(tonicPc);
    return normPc(2 * t - p);
}

int negativeHarmonyMirrorMidi(int midi, int tonicPc) {
    if (midi < 0) midi = 0;
    if (midi > 127) midi = 127;
    const int oct = midi / 12;
    const int pc = normPc(midi);
    const int mirroredPc = negativeHarmonyMirrorPc(pc, tonicPc);
    int out = oct * 12 + mirroredPc;
    if (out < 0) out = 0;
    if (out > 127) out = 127;
    return out;
}

} // namespace virtuoso::theory

