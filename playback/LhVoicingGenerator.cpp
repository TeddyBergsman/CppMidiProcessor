#include "LhVoicingGenerator.h"
#include "VoicingUtils.h"
#include <algorithm>
#include <QtGlobal>

namespace playback {

using namespace voicing_utils;

LhVoicingGenerator::LhVoicingGenerator(const virtuoso::ontology::OntologyRegistry* ont)
    : m_ont(ont)
{}

// =============================================================================
// ROOTLESS VOICING (Bill Evans Type A / Type B)
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::generateRootless(const Context& c) const {
    LhVoicing lh;
    const auto& chord = c.chord;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return lh;
    
    // Get pitch classes: 3rd, 5th, 7th (no root - bass plays that)
    const int root = chord.rootPc;
    const int third = pcForDegree(chord, 3);
    const int fifth = pcForDegree(chord, 5);
    const int seventh = pcForDegree(chord, 7);
    const int sixth = pcForDegree(chord, 6);
    
    const bool is6thChord = (chord.extension == 6 && chord.seventh == music::SeventhQuality::None);
    const bool hasSeventh = (seventh >= 0);
    
    // Check for clusters (adjacent notes 1-2 semitones apart)
    auto tooClose = [](int pc1, int pc2) -> bool {
        if (pc1 < 0 || pc2 < 0) return false;
        int interval = qAbs(pc1 - pc2);
        if (interval > 6) interval = 12 - interval;
        return (interval <= 2);
    };
    
    const bool fifthSeventhCluster = tooClose(fifth, seventh);
    const bool thirdFifthCluster = tooClose(third, fifth);
    const bool fifthSixthCluster = tooClose(fifth, sixth);
    
    // Collect pitch classes avoiding clusters
    QVector<int> targetPcs;
    
    // 3rd is always included
    if (third >= 0) targetPcs.push_back(third);
    
    // 5th: include only if it doesn't create clusters
    if (fifth >= 0) {
        bool includeFifth = true;
        if (fifthSeventhCluster) includeFifth = false;
        if (thirdFifthCluster) includeFifth = false;
        if (is6thChord && fifthSixthCluster) includeFifth = false;
        
        if (includeFifth) {
            targetPcs.push_back(fifth);
        }
    }
    
    // 7th or 6th
    if (is6thChord && sixth >= 0) {
        targetPcs.push_back(sixth);
    } else if (hasSeventh) {
        targetPcs.push_back(seventh);
    }
    
    // Fallback for < 2 notes
    if (targetPcs.size() < 2) {
        targetPcs.clear();
        if (third >= 0) targetPcs.push_back(third);
        if (hasSeventh) {
            targetPcs.push_back(seventh);
        } else if (fifth >= 0) {
            targetPcs.push_back(fifth);
        }
    }
    
    if (targetPcs.isEmpty()) return lh;
    
    // Determine starting register (voice-lead from previous)
    int startMidi = 52; // E3
    
    if (!m_state.lastLhMidi.isEmpty()) {
        int lastCenter = 0;
        for (int m : m_state.lastLhMidi) lastCenter += m;
        lastCenter /= m_state.lastLhMidi.size();
        startMidi = qBound(50, lastCenter, 60);
    }
    
    // Build voicing by stacking notes upward
    int firstPc = targetPcs[0];
    
    int bestFirst = -1;
    int bestFirstDist = 999;
    for (int m = 48; m <= 64; ++m) {
        if (normalizePc(m) == firstPc) {
            int dist = qAbs(m - startMidi);
            if (dist < bestFirstDist) {
                bestFirstDist = dist;
                bestFirst = m;
            }
        }
    }
    
    if (bestFirst < 0) return lh;
    
    lh.midiNotes.push_back(bestFirst);
    int cursor = bestFirst;
    
    // Stack remaining notes above
    for (int i = 1; i < targetPcs.size(); ++i) {
        int pc = targetPcs[i];
        int nextMidi = cursor + 1;
        while (normalizePc(nextMidi) != pc && nextMidi < cursor + 12) {
            nextMidi++;
        }
        
        if (nextMidi >= cursor + 12) {
            nextMidi = cursor + 1;
            while (normalizePc(nextMidi) != pc) {
                nextMidi++;
            }
        }
        
        if (nextMidi > 67) nextMidi -= 12;
        if (nextMidi < 48) nextMidi += 12;
        
        lh.midiNotes.push_back(nextMidi);
        cursor = nextMidi;
    }
    
    std::sort(lh.midiNotes.begin(), lh.midiNotes.end());
    
    // Validate span
    if (lh.midiNotes.size() >= 2) {
        int span = lh.midiNotes.last() - lh.midiNotes.first();
        if (span > 12) {
            lh.midiNotes.last() -= 12;
            std::sort(lh.midiNotes.begin(), lh.midiNotes.end());
        }
        
        for (int& m : lh.midiNotes) {
            while (m < 48) m += 12;
            while (m > 67) m -= 12;
        }
        std::sort(lh.midiNotes.begin(), lh.midiNotes.end());
    }
    
    // Check for clusters
    bool hasCluster = false;
    for (int i = 0; i < lh.midiNotes.size() - 1; ++i) {
        if (lh.midiNotes[i + 1] - lh.midiNotes[i] <= 1) {
            hasCluster = true;
            break;
        }
    }
    
    if (hasCluster) {
        lh.midiNotes.clear();
        if (third >= 0) {
            int thirdMidi = 52;
            while (normalizePc(thirdMidi) != third) thirdMidi++;
            lh.midiNotes.push_back(thirdMidi);
        }
        if (seventh >= 0 || (is6thChord && sixth >= 0)) {
            int topPc = is6thChord ? sixth : seventh;
            int topMidi = lh.midiNotes.isEmpty() ? 52 : lh.midiNotes.last() + 3;
            while (normalizePc(topMidi) != topPc && topMidi < 67) topMidi++;
            if (topMidi <= 67) lh.midiNotes.push_back(topMidi);
        }
        std::sort(lh.midiNotes.begin(), lh.midiNotes.end());
    }
    
    // Set ontology key
    if (lh.midiNotes.size() >= 3) {
        lh.ontologyKey = "piano_lh_voicing";
    } else if (lh.midiNotes.size() == 2) {
        lh.ontologyKey = "piano_lh_shell";
    } else {
        lh.ontologyKey = "piano_lh_single";
    }
    
    lh.isTypeA = (chord.rootPc <= 5);
    lh.cost = voiceLeadingCost(m_state.lastLhMidi, lh.midiNotes);
    
    return lh;
}

// =============================================================================
// QUARTAL VOICING (McCoy Tyner style)
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::generateQuartal(const Context& c) const {
    LhVoicing lh;
    const auto& chord = c.chord;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return lh;
    
    const int root = chord.rootPc;
    const int fifth = pcForDegree(chord, 5);
    
    // Start from the 5th of the chord
    int startPc = (fifth >= 0) ? fifth : root;
    
    int startMidi = 50;
    while (startMidi % 12 != startPc && startMidi < 55) startMidi++;
    if (startMidi > 55) startMidi -= 12;
    
    // Stack 4ths (5 semitones each)
    lh.midiNotes.push_back(startMidi);
    lh.midiNotes.push_back(startMidi + 5);
    
    // Add third 4th if it fits
    int thirdNote = startMidi + 10;
    if (thirdNote <= 65) {
        lh.midiNotes.push_back(thirdNote);
    }
    
    lh.ontologyKey = "piano_lh_quartal";
    lh.isTypeA = true;
    lh.cost = voiceLeadingCost(m_state.lastLhMidi, lh.midiNotes);
    
    return lh;
}

// =============================================================================
// SHELL VOICING (just 3-7 guide tones)
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::generateShell(const Context& c) const {
    LhVoicing lh;
    const auto& chord = c.chord;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return lh;
    
    const int third = pcForDegree(chord, 3);
    const int seventh = pcForDegree(chord, 7);
    
    if (third < 0) return lh;
    
    // Find the 3rd near MIDI 52
    int thirdMidi = 52;
    while (normalizePc(thirdMidi) != third && thirdMidi < 60) thirdMidi++;
    if (thirdMidi > 60) thirdMidi -= 12;
    
    lh.midiNotes.push_back(thirdMidi);
    
    // Add 7th above
    if (seventh >= 0) {
        int seventhMidi = thirdMidi + 1;
        while (normalizePc(seventhMidi) != seventh && seventhMidi < thirdMidi + 12) {
            seventhMidi++;
        }
        if (seventhMidi <= 67) {
            lh.midiNotes.push_back(seventhMidi);
        }
    }
    
    lh.ontologyKey = "piano_lh_shell";
    lh.isTypeA = true;
    lh.cost = voiceLeadingCost(m_state.lastLhMidi, lh.midiNotes);
    
    return lh;
}

// =============================================================================
// INNER VOICE MOVEMENT
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::applyInnerVoiceMovement(
    const LhVoicing& base, const Context& c, int direction) const {
    
    if (c.chordIsNew) return base;
    if (c.beatInBar != 2) return base; // Only move on beat 3
    
    LhVoicing moved = base;
    if (moved.midiNotes.size() < 2) return moved;
    
    // Choose inner voice (not top or bottom)
    int moveIndex = (moved.midiNotes.size() >= 3) ? 1 : 0;
    int originalNote = moved.midiNotes[moveIndex];
    
    // Determine direction
    int dir = (direction != 0) ? direction : ((m_state.lastInnerVoiceIndex % 2 == 0) ? 1 : -1);
    
    // Target a color tone if available
    int targetPc = -1;
    int ninth = pcForDegree(c.chord, 9);
    int thirteenth = pcForDegree(c.chord, 13);
    
    // Use energy for tension (more color tones at higher energy)
    if (c.energy > 0.4 && ninth >= 0) {
        targetPc = ninth;
    } else if (c.energy > 0.6 && thirteenth >= 0) {
        targetPc = thirteenth;
    }
    
    // Apply movement
    int delta = (dir > 0) ? 1 : -1;
    if (targetPc >= 0) {
        int targetMidi = originalNote;
        while (targetMidi % 12 != targetPc && qAbs(targetMidi - originalNote) < 4) {
            targetMidi += delta;
        }
        if (qAbs(targetMidi - originalNote) <= 3 && targetMidi >= 48 && targetMidi <= 67) {
            moved.midiNotes[moveIndex] = targetMidi;
        }
    } else {
        int newNote = originalNote + delta;
        if (newNote >= 48 && newNote <= 67) {
            bool safe = true;
            for (int i = 0; i < moved.midiNotes.size(); ++i) {
                if (i != moveIndex && qAbs(moved.midiNotes[i] - newNote) <= 1) {
                    safe = false;
                    break;
                }
            }
            if (safe) {
                moved.midiNotes[moveIndex] = newNote;
            }
        }
    }
    
    std::sort(moved.midiNotes.begin(), moved.midiNotes.end());
    moved.ontologyKey = "piano_lh_inner_move";
    return moved;
}

// =============================================================================
// SHOULD PLAY BEAT
// =============================================================================

bool LhVoicingGenerator::shouldPlayBeat(const Context& c, quint32 hash) const {
    // Chord changes: always play
    if (c.chordIsNew) return true;
    
    const double e = c.energy;
    
    // ========================================================================
    // HIGH ENERGY MODE: LH drives the rhythm on almost every beat!
    // ========================================================================
    if (e >= 0.6) {
        // Beat 1: anchor (90-98%)
        if (c.beatInBar == 0) {
            double prob = 0.90 + 0.08 * e;
            return (hash % 100) < int(prob * 100);
        }
        // Beat 3: backbeat (85-95%)
        if (c.beatInBar == 2) {
            double prob = 0.85 + 0.10 * e;
            return (hash % 100) < int(prob * 100);
        }
        // Beat 2: push (70-85%)
        if (c.beatInBar == 1) {
            double prob = 0.70 + 0.15 * e;
            return (hash % 100) < int(prob * 100);
        }
        // Beat 4: pickup (75-88%)
        if (c.beatInBar == 3) {
            double prob = 0.75 + 0.13 * e;
            return (hash % 100) < int(prob * 100);
        }
    }
    
    // ========================================================================
    // LOWER ENERGY: Sparser, more traditional jazz comping
    // ========================================================================
    // Beat 1: strong probability to reinforce (65-85%)
    if (c.beatInBar == 0) {
        double prob = 0.65 + 0.20 * e;
        return (hash % 100) < int(prob * 100);
    }
    
    // Beat 3: secondary strong beat (40-70%)
    if (c.beatInBar == 2) {
        double prob = 0.40 + 0.30 * e;
        return (hash % 100) < int(prob * 100);
    }
    
    // Beat 2: syncopation opportunity (15-45%)
    if (c.beatInBar == 1) {
        double prob = 0.15 + 0.30 * e;
        return (hash % 100) < int(prob * 100);
    }
    
    // Beat 4: pickup (10-35%)
    if (c.beatInBar == 3) {
        double prob = 0.10 + 0.25 * e;
        return (hash % 100) < int(prob * 100);
    }
    
    return false;
}

// =============================================================================
// GENERATE BEST
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::generateBest(const Context& c) const {
    // Choose voicing type based on context (quartal voicings at higher energy)
    const double quartalChance = c.energy * 0.25;  // More quartal at higher energy
    const bool useQuartal = (c.energy > 0.5) && ((c.chord.rootPc * 7 + c.beatInBar) % 100 < int(quartalChance * 100));
    
    // Shell for very sparse moments
    if (c.preferShells && c.energy < 0.3) {
        return generateShell(c);
    }
    
    // Quartal for modern sound
    if (useQuartal) {
        return generateQuartal(c);
    }
    
    // Default: rootless
    return generateRootless(c);
}

// =============================================================================
// VOICE LEADING
// =============================================================================

double LhVoicingGenerator::voiceLeadingCost(const QVector<int>& prev, const QVector<int>& next) const {
    return voicing_utils::voiceLeadingCost(prev, next);
}

QVector<int> LhVoicingGenerator::realizePcsToMidi(const QVector<int>& pcs, int lo, int hi,
                                                   const QVector<int>& prevVoicing) const {
    return voicing_utils::realizePcsToMidi(pcs, lo, hi, prevVoicing, -1);
}

// =============================================================================
// STATIC HELPERS (delegate to voicing_utils)
// =============================================================================

int LhVoicingGenerator::pcForDegree(const music::ChordSymbol& c, int degree) {
    return voicing_utils::pcForDegree(c, degree);
}

int LhVoicingGenerator::thirdInterval(music::ChordQuality q) {
    return voicing_utils::thirdInterval(q);
}

int LhVoicingGenerator::fifthInterval(music::ChordQuality q) {
    return voicing_utils::fifthInterval(q);
}

int LhVoicingGenerator::seventhInterval(const music::ChordSymbol& c) {
    return voicing_utils::seventhInterval(c);
}

int LhVoicingGenerator::nearestMidiForPc(int pc, int around, int lo, int hi) {
    return voicing_utils::nearestMidiForPc(pc, around, lo, hi);
}

QVector<int> LhVoicingGenerator::realizeVoicingTemplate(const QVector<int>& degrees,
                                                         const music::ChordSymbol& chord,
                                                         int bassMidi, int ceiling) const {
    return voicing_utils::realizeVoicingTemplate(degrees, chord, bassMidi, ceiling);
}

} // namespace playback
