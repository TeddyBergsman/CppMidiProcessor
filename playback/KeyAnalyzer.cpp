#include "KeyAnalyzer.h"
#include "HarmonyContext.h"

#include <QDebug>
#include <algorithm>

namespace playback {

KeyAnalyzer::KeyAnalyzer(const virtuoso::ontology::OntologyRegistry& ontology)
    : m_ontology(ontology) {
}

virtuoso::theory::KeyMode KeyAnalyzer::modeFromChordQuality(music::ChordQuality quality) {
    using music::ChordQuality;
    switch (quality) {
        case ChordQuality::Minor:
        case ChordQuality::HalfDiminished:
        case ChordQuality::Diminished:
            return virtuoso::theory::KeyMode::Minor;
        case ChordQuality::Major:
        case ChordQuality::Dominant:
        case ChordQuality::Augmented:
        case ChordQuality::Sus2:
        case ChordQuality::Sus4:
        case ChordQuality::Power5:
        default:
            return virtuoso::theory::KeyMode::Major;
    }
}

QVector<KeyAnalyzer::ChordAtBar> KeyAnalyzer::parseChords(const chart::ChartModel& model) const {
    QVector<ChordAtBar> result;
    
    int barIdx = 0;
    for (const auto& line : model.lines) {
        for (const auto& bar : line.bars) {
            int beatIdx = 0;
            for (const auto& cell : bar.cells) {
                const QString text = cell.chord.trimmed();
                if (text.isEmpty()) {
                    ++beatIdx;
                    continue;
                }
                
                music::ChordSymbol parsed;
                if (!music::parseChordSymbol(text, parsed)) {
                    ++beatIdx;
                    continue;
                }
                if (parsed.placeholder || parsed.noChord || parsed.rootPc < 0) {
                    ++beatIdx;
                    continue;
                }
                
                ChordAtBar cab;
                cab.barIndex = barIdx;
                cab.beatIndex = beatIdx;
                cab.chord = parsed;
                result.push_back(cab);
                ++beatIdx;
            }
            ++barIdx;
        }
    }
    
    return result;
}

int KeyAnalyzer::detectIiVI(const ChordAtBar& a, const ChordAtBar& b, const ChordAtBar& c,
                            virtuoso::theory::KeyMode* outMode) const {
    // ii-V-I pattern:
    //   MAJOR: min7 → dom7 → maj7  (intervals: P4 up, P4 up)
    //   MINOR: min7b5 → dom7 → min7 OR min7 → dom7 → min7 (Dorian)
    
    using music::ChordQuality;
    
    const int rootA = a.chord.rootPc;
    const int rootB = b.chord.rootPc;
    const int rootC = c.chord.rootPc;
    
    // ii→V→I: each step is up a perfect 4th (5 semitones)
    int intervalAtoB = (rootB - rootA + 12) % 12;
    int intervalBtoC = (rootC - rootB + 12) % 12;
    
    if (intervalAtoB != 5 || intervalBtoC != 5) return -1;
    
    // V chord MUST be dominant
    if (b.chord.quality != ChordQuality::Dominant) return -1;
    
    const bool isMinorII = (a.chord.quality == ChordQuality::Minor);
    const bool isHalfDimII = (a.chord.quality == ChordQuality::HalfDiminished);
    const bool isMajI = (c.chord.quality == ChordQuality::Major);
    const bool isMinorI = (c.chord.quality == ChordQuality::Minor);
    
    // Major ii-V-I
    if (isMinorII && isMajI) {
        if (outMode) *outMode = virtuoso::theory::KeyMode::Major;
        return rootC;
    }
    
    // Minor ii-V-i (half-diminished ii)
    if (isHalfDimII && isMinorI) {
        if (outMode) *outMode = virtuoso::theory::KeyMode::Minor;
        return rootC;
    }
    
    // Dorian ii-V-i (minor ii)
    if (isMinorII && isMinorI) {
        if (outMode) *outMode = virtuoso::theory::KeyMode::Minor;
        return rootC;
    }
    
    return -1;
}

int KeyAnalyzer::detectVI(const ChordAtBar& v, const ChordAtBar& i,
                          virtuoso::theory::KeyMode* outMode) const {
    using music::ChordQuality;
    
    const int rootV = v.chord.rootPc;
    const int rootI = i.chord.rootPc;
    
    // V→I is up a perfect 4th = 5 semitones
    if ((rootI - rootV + 12) % 12 != 5) return -1;
    
    // V must be dominant
    if (v.chord.quality != ChordQuality::Dominant) return -1;
    
    if (i.chord.quality == ChordQuality::Major) {
        if (outMode) *outMode = virtuoso::theory::KeyMode::Major;
        return rootI;
    } else if (i.chord.quality == ChordQuality::Minor) {
        if (outMode) *outMode = virtuoso::theory::KeyMode::Minor;
        return rootI;
    }
    
    return -1;
}

int KeyAnalyzer::detectTritoneSubResolution(const ChordAtBar& bII, const ChordAtBar& i,
                                            virtuoso::theory::KeyMode* outMode) const {
    // Tritone substitution: bII7 → I
    // The bII7 is a tritone (6 semitones) away from V7
    // It resolves DOWN by half step to I
    // Example: Db7 → Cmaj7 (in key of C)
    
    using music::ChordQuality;
    
    const int rootBII = bII.chord.rootPc;
    const int rootI = i.chord.rootPc;
    
    // bII→I is DOWN a half step = up 11 semitones (or down 1)
    int interval = (rootI - rootBII + 12) % 12;
    if (interval != 11) return -1;  // 11 semitones up = 1 semitone down
    
    // bII must be dominant
    if (bII.chord.quality != ChordQuality::Dominant) return -1;
    
    if (i.chord.quality == ChordQuality::Major) {
        if (outMode) *outMode = virtuoso::theory::KeyMode::Major;
        return rootI;
    } else if (i.chord.quality == ChordQuality::Minor) {
        if (outMode) *outMode = virtuoso::theory::KeyMode::Minor;
        return rootI;
    }
    
    return -1;
}

int KeyAnalyzer::detectDeceptiveCadence(const ChordAtBar& v, const ChordAtBar& vi,
                                        virtuoso::theory::KeyMode* outMode) const {
    // Deceptive cadence: V → vi (in major) or V → VI (in minor)
    // Confirms the key but doesn't resolve to tonic
    // Example in C major: G7 → Am
    
    using music::ChordQuality;
    
    const int rootV = v.chord.rootPc;
    const int rootVI = vi.chord.rootPc;
    
    // V→vi in major: V is at scale degree 5, vi is at 6 (2 semitones up)
    // Actually it's up a minor 2nd from the expected I
    // V→vi: up 2 semitones from V (whole step)
    // Wait, let me recalculate:
    // In C major: G (7) → Am (9 = A). Interval = 2 semitones
    // But we need to find the KEY (C), not just detect the pattern
    // If V=G(7) and vi=A(9), then I=C(0). V→I would be 5 semitones
    // So tonic = (rootV + 5) % 12
    
    int interval = (rootVI - rootV + 12) % 12;
    if (interval != 2) return -1;  // V→vi is up a major 2nd
    
    // V must be dominant
    if (v.chord.quality != ChordQuality::Dominant) return -1;
    
    // vi should be minor (in major key)
    if (vi.chord.quality == ChordQuality::Minor) {
        // Deceptive cadence in MAJOR key
        // The tonic is a P4 up from V (same as V-I)
        int tonic = (rootV + 5) % 12;
        if (outMode) *outMode = virtuoso::theory::KeyMode::Major;
        return tonic;
    }
    
    // VI should be major (in minor key)
    if (vi.chord.quality == ChordQuality::Major) {
        // Deceptive cadence in MINOR key
        int tonic = (rootV + 5) % 12;
        if (outMode) *outMode = virtuoso::theory::KeyMode::Minor;
        return tonic;
    }
    
    return -1;
}

int KeyAnalyzer::detectIVVI(const ChordAtBar& iv, const ChordAtBar& v, const ChordAtBar& i,
                            virtuoso::theory::KeyMode* outMode) const {
    // IV-V-I (plagal-authentic cadence)
    // In C major: F → G → C
    // Intervals: IV→V is up a major 2nd (2 semitones), V→I is up P4 (5 semitones)
    
    using music::ChordQuality;
    
    const int rootIV = iv.chord.rootPc;
    const int rootV = v.chord.rootPc;
    const int rootI = i.chord.rootPc;
    
    int intervalIVtoV = (rootV - rootIV + 12) % 12;
    int intervalVtoI = (rootI - rootV + 12) % 12;
    
    if (intervalIVtoV != 2 || intervalVtoI != 5) return -1;
    
    // V must be dominant
    if (v.chord.quality != ChordQuality::Dominant) return -1;
    
    // IV should be major (in major key) or minor (in minor key)
    // I determines the mode
    if (i.chord.quality == ChordQuality::Major && iv.chord.quality == ChordQuality::Major) {
        if (outMode) *outMode = virtuoso::theory::KeyMode::Major;
        return rootI;
    }
    
    if (i.chord.quality == ChordQuality::Minor && iv.chord.quality == ChordQuality::Minor) {
        if (outMode) *outMode = virtuoso::theory::KeyMode::Minor;
        return rootI;
    }
    
    return -1;
}

QVector<CadencePattern> KeyAnalyzer::detectCadences(const chart::ChartModel& model) const {
    QVector<CadencePattern> result;
    const auto chords = parseChords(model);
    
    if (chords.size() < 2) return result;
    
    // Track which bars already have a detected cadence to avoid duplicates
    QSet<int> detectedBars;
    
    // === ii-V-I patterns (STRONGEST evidence, strength 1.0) ===
    for (int i = 0; i + 2 < chords.size(); ++i) {
        virtuoso::theory::KeyMode mode;
        int tonic = detectIiVI(chords[i], chords[i+1], chords[i+2], &mode);
        if (tonic >= 0) {
            CadencePattern cp;
            cp.barIndex = chords[i+2].barIndex;
            cp.tonicPc = tonic;
            cp.mode = mode;
            cp.patternType = "ii-V-I";
            cp.strength = 1.0;
            result.push_back(cp);
            detectedBars.insert(cp.barIndex);
        }
    }
    
    // === IV-V-I patterns (strength 0.9) ===
    for (int i = 0; i + 2 < chords.size(); ++i) {
        virtuoso::theory::KeyMode mode;
        int tonic = detectIVVI(chords[i], chords[i+1], chords[i+2], &mode);
        if (tonic >= 0 && !detectedBars.contains(chords[i+2].barIndex)) {
            CadencePattern cp;
            cp.barIndex = chords[i+2].barIndex;
            cp.tonicPc = tonic;
            cp.mode = mode;
            cp.patternType = "IV-V-I";
            cp.strength = 0.9;
            result.push_back(cp);
            detectedBars.insert(cp.barIndex);
        }
    }
    
    // === V-I patterns (strength 0.75) ===
    for (int i = 0; i + 1 < chords.size(); ++i) {
        virtuoso::theory::KeyMode mode;
        int tonic = detectVI(chords[i], chords[i+1], &mode);
        if (tonic >= 0 && !detectedBars.contains(chords[i+1].barIndex)) {
            CadencePattern cp;
            cp.barIndex = chords[i+1].barIndex;
            cp.tonicPc = tonic;
            cp.mode = mode;
            cp.patternType = "V-I";
            cp.strength = 0.75;
            result.push_back(cp);
            detectedBars.insert(cp.barIndex);
        }
    }
    
    // === Tritone substitution: bII7 → I (strength 0.7) ===
    for (int i = 0; i + 1 < chords.size(); ++i) {
        virtuoso::theory::KeyMode mode;
        int tonic = detectTritoneSubResolution(chords[i], chords[i+1], &mode);
        if (tonic >= 0 && !detectedBars.contains(chords[i+1].barIndex)) {
            CadencePattern cp;
            cp.barIndex = chords[i+1].barIndex;
            cp.tonicPc = tonic;
            cp.mode = mode;
            cp.patternType = "bII-I";
            cp.strength = 0.7;
            result.push_back(cp);
            detectedBars.insert(cp.barIndex);
        }
    }
    
    // === Deceptive cadence: V → vi (strength 0.6) ===
    for (int i = 0; i + 1 < chords.size(); ++i) {
        virtuoso::theory::KeyMode mode;
        int tonic = detectDeceptiveCadence(chords[i], chords[i+1], &mode);
        if (tonic >= 0 && !detectedBars.contains(chords[i+1].barIndex)) {
            CadencePattern cp;
            cp.barIndex = chords[i+1].barIndex;
            cp.tonicPc = tonic;
            cp.mode = mode;
            cp.patternType = "V-vi";
            cp.strength = 0.6;
            result.push_back(cp);
            detectedBars.insert(cp.barIndex);
        }
    }
    
    // Sort by bar index, then by strength (strongest first for same bar)
    std::sort(result.begin(), result.end(), [](const CadencePattern& a, const CadencePattern& b) {
        if (a.barIndex != b.barIndex) return a.barIndex < b.barIndex;
        return a.strength > b.strength;
    });
    
    return result;
}

void KeyAnalyzer::mergeRegions(QVector<KeyRegion>& regions) const {
    if (regions.size() < 2) return;
    
    std::sort(regions.begin(), regions.end(), [](const KeyRegion& a, const KeyRegion& b) {
        return a.startBar < b.startBar;
    });
    
    QVector<KeyRegion> merged;
    merged.push_back(regions.first());
    
    for (int i = 1; i < regions.size(); ++i) {
        const auto& curr = regions[i];
        auto& prev = merged.last();
        
        if (curr.tonicPc == prev.tonicPc && curr.mode == prev.mode) {
            prev.endBar = qMax(prev.endBar, curr.endBar);
            if (curr.confidence > prev.confidence) {
                prev.confidence = curr.confidence;
                prev.evidence = curr.evidence;
            }
        } else {
            merged.push_back(curr);
        }
    }
    
    regions = merged;
}

void KeyAnalyzer::fillGaps(QVector<KeyRegion>& regions, int totalBars, 
                           int fallbackPc, virtuoso::theory::KeyMode fallbackMode) const {
    if (regions.isEmpty()) {
        // No detected keys - use first chord as key center with inferred mode
        KeyRegion r;
        r.startBar = 0;
        r.endBar = totalBars - 1;
        r.tonicPc = fallbackPc;
        r.mode = fallbackMode;
        
        if (fallbackMode == virtuoso::theory::KeyMode::Minor) {
            r.scaleKey = "aeolian";
            r.scaleName = "Aeolian (Natural Minor)";
        } else {
            r.scaleKey = "ionian";
            r.scaleName = "Ionian (Major)";
        }
        
        r.confidence = 0.5;  // Moderate confidence - based on first chord
        r.evidence = QString("first chord (%1%2)")
            .arg(HarmonyContext::pcName(fallbackPc))
            .arg(fallbackMode == virtuoso::theory::KeyMode::Minor ? "m" : "");
        regions.push_back(r);
        return;
    }
    
    // Extend each region to fill gaps
    for (int i = regions.size() - 1; i >= 0; --i) {
        auto& curr = regions[i];
        int prevEnd = (i > 0) ? regions[i-1].endBar : -1;
        
        if (curr.startBar > prevEnd + 1) {
            curr.startBar = prevEnd + 1;
        }
        
        int nextStart = (i + 1 < regions.size()) ? regions[i+1].startBar : totalBars;
        if (curr.endBar < nextStart - 1) {
            curr.endBar = nextStart - 1;
        }
    }
    
    if (!regions.isEmpty() && regions.first().startBar > 0) {
        regions.first().startBar = 0;
    }
    
    if (!regions.isEmpty() && regions.last().endBar < totalBars - 1) {
        regions.last().endBar = totalBars - 1;
    }
}

QVector<KeyRegion> KeyAnalyzer::analyze(const chart::ChartModel& model) const {
    QVector<KeyRegion> regions;
    
    // Count total bars
    int totalBars = 0;
    for (const auto& line : model.lines) {
        totalBars += line.bars.size();
    }
    if (totalBars == 0) return regions;
    
    // Parse all chords
    const auto chords = parseChords(model);
    
    // === FIRST CHORD ANALYSIS ===
    // The first chord provides a strong hint about the key, especially for the mode
    int fallbackPc = 0;  // C
    virtuoso::theory::KeyMode fallbackMode = virtuoso::theory::KeyMode::Major;
    
    if (!chords.isEmpty()) {
        const auto& firstChord = chords.first().chord;
        fallbackPc = firstChord.rootPc;
        fallbackMode = modeFromChordQuality(firstChord.quality);
        
        qDebug().noquote() << QString("KeyAnalyzer: First chord is %1%2 → initial key guess: %3 %4")
            .arg(HarmonyContext::pcName(firstChord.rootPc))
            .arg(firstChord.quality == music::ChordQuality::Minor ? "m" : 
                 firstChord.quality == music::ChordQuality::Dominant ? "7" : "")
            .arg(HarmonyContext::pcName(fallbackPc))
            .arg(fallbackMode == virtuoso::theory::KeyMode::Minor ? "Minor" : "Major");
    }
    
    // === CADENCE DETECTION ===
    const auto cadences = detectCadences(model);
    
    qDebug() << "KeyAnalyzer: Detected" << cadences.size() << "cadence pattern(s)";
    for (const auto& c : cadences) {
        qDebug().noquote() << QString("  - %1 at bar %2 → %3 %4 (strength %.2f)")
            .arg(c.patternType)
            .arg(c.barIndex + 1)
            .arg(HarmonyContext::pcName(c.tonicPc))
            .arg(c.mode == virtuoso::theory::KeyMode::Minor ? "Minor" : "Major")
            .arg(c.strength);
    }
    
    if (cadences.isEmpty()) {
        qDebug() << "KeyAnalyzer: No cadences found, using first chord as key center";
        fillGaps(regions, totalBars, fallbackPc, fallbackMode);
        return regions;
    }
    
    // === BUILD REGIONS FROM CADENCES ===
    for (const auto& c : cadences) {
        KeyRegion r;
        r.startBar = c.barIndex;
        r.endBar = c.barIndex;
        r.tonicPc = c.tonicPc;
        r.mode = c.mode;
        r.confidence = c.strength;
        r.evidence = QString("%1 at bar %2").arg(c.patternType).arg(c.barIndex + 1);
        r.isTonicization = c.isTonicization;
        
        if (c.mode == virtuoso::theory::KeyMode::Major) {
            r.scaleKey = "ionian";
            r.scaleName = "Ionian (Major)";
        } else {
            r.scaleKey = "aeolian";
            r.scaleName = "Aeolian (Natural Minor)";
        }
        
        regions.push_back(r);
    }
    
    // === CHECK IF FIRST CHORD'S KEY MATCHES ANY EARLY CADENCE ===
    // If the first cadence confirms the same key as the first chord,
    // that's very strong evidence. If it differs, the cadence takes precedence.
    if (!regions.isEmpty()) {
        const auto& firstRegion = regions.first();
        if (firstRegion.tonicPc == fallbackPc && firstRegion.mode == fallbackMode) {
            // First chord matches first cadence - boost confidence
            regions.first().confidence = qMin(1.0, firstRegion.confidence + 0.2);
            regions.first().evidence += " (confirmed by first chord)";
        }
    }
    
    // Merge and fill gaps
    mergeRegions(regions);
    fillGaps(regions, totalBars, fallbackPc, fallbackMode);
    
    // === FINAL OUTPUT ===
    for (const auto& r : regions) {
        qDebug().noquote() << QString("KeyAnalyzer: Bars %1-%2 = %3 %4 (confidence: %.2f, evidence: %5)")
            .arg(r.startBar + 1)
            .arg(r.endBar + 1)
            .arg(HarmonyContext::pcName(r.tonicPc))
            .arg(r.mode == virtuoso::theory::KeyMode::Major ? "Major" : "Minor")
            .arg(r.confidence)
            .arg(r.evidence);
    }
    
    return regions;
}

KeyRegion KeyAnalyzer::keyAtBar(const QVector<KeyRegion>& regions, int barIndex) {
    if (regions.isEmpty()) {
        KeyRegion fallback;
        fallback.tonicPc = 0;
        fallback.mode = virtuoso::theory::KeyMode::Major;
        fallback.scaleKey = "ionian";
        fallback.scaleName = "Ionian";
        return fallback;
    }
    
    // Binary search for the region containing this bar
    int lo = 0;
    int hi = regions.size() - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (regions[mid].startBar <= barIndex) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    
    const auto& r = regions[lo];
    if (barIndex >= r.startBar && barIndex <= r.endBar) {
        return r;
    }
    
    return regions.first();
}

} // namespace playback
