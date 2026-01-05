#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>

namespace virtuoso::ontology {

enum class InstrumentKind {
    Piano = 0,
    Bass,
    Drums,
    Guitar,
    Winds,
    Strings,
};

enum class ChordId {
    Power5,
    Shell_1_3,
    Shell_1_7,

    MajorTriad,
    MinorTriad,
    DiminishedTriad,
    AugmentedTriad,
    Sus2Triad,
    Sus4Triad,
    PhrygianTriad, // 1-b2-5

    Major7,
    Minor7,
    Dominant7,
    HalfDiminished7,
    Diminished7,

    MinorMajor7,     // min(maj7)
    Augmented7,      // aug7
    Dominant7Sus4,   // 7sus4
    SevenSharp5,     // 7#5
    SevenFlat5,      // 7b5
    Six,             // 6
    MinorSix,        // min6
};

// Allow strongly-typed enum keys in QHash.
inline size_t qHash(ChordId key, size_t seed = 0) noexcept {
    return ::qHash(static_cast<int>(key), seed);
}

struct ChordDef {
    ChordId id{};
    QString name;               // human label: "maj7", "m7", "7", etc.
    QVector<int> intervals;     // semitone offsets from root (0..)
    QStringList tags;           // e.g. "triad", "seventh", "shell"
};

enum class ScaleId {
    Ionian,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Aeolian,
    Locrian,

    MelodicMinor,
    DorianB2,        // melodic minor mode 2
    LydianAugmented, // melodic minor mode 3
    Altered,         // melodic minor mode 7
    LydianDominant,  // melodic minor mode 4
    MixolydianB6,    // melodic minor mode 5
    LocrianNat2,     // melodic minor mode 6

    HarmonicMinor,
    LocrianSharp6,    // harmonic minor mode 2
    IonianSharp5,     // harmonic minor mode 3
    DorianSharp4,     // harmonic minor mode 4
    PhrygianDominant, // harmonic minor mode 5
    LydianSharp2,     // harmonic minor mode 6
    SuperLocrianBb7,  // harmonic minor mode 7

    HarmonicMajor,
    DorianB5,
    PhrygianB4,
    LydianB3,
    MixolydianB2,
    LydianAugSharp2,
    LocrianBb7,

    WholeTone,
    DiminishedWH,
    DiminishedHW,

    MajorPentatonic,
    MinorPentatonic,
    Blues,
    MajorBlues,
    DominantPentatonic,

    MajorBebop,
    DominantBebop,
    MinorBebop,
    DorianBebop,
};

inline size_t qHash(ScaleId key, size_t seed = 0) noexcept {
    return ::qHash(static_cast<int>(key), seed);
}

struct ScaleDef {
    ScaleId id{};
    QString name;           // e.g. "Ionian"
    QVector<int> intervals; // semitone offsets from tonic
    QStringList tags;       // e.g. "diatonic", "symmetric"
};

enum class VoicingId {
    // Piano basics (subset for Stage 1)
    PianoShell_1_7,
    PianoShell_1_3,
    PianoGuideTones_3_7,
    PianoRootlessA_3_5_7_9,
    PianoRootlessB_7_9_3_5,
    PianoQuartal_Stack4ths,
    PianoQuartal_3Notes,
    PianoQuartal_4Notes,
    PianoSoWhat,
};

inline size_t qHash(VoicingId key, size_t seed = 0) noexcept {
    return ::qHash(static_cast<int>(key), seed);
}

struct VoicingDef {
    VoicingId id{};
    InstrumentKind instrument{};
    QString name;           // e.g. "Shell (1-7)"
    QString category;       // e.g. "Shell", "Rootless", "Quartal"
    QString formula;        // free-form descriptor; later becomes structured
    QVector<int> chordDegrees; // e.g. {1,7} or {3,5,7,9}
    QStringList tags;       // e.g. "piano", "rootless"
};

// Static registry (in-memory knowledge base).
// Stage 1: code-defined tables; later can be made data-driven (JSON).
class OntologyRegistry {
public:
    static OntologyRegistry builtins();

    const ChordDef* chord(ChordId id) const;
    const ScaleDef* scale(ScaleId id) const;
    const VoicingDef* voicing(VoicingId id) const;

    QVector<const ChordDef*> chordsWithTag(const QString& tag) const;
    QVector<const ScaleDef*> scalesWithTag(const QString& tag) const;
    QVector<const VoicingDef*> voicingsFor(InstrumentKind instrument) const;

    QVector<const ChordDef*> allChords() const;
    QVector<const ScaleDef*> allScales() const;
    QVector<const VoicingDef*> allVoicings() const;

private:
    QHash<ChordId, ChordDef> m_chords;
    QHash<ScaleId, ScaleDef> m_scales;
    QHash<VoicingId, VoicingDef> m_voicings;
};

} // namespace virtuoso::ontology

