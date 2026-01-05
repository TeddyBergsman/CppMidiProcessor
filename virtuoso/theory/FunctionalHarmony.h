#pragma once

#include <QString>

#include "virtuoso/ontology/OntologyRegistry.h"

namespace virtuoso::theory {

struct HarmonyLabel {
    QString roman;      // e.g. "V7", "iiø7", "V/V"
    QString function;   // "Tonic" | "Subdominant" | "Dominant" | "Other"
    QString detail;     // optional extra hint (e.g. "secondary dominant")
    double confidence = 0.0; // 0..1
};

// Minimal functional-harmony analyzer for major keys (expandable).
HarmonyLabel analyzeChordInMajorKey(int tonicPc,
                                   int chordRootPc,
                                   const virtuoso::ontology::ChordDef& chord);

} // namespace virtuoso::theory

