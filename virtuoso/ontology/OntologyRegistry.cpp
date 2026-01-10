#include "virtuoso/ontology/OntologyRegistry.h"

namespace virtuoso::ontology {
namespace {
// (intentionally empty) helper namespace reserved for future ontology utilities
} // namespace

OntologyRegistry OntologyRegistry::builtins() {
    OntologyRegistry r;

    // --- Chord primitives (subset, extensible) ---
    auto addChord = [&](QString key, QString name, QVector<int> iv, QStringList tags, int order, int bassInterval = -1) {
        ChordDef d;
        d.key = std::move(key);
        d.name = std::move(name);
        d.intervals = std::move(iv);
        d.tags = std::move(tags);
        d.order = order;
        d.bassInterval = bassInterval;
        r.m_chords.insert(d.key, d);
    };

    // Chord ordering requested: Maj, Maj7, 7, Sus2, Sus4, Min, Min7, m7b5, dim7, aug, 5
    addChord("maj", "maj", {0, 4, 7}, {"triad"}, 0);
    addChord("maj7", "maj7", {0, 4, 7, 11}, {"seventh"}, 1);
    addChord("7", "7", {0, 4, 7, 10}, {"seventh", "dominant"}, 2);
    addChord("sus2", "sus2", {0, 2, 7}, {"triad", "sus"}, 3);
    addChord("sus4", "sus4", {0, 5, 7}, {"triad", "sus"}, 4);
    addChord("min", "min", {0, 3, 7}, {"triad"}, 5);
    addChord("min7", "min7", {0, 3, 7, 10}, {"seventh"}, 6);
    addChord("m7b5", "m7b5", {0, 3, 6, 10}, {"seventh"}, 7);
    addChord("dim7", "dim7", {0, 3, 6, 9}, {"seventh", "symmetric"}, 8);
    addChord("aug", "aug", {0, 4, 8}, {"triad"}, 9);
    addChord("5", "5", {0, 7}, {"dyad"}, 10);

    // Additional dyads/intervals/shells
    // Shell dyads (both major and minor variants)
    addChord("shell_1_3", "shell(1-3)", {0, 4}, {"dyad", "shell"}, 50);
    addChord("shell_1_b3", "shell(1-b3)", {0, 3}, {"dyad", "shell"}, 51);
    addChord("shell_1_7", "shell(1-7)", {0, 11}, {"dyad", "shell"}, 52);
    addChord("shell_1_b7", "shell(1-b7)", {0, 10}, {"dyad", "shell"}, 53);
    addChord("m2", "interval(m2)", {0, 1}, {"dyad", "interval"}, 60);
    addChord("M2", "interval(M2)", {0, 2}, {"dyad", "interval"}, 61);
    addChord("m3", "interval(m3)", {0, 3}, {"dyad", "interval"}, 62);
    addChord("M3", "interval(M3)", {0, 4}, {"dyad", "interval"}, 63);
    addChord("P4", "interval(P4)", {0, 5}, {"dyad", "interval"}, 64);
    addChord("TT", "interval(TT)", {0, 6}, {"dyad", "interval"}, 65);
    addChord("P5", "interval(P5)", {0, 7}, {"dyad", "interval"}, 66);
    addChord("m6", "interval(m6)", {0, 8}, {"dyad", "interval"}, 67);
    addChord("M6", "interval(M6)", {0, 9}, {"dyad", "interval"}, 68);
    addChord("m7", "interval(m7)", {0, 10}, {"dyad", "interval"}, 69);
    addChord("M7", "interval(M7)", {0, 11}, {"dyad", "interval"}, 70);

    // Additional triads / seventh variants
    addChord("dim", "dim", {0, 3, 6}, {"triad"}, 100);
    addChord("phryg", "phryg(1-b2-5)", {0, 1, 7}, {"triad", "exotic"}, 101);
    addChord("min_maj7", "min(maj7)", {0, 3, 7, 11}, {"seventh"}, 110);
    addChord("aug7", "aug7", {0, 4, 8, 10}, {"seventh"}, 111);
    addChord("7sus4", "7sus4", {0, 5, 7, 10}, {"seventh", "sus", "dominant"}, 112);
    addChord("7#5", "7#5", {0, 4, 8, 10}, {"seventh", "dominant"}, 113);
    addChord("7b5", "7b5", {0, 4, 6, 10}, {"seventh", "dominant"}, 114);
    addChord("6", "6", {0, 4, 7, 9}, {"six"}, 120);
    addChord("min6", "min6", {0, 3, 7, 9}, {"six"}, 121);

    // Add-chords
    addChord("add9", "add9", {0,4,7,14}, {"add"}, 180);
    addChord("madd9", "madd9", {0,3,7,14}, {"add"}, 181);
    addChord("6_9", "6/9", {0,4,7,9,14}, {"six","extended"}, 182);
    addChord("sus4add9", "sus4(add9)", {0,5,7,14}, {"sus","add"}, 183);

    // Sus extensions
    addChord("9sus4", "9sus4", {0,5,7,10,14}, {"extended","sus","dominant"}, 236);
    addChord("13sus4", "13sus4", {0,5,7,10,14,21}, {"extended","sus","dominant"}, 237);

    // Minor-major extensions
    addChord("minmaj9", "min(maj9)", {0,3,7,11,14}, {"extended"}, 240);
    addChord("minmaj11", "min(maj11)", {0,3,7,11,14,17}, {"extended"}, 241);
    addChord("minmaj13", "min(maj13)", {0,3,7,11,14,17,21}, {"extended"}, 242);

    // Slash-bass / inversions (audible bass is handled by bassInterval in the UI playback)
    addChord("maj/3", "maj/3", {0,4,7}, {"triad","slash"}, 300, /*bassInterval=*/4);
    addChord("maj/5", "maj/5", {0,4,7}, {"triad","slash"}, 301, /*bassInterval=*/7);
    addChord("min/b3", "min/b3", {0,3,7}, {"triad","slash"}, 302, /*bassInterval=*/3);
    addChord("min/5", "min/5", {0,3,7}, {"triad","slash"}, 303, /*bassInterval=*/7);
    addChord("maj7/3", "maj7/3", {0,4,7,11}, {"seventh","slash"}, 310, /*bassInterval=*/4);
    addChord("maj7/5", "maj7/5", {0,4,7,11}, {"seventh","slash"}, 311, /*bassInterval=*/7);
    addChord("maj7/7", "maj7/7", {0,4,7,11}, {"seventh","slash"}, 312, /*bassInterval=*/11);
    addChord("7/3", "7/3", {0,4,7,10}, {"seventh","dominant","slash"}, 313, /*bassInterval=*/4);
    addChord("7/5", "7/5", {0,4,7,10}, {"seventh","dominant","slash"}, 314, /*bassInterval=*/7);
    addChord("7/b7", "7/b7", {0,4,7,10}, {"seventh","dominant","slash"}, 315, /*bassInterval=*/10);
    addChord("min7/b3", "min7/b3", {0,3,7,10}, {"seventh","slash"}, 316, /*bassInterval=*/3);
    addChord("min7/5", "min7/5", {0,3,7,10}, {"seventh","slash"}, 317, /*bassInterval=*/7);
    addChord("m7b5/b3", "m7b5/b3", {0,3,6,10}, {"seventh","slash"}, 318, /*bassInterval=*/3);
    addChord("m7b5/b5", "m7b5/b5", {0,3,6,10}, {"seventh","slash"}, 319, /*bassInterval=*/6);

    // Extensions & alterations (core set; more will be added below when we broaden the catalog)
    addChord("maj9", "maj9", {0,4,7,11,14}, {"extended"}, 200);
    addChord("maj13#11", "maj13#11", {0,4,7,11,14,18,21}, {"extended"}, 201);
    addChord("min9", "min9", {0,3,7,10,14}, {"extended"}, 210);
    addChord("min11", "min11", {0,3,7,10,14,17}, {"extended"}, 211);
    addChord("min13", "min13", {0,3,7,10,14,17,21}, {"extended"}, 212);
    addChord("7b9", "7b9", {0,4,7,10,13}, {"extended","dominant"}, 220);
    addChord("7#9", "7#9", {0,4,7,10,15}, {"extended","dominant"}, 221);
    addChord("7b13", "7b13", {0,4,7,10,20}, {"extended","dominant"}, 222);
    addChord("13", "13", {0,4,7,10,14,21}, {"extended","dominant"}, 223);
    addChord("7alt", "7alt", {0,4,10,13,15,20}, {"extended","dominant","alt"}, 224);

    // More common extensions/alterations (finite but broad coverage)
    addChord("maj11", "maj11", {0,4,7,11,14,17}, {"extended"}, 202);
    addChord("maj13", "maj13", {0,4,7,11,14,17,21}, {"extended"}, 203);
    addChord("maj9#11", "maj9#11", {0,4,7,11,14,18}, {"extended"}, 204);

    addChord("min9b13", "min9b13", {0,3,7,10,14,20}, {"extended"}, 213);
    addChord("min13b13", "min13b13", {0,3,7,10,14,17,20}, {"extended"}, 214);

    addChord("9", "9", {0,4,7,10,14}, {"extended","dominant"}, 225);
    addChord("11", "11", {0,4,7,10,14,17}, {"extended","dominant"}, 226);
    addChord("13#11", "13#11", {0,4,7,10,14,18,21}, {"extended","dominant"}, 227);
    addChord("7#11", "7#11", {0,4,7,10,18}, {"extended","dominant"}, 228);
    addChord("7b9#9", "7b9#9", {0,4,7,10,13,15}, {"extended","dominant","alt"}, 229);
    addChord("7b9b13", "7b9b13", {0,4,7,10,13,20}, {"extended","dominant","alt"}, 230);
    addChord("7#9b13", "7#9b13", {0,4,7,10,15,20}, {"extended","dominant","alt"}, 231);
    addChord("13b9", "13b9", {0,4,7,10,13,21}, {"extended","dominant"}, 232);
    addChord("13#9", "13#9", {0,4,7,10,15,21}, {"extended","dominant"}, 233);
    addChord("13b9#11", "13b9#11", {0,4,7,10,13,18,21}, {"extended","dominant"}, 234);
    addChord("13#9#11", "13#9#11", {0,4,7,10,15,18,21}, {"extended","dominant"}, 235);

    // --- Scale syllabus (subset, extensible) ---
    auto addScale = [&](QString key, QString name, QVector<int> iv, QStringList tags, int order) {
        ScaleDef s;
        s.key = std::move(key);
        s.name = std::move(name);
        s.intervals = std::move(iv);
        s.tags = std::move(tags);
        s.order = order;
        r.m_scales.insert(s.key, s);
    };

    // Diatonic modes (requested order)
    addScale("ionian", "Ionian (Major)", {0,2,4,5,7,9,11}, {"diatonic"}, 0);
    addScale("dorian", "Dorian", {0,2,3,5,7,9,10}, {"diatonic"}, 1);
    addScale("phrygian", "Phrygian", {0,1,3,5,7,8,10}, {"diatonic"}, 2);
    addScale("lydian", "Lydian", {0,2,4,6,7,9,11}, {"diatonic"}, 3);
    addScale("mixolydian", "Mixolydian", {0,2,4,5,7,9,10}, {"diatonic"}, 4);
    addScale("aeolian", "Aeolian (Natural Minor)", {0,2,3,5,7,8,10}, {"diatonic"}, 5);
    addScale("locrian", "Locrian", {0,1,3,5,6,8,10}, {"diatonic"}, 6);

    // Melodic minor universe
    addScale("melodic_minor", "Melodic Minor", {0,2,3,5,7,9,11}, {"melodic_minor"}, 20);
    addScale("dorian_b2", "Dorian b2", {0,1,3,5,7,9,10}, {"melodic_minor"}, 21);
    addScale("lydian_augmented", "Lydian Augmented", {0,2,4,6,8,9,11}, {"melodic_minor"}, 22);
    addScale("lydian_dominant", "Lydian Dominant", {0,2,4,6,7,9,10}, {"melodic_minor"}, 23);
    addScale("mixolydian_b6", "Mixolydian b6", {0,2,4,5,7,8,10}, {"melodic_minor"}, 24);
    addScale("locrian_nat2", "Locrian #2", {0,2,3,5,6,8,10}, {"melodic_minor"}, 25);
    addScale("altered", "Altered (Super Locrian)", {0,1,3,4,6,8,10}, {"melodic_minor"}, 26);

    // Harmonic minor universe
    addScale("harmonic_minor", "Harmonic Minor", {0,2,3,5,7,8,11}, {"harmonic_minor"}, 30);
    addScale("locrian_sharp6", "Locrian #6", {0,1,3,5,6,9,10}, {"harmonic_minor"}, 31);
    addScale("ionian_sharp5", "Ionian #5", {0,2,4,5,8,9,11}, {"harmonic_minor"}, 32);
    addScale("dorian_sharp4", "Dorian #4", {0,2,3,6,7,9,10}, {"harmonic_minor"}, 33);
    addScale("phrygian_dominant", "Phrygian Dominant", {0,1,4,5,7,8,10}, {"harmonic_minor"}, 34);
    addScale("lydian_sharp2", "Lydian #2", {0,3,4,6,7,9,11}, {"harmonic_minor"}, 35);
    addScale("super_locrian_bb7", "Super Locrian bb7", {0,1,3,4,6,8,9}, {"harmonic_minor"}, 36);

    // Harmonic major universe
    addScale("harmonic_major", "Harmonic Major", {0,2,4,5,7,8,11}, {"harmonic_major"}, 40);
    addScale("dorian_b5", "Dorian b5", {0,2,3,5,6,9,10}, {"harmonic_major"}, 41);
    addScale("phrygian_b4", "Phrygian b4", {0,1,3,4,7,8,10}, {"harmonic_major"}, 42);
    addScale("lydian_b3", "Lydian b3", {0,2,3,6,7,9,11}, {"harmonic_major"}, 43);
    addScale("mixolydian_b2", "Mixolydian b2", {0,1,4,5,7,9,10}, {"harmonic_major"}, 44);
    addScale("lydian_aug_sharp2", "Lydian Augmented #2", {0,3,4,6,8,9,11}, {"harmonic_major"}, 45);
    addScale("locrian_bb7", "Locrian bb7", {0,1,3,5,6,8,9}, {"harmonic_major"}, 46);

    // Symmetric scales
    addScale("whole_tone", "Whole Tone", {0,2,4,6,8,10}, {"symmetric"}, 60);
    addScale("diminished_wh", "Diminished (Whole-Half)", {0,2,3,5,6,8,9,11}, {"symmetric"}, 61);
    addScale("diminished_hw", "Diminished (Half-Whole)", {0,1,3,4,6,7,9,10}, {"symmetric"}, 62);
    // Aliases commonly used in jazz
    addScale("dominant_diminished", "Dominant Diminished (Half-Whole)", {0,1,3,4,6,7,9,10}, {"symmetric"}, 62);
    addScale("whole_half_diminished", "Whole-Half Diminished", {0,2,3,5,6,8,9,11}, {"symmetric"}, 61);
    addScale("augmented_hexatonic", "Augmented Hexatonic", {0,3,4,7,8,11}, {"symmetric"}, 63);

    // Pentatonics / blues
    addScale("major_pentatonic", "Major Pentatonic", {0,2,4,7,9}, {"pentatonic"}, 70);
    addScale("minor_pentatonic", "Minor Pentatonic", {0,3,5,7,10}, {"pentatonic"}, 71);
    addScale("dominant_pentatonic", "Dominant Pentatonic", {0,2,4,7,10}, {"pentatonic"}, 72);
    addScale("minor_blues", "Minor Blues", {0,3,5,6,7,10}, {"pentatonic","blues"}, 73);
    addScale("major_blues", "Major Blues", {0,2,3,4,7,9}, {"pentatonic","blues"}, 74);

    // Bebop
    addScale("major_bebop", "Major Bebop", {0,2,4,5,7,8,9,11}, {"bebop"}, 80);
    addScale("dominant_bebop", "Dominant Bebop", {0,2,4,5,7,9,10,11}, {"bebop"}, 81);
    addScale("minor_bebop", "Minor Bebop", {0,2,3,5,7,8,9,10}, {"bebop"}, 82);
    addScale("dorian_bebop", "Dorian Bebop", {0,2,3,5,7,9,10,11}, {"bebop"}, 83);

    // Exotic / synthetic (subset; more will be appended in later patch step)
    addScale("hungarian_minor", "Hungarian Minor", {0,2,3,6,7,8,11}, {"exotic"}, 200);
    addScale("neapolitan_major", "Neapolitan Major", {0,1,3,5,7,9,11}, {"exotic"}, 201);
    addScale("neapolitan_minor", "Neapolitan Minor", {0,1,3,5,7,8,11}, {"exotic"}, 202);
    addScale("double_harmonic", "Double Harmonic (Byzantine)", {0,1,4,5,7,8,11}, {"exotic"}, 203);
    addScale("enigmatic", "Enigmatic", {0,1,4,6,8,10,11}, {"exotic"}, 204);
    addScale("prometheus", "Prometheus", {0,2,4,6,9,10}, {"exotic"}, 205);
    addScale("persian", "Persian", {0,1,4,5,6,8,11}, {"exotic"}, 206);

    // Japanese / world pentatonics (canonical interval-set approximations)
    addScale("kumoi", "Kumoi", {0,2,3,7,9}, {"pentatonic","world"}, 210);
    addScale("hirajoshi", "Hirajoshi", {0,2,3,7,8}, {"pentatonic","world"}, 211);
    addScale("iwato", "Iwato", {0,1,5,6,10}, {"pentatonic","world"}, 212);
    addScale("in_sen", "In Sen", {0,1,5,7,10}, {"pentatonic","world"}, 213);
    addScale("pelog", "Pelog (5-tone approx)", {0,1,3,7,8}, {"pentatonic","world"}, 214);
    addScale("ryukyu", "Ryuukyuu", {0,4,5,7,11}, {"pentatonic","world"}, 215);

    // Messiaen modes (using common pitch-class sets; further validation can refine)
    addScale("messiaen_mode1", "Messiaen Mode 1 (Whole Tone)", {0,2,4,6,8,10}, {"messiaen","symmetric"}, 290);
    addScale("messiaen_mode2", "Messiaen Mode 2 (Octatonic)", {0,1,3,4,6,7,9,10}, {"messiaen","symmetric"}, 291);
    addScale("messiaen_mode3", "Messiaen Mode 3", {0,2,3,4,6,7,8,10,11}, {"messiaen"}, 300);
    addScale("messiaen_mode4", "Messiaen Mode 4", {0,1,2,5,6,7,8,11}, {"messiaen"}, 301);
    addScale("messiaen_mode5", "Messiaen Mode 5", {0,1,5,6,7,11}, {"messiaen"}, 302);
    addScale("messiaen_mode6", "Messiaen Mode 6", {0,2,4,5,6,8,10,11}, {"messiaen"}, 303);
    addScale("messiaen_mode7", "Messiaen Mode 7", {0,1,2,3,5,6,7,8,9,11}, {"messiaen"}, 304);

    // Tritone scale (hexatonic; tritone symmetry)
    addScale("tritone_scale", "Tritone Scale", {0,1,4,6,7,10}, {"symmetric"}, 64);

    // --- Voicing library (piano + later guitar) ---
    auto addVoicing2 = [&](QString key,
                           InstrumentKind inst,
                           QString name,
                           QString category,
                           QString formula,
                           QVector<int> degrees,
                           QVector<int> intervals,
                           QStringList tags,
                           int order) {
        VoicingDef v;
        v.key = std::move(key);
        v.instrument = inst;
        v.name = std::move(name);
        v.category = std::move(category);
        v.formula = std::move(formula);
        v.chordDegrees = std::move(degrees);
        v.intervals = std::move(intervals);
        v.tags = std::move(tags);
        v.order = order;
        r.m_voicings.insert(v.key, v);
    };

    addVoicing2("piano_shell_1_7", InstrumentKind::Piano, "Shell (1-7)", "Shell", "1-7", {1, 7}, {}, {"piano","shell"}, 0);
    addVoicing2("piano_shell_1_3", InstrumentKind::Piano, "Shell (1-3)", "Shell", "1-3", {1, 3}, {}, {"piano","shell"}, 1);
    addVoicing2("piano_guide_3_7", InstrumentKind::Piano, "Guide tones (3-7)", "Shell", "3-7", {3, 7}, {}, {"piano","guide_tones"}, 2);
    addVoicing2("piano_rootless_a", InstrumentKind::Piano, "Rootless Type A (3-5-7-9)", "Rootless", "3-5-7-9", {3,5,7,9}, {}, {"piano","rootless"}, 10);
    addVoicing2("piano_rootless_b", InstrumentKind::Piano, "Rootless Type B (7-9-3-5)", "Rootless", "7-9-3-5", {7,9,3,5}, {}, {"piano","rootless"}, 11);
    addVoicing2("piano_quartal_stack4ths", InstrumentKind::Piano, "Quartal (stack 4ths)", "Quartal", "Approx: 3-7-9", {3,7,9}, {}, {"piano","quartal"}, 20);
    addVoicing2("piano_quartal_3", InstrumentKind::Piano, "Quartal (3-note)", "Quartal", "3-7-9", {3,7,9}, {}, {"piano","quartal"}, 21);
    addVoicing2("piano_quartal_4", InstrumentKind::Piano, "Quartal (4-note)", "Quartal", "3-7-9-11", {3,7,9,11}, {}, {"piano","quartal"}, 22);
    addVoicing2("piano_so_what", InstrumentKind::Piano, "\"So What\" (quartal + M3)", "Quartal", "3-7-9-11", {3,7,9,11}, {}, {"piano","quartal"}, 23);

    // Upper Structure Triads (UST) over a dominant root (intervals are relative to the dominant root).
    // We use interval-based voicings so the Library can display/play them without requiring complex degree parsing.
    // Major USTs (complete set for all 12 intervals)
    addVoicing2("piano_ust_I", InstrumentKind::Piano, "UST I (I Major triad)", "UST", "Major triad on I", {}, {0,4,7}, {"piano","ust"}, 100);
    addVoicing2("piano_ust_bII", InstrumentKind::Piano, "UST bII (bII Major triad)", "UST", "Major triad on bII", {}, {1,5,8}, {"piano","ust"}, 101);
    addVoicing2("piano_ust_II", InstrumentKind::Piano, "UST II (II Major triad)", "UST", "Major triad on II", {}, {2,6,9}, {"piano","ust"}, 102);
    addVoicing2("piano_ust_bIII", InstrumentKind::Piano, "UST bIII (bIII Major triad)", "UST", "Major triad on bIII", {}, {3,7,10}, {"piano","ust"}, 103);
    addVoicing2("piano_ust_III", InstrumentKind::Piano, "UST III (III Major triad)", "UST", "Major triad on III", {}, {4,8,11}, {"piano","ust"}, 104);
    addVoicing2("piano_ust_IV", InstrumentKind::Piano, "UST IV (IV Major triad)", "UST", "Major triad on IV", {}, {5,9,12}, {"piano","ust"}, 105);
    addVoicing2("piano_ust_bV", InstrumentKind::Piano, "UST bV (bV Major triad)", "UST", "Major triad on bV", {}, {6,10,13}, {"piano","ust"}, 106);
    addVoicing2("piano_ust_V", InstrumentKind::Piano, "UST V (V Major triad)", "UST", "Major triad on V", {}, {7,11,14}, {"piano","ust"}, 107);
    addVoicing2("piano_ust_bVI", InstrumentKind::Piano, "UST bVI (bVI Major triad)", "UST", "Major triad on bVI", {}, {8,12,15}, {"piano","ust"}, 108);
    addVoicing2("piano_ust_VI", InstrumentKind::Piano, "UST VI (VI Major triad)", "UST", "Major triad on VI", {}, {9,13,16}, {"piano","ust"}, 109);
    addVoicing2("piano_ust_bVII", InstrumentKind::Piano, "UST bVII (bVII Major triad)", "UST", "Major triad on bVII", {}, {10,14,17}, {"piano","ust"}, 110);
    addVoicing2("piano_ust_VII", InstrumentKind::Piano, "UST VII (VII Major triad)", "UST", "Major triad on VII", {}, {11,15,18}, {"piano","ust"}, 111);

    // Piano textures (initial placeholders; degrees chosen to be audible + recognizable)
    addVoicing2("piano_block_shearing", InstrumentKind::Piano, "Block Chords (Shearing-style)", "Block", "4-way close (approx)", {1,3,5,7}, {}, {"piano","block"}, 200);
    addVoicing2("piano_drop2", InstrumentKind::Piano, "Drop 2 (piano)", "Block", "Drop 2 (approx)", {1,3,5,7}, {}, {"piano","block"}, 201);
    addVoicing2("piano_cluster_diatonic", InstrumentKind::Piano, "Cluster (diatonic)", "Cluster", "Diatonic cluster (approx)", {9,11,13}, {}, {"piano","cluster"}, 220);
    addVoicing2("piano_cluster_chromatic", InstrumentKind::Piano, "Cluster (chromatic)", "Cluster", "Chromatic cluster (approx)", {}, {0,1,2,3}, {"piano","cluster"}, 221);
    addVoicing2("piano_gospel_triads", InstrumentKind::Piano, "Gospel (triad cycling)", "Gospel", "Inversion cycling (placeholder)", {1,3,5}, {}, {"piano","gospel"}, 240);
    addVoicing2("piano_stride_basic", InstrumentKind::Piano, "Stride (basic)", "Stride", "Tenths + chord (placeholder)", {1,7,10}, {}, {"piano","stride"}, 260);

    // LH (left hand) voicings - used by the jazz ballad piano planner
    addVoicing2("piano_lh_voicing", InstrumentKind::Piano, "LH Voicing (3+ notes)", "LH", "Left hand voicing with 3+ notes", {3,5,7}, {}, {"piano","lh"}, 30);
    addVoicing2("piano_lh_shell", InstrumentKind::Piano, "LH Shell (2 notes)", "LH", "Left hand shell voicing", {3,7}, {}, {"piano","lh","shell"}, 31);
    addVoicing2("piano_lh_single", InstrumentKind::Piano, "LH Single", "LH", "Left hand single note", {1}, {}, {"piano","lh"}, 32);
    addVoicing2("piano_lh_inversion", InstrumentKind::Piano, "LH Inversion", "LH", "Inverted left hand voicing", {3,5,7}, {}, {"piano","lh"}, 33);
    addVoicing2("piano_lh_inner_move", InstrumentKind::Piano, "LH Inner Voice Move", "LH", "Inner voice movement variation", {3,5,7}, {}, {"piano","lh"}, 34);
    addVoicing2("piano_lh_quartal", InstrumentKind::Piano, "LH Quartal", "LH", "Quartal left hand voicing", {3,7,9}, {}, {"piano","lh","quartal"}, 35);

    // RH (right hand) voicings - melodic and color tones
    addVoicing2("piano_rh_single_color", InstrumentKind::Piano, "RH Single (color)", "RH", "Single color tone (9, 13)", {9}, {}, {"piano","rh","color"}, 40);
    addVoicing2("piano_rh_dyad_color", InstrumentKind::Piano, "RH Dyad (color)", "RH", "Dyad with color tones", {9,13}, {}, {"piano","rh","color"}, 41);
    addVoicing2("piano_rh_single_guide", InstrumentKind::Piano, "RH Single (guide)", "RH", "Single guide tone (3, 7)", {3}, {}, {"piano","rh","guide"}, 42);
    addVoicing2("piano_rh_dyad_guide", InstrumentKind::Piano, "RH Dyad (guide)", "RH", "Dyad with guide tones (3-7)", {3,7}, {}, {"piano","rh","guide"}, 43);
    addVoicing2("piano_rh_melodic", InstrumentKind::Piano, "RH Melodic", "RH", "Melodic right hand line", {}, {}, {"piano","rh","melodic"}, 44);

    // Basic triads
    addVoicing2("piano_triad_root", InstrumentKind::Piano, "Triad (root position)", "Triad", "1-3-5 root position", {1,3,5}, {}, {"piano","triad"}, 50);
    addVoicing2("piano_triad_first_inv", InstrumentKind::Piano, "Triad (1st inversion)", "Triad", "3-5-1 first inversion", {3,5,1}, {}, {"piano","triad"}, 51);

    // Minor UST variants (complete set for all 12 intervals)
    addVoicing2("piano_ust_i_min", InstrumentKind::Piano, "UST i (i minor triad)", "UST", "Minor triad on i", {}, {0,3,7}, {"piano","ust","minor"}, 120);
    addVoicing2("piano_ust_bii_min", InstrumentKind::Piano, "UST bii (bii minor triad)", "UST", "Minor triad on bii", {}, {1,4,8}, {"piano","ust","minor"}, 121);
    addVoicing2("piano_ust_ii_min", InstrumentKind::Piano, "UST ii (ii minor triad)", "UST", "Minor triad on ii", {}, {2,5,9}, {"piano","ust","minor"}, 122);
    addVoicing2("piano_ust_biii_min", InstrumentKind::Piano, "UST biii (biii minor triad)", "UST", "Minor triad on biii", {}, {3,6,10}, {"piano","ust","minor"}, 123);
    addVoicing2("piano_ust_iii_min", InstrumentKind::Piano, "UST iii (iii minor triad)", "UST", "Minor triad on iii", {}, {4,7,11}, {"piano","ust","minor"}, 124);
    addVoicing2("piano_ust_iv_min", InstrumentKind::Piano, "UST iv (iv minor triad)", "UST", "Minor triad on iv", {}, {5,8,12}, {"piano","ust","minor"}, 125);
    addVoicing2("piano_ust_bv_min", InstrumentKind::Piano, "UST bv (bv minor triad)", "UST", "Minor triad on bv", {}, {6,9,13}, {"piano","ust","minor"}, 126);
    addVoicing2("piano_ust_v_min", InstrumentKind::Piano, "UST v (v minor triad)", "UST", "Minor triad on v", {}, {7,10,14}, {"piano","ust","minor"}, 127);
    addVoicing2("piano_ust_bvi_min", InstrumentKind::Piano, "UST bvi (bvi minor triad)", "UST", "Minor triad on bvi", {}, {8,11,15}, {"piano","ust","minor"}, 128);
    addVoicing2("piano_ust_vi_min", InstrumentKind::Piano, "UST vi (vi minor triad)", "UST", "Minor triad on vi", {}, {9,12,16}, {"piano","ust","minor"}, 129);
    addVoicing2("piano_ust_bvii_min", InstrumentKind::Piano, "UST bvii (bvii minor triad)", "UST", "Minor triad on bvii", {}, {10,13,17}, {"piano","ust","minor"}, 130);
    addVoicing2("piano_ust_vii_min", InstrumentKind::Piano, "UST vii (vii minor triad)", "UST", "Minor triad on vii", {}, {11,14,18}, {"piano","ust","minor"}, 131);

    // Guitar voicings (shape-level placeholders; still useful for pitch-class visualization)
    addVoicing2("guitar_shell_3_7", InstrumentKind::Guitar, "Shell (3-7)", "Shell", "Freddie Green shell", {3,7}, {}, {"guitar","shell"}, 300);
    addVoicing2("guitar_drop2_1234", InstrumentKind::Guitar, "Drop 2 (strings 1-2-3-4)", "Drop2", "Drop 2 set 1234", {3,5,7,9}, {}, {"guitar","drop2"}, 310);
    addVoicing2("guitar_drop2_2345", InstrumentKind::Guitar, "Drop 2 (strings 2-3-4-5)", "Drop2", "Drop 2 set 2345", {3,5,7,9}, {}, {"guitar","drop2"}, 311);
    addVoicing2("guitar_drop2_3456", InstrumentKind::Guitar, "Drop 2 (strings 3-4-5-6)", "Drop2", "Drop 2 set 3456", {3,5,7,9}, {}, {"guitar","drop2"}, 312);
    addVoicing2("guitar_drop3_1235", InstrumentKind::Guitar, "Drop 3 (set 1235)", "Drop3", "Drop 3 set 1235", {1,3,7,9}, {}, {"guitar","drop3"}, 320);
    addVoicing2("guitar_drop3_2346", InstrumentKind::Guitar, "Drop 3 (set 2346)", "Drop3", "Drop 3 set 2346", {1,3,7,9}, {}, {"guitar","drop3"}, 321);

    // --- Polychord templates (procedural combinations; do not enumerate all pairs) ---
    auto addPoly = [&](QString key, QString name, QString formula, QStringList tags, int order) {
        PolychordTemplate t;
        t.key = std::move(key);
        t.name = std::move(name);
        t.formula = std::move(formula);
        t.tags = std::move(tags);
        t.order = order;
        r.m_polychords.insert(t.key, t);
    };
    addPoly("triad_over_bass", "Triad over Bass (D/C)", "UpperTriad / Bass", {"polychord","slash"}, 0);
    addPoly("triad_over_chord", "Triad over Chord (D over Cmaj7#11)", "UpperTriad over LowerChord", {"polychord","stack"}, 1);

    return r;
}

const ChordDef* OntologyRegistry::chord(const Key& key) const {
    auto it = m_chords.find(key);
    if (it == m_chords.end()) return nullptr;
    return &it.value();
}

const ScaleDef* OntologyRegistry::scale(const Key& key) const {
    auto it = m_scales.find(key);
    if (it == m_scales.end()) return nullptr;
    return &it.value();
}

const VoicingDef* OntologyRegistry::voicing(const Key& key) const {
    auto it = m_voicings.find(key);
    if (it == m_voicings.end()) return nullptr;
    return &it.value();
}

QVector<const ChordDef*> OntologyRegistry::chordsWithTag(const QString& tag) const {
    QVector<const ChordDef*> out;
    out.reserve(m_chords.size());
    for (const ChordDef& v : m_chords) {
        if (v.tags.contains(tag)) out.push_back(&v);
    }
    return out;
}

QVector<const ScaleDef*> OntologyRegistry::scalesWithTag(const QString& tag) const {
    QVector<const ScaleDef*> out;
    out.reserve(m_scales.size());
    for (const ScaleDef& v : m_scales) {
        if (v.tags.contains(tag)) out.push_back(&v);
    }
    return out;
}

QVector<const VoicingDef*> OntologyRegistry::voicingsFor(InstrumentKind instrument) const {
    QVector<const VoicingDef*> out;
    out.reserve(m_voicings.size());
    for (const VoicingDef& v : m_voicings) {
        if (v.instrument == instrument) out.push_back(&v);
    }
    return out;
}

QVector<const ChordDef*> OntologyRegistry::allChords() const {
    QVector<const ChordDef*> out;
    out.reserve(m_chords.size());
    for (const ChordDef& v : m_chords) out.push_back(&v);
    return out;
}

QVector<const ScaleDef*> OntologyRegistry::allScales() const {
    QVector<const ScaleDef*> out;
    out.reserve(m_scales.size());
    for (const ScaleDef& v : m_scales) out.push_back(&v);
    return out;
}

QVector<const VoicingDef*> OntologyRegistry::allVoicings() const {
    QVector<const VoicingDef*> out;
    out.reserve(m_voicings.size());
    for (const VoicingDef& v : m_voicings) out.push_back(&v);
    return out;
}

QVector<const PolychordTemplate*> OntologyRegistry::allPolychordTemplates() const {
    QVector<const PolychordTemplate*> out;
    out.reserve(m_polychords.size());
    for (const PolychordTemplate& t : m_polychords) out.push_back(&t);
    std::sort(out.begin(), out.end(), [](const PolychordTemplate* a, const PolychordTemplate* b) {
        if (!a || !b) return a != nullptr;
        if (a->order != b->order) return a->order < b->order;
        return a->name < b->name;
    });
    return out;
}

const PolychordTemplate* OntologyRegistry::polychordTemplate(const Key& key) const {
    auto it = m_polychords.find(key);
    if (it == m_polychords.end()) return nullptr;
    return &it.value();
}

} // namespace virtuoso::ontology

