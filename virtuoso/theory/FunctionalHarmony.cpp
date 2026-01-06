#include "virtuoso/theory/FunctionalHarmony.h"

#include <QtGlobal>

namespace virtuoso::theory {
namespace {

static int normalizePc(int pc) {
    int v = pc % 12;
    if (v < 0) v += 12;
    return v;
}

static bool hasInterval(const virtuoso::ontology::ChordDef& c, int iv) {
    for (int x : c.intervals) if (x == iv) return true;
    return false;
}

static QString romanDegree(int degree, bool uppercase) {
    static const char* romans[] = {"I","II","III","IV","V","VI","VII"};
    QString r = romans[qBound(1, degree, 7) - 1];
    return uppercase ? r : r.toLower();
}

static int diatonicDegreeForMajor(int tonicPc, int chordRootPc) {
    const int rel = normalizePc(chordRootPc - tonicPc);
    switch (rel) {
    case 0:  return 1; // I
    case 2:  return 2; // ii
    case 4:  return 3; // iii
    case 5:  return 4; // IV
    case 7:  return 5; // V
    case 9:  return 6; // vi
    case 11: return 7; // vii°
    default: return 0;
    }
}

static int diatonicDegreeForNaturalMinor(int tonicPc, int chordRootPc) {
    const int rel = normalizePc(chordRootPc - tonicPc);
    // Natural minor: 1 2 b3 4 5 b6 b7
    switch (rel) {
    case 0:  return 1; // i
    case 2:  return 2; // ii°
    case 3:  return 3; // III
    case 5:  return 4; // iv
    case 7:  return 5; // v/V
    case 8:  return 6; // VI
    case 10: return 7; // VII
    default: return 0;
    }
}

static QString functionForDegreeMinor(int degree) {
    switch (degree) {
    case 1: case 3: case 6: return "Tonic";
    case 2: case 4: return "Subdominant";
    case 5: case 7: return "Dominant";
    default: return "Other";
    }
}

static QString functionForDegreeMajor(int degree) {
    switch (degree) {
    case 1: case 3: case 6: return "Tonic";
    case 2: case 4: return "Subdominant";
    case 5: case 7: return "Dominant";
    default: return "Other";
    }
}

static QString qualitySuffix(const virtuoso::ontology::ChordDef& chord,
                             bool* outUppercase,
                             bool* outHalfDim) {
    // Determine quality from 3rd + 5th, fall back to tags.
    const bool hasMin3 = hasInterval(chord, 3);
    const bool hasMaj3 = hasInterval(chord, 4);
    const bool hasDim5 = hasInterval(chord, 6);
    const bool hasP5   = hasInterval(chord, 7);
    const bool hasAug5 = hasInterval(chord, 8);
    const bool hasMin7 = hasInterval(chord, 10);
    const bool hasMaj7 = hasInterval(chord, 11);

    *outHalfDim = false;

    // Diminished / half-diminished
    if (hasMin3 && hasDim5) {
        *outUppercase = false;
        if (hasMin7) {
            *outHalfDim = true;
            return "ø7";
        }
        return "°";
    }
    // Augmented
    if (hasMaj3 && hasAug5) {
        *outUppercase = true;
        return "+";
    }
    // Minor
    if (hasMin3 && (hasP5 || chord.tags.contains("minor"))) {
        *outUppercase = false;
        if (hasMin7) return "7";
        if (hasMaj7) return "maj7";
        return "";
    }
    // Major / dominant
    *outUppercase = true;
    if (hasMin7) return "7";
    if (hasMaj7) return "maj7";
    return "";
}

} // namespace

HarmonyLabel analyzeChordInMajorKey(int tonicPc, int chordRootPc, const virtuoso::ontology::ChordDef& chord) {
    HarmonyLabel out;
    tonicPc = normalizePc(tonicPc);
    chordRootPc = normalizePc(chordRootPc);

    bool uppercase = true;
    bool halfDim = false;
    const QString suffix = qualitySuffix(chord, &uppercase, &halfDim);

    const int deg = diatonicDegreeForMajor(tonicPc, chordRootPc);
    const bool isDom7 = hasInterval(chord, 4) && hasInterval(chord, 10);

    // Secondary dominant heuristic: dominant 7 (Maj3 + min7) resolving to a diatonic target.
    // If it's a diatonic degree but "dominant-7 quality" doesn't match the key (e.g. D7 in C),
    // we prefer explaining it as a secondary dominant.
    if (isDom7 && deg != 5) {
        const int targetPc = normalizePc(chordRootPc - 7); // a 5th below
        const int targetDeg = diatonicDegreeForMajor(tonicPc, targetPc);
        if (targetDeg != 0) {
            out.roman = "V/" + romanDegree(targetDeg, true);
            out.function = "Dominant";
            out.detail = "secondary dominant";
            out.confidence = 0.75;
            return out;
        }

        // Tritone-sub heuristic for V: bII7 in major.
        const int rel = normalizePc(chordRootPc - tonicPc);
        if (rel == 1) {
            out.roman = "subV7";
            out.function = "Dominant";
            out.detail = "tritone sub (heuristic)";
            out.confidence = 0.55;
            return out;
        }
    }

    if (deg != 0) {
        out.roman = romanDegree(deg, uppercase);
        if (halfDim) out.roman += "ø7";
        else if (suffix == "°") out.roman += "°";
        else if (!suffix.isEmpty()) out.roman += suffix;
        out.function = functionForDegreeMajor(deg);
        out.detail = "diatonic";
        out.confidence = 0.95;
        return out;
    }

    out.roman = "N/A";
    out.function = "Other";
    out.detail = "non-diatonic (currently)";
    out.confidence = 0.25;
    return out;
}

HarmonyLabel analyzeChordInMinorKey(int tonicPc, int chordRootPc, const virtuoso::ontology::ChordDef& chord) {
    HarmonyLabel out;
    tonicPc = normalizePc(tonicPc);
    chordRootPc = normalizePc(chordRootPc);

    bool uppercase = true;
    bool halfDim = false;
    const QString suffix = qualitySuffix(chord, &uppercase, &halfDim);

    // Minor key: allow both b7 (natural minor) and leading tone (harmonic/melodic minor).
    int deg = diatonicDegreeForNaturalMinor(tonicPc, chordRootPc);
    const int rel = normalizePc(chordRootPc - tonicPc);
    if (deg == 0 && rel == 11) deg = 7; // leading-tone degree (vii°) in harmonic minor

    const bool isDom7 = hasInterval(chord, 4) && hasInterval(chord, 10);

    // Secondary dominant heuristic as in major.
    if (isDom7 && deg != 5) {
        const int targetPc = normalizePc(chordRootPc - 7); // a 5th below
        int targetDeg = diatonicDegreeForNaturalMinor(tonicPc, targetPc);
        const int targetRel = normalizePc(targetPc - tonicPc);
        if (targetDeg == 0 && targetRel == 11) targetDeg = 7;
        if (targetDeg != 0) {
            out.roman = "V/" + romanDegree(targetDeg, true);
            out.function = "Dominant";
            out.detail = "secondary dominant";
            out.confidence = 0.70;
            return out;
        }

        // Tritone-sub heuristic for dominant: bII7.
        if (normalizePc(chordRootPc - tonicPc) == 1) {
            out.roman = "subV7";
            out.function = "Dominant";
            out.detail = "tritone sub (heuristic)";
            out.confidence = 0.50;
            return out;
        }
    }

    if (deg != 0) {
        // Minor: use the chord quality to choose case (dominant/major => uppercase, minor/dim => lowercase).
        // This allows V7 in minor to appear as "V7" (harmonic minor common practice).
        out.roman = romanDegree(deg, uppercase);
        if (halfDim) out.roman += "ø7";
        else if (suffix == "°") out.roman += "°";
        else if (!suffix.isEmpty()) out.roman += suffix;
        out.function = functionForDegreeMinor(deg);
        out.detail = (rel == 11) ? "leading-tone (harmonic/melodic minor heuristic)" : "diatonic";
        out.confidence = (rel == 11) ? 0.75 : 0.90;
        return out;
    }

    out.roman = "N/A";
    out.function = "Other";
    out.detail = "non-diatonic (currently)";
    out.confidence = 0.25;
    return out;
}

HarmonyLabel analyzeChordInKey(int tonicPc,
                               KeyMode mode,
                               int chordRootPc,
                               const virtuoso::ontology::ChordDef& chord) {
    return (mode == KeyMode::Minor)
        ? analyzeChordInMinorKey(tonicPc, chordRootPc, chord)
        : analyzeChordInMajorKey(tonicPc, chordRootPc, chord);
}

} // namespace virtuoso::theory

