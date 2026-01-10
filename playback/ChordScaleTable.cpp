#include "ChordScaleTable.h"

#include <QElapsedTimer>
#include <QDebug>

#include "virtuoso/theory/ScaleSuggester.h"

namespace playback {

// Static members
QHash<QString, ChordScaleEntry> ChordScaleTable::s_table;
bool ChordScaleTable::s_initialized = false;
int ChordScaleTable::s_hits = 0;
int ChordScaleTable::s_misses = 0;

QString ChordScaleTable::makeKey(const QString& chordDefKey, int interval, virtuoso::theory::KeyMode mode) {
    // Normalize interval to 0-11
    int norm = interval % 12;
    if (norm < 0) norm += 12;
    return QString("%1:%2:%3").arg(chordDefKey).arg(norm).arg(static_cast<int>(mode));
}

void ChordScaleTable::initialize(const virtuoso::ontology::OntologyRegistry& ontology) {
    if (s_initialized) return;
    
    QElapsedTimer timer;
    timer.start();
    
    s_table.clear();
    
    const auto allChords = ontology.allChords();
    const auto allScales = ontology.allScales();
    
    if (allChords.isEmpty() || allScales.isEmpty()) {
        qWarning() << "ChordScaleTable: Empty ontology, skipping initialization";
        return;
    }
    
    // For each chord type × each interval (0-11) × each mode (Major/Minor)
    // compute the best scale choice
    
    for (const auto* chordDef : allChords) {
        if (!chordDef) continue;
        
        for (int interval = 0; interval < 12; ++interval) {
            for (int modeInt = 0; modeInt <= 1; ++modeInt) {
                const auto mode = static_cast<virtuoso::theory::KeyMode>(modeInt);
                
                // Compute pitch classes for this chord at this interval
                // (as if the key tonic is at PC 0, and chord root is at `interval`)
                QSet<int> pcs;
                for (int iv : chordDef->intervals) {
                    pcs.insert((interval + iv) % 12);
                }
                
                // Get scale suggestions
                const auto suggestions = virtuoso::theory::suggestScalesForPitchClasses(ontology, pcs, 12);
                if (suggestions.isEmpty()) continue;
                
                // Analyze function (key tonic = 0, chord root = interval)
                const auto harmony = virtuoso::theory::analyzeChordInKey(0, mode, interval, *chordDef);
                
                // Rank scales by function-appropriate bonuses
                struct Ranked { virtuoso::theory::ScaleSuggestion s; double score; };
                QVector<Ranked> ranked;
                ranked.reserve(suggestions.size());
                
                for (const auto& s : suggestions) {
                    double bonus = 0.0;
                    
                    // Prefer scales rooted on the chord root
                    if ((s.bestTranspose % 12) == interval) bonus += 0.6;
                    
                    const QString name = s.name.toLower();
                    
                    // Function-specific bonuses (music theory rules)
                    if (harmony.function == "Dominant") {
                        // V7 chords: prefer Mixolydian, Altered, Lydian Dominant
                        if (name.contains("altered")) bonus += 0.45;
                        else if (name.contains("lydian dominant")) bonus += 0.40;
                        else if (name.contains("mixolydian")) bonus += 0.35;
                        else if (name.contains("half-whole") || name.contains("diminished")) bonus += 0.30;
                        else if (name.contains("phrygian dominant")) bonus += 0.25;
                    } else if (harmony.function == "Subdominant") {
                        // ii, IV chords: prefer Dorian, Lydian
                        if (name.contains("dorian")) bonus += 0.40;
                        else if (name.contains("lydian")) bonus += 0.35;
                        else if (name.contains("phrygian")) bonus += 0.20;
                    } else if (harmony.function == "Tonic") {
                        // I, vi chords: prefer Ionian, Aeolian, Lydian
                        if (name.contains("ionian") || name.contains("major")) bonus += 0.40;
                        else if (name.contains("aeolian") || name.contains("natural minor")) bonus += 0.35;
                        else if (name.contains("lydian")) bonus += 0.30;
                    }
                    
                    // Special cases for common jazz chords
                    const QString chordKey = chordDef->key.toLower();
                    if (chordKey.contains("halfdim") || chordKey.contains("min7b5")) {
                        // Half-diminished: Locrian ♮2 is preferred
                        if (name.contains("locrian") && name.contains("2")) bonus += 0.50;
                        else if (name.contains("locrian")) bonus += 0.30;
                    }
                    if (chordKey.contains("dim7")) {
                        // Fully diminished: whole-half diminished
                        if (name.contains("whole-half") || name.contains("diminished")) bonus += 0.50;
                    }
                    if (chordKey.contains("aug") || chordKey.contains("+")) {
                        // Augmented: whole tone or Lydian augmented
                        if (name.contains("whole tone")) bonus += 0.50;
                        else if (name.contains("lydian augmented")) bonus += 0.45;
                    }
                    
                    ranked.push_back({s, s.score + bonus});
                }
                
                // Sort by score descending
                std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
                    if (qAbs(a.score - b.score) > 0.001) return a.score > b.score;
                    return a.s.name < b.s.name;
                });
                
                // Store the best choice
                if (!ranked.isEmpty()) {
                    const auto& best = ranked.first().s;
                    ChordScaleEntry entry;
                    entry.scaleKey = best.key;
                    entry.scaleName = best.name;
                    entry.function = harmony.function;
                    entry.roman = harmony.roman;
                    
                    const QString key = makeKey(chordDef->key, interval, mode);
                    s_table.insert(key, entry);
                }
            }
        }
    }
    
    s_initialized = true;
    s_hits = 0;
    s_misses = 0;
    
    qInfo().noquote() << QString("ChordScaleTable: Initialized with %1 entries in %2ms")
                             .arg(s_table.size())
                             .arg(timer.elapsed());
}

bool ChordScaleTable::isInitialized() {
    return s_initialized;
}

const ChordScaleEntry* ChordScaleTable::lookup(
    const QString& chordDefKey,
    int intervalFromKey,
    virtuoso::theory::KeyMode keyMode) {
    
    if (!s_initialized) return nullptr;
    
    const QString key = makeKey(chordDefKey, intervalFromKey, keyMode);
    auto it = s_table.find(key);
    if (it != s_table.end()) {
        ++s_hits;
        return &it.value();
    }
    ++s_misses;
    return nullptr;
}

const ChordScaleEntry* ChordScaleTable::lookup(
    const virtuoso::ontology::ChordDef& chordDef,
    int chordRootPc,
    int keyTonicPc,
    virtuoso::theory::KeyMode keyMode) {
    
    // Calculate interval from key
    int interval = (chordRootPc - keyTonicPc) % 12;
    if (interval < 0) interval += 12;
    
    return lookup(chordDef.key, interval, keyMode);
}

int ChordScaleTable::entryCount() { return s_table.size(); }
int ChordScaleTable::hitCount() { return s_hits; }
int ChordScaleTable::missCount() { return s_misses; }
void ChordScaleTable::resetStats() { s_hits = 0; s_misses = 0; }

} // namespace playback
