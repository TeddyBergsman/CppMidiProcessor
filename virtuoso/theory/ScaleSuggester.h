#pragma once

#include <QSet>
#include <QVector>
#include <QString>

#include "virtuoso/ontology/OntologyRegistry.h"

namespace virtuoso::theory {

struct ScaleSuggestion {
    QString key;
    QString name;
    double score = 0.0;      // higher is better
    double coverage = 0.0;   // 0..1 (fraction of pcs covered)
    int matched = 0;
    int total = 0;
    int bestTranspose = 0;   // 0..11 (how to transpose the scale to best match the target pc-set)
};

// Deterministic scale ranking for a target pitch-class set.
// - Prefers full coverage (all pcs present in scale)
// - Then prefers smaller scales (more "specific")
// - Then prefers scales tagged closer to jazz usage, if present
QVector<ScaleSuggestion> suggestScalesForPitchClasses(
    const virtuoso::ontology::OntologyRegistry& registry,
    const QSet<int>& pitchClasses,
    int limit = 6);

// Optional explicit hint mapping for UST and common dominant sounds.
// Returns scale keys in descending preference order. Empty means "no hint".
QVector<QString> explicitHintScalesForContext(const QString& voicingKey, const QString& chordKey);

} // namespace virtuoso::theory

