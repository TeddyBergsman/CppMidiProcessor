#include "virtuoso/ontology/OntologyRegistry.h"

namespace virtuoso::ontology {
namespace {
// (intentionally empty) helper namespace reserved for future ontology utilities
} // namespace

OntologyRegistry OntologyRegistry::builtins() {
    OntologyRegistry r;

    // --- Chord primitives (subset, extensible) ---
    auto addChord = [&](ChordId id, QString name, QVector<int> iv, QStringList tags) {
        ChordDef d;
        d.id = id;
        d.name = std::move(name);
        d.intervals = std::move(iv);
        d.tags = std::move(tags);
        r.m_chords.insert(id, d);
    };

    addChord(ChordId::Shell_1_3, "shell(1-3)", {0, 4}, {"dyad", "shell"});
    addChord(ChordId::Shell_1_7, "shell(1-7)", {0, 10}, {"dyad", "shell"});
    addChord(ChordId::Power5, "5", {0, 7}, {"dyad"});
    addChord(ChordId::MajorTriad, "maj", {0, 4, 7}, {"triad"});
    addChord(ChordId::MinorTriad, "min", {0, 3, 7}, {"triad"});
    addChord(ChordId::DiminishedTriad, "dim", {0, 3, 6}, {"triad"});
    addChord(ChordId::AugmentedTriad, "aug", {0, 4, 8}, {"triad"});
    addChord(ChordId::Sus2Triad, "sus2", {0, 2, 7}, {"triad", "sus"});
    addChord(ChordId::Sus4Triad, "sus4", {0, 5, 7}, {"triad", "sus"});
    addChord(ChordId::PhrygianTriad, "phryg", {0, 1, 7}, {"triad", "exotic"});

    addChord(ChordId::Major7, "maj7", {0, 4, 7, 11}, {"seventh"});
    addChord(ChordId::Minor7, "min7", {0, 3, 7, 10}, {"seventh"});
    addChord(ChordId::Dominant7, "7", {0, 4, 7, 10}, {"seventh", "dominant"});
    addChord(ChordId::HalfDiminished7, "m7b5", {0, 3, 6, 10}, {"seventh"});
    addChord(ChordId::Diminished7, "dim7", {0, 3, 6, 9}, {"seventh", "symmetric"});
    addChord(ChordId::MinorMajor7, "min(maj7)", {0, 3, 7, 11}, {"seventh"});
    addChord(ChordId::Augmented7, "aug7", {0, 4, 8, 10}, {"seventh"});
    addChord(ChordId::Dominant7Sus4, "7sus4", {0, 5, 7, 10}, {"seventh", "sus", "dominant"});
    addChord(ChordId::SevenSharp5, "7#5", {0, 4, 8, 10}, {"seventh", "dominant"});
    addChord(ChordId::SevenFlat5, "7b5", {0, 4, 6, 10}, {"seventh", "dominant"});
    addChord(ChordId::Six, "6", {0, 4, 7, 9}, {"six"});
    addChord(ChordId::MinorSix, "min6", {0, 3, 7, 9}, {"six"});

    // --- Scale syllabus (subset, extensible) ---
    auto addScale = [&](ScaleId id, QString name, QVector<int> iv, QStringList tags) {
        ScaleDef s;
        s.id = id;
        s.name = std::move(name);
        s.intervals = std::move(iv);
        s.tags = std::move(tags);
        r.m_scales.insert(id, s);
    };

    addScale(ScaleId::Ionian, "Ionian", {0,2,4,5,7,9,11}, {"diatonic"});
    addScale(ScaleId::Dorian, "Dorian", {0,2,3,5,7,9,10}, {"diatonic"});
    addScale(ScaleId::Phrygian, "Phrygian", {0,1,3,5,7,8,10}, {"diatonic"});
    addScale(ScaleId::Lydian, "Lydian", {0,2,4,6,7,9,11}, {"diatonic"});
    addScale(ScaleId::Mixolydian, "Mixolydian", {0,2,4,5,7,9,10}, {"diatonic"});
    addScale(ScaleId::Aeolian, "Aeolian", {0,2,3,5,7,8,10}, {"diatonic"});
    addScale(ScaleId::Locrian, "Locrian", {0,1,3,5,6,8,10}, {"diatonic"});

    addScale(ScaleId::MelodicMinor, "Melodic Minor", {0,2,3,5,7,9,11}, {"melodic_minor"});
    addScale(ScaleId::DorianB2, "Dorian b2", {0,1,3,5,7,9,10}, {"melodic_minor"});
    addScale(ScaleId::LydianAugmented, "Lydian Augmented", {0,2,4,6,8,9,11}, {"melodic_minor"});
    addScale(ScaleId::LydianDominant, "Lydian Dominant", {0,2,4,6,7,9,10}, {"melodic_minor"});
    addScale(ScaleId::MixolydianB6, "Mixolydian b6", {0,2,4,5,7,8,10}, {"melodic_minor"});
    addScale(ScaleId::LocrianNat2, "Locrian #2", {0,2,3,5,6,8,10}, {"melodic_minor"});
    addScale(ScaleId::Altered, "Altered (Super Locrian)", {0,1,3,4,6,8,10}, {"melodic_minor"});
    addScale(ScaleId::HarmonicMinor, "Harmonic Minor", {0,2,3,5,7,8,11}, {"harmonic_minor"});
    addScale(ScaleId::LocrianSharp6, "Locrian #6", {0,1,3,5,6,9,10}, {"harmonic_minor"});
    addScale(ScaleId::IonianSharp5, "Ionian #5", {0,2,4,5,8,9,11}, {"harmonic_minor"});
    addScale(ScaleId::DorianSharp4, "Dorian #4", {0,2,3,6,7,9,10}, {"harmonic_minor"});
    addScale(ScaleId::PhrygianDominant, "Phrygian Dominant", {0,1,4,5,7,8,10}, {"harmonic_minor"});
    addScale(ScaleId::LydianSharp2, "Lydian #2", {0,3,4,6,7,9,11}, {"harmonic_minor"});
    addScale(ScaleId::SuperLocrianBb7, "Super Locrian bb7", {0,1,3,4,6,8,9}, {"harmonic_minor"});

    addScale(ScaleId::HarmonicMajor, "Harmonic Major", {0,2,4,5,7,8,11}, {"harmonic_major"});
    addScale(ScaleId::DorianB5, "Dorian b5", {0,2,3,5,6,9,10}, {"harmonic_major"});
    addScale(ScaleId::PhrygianB4, "Phrygian b4", {0,1,3,4,7,8,10}, {"harmonic_major"});
    addScale(ScaleId::LydianB3, "Lydian b3", {0,2,3,6,7,9,11}, {"harmonic_major"});
    addScale(ScaleId::MixolydianB2, "Mixolydian b2", {0,1,4,5,7,9,10}, {"harmonic_major"});
    addScale(ScaleId::LydianAugSharp2, "Lydian Augmented #2", {0,3,4,6,8,9,11}, {"harmonic_major"});
    addScale(ScaleId::LocrianBb7, "Locrian bb7", {0,1,3,5,6,8,9}, {"harmonic_major"});

    addScale(ScaleId::WholeTone, "Whole Tone", {0,2,4,6,8,10}, {"symmetric"});
    addScale(ScaleId::DiminishedWH, "Diminished (Whole-Half)", {0,2,3,5,6,8,9,11}, {"symmetric"});
    addScale(ScaleId::DiminishedHW, "Diminished (Half-Whole)", {0,1,3,4,6,7,9,10}, {"symmetric"});

    addScale(ScaleId::MajorPentatonic, "Major Pentatonic", {0,2,4,7,9}, {"pentatonic"});
    addScale(ScaleId::MinorPentatonic, "Minor Pentatonic", {0,3,5,7,10}, {"pentatonic"});
    addScale(ScaleId::Blues, "Minor Blues", {0,3,5,6,7,10}, {"pentatonic", "blues"});
    addScale(ScaleId::MajorBlues, "Major Blues", {0,2,3,4,7,9}, {"pentatonic", "blues"});
    addScale(ScaleId::DominantPentatonic, "Dominant Pentatonic", {0,2,4,7,10}, {"pentatonic"});

    addScale(ScaleId::MajorBebop, "Major Bebop", {0,2,4,5,7,8,9,11}, {"bebop"});
    addScale(ScaleId::DominantBebop, "Dominant Bebop", {0,2,4,5,7,9,10,11}, {"bebop"});
    addScale(ScaleId::MinorBebop, "Minor Bebop", {0,2,3,5,7,8,9,10}, {"bebop"});
    addScale(ScaleId::DorianBebop, "Dorian Bebop", {0,2,3,5,7,9,10,11}, {"bebop"});

    // --- Voicing library (Stage 1 subset for Piano) ---
    auto addVoicing = [&](VoicingId id,
                          InstrumentKind inst,
                          QString name,
                          QString category,
                          QString formula,
                          QVector<int> degrees,
                          QStringList tags) {
        VoicingDef v;
        v.id = id;
        v.instrument = inst;
        v.name = std::move(name);
        v.category = std::move(category);
        v.formula = std::move(formula);
        v.chordDegrees = std::move(degrees);
        v.tags = std::move(tags);
        r.m_voicings.insert(id, v);
    };

    addVoicing(VoicingId::PianoShell_1_7, InstrumentKind::Piano,
               "Shell (1-7)", "Shell", "1-7",
               {1, 7}, {"piano", "shell"});
    addVoicing(VoicingId::PianoShell_1_3, InstrumentKind::Piano,
               "Shell (1-3)", "Shell", "1-3",
               {1, 3}, {"piano", "shell"});
    addVoicing(VoicingId::PianoGuideTones_3_7, InstrumentKind::Piano,
               "Guide tones (3-7)", "Shell", "3-7",
               {3, 7}, {"piano", "guide_tones"});
    addVoicing(VoicingId::PianoRootlessA_3_5_7_9, InstrumentKind::Piano,
               "Rootless Type A (3-5-7-9)", "Rootless", "3-5-7-9",
               {3, 5, 7, 9}, {"piano", "rootless"});
    addVoicing(VoicingId::PianoRootlessB_7_9_3_5, InstrumentKind::Piano,
               "Rootless Type B (7-9-3-5)", "Rootless", "7-9-3-5",
               {7, 9, 3, 5}, {"piano", "rootless"});
    addVoicing(VoicingId::PianoQuartal_Stack4ths, InstrumentKind::Piano,
               "Quartal (stack 4ths)", "Quartal", "Approx: 3-7-9 (placeholder for quartal stacks)",
               {3, 7, 9}, {"piano", "quartal"});

    addVoicing(VoicingId::PianoQuartal_3Notes, InstrumentKind::Piano,
               "Quartal (3-note)", "Quartal", "3-note quartal color (3-7-9)",
               {3, 7, 9}, {"piano", "quartal"});
    addVoicing(VoicingId::PianoQuartal_4Notes, InstrumentKind::Piano,
               "Quartal (4-note)", "Quartal", "4-note quartal color (3-7-9-11)",
               {3, 7, 9, 11}, {"piano", "quartal"});
    addVoicing(VoicingId::PianoSoWhat, InstrumentKind::Piano,
               "\"So What\" (quartal + M3)", "Quartal", "So-What-like: 3-7-9-11-(M3 above)",
               {3, 7, 9, 11}, {"piano", "quartal"});

    return r;
}

const ChordDef* OntologyRegistry::chord(ChordId id) const {
    auto it = m_chords.find(id);
    if (it == m_chords.end()) return nullptr;
    return &it.value();
}

const ScaleDef* OntologyRegistry::scale(ScaleId id) const {
    auto it = m_scales.find(id);
    if (it == m_scales.end()) return nullptr;
    return &it.value();
}

const VoicingDef* OntologyRegistry::voicing(VoicingId id) const {
    auto it = m_voicings.find(id);
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

} // namespace virtuoso::ontology

