#pragma once

#include <QString>
#include <QHash>

#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/theory/FunctionalHarmony.h"

namespace playback {

/**
 * ChordScaleTable: Pre-computed chord→scale mappings for O(1) lookup.
 * 
 * This table is built ONCE at startup and provides instant answers to:
 * "Given chord type X, interval Y from key, and key mode Z, what scale should I use?"
 * 
 * MUSIC THEORY BASIS:
 * The scale choice for a chord depends on:
 * 1. The chord quality (min7, dom7, maj7, etc.)
 * 2. The chord's function in the key (Tonic, Subdominant, Dominant)
 * 3. The key mode (Major or Minor)
 * 
 * Examples:
 *   min7 at interval 2 (ii7) in Major → Dorian
 *   dom7 at interval 7 (V7) in Major → Mixolydian (or Altered if resolving)
 *   min7b5 at interval 2 (iiø7) in Minor → Locrian ♮2
 *   maj7 at interval 0 (I) in Major → Ionian
 */

struct ChordScaleEntry {
    QString scaleKey;     // Ontology scale key, e.g., "dorian"
    QString scaleName;    // Display name, e.g., "Dorian"
    QString function;     // "Tonic", "Subdominant", "Dominant", "Other"
    QString roman;        // "ii7", "V7", "Imaj7", etc.
};

class ChordScaleTable {
public:
    // Build the table from ontology (call once at startup)
    static void initialize(const virtuoso::ontology::OntologyRegistry& ontology);
    
    // Check if initialized
    static bool isInitialized();
    
    // O(1) lookup: given chord type, interval from key, and key mode
    // Returns nullptr if not found (fallback to runtime computation)
    static const ChordScaleEntry* lookup(
        const QString& chordDefKey,     // e.g., "min7", "dom7"
        int intervalFromKey,            // 0-11, chord root relative to key tonic
        virtuoso::theory::KeyMode keyMode
    );
    
    // Convenience: lookup by chord symbol and key context
    static const ChordScaleEntry* lookup(
        const virtuoso::ontology::ChordDef& chordDef,
        int chordRootPc,
        int keyTonicPc,
        virtuoso::theory::KeyMode keyMode
    );
    
    // Statistics for debugging
    static int entryCount();
    static int hitCount();
    static int missCount();
    static void resetStats();

private:
    // Key format: "chordDefKey:interval:mode"
    static QString makeKey(const QString& chordDefKey, int interval, virtuoso::theory::KeyMode mode);
    
    // The actual table
    static QHash<QString, ChordScaleEntry> s_table;
    static bool s_initialized;
    static int s_hits;
    static int s_misses;
};

} // namespace playback
