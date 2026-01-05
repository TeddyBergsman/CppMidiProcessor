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

using Key = QString;

struct ChordDef {
    Key key;                  // stable id, e.g. "maj7", "7b9", "ust_bVI"
    QString name;               // human label: "maj7", "m7", "7", etc.
    QVector<int> intervals;     // semitone offsets from root (0..)
    QStringList tags;           // e.g. "triad", "seventh", "shell"
    int order = 1000;           // stable UI ordering
    int bassInterval = -1;      // optional slash-bass/inversion bass note (semitones from root)
};

struct ScaleDef {
    Key key;               // stable id, e.g. "ionian", "altered", "hungarian_minor"
    QString name;           // e.g. "Ionian"
    QVector<int> intervals; // semitone offsets from tonic
    QStringList tags;       // e.g. "diatonic", "symmetric"
    int order = 1000;       // stable UI ordering
};

struct VoicingDef {
    Key key;                  // stable id, e.g. "piano_rootless_a"
    InstrumentKind instrument{};
    QString name;           // e.g. "Shell (1-7)"
    QString category;       // e.g. "Shell", "Rootless", "Quartal"
    QString formula;        // free-form descriptor; later becomes structured
    QVector<int> chordDegrees; // e.g. {1,7} or {3,5,7,9} (degree-based)
    QVector<int> intervals;    // optional alternative: absolute semitone offsets from root (0..)
    QStringList tags;       // e.g. "piano", "rootless"
    int order = 1000;       // stable UI ordering
};

struct PolychordTemplate {
    Key key;          // stable id
    QString name;     // display name
    QString formula;  // e.g. "UpperTriad / Bass" or "UpperTriad over LowerChord"
    QStringList tags;
    int order = 1000;
};

// Static registry (in-memory knowledge base).
// Stage 1: code-defined tables; later can be made data-driven (JSON).
class OntologyRegistry {
public:
    static OntologyRegistry builtins();

    const ChordDef* chord(const Key& key) const;
    const ScaleDef* scale(const Key& key) const;
    const VoicingDef* voicing(const Key& key) const;

    QVector<const ChordDef*> chordsWithTag(const QString& tag) const;
    QVector<const ScaleDef*> scalesWithTag(const QString& tag) const;
    QVector<const VoicingDef*> voicingsFor(InstrumentKind instrument) const;

    QVector<const ChordDef*> allChords() const;
    QVector<const ScaleDef*> allScales() const;
    QVector<const VoicingDef*> allVoicings() const;
    QVector<const PolychordTemplate*> allPolychordTemplates() const;

    const PolychordTemplate* polychordTemplate(const Key& key) const;

private:
    QHash<Key, ChordDef> m_chords;
    QHash<Key, ScaleDef> m_scales;
    QHash<Key, VoicingDef> m_voicings;
    QHash<Key, PolychordTemplate> m_polychords;
};

} // namespace virtuoso::ontology

