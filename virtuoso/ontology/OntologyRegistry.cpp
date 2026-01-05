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

    addChord(ChordId::Power5, "5", {0, 7}, {"dyad"});
    addChord(ChordId::MajorTriad, "maj", {0, 4, 7}, {"triad"});
    addChord(ChordId::MinorTriad, "min", {0, 3, 7}, {"triad"});
    addChord(ChordId::DiminishedTriad, "dim", {0, 3, 6}, {"triad"});
    addChord(ChordId::AugmentedTriad, "aug", {0, 4, 8}, {"triad"});
    addChord(ChordId::Sus2Triad, "sus2", {0, 2, 7}, {"triad", "sus"});
    addChord(ChordId::Sus4Triad, "sus4", {0, 5, 7}, {"triad", "sus"});

    addChord(ChordId::Major7, "maj7", {0, 4, 7, 11}, {"seventh"});
    addChord(ChordId::Minor7, "min7", {0, 3, 7, 10}, {"seventh"});
    addChord(ChordId::Dominant7, "7", {0, 4, 7, 10}, {"seventh", "dominant"});
    addChord(ChordId::HalfDiminished7, "m7b5", {0, 3, 6, 10}, {"seventh"});
    addChord(ChordId::Diminished7, "dim7", {0, 3, 6, 9}, {"seventh", "symmetric"});

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
    addScale(ScaleId::LydianDominant, "Lydian Dominant", {0,2,4,6,7,9,10}, {"melodic_minor"});
    addScale(ScaleId::Altered, "Altered (Super Locrian)", {0,1,3,4,6,8,10}, {"melodic_minor"});
    addScale(ScaleId::HarmonicMinor, "Harmonic Minor", {0,2,3,5,7,8,11}, {"harmonic_minor"});

    addScale(ScaleId::WholeTone, "Whole Tone", {0,2,4,6,8,10}, {"symmetric"});
    addScale(ScaleId::DiminishedWH, "Diminished (Whole-Half)", {0,2,3,5,6,8,9,11}, {"symmetric"});
    addScale(ScaleId::DiminishedHW, "Diminished (Half-Whole)", {0,1,3,4,6,7,9,10}, {"symmetric"});

    addScale(ScaleId::MajorPentatonic, "Major Pentatonic", {0,2,4,7,9}, {"pentatonic"});
    addScale(ScaleId::MinorPentatonic, "Minor Pentatonic", {0,3,5,7,10}, {"pentatonic"});
    addScale(ScaleId::Blues, "Minor Blues", {0,3,5,6,7,10}, {"pentatonic", "blues"});

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
               "Quartal (stack 4ths)", "Quartal", "P4-P4-(M3) So-What-like",
               {}, {"piano", "quartal"});

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

