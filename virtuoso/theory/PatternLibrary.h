#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "virtuoso/ontology/OntologyRegistry.h"

namespace virtuoso::theory {

enum class PatternStepKind {
    ChordDegree = 0, // degrees: 1,3,5,7,9,11,13...
    ScaleDegree,     // degrees: 1..N (index into scale intervals)
    SemitoneOffset,  // direct semitone offset from root
    Rest,
};

struct PatternStep {
    PatternStepKind kind = PatternStepKind::ChordDegree;
    int value = 0; // degree number or semitone offset
};

enum class PatternContour {
    AsWritten = 0,
    Up,
    Down,
    UpDown,
    DownUp,
};

struct PatternDef {
    QString key;
    QString name;
    QStringList tags;     // e.g. "arpeggio", "bebop", "triad"
    int order = 1000;
    PatternContour contour = PatternContour::AsWritten;
    QVector<PatternStep> steps;
};

class PatternLibrary {
public:
    static PatternLibrary builtins();

    QVector<const PatternDef*> all() const;
    const PatternDef* pattern(const QString& key) const;

    // Applicators (pure logic)
    static int chordDegreeToSemitone(const virtuoso::ontology::ChordDef* chordCtx, int degree);
    static int scaleDegreeToSemitone(const virtuoso::ontology::ScaleDef* scale, int degree);

    static QVector<int> renderSemitoneSequence(const PatternDef& pattern,
                                               const virtuoso::ontology::ChordDef* chordCtx,
                                               const virtuoso::ontology::ScaleDef* scaleCtx);

private:
    QVector<PatternDef> m_patterns;
};

} // namespace virtuoso::theory

