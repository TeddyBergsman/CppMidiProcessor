#pragma once

#include <QString>
#include <QVector>
#include <QHash>

#include "music/ChordSymbol.h"
#include "chart/ChartModel.h"
#include "virtuoso/theory/FunctionalHarmony.h"
#include "virtuoso/ontology/OntologyRegistry.h"

namespace playback {

/**
 * KeyAnalyzer: Music theory-precise key detection using cadence patterns.
 * 
 * Unlike the current "average pitch classes over 8 bars" approach,
 * this analyzer uses FUNCTIONAL HARMONY patterns to detect key changes:
 * 
 * STRONGEST KEY INDICATORS:
 * 1. ii-V-I pattern → Definitive key indicator (the I is the tonic)
 * 2. V7 → I resolution → Strong key indicator
 * 3. IV-V-I (plagal-authentic) → Strong key indicator
 * 4. V/X (secondary dominant) → Tonicization of X
 * 
 * KEY CHANGE DETECTION:
 * - Look for ii-V-I in new key
 * - Section boundaries (form labels like A, B, C)
 * - Pivot chords (chord that fits both old and new key)
 * 
 * OUTPUT:
 * - Per-bar key assignments (not averages!)
 * - Key change boundaries with precise bar numbers
 * - Tonicization markers (temporary key vs modulation)
 */

struct KeyRegion {
    int startBar = 0;      // First bar of this key region (inclusive)
    int endBar = 0;        // Last bar of this key region (inclusive)
    int tonicPc = 0;       // 0-11
    virtuoso::theory::KeyMode mode = virtuoso::theory::KeyMode::Major;
    QString scaleKey;      // Ontology scale key
    QString scaleName;     // Display name
    double confidence = 0.0;
    QString evidence;      // e.g., "ii-V-I at bar 5", "section B start"
    bool isTonicization = false; // Temporary key (returns to previous)
};

struct CadencePattern {
    int barIndex = 0;      // Where the cadence resolves (I chord)
    int tonicPc = 0;       // Detected tonic
    virtuoso::theory::KeyMode mode = virtuoso::theory::KeyMode::Major;
    QString patternType;   // "ii-V-I", "V-I", "IV-V-I", "bII-I" (tritone sub), "V-vi" (deceptive)
    double strength = 0.0; // 0-1
    bool isTonicization = false; // Temporary key (secondary dominant)
};

class KeyAnalyzer {
public:
    explicit KeyAnalyzer(const virtuoso::ontology::OntologyRegistry& ontology);
    
    /**
     * Analyze the entire song and return key regions.
     * This is called ONCE during pre-planning and provides precise key assignments.
     */
    QVector<KeyRegion> analyze(const chart::ChartModel& model) const;
    
    /**
     * Get the key at a specific bar (uses pre-analyzed regions).
     * O(log n) lookup.
     */
    static KeyRegion keyAtBar(const QVector<KeyRegion>& regions, int barIndex);
    
    /**
     * Detect all cadence patterns in the song.
     * Used internally by analyze() and exposed for debugging.
     */
    QVector<CadencePattern> detectCadences(const chart::ChartModel& model) const;

private:
    const virtuoso::ontology::OntologyRegistry& m_ontology;
    
    // Parse all chords in the song with bar indices
    struct ChordAtBar {
        int barIndex;
        int beatIndex;
        music::ChordSymbol chord;
        // Note: We don't cache ChordDef here - pattern detection uses ChordSymbol directly
    };
    QVector<ChordAtBar> parseChords(const chart::ChartModel& model) const;
    
    // Check if three consecutive chords form ii-V-I (in any key)
    // Returns the detected tonic PC, or -1 if not a ii-V-I
    int detectIiVI(const ChordAtBar& a, const ChordAtBar& b, const ChordAtBar& c,
                   virtuoso::theory::KeyMode* outMode = nullptr) const;
    
    // Check if two consecutive chords form V-I
    int detectVI(const ChordAtBar& v, const ChordAtBar& i,
                 virtuoso::theory::KeyMode* outMode = nullptr) const;
    
    // Check for tritone substitution: bII7 → I (e.g., Db7 → Cmaj7)
    // The bII7 is a tritone away from V7 and resolves down by half step
    int detectTritoneSubResolution(const ChordAtBar& bII, const ChordAtBar& i,
                                   virtuoso::theory::KeyMode* outMode = nullptr) const;
    
    // Check for deceptive cadence: V → vi (confirms key but doesn't resolve to I)
    int detectDeceptiveCadence(const ChordAtBar& v, const ChordAtBar& vi,
                               virtuoso::theory::KeyMode* outMode = nullptr) const;
    
    // Check for IV-V-I plagal-authentic cadence
    int detectIVVI(const ChordAtBar& iv, const ChordAtBar& v, const ChordAtBar& i,
                   virtuoso::theory::KeyMode* outMode = nullptr) const;
    
    // Infer mode from a single chord (for initial key estimation)
    static virtuoso::theory::KeyMode modeFromChordQuality(music::ChordQuality quality);
    
    // Merge adjacent regions with same key
    void mergeRegions(QVector<KeyRegion>& regions) const;
    
    // Fill gaps between detected regions, using first chord quality for mode
    void fillGaps(QVector<KeyRegion>& regions, int totalBars, 
                  int fallbackPc, virtuoso::theory::KeyMode fallbackMode) const;
};

} // namespace playback
