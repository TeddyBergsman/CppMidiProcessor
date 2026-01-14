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
// ROOTLESS FROM SPECIFIC DEGREE (for voice-leading optimization)
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::generateRootlessFromDegree(const Context& c, int startDegree) const {
    LhVoicing lh;
    const auto& chord = c.chord;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return lh;
    
    // Get all available pitch classes
    const int third = pcForDegree(chord, 3);
    const int fifth = pcForDegree(chord, 5);
    const int seventh = pcForDegree(chord, 7);
    const int sixth = pcForDegree(chord, 6);
    
    const bool is6thChord = (chord.extension == 6 && chord.seventh == music::SeventhQuality::None);
    const int topNote = is6thChord ? sixth : seventh;
    
    // Build pitch class order based on startDegree
    QVector<int> targetPcs;
    
    if (startDegree == 7 && topNote >= 0) {
        // Type B style: 7-3-5 (or 6-3-5 for 6th chords)
        targetPcs.push_back(topNote);
        if (third >= 0) targetPcs.push_back(third);
        if (fifth >= 0 && fifth != topNote && fifth != third) targetPcs.push_back(fifth);
        lh.isTypeA = false;
        lh.ontologyKey = "piano_rootless_b";
    } else {
        // Type A style: 3-5-7 (or 3-5-6 for 6th chords)
        if (third >= 0) targetPcs.push_back(third);
        if (fifth >= 0 && fifth != third) targetPcs.push_back(fifth);
        if (topNote >= 0 && topNote != fifth) targetPcs.push_back(topNote);
        lh.isTypeA = true;
        lh.ontologyKey = "piano_rootless_a";
    }
    
    if (targetPcs.size() < 2) {
        // Fallback to shell (3-7)
        targetPcs.clear();
        if (third >= 0) targetPcs.push_back(third);
        if (topNote >= 0) targetPcs.push_back(topNote);
        lh.ontologyKey = "piano_guide_3_7";
    }
    
    if (targetPcs.isEmpty()) return lh;
    
    // Realize to MIDI with voice-leading
    lh.midiNotes = realizePcsToMidi(targetPcs, c.lhLo, c.lhHi, m_state.lastLhMidi);
    lh.cost = voiceLeadingCost(m_state.lastLhMidi, lh.midiNotes);
    
    return lh;
}

// =============================================================================
// OPTIMAL ROOTLESS (Bill Evans voice-leading)
// Generates both Type A and Type B, picks the one with lower voice-leading cost
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::generateRootlessOptimal(const Context& c) const {
    // Generate both voicing types
    LhVoicing typeA = generateRootlessFromDegree(c, 3);  // Start from 3rd
    LhVoicing typeB = generateRootlessFromDegree(c, 7);  // Start from 7th
    
    // If either is empty, return the other
    if (typeA.midiNotes.isEmpty()) return typeB;
    if (typeB.midiNotes.isEmpty()) return typeA;
    
    // Pick the one with lower voice-leading cost
    // This is the essence of Bill Evans' smooth voice-leading
    if (typeB.cost < typeA.cost) {
        return typeB;
    }
    return typeA;
}

// =============================================================================
// ROOTLESS WITH TOP TARGET (Soprano Line Control)
// Generate voicing that aims for a specific top note while maintaining harmony
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::generateRootlessWithTopTarget(
    const Context& c, int targetTopMidi) const {
    
    LhVoicing lh;
    const auto& chord = c.chord;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return lh;
    
    // Get available pitch classes for voicing
    const int third = pcForDegree(chord, 3);
    const int fifth = pcForDegree(chord, 5);
    const int seventh = pcForDegree(chord, 7);
    const int sixth = pcForDegree(chord, 6);
    
    const bool is6thChord = (chord.extension == 6 && chord.seventh == music::SeventhQuality::None);
    const int topNote = is6thChord ? sixth : seventh;
    
    // Collect chord tones we can use
    QVector<int> availablePcs;
    if (third >= 0) availablePcs.push_back(third);
    if (fifth >= 0) availablePcs.push_back(fifth);
    if (topNote >= 0) availablePcs.push_back(topNote);
    
    if (availablePcs.size() < 2) return lh;
    
    // If we have a target top, find the chord tone closest to it
    int targetPc = -1;
    if (targetTopMidi > 0) {
        int targetNotePc = targetTopMidi % 12;
        int minDist = 999;
        for (int pc : availablePcs) {
            int dist = qAbs(pc - targetNotePc);
            if (dist > 6) dist = 12 - dist;
            if (dist < minDist) {
                minDist = dist;
                targetPc = pc;
            }
        }
    }
    
    // Build voicing with target on top
    QVector<int> orderedPcs;
    for (int pc : availablePcs) {
        if (pc != targetPc) {
            orderedPcs.push_back(pc);
        }
    }
    if (targetPc >= 0) {
        orderedPcs.push_back(targetPc);  // Target goes on top
    }
    
    // Realize to MIDI, respecting register bounds
    // Use the register center as the target, not previous voicing
    int regCenter = (c.lhLo + c.lhHi) / 2;
    
    lh.midiNotes = voicing_utils::realizePcsToMidi(orderedPcs, c.lhLo, c.lhHi, {});
    
    // Adjust to get target on top if specified
    if (targetTopMidi > 0 && !lh.midiNotes.isEmpty()) {
        int currentTop = lh.midiNotes.last();
        int currentTopPc = currentTop % 12;
        int targetPcMod = targetTopMidi % 12;
        
        // If the top note matches the target pitch class, adjust octave if needed
        if (currentTopPc == targetPcMod) {
            int octaveDiff = (targetTopMidi / 12) - (currentTop / 12);
            if (octaveDiff != 0) {
                int shift = octaveDiff * 12;
                // Check if shift keeps voicing in range
                int newLow = lh.midiNotes.first() + shift;
                int newHigh = lh.midiNotes.last() + shift;
                if (newLow >= c.lhLo && newHigh <= c.lhHi) {
                    for (int& m : lh.midiNotes) m += shift;
                }
            }
        }
    }
    
    lh.ontologyKey = "piano_rootless_soprano";
    lh.isTypeA = true;
    lh.cost = voiceLeadingCost(m_state.lastLhMidi, lh.midiNotes);
    
    return lh;
}

// =============================================================================
// QUARTAL VOICING (McCoy Tyner style)
// FIXED: Now voice-leads from previous voicing, respects register bounds
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::generateQuartal(const Context& c) const {
    LhVoicing lh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return lh;

    const int root = chord.rootPc;
    const int fifth = pcForDegree(chord, 5);
    // Note: 11th (natural 4th) is implicit in quartal stacking via pure 4ths

    // For quartal voicing, we can start from:
    // - The 5th (classic "So What" voicing)
    // - The 11th/4th if it's a sus4 or modal chord
    // - The root for a more grounded sound
    int startPc = (fifth >= 0) ? fifth : root;

    // Voice-lead: find starting point closest to previous voicing center
    int targetCenter = (c.lhLo + c.lhHi) / 2;
    if (!m_state.lastLhMidi.isEmpty()) {
        int prevCenter = 0;
        for (int m : m_state.lastLhMidi) prevCenter += m;
        prevCenter /= m_state.lastLhMidi.size();
        // Quartal voicings work well slightly lower in register
        targetCenter = qBound(c.lhLo + 2, prevCenter - 2, c.lhHi - 10);
    }

    // Find starting MIDI note
    int startMidi = nearestMidiForPc(startPc, targetCenter, c.lhLo, c.lhHi - 10);
    if (startMidi < 0) {
        // Fallback to register bottom
        startMidi = c.lhLo;
        while (normalizePc(startMidi) != startPc && startMidi < c.lhLo + 12) {
            startMidi++;
        }
    }

    // Stack 4ths (5 semitones each) - classic quartal sound
    lh.midiNotes.push_back(startMidi);

    int secondNote = startMidi + 5;
    if (secondNote <= c.lhHi) {
        lh.midiNotes.push_back(secondNote);

        // Add third 4th if it fits in register
        int thirdNote = secondNote + 5;
        if (thirdNote <= c.lhHi) {
            lh.midiNotes.push_back(thirdNote);
        }
    }

    lh.ontologyKey = "piano_lh_quartal";
    lh.isTypeA = true;
    lh.cost = voiceLeadingCost(m_state.lastLhMidi, lh.midiNotes);

    return lh;
}

// =============================================================================
// SHELL VOICING (just 3-7 guide tones)
// FIXED: Now voice-leads from previous voicing instead of fixed register
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::generateShell(const Context& c) const {
    LhVoicing lh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return lh;

    const int third = pcForDegree(chord, 3);
    const int seventh = pcForDegree(chord, 7);
    const int sixth = pcForDegree(chord, 6);

    // For 6th chords, use 6th instead of 7th
    const bool is6thChord = (chord.extension == 6 && chord.seventh == music::SeventhQuality::None);
    const int topPc = is6thChord ? sixth : seventh;

    if (third < 0) return lh;

    // Determine starting register from previous voicing (voice-leading)
    int targetCenter = (c.lhLo + c.lhHi) / 2;  // Default to register center
    if (!m_state.lastLhMidi.isEmpty()) {
        // Voice-lead: use center of previous voicing
        int prevCenter = 0;
        for (int m : m_state.lastLhMidi) prevCenter += m;
        prevCenter /= m_state.lastLhMidi.size();
        targetCenter = qBound(c.lhLo + 3, prevCenter, c.lhHi - 3);
    }

    // Find the 3rd closest to target center
    int thirdMidi = nearestMidiForPc(third, targetCenter, c.lhLo, c.lhHi);
    if (thirdMidi < 0) return lh;

    lh.midiNotes.push_back(thirdMidi);

    // Add 7th (or 6th) - prefer above the 3rd for standard shell
    if (topPc >= 0) {
        // Try to find top note above the third
        int topMidi = thirdMidi + 1;
        while (normalizePc(topMidi) != topPc && topMidi < thirdMidi + 12) {
            topMidi++;
        }

        // If above register, try below
        if (topMidi > c.lhHi) {
            topMidi = thirdMidi - 1;
            while (normalizePc(topMidi) != topPc && topMidi > thirdMidi - 12) {
                topMidi--;
            }
        }

        // Only add if in register
        if (topMidi >= c.lhLo && topMidi <= c.lhHi) {
            lh.midiNotes.push_back(topMidi);
        }
    }

    std::sort(lh.midiNotes.begin(), lh.midiNotes.end());

    lh.ontologyKey = "piano_lh_shell";
    lh.isTypeA = true;
    lh.cost = voiceLeadingCost(m_state.lastLhMidi, lh.midiNotes);

    return lh;
}

// =============================================================================
// BASS ANCHOR (Single low note for structural emphasis)
// Used at phrase boundaries, climaxes, and when the bass is not playing
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::generateBassAnchor(const Context& c) const {
    LhVoicing lh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return lh;

    // Choose anchor note based on context
    // Default: root of chord (when bass is not playing, piano can provide foundation)
    // Alternative: 5th for a more open sound, or 3rd for color
    int anchorPc = chord.rootPc;

    // If there's a slash bass, use that instead
    if (chord.bassPc >= 0) {
        anchorPc = chord.bassPc;
    }

    // Find the anchor in the low register (below typical LH voicing)
    // Target around C3 (MIDI 48) for bass anchor
    int targetMidi = 48;
    int anchorMidi = nearestMidiForPc(anchorPc, targetMidi, 36, 55);

    if (anchorMidi < 0) {
        // Fallback
        anchorMidi = 48;
        while (normalizePc(anchorMidi) != anchorPc && anchorMidi > 36) {
            anchorMidi--;
        }
    }

    lh.midiNotes.push_back(anchorMidi);
    lh.ontologyKey = "piano_lh_anchor";
    lh.isTypeA = true;
    lh.cost = 0.0;  // Bass anchor doesn't really voice-lead

    return lh;
}

// =============================================================================
// BLOCK LOWER (Lower portion of block chord, coordinated with RH)
// Used for climax moments when both hands play together (George Shearing style)
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::generateBlockLower(const Context& c, int targetTopMidi) const {
    LhVoicing lh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return lh;

    // Block chord: LH takes the lower portion (root + middle voices)
    // RH takes upper portion (will be coordinated by orchestrator)

    // Get chord tones
    const int root = chord.rootPc;
    const int third = pcForDegree(chord, 3);
    const int fifth = pcForDegree(chord, 5);
    const int seventh = pcForDegree(chord, 7);

    // For block chords, we DO include the root (unlike rootless voicings)
    QVector<int> targetPcs;
    targetPcs.push_back(root);
    if (third >= 0) targetPcs.push_back(third);
    if (fifth >= 0) targetPcs.push_back(fifth);

    // Realize in LH register, aiming for close position
    // Block chords are typically in close position for that "locked hands" sound
    int startMidi = qBound(c.lhLo, 48, c.lhHi - 8);

    // Find root near start
    int rootMidi = nearestMidiForPc(root, startMidi, c.lhLo, c.lhHi);
    if (rootMidi < 0) return lh;

    lh.midiNotes.push_back(rootMidi);

    // Stack remaining notes closely above root
    int cursor = rootMidi;
    for (int i = 1; i < targetPcs.size(); ++i) {
        int pc = targetPcs[i];
        // Find next note within octave above cursor
        int nextMidi = cursor + 1;
        while (normalizePc(nextMidi) != pc && nextMidi < cursor + 12) {
            nextMidi++;
        }
        // Keep in register
        if (nextMidi <= c.lhHi) {
            lh.midiNotes.push_back(nextMidi);
            cursor = nextMidi;
        }
    }

    std::sort(lh.midiNotes.begin(), lh.midiNotes.end());

    lh.ontologyKey = "piano_lh_block";
    lh.isTypeA = true;
    lh.cost = voiceLeadingCost(m_state.lastLhMidi, lh.midiNotes);

    return lh;
}

// =============================================================================
// INNER VOICE MOVEMENT
// FIXED: Now triggers more frequently (beats 2 and 3) and has better targeting
// =============================================================================
// CONTRAPUNTAL INNER VOICE MOVEMENT (Evans' "piled up contrapuntal lines")
// =============================================================================
// Per Chuck Israels: Evans' harmonies were "a piling up of contrapuntal lines"
// where tension/release between voices was "exquisitely shaded."
//
// KEY INSIGHT: This is NOT about mechanical movement on beat 3, but about
// voices with DIRECTION - ascending toward tension, descending toward resolution.
// Movement is triggered by PHRASE CONTEXT, not beat position.
// =============================================================================

LhVoicingGenerator::LhVoicing LhVoicingGenerator::applyInnerVoiceMovement(
    const LhVoicing& base, const Context& c, int direction) const {

    // Skip if chord just changed - let voicing establish first
    if (c.chordIsNew) {
        m_state.beatsOnCurrentTarget = 0;
        return base;
    }

    LhVoicing moved = base;
    if (moved.midiNotes.size() < 2) return moved;

    // =========================================================================
    // PHRASE-DRIVEN DIRECTION (the heart of contrapuntal voice movement)
    // =========================================================================
    // Building tension (phase 0): Inner voices ASCEND toward dissonance
    // Peak (phase 1): Maximum activity, voices can move freely
    // Resolving (phase 2): Inner voices DESCEND toward consonance

    int dir = direction;
    if (dir == 0) {
        // Determine direction from phrase context
        switch (c.phraseArcPhase) {
        case 0:  // Building - ascend toward tension
            dir = 1;
            break;
        case 1:  // Peak - alternate for maximum shimmer
            dir = (m_state.innerVoiceDirection > 0) ? -1 : 1;
            break;
        case 2:  // Resolving - descend toward consonance
            dir = -1;
            break;
        default:
            dir = (m_state.innerVoiceDirection > 0) ? -1 : 1;
        }

        // Cadence approach: always resolve downward
        if (c.cadence01 > 0.6) {
            dir = -1;
        }
    }

    // =========================================================================
    // CHOOSE INNER VOICE TO MOVE
    // =========================================================================
    // For 3+ note voicings: move middle voice (preserves bass and melodic top)
    // For 2-note voicings: move lower voice (preserves melodic top)
    // Cycle through voices over time for variety

    int moveIndex;
    int numNotes = moved.midiNotes.size();
    if (numNotes >= 4) {
        // 4-note voicing: alternate between inner voices (index 1 and 2)
        moveIndex = 1 + (m_state.lastInnerVoiceIndex % 2);
    } else if (numNotes >= 3) {
        moveIndex = 1;  // Middle voice
    } else {
        moveIndex = 0;  // Lower voice of dyad
    }
    int originalNote = moved.midiNotes[moveIndex];

    // =========================================================================
    // BUILD TARGET PITCH CLASSES
    // =========================================================================
    int ninth = pcForDegree(c.chord, 9);
    int thirteenth = pcForDegree(c.chord, 13);

    QSet<int> validPcs;
    int root = c.chord.rootPc;
    validPcs.insert(root);
    int thirdPc = pcForDegree(c.chord, 3);
    if (thirdPc >= 0) validPcs.insert(thirdPc);
    int fifthPc = pcForDegree(c.chord, 5);
    if (fifthPc >= 0) validPcs.insert(fifthPc);
    int seventhPc = pcForDegree(c.chord, 7);
    if (seventhPc >= 0) validPcs.insert(seventhPc);

    // Color tones for Evans-style richness
    if (c.energy > 0.15 && ninth >= 0) validPcs.insert(ninth);
    if (c.energy > 0.3 && thirteenth >= 0) validPcs.insert(thirteenth);

    // During resolution, prefer chord tones over extensions
    bool preferConsonant = (c.phraseArcPhase == 2 || c.cadence01 > 0.5);

    // =========================================================================
    // VOICE-LEADING TO NEXT CHORD (contrapuntal preparation)
    // =========================================================================
    // If we know the next chord and it's coming soon, prepare for it
    QSet<int> nextChordPcs;
    if (c.hasNextChord && c.beatsUntilChordChange <= 2) {
        int nextThird = pcForDegree(c.nextChord, 3);
        int nextSeventh = pcForDegree(c.nextChord, 7);
        if (nextThird >= 0) nextChordPcs.insert(nextThird);
        if (nextSeventh >= 0) nextChordPcs.insert(nextSeventh);
        // Root of next chord is also a good target
        nextChordPcs.insert(c.nextChord.rootPc);
    }

    // =========================================================================
    // FIND BEST TARGET NOTE
    // =========================================================================
    int bestTarget = -1;
    int bestScore = -999;

    // Search in preferred direction first, but consider both directions
    // Use ±6 semitones to find valid options even in compact voicings
    for (int offset = -6; offset <= 6; ++offset) {
        if (offset == 0) continue;

        int candidate = originalNote + offset;
        if (candidate < c.lhLo || candidate > c.lhHi) continue;

        int candidatePc = candidate % 12;

        // Must be a valid pitch class for current chord
        if (!validPcs.contains(candidatePc)) continue;

        // Check for clusters with other notes (min 2 semitones apart)
        bool safe = true;
        for (int i = 0; i < moved.midiNotes.size(); ++i) {
            if (i != moveIndex && qAbs(moved.midiNotes[i] - candidate) < 2) {
                safe = false;
                break;
            }
        }
        if (!safe) continue;

        // =====================================================================
        // SCORING: Contrapuntal priorities
        // =====================================================================
        int score = 0;

        // 1. Stepwise motion is most melodic (±1 or ±2 semitones)
        int dist = qAbs(offset);
        score += (6 - dist);  // Stepwise strongly preferred

        // 2. Moving in phrase-driven direction
        bool inDirection = (dir > 0 && offset > 0) || (dir < 0 && offset < 0);
        if (inDirection) {
            score += 4;  // Strong preference for phrase-driven direction
        }

        // 3. Voice-leading bonus: moves toward next chord's tones
        if (!nextChordPcs.isEmpty() && nextChordPcs.contains(candidatePc)) {
            score += 3;  // Prepares the next chord
        }

        // 4. Resolution phase: prefer chord tones over extensions
        if (preferConsonant) {
            bool isChordTone = (candidatePc == root || candidatePc == thirdPc ||
                               candidatePc == fifthPc || candidatePc == seventhPc);
            if (isChordTone) score += 2;
        } else {
            // Building phase: color tones add tension
            if (candidatePc == ninth || candidatePc == thirteenth) {
                score += 2;
            }
        }

        // 5. Continuation bonus: if we have a target, keep pursuing it
        if (m_state.innerVoiceTarget >= 0) {
            int targetDist = qAbs(candidate - m_state.innerVoiceTarget);
            if (targetDist < qAbs(originalNote - m_state.innerVoiceTarget)) {
                score += 2;  // Getting closer to our target
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestTarget = candidate;
        }
    }

    // =========================================================================
    // APPLY MOVEMENT AND UPDATE STATE
    // =========================================================================
    if (bestTarget >= 0 && bestTarget != originalNote) {
        moved.midiNotes[moveIndex] = bestTarget;
        std::sort(moved.midiNotes.begin(), moved.midiNotes.end());
        moved.ontologyKey = "piano_lh_inner_move";

        // Update contrapuntal line state
        m_state.innerVoiceDirection = (bestTarget > originalNote) ? 1 : -1;
        m_state.lastInnerVoiceIndex = moveIndex;

        // Set or update target based on phrase
        if (c.phraseArcPhase == 0) {
            // Building: target is upward toward a color tone
            m_state.innerVoiceTarget = originalNote + 3;  // Aim 3 semitones up
        } else if (c.phraseArcPhase == 2) {
            // Resolving: target is downward toward a chord tone
            m_state.innerVoiceTarget = originalNote - 3;  // Aim 3 semitones down
        }
        m_state.beatsOnCurrentTarget++;
    }

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
