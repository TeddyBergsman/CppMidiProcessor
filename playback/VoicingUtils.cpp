#include "VoicingUtils.h"
#include <algorithm>

namespace playback {
namespace voicing_utils {

int thirdInterval(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::Minor:
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished:
            return 3;
        case music::ChordQuality::Sus2:
            return 2;
        case music::ChordQuality::Sus4:
            return 5;
        default:
            return 4;
    }
}

int fifthInterval(music::ChordQuality q) {
    switch (q) {
        case music::ChordQuality::HalfDiminished:
        case music::ChordQuality::Diminished:
            return 6;
        case music::ChordQuality::Augmented:
            return 8;
        default:
            return 7;
    }
}

int seventhInterval(const music::ChordSymbol& c) {
    if (c.seventh == music::SeventhQuality::Major7) return 11;
    if (c.seventh == music::SeventhQuality::Dim7) return 9;
    if (c.seventh == music::SeventhQuality::Minor7) return 10;
    if (c.extension >= 7) return 10;
    // 6th chords: the 6th replaces the 7th as the color tone
    if (c.extension == 6) return 9;  // Major 6th interval
    return -1;
}

// Returns true if this chord uses a 6th instead of a 7th
bool is6thChord(const music::ChordSymbol& c) {
    return (c.extension == 6 && c.seventh == music::SeventhQuality::None);
}

int pcForDegree(const music::ChordSymbol& c, int degree) {
    const int root = (c.rootPc >= 0) ? c.rootPc : 0;

    auto applyAlter = [&](int deg, int basePc) -> int {
        for (const auto& a : c.alterations) {
            if (a.degree == deg) {
                return normalizePc(basePc + a.delta);
            }
        }
        return normalizePc(basePc);
    };
    
    auto hasAlteration = [&](int deg) -> bool {
        for (const auto& a : c.alterations) {
            if (a.degree == deg) return true;
        }
        return false;
    };

    const bool isAlt = c.alt && (c.quality == music::ChordQuality::Dominant);
    const bool is6thChord = (c.extension == 6 && c.seventh == music::SeventhQuality::None);
    const bool isMajor = (c.quality == music::ChordQuality::Major);
    const bool isDominant = (c.quality == music::ChordQuality::Dominant);
    const bool isMinor = (c.quality == music::ChordQuality::Minor);
    const bool isHalfDim = (c.quality == music::ChordQuality::HalfDiminished);
    const bool isDim = (c.quality == music::ChordQuality::Diminished);

    int pc = root;
    switch (degree) {
        case 1:
            pc = root;
            break;
        case 3:
            pc = normalizePc(root + thirdInterval(c.quality));
            break;
        case 5:
            if (isAlt) {
                pc = hasAlteration(5) ? applyAlter(5, normalizePc(root + 7)) : normalizePc(root + 6);
            } else {
                pc = applyAlter(5, normalizePc(root + fifthInterval(c.quality)));
            }
            break;
        case 6:
            if (is6thChord || hasAlteration(6)) {
                pc = applyAlter(6, normalizePc(root + 9));
            } else {
                return -1;
            }
            break;
        case 7:
            if (is6thChord) {
                pc = normalizePc(root + 9);
            } else {
                const int iv = seventhInterval(c);
                if (iv < 0) return -1;
                pc = normalizePc(root + iv);
            }
            break;
        case 9:
            if (is6thChord) {
                return -1;  // 9th clashes with 6th
            } else if (isAlt) {
                pc = normalizePc(root + 1); // b9
            } else if (c.extension >= 9 || hasAlteration(9)) {
                pc = applyAlter(9, normalizePc(root + 2));
            } else if (isDominant) {
                pc = normalizePc(root + 2);  // Natural 9 on dominant
            } else if (isMajor && c.seventh == music::SeventhQuality::Major7) {
                // Major 7th chords: 9th is safe (e.g., F over EbMaj7)
                pc = normalizePc(root + 2);
            } else if (isMinor && c.seventh != music::SeventhQuality::None) {
                pc = normalizePc(root + 2);  // Natural 9 on minor 7th
            } else if (isHalfDim) {
                // Half-diminished: major 9th is safe (e.g., E over Dm7b5)
                pc = normalizePc(root + 2);
            } else if (isDim) {
                // Diminished: avoid natural 9, return -1
                return -1;
            } else {
                return -1;
            }
            break;
        case 11:
            if (isMajor) {
                if (c.extension >= 11 || hasAlteration(11)) {
                    pc = applyAlter(11, normalizePc(root + 6)); // #11
                } else {
                    return -1;
                }
            } else if (isDominant) {
                if (isAlt || c.extension >= 11 || hasAlteration(11)) {
                    pc = applyAlter(11, normalizePc(root + 6)); // #11
                } else {
                    return -1;
                }
            } else if (isMinor) {
                pc = applyAlter(11, normalizePc(root + 5)); // natural 11
            } else if (isHalfDim) {
                // Half-diminished: 11 = perfect 4th = same as natural 11
                // Note: this is the same pitch as the b5 enharmonically in some voicings
                // Generally safe but not commonly used
                if (c.extension >= 11 || hasAlteration(11)) {
                    pc = applyAlter(11, normalizePc(root + 5));
                } else {
                    return -1;  // Don't add 11 unless explicit
                }
            } else if (isDim) {
                // Diminished: avoid 11
                return -1;
            } else {
                pc = applyAlter(11, normalizePc(root + 5));
            }
            break;
        case 13:
            if (isAlt) {
                pc = normalizePc(root + 8); // b13
            } else if (c.extension >= 13 || hasAlteration(13)) {
                pc = applyAlter(13, normalizePc(root + 9));
            } else if (isDominant) {
                pc = normalizePc(root + 9);  // Natural 13 on dominant
            } else if (isMajor && c.seventh == music::SeventhQuality::Major7) {
                // Major 7th chords: 13th is safe (e.g., C over EbMaj7)
                pc = normalizePc(root + 9);
            } else if (isMinor && c.seventh != music::SeventhQuality::None) {
                // Minor 7th chords: 13th is safe (dorian sound)
                pc = normalizePc(root + 9);
            } else if (isHalfDim || isDim) {
                // Half-dim/Dim: natural 13 clashes with minor 7th (half step)
                // b13 also problematic - avoid entirely
                return -1;
            } else {
                return -1;
            }
            break;
        default:
            pc = root;
            break;
    }
    return normalizePc(pc);
}

int nearestMidiForPc(int pc, int around, int lo, int hi) {
    pc = normalizePc(pc);
    around = clampMidi(around);

    int best = -1;
    int bestDist = 9999;

    for (int m = lo; m <= hi; ++m) {
        if (normalizePc(m) != pc) continue;
        const int d = qAbs(m - around);
        if (d < bestDist) {
            bestDist = d;
            best = m;
        }
    }

    if (best >= 0) return best;

    int m = lo + ((pc - normalizePc(lo) + 12) % 12);
    while (m < lo) m += 12;
    while (m > hi) m -= 12;
    return clampMidi(m);
}

int getDegreeForPc(int pc, const music::ChordSymbol& chord) {
    pc = normalizePc(pc);
    
    for (int deg : {1, 3, 5, 7, 9, 11, 13}) {
        if (pcForDegree(chord, deg) == pc) {
            return deg;
        }
    }
    return 0;
}

double voiceLeadingCost(const QVector<int>& prev, const QVector<int>& next) {
    if (prev.isEmpty()) return 0.0;
    if (next.isEmpty()) return 0.0;

    double cost = 0.0;
    int totalMotion = 0;
    int commonTones = 0;

    QVector<bool> prevUsed(prev.size(), false);
    QVector<bool> nextUsed(next.size(), false);

    // First pass: find common tones
    for (int i = 0; i < next.size(); ++i) {
        int nextPc = normalizePc(next[i]);
        for (int j = 0; j < prev.size(); ++j) {
            if (prevUsed[j]) continue;
            if (normalizePc(prev[j]) == nextPc) {
                totalMotion += qAbs(next[i] - prev[j]);
                prevUsed[j] = true;
                nextUsed[i] = true;
                commonTones++;
                break;
            }
        }
    }

    // Second pass: match remaining by nearest neighbor
    for (int i = 0; i < next.size(); ++i) {
        if (nextUsed[i]) continue;
        
        int bestJ = -1;
        int bestDist = 999;
        for (int j = 0; j < prev.size(); ++j) {
            if (prevUsed[j]) continue;
            int dist = qAbs(next[i] - prev[j]);
            if (dist < bestDist) {
                bestDist = dist;
                bestJ = j;
            }
        }
        
        if (bestJ >= 0) {
            totalMotion += bestDist;
            prevUsed[bestJ] = true;
        } else {
            totalMotion += 6; // Penalty for unmatched notes
        }
    }

    cost = totalMotion * 0.5;
    cost -= commonTones * 1.0; // Reward common tones
    
    return qMax(0.0, cost);
}

QVector<int> realizePcsToMidi(const QVector<int>& pcs, int lo, int hi,
                              const QVector<int>& prevVoicing, int /*targetTopMidi*/) {
    if (pcs.isEmpty()) return {};

    QVector<int> midi;
    midi.reserve(pcs.size());

    int prevCenter = (lo + hi) / 2;
    if (!prevVoicing.isEmpty()) {
        int sum = 0;
        for (int m : prevVoicing) sum += m;
        prevCenter = sum / prevVoicing.size();
    }

    for (int pc : pcs) {
        int m = nearestMidiForPc(pc, prevCenter, lo, hi);
        midi.push_back(m);
    }

    std::sort(midi.begin(), midi.end());
    return midi;
}

QVector<int> realizeVoicingTemplate(const QVector<int>& degrees,
                                    const music::ChordSymbol& chord,
                                    int bassMidi, int ceiling) {
    QVector<int> midi;
    midi.reserve(degrees.size());

    QVector<int> pcs;
    for (int deg : degrees) {
        int pc = pcForDegree(chord, deg);
        if (pc < 0) continue;
        pcs.push_back(pc);
    }

    if (pcs.isEmpty()) return midi;

    int cursor = bassMidi;
    
    const int bottomPc = pcs[0];
    int bottomMidi = cursor;
    while (normalizePc(bottomMidi) != bottomPc && bottomMidi <= ceiling) {
        bottomMidi++;
    }
    if (bottomMidi > ceiling) {
        bottomMidi = bassMidi;
        while (normalizePc(bottomMidi) != bottomPc && bottomMidi >= 36) {
            bottomMidi--;
        }
    }
    
    midi.push_back(bottomMidi);
    cursor = bottomMidi;

    for (int i = 1; i < pcs.size(); ++i) {
        int pc = pcs[i];
        int note = cursor + 1;
        while (normalizePc(note) != pc && note <= ceiling + 12) {
            note++;
        }
        
        if (note > ceiling) {
            note = cursor;
            while (normalizePc(note) != pc && note >= 36) {
                note--;
            }
        }
        
        midi.push_back(note);
        cursor = note;
    }

    return midi;
}

int selectMelodicTopNote(const QVector<int>& candidatePcs, int lo, int hi, int lastTopMidi) {
    if (candidatePcs.isEmpty()) return (lo + hi) / 2;
    
    int bestMidi = -1;
    int bestCost = 9999;
    
    for (int pc : candidatePcs) {
        for (int oct = 5; oct <= 7; ++oct) {
            int midi = pc + 12 * oct;
            if (midi < lo || midi > hi) continue;
            
            int cost = qAbs(midi - lastTopMidi);
            if (cost <= 2) cost = 0;  // Stepwise is free
            else if (cost <= 4) cost = 1;
            else cost = cost - 2;
            
            if (cost < bestCost) {
                bestCost = cost;
                bestMidi = midi;
            }
        }
    }
    
    return (bestMidi > 0) ? bestMidi : (lo + hi) / 2;
}

// =============================================================================
// CONSONANCE VALIDATION
// =============================================================================

bool isChordTone(int pc, const music::ChordSymbol& chord) {
    pc = normalizePc(pc);
    
    // Check core chord tones: 1, 3, 5, 7
    for (int deg : {1, 3, 5, 7}) {
        int chordPc = pcForDegree(chord, deg);
        if (chordPc >= 0 && normalizePc(chordPc) == pc) {
            return true;
        }
    }
    
    // Check valid extensions: 9, 11, 13 (if chord includes them)
    for (int deg : {9, 11, 13}) {
        int chordPc = pcForDegree(chord, deg);
        if (chordPc >= 0 && normalizePc(chordPc) == pc) {
            return true;
        }
    }
    
    return false;
}

bool isScaleTone(int pc, const music::ChordSymbol& chord) {
    pc = normalizePc(pc);
    const int root = chord.rootPc;
    if (root < 0) return false;
    
    // If it's already a chord tone, it's definitely a scale tone
    if (isChordTone(pc, chord)) return true;
    
    // Build appropriate scale based on chord quality
    QVector<int> scaleIntervals;
    
    const bool isMajor = (chord.quality == music::ChordQuality::Major);
    const bool isMinor = (chord.quality == music::ChordQuality::Minor);
    const bool isDominant = (chord.quality == music::ChordQuality::Dominant);
    const bool isHalfDim = (chord.quality == music::ChordQuality::HalfDiminished);
    const bool isAlt = chord.alt && isDominant;
    
    if (isMajor) {
        // Major scale (Ionian) or Lydian
        scaleIntervals = {0, 2, 4, 5, 7, 9, 11};  // Ionian
        // Check if #11 is explicitly present -> Lydian
        for (const auto& a : chord.alterations) {
            if (a.degree == 11 && a.delta > 0) {
                scaleIntervals[3] = 6;  // #4/11
            }
        }
    } else if (isMinor) {
        // Dorian (natural 6) by default
        scaleIntervals = {0, 2, 3, 5, 7, 9, 10};
    } else if (isDominant) {
        if (isAlt) {
            // Altered scale: 1 b9 #9 3 b5 b13 b7
            scaleIntervals = {0, 1, 3, 4, 6, 8, 10};
        } else {
            // Mixolydian
            scaleIntervals = {0, 2, 4, 5, 7, 9, 10};
        }
    } else if (isHalfDim) {
        // Locrian natural 2
        scaleIntervals = {0, 2, 3, 5, 6, 8, 10};
    } else {
        // Default to major scale
        scaleIntervals = {0, 2, 4, 5, 7, 9, 11};
    }
    
    // Check if pc is in the scale
    int interval = (pc - root + 12) % 12;
    return scaleIntervals.contains(interval);
}

QVector<int> getChordTonePcs(const music::ChordSymbol& chord) {
    QVector<int> pcs;
    
    for (int deg : {1, 3, 5, 7, 9, 11, 13}) {
        int pc = pcForDegree(chord, deg);
        if (pc >= 0 && !pcs.contains(pc)) {
            pcs.push_back(pc);
        }
    }
    
    return pcs;
}

QVector<int> getScalePcs(const music::ChordSymbol& chord) {
    QVector<int> pcs;
    const int root = chord.rootPc;
    if (root < 0) return pcs;
    
    // Start with chord tones
    pcs = getChordTonePcs(chord);
    
    // Add remaining scale tones
    for (int i = 0; i < 12; ++i) {
        int pc = (root + i) % 12;
        if (!pcs.contains(pc) && isScaleTone(pc, chord)) {
            pcs.push_back(pc);
        }
    }
    
    return pcs;
}

int validateToConsonant(int midi, const music::ChordSymbol& chord, int lo, int hi) {
    const int pc = normalizePc(midi);
    
    // Already consonant? Return as-is
    if (isChordTone(pc, chord)) {
        return qBound(lo, midi, hi);
    }
    
    // Check if it's at least a scale tone
    if (isScaleTone(pc, chord)) {
        return qBound(lo, midi, hi);
    }
    
    // Not consonant - find the nearest chord tone
    QVector<int> chordTones = getChordTonePcs(chord);
    if (chordTones.isEmpty()) {
        return qBound(lo, midi, hi);  // Can't validate, return as-is
    }
    
    // Find nearest chord tone by minimal semitone movement
    int bestMidi = midi;
    int bestDist = 999;
    
    for (int chordPc : chordTones) {
        // Try moving up
        for (int delta = 0; delta <= 3; ++delta) {
            int candidate = midi + delta;
            if (candidate > hi) break;
            if (normalizePc(candidate) == chordPc && delta < bestDist) {
                bestDist = delta;
                bestMidi = candidate;
            }
        }
        // Try moving down
        for (int delta = 1; delta <= 3; ++delta) {
            int candidate = midi - delta;
            if (candidate < lo) break;
            if (normalizePc(candidate) == chordPc && delta < bestDist) {
                bestDist = delta;
                bestMidi = candidate;
            }
        }
    }
    
    return bestMidi;
}

QVector<int> validateVoicing(const QVector<int>& midiNotes, 
                             const music::ChordSymbol& chord,
                             int lo, int hi) {
    QVector<int> validated;
    validated.reserve(midiNotes.size());
    
    for (int midi : midiNotes) {
        validated.push_back(validateToConsonant(midi, chord, lo, hi));
    }
    
    // Remove duplicates and sort
    std::sort(validated.begin(), validated.end());
    validated.erase(std::unique(validated.begin(), validated.end()), validated.end());
    
    return validated;
}

} // namespace voicing_utils
} // namespace playback
