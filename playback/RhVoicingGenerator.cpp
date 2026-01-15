#include "RhVoicingGenerator.h"
#include "VoicingUtils.h"
#include <algorithm>
#include <QtGlobal>

namespace playback {

using namespace voicing_utils;

RhVoicingGenerator::RhVoicingGenerator(const virtuoso::ontology::OntologyRegistry* ont)
    : m_ont(ont)
{}

// =============================================================================
// DROP-2 VOICING
// =============================================================================

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateDrop2(const Context& c) const {
    RhVoicing rh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return rh;

    // Drop-2 voicings: 4 notes, 2nd from top dropped an octave
    // Degrees: typically 3-5-7-9 or 1-3-5-7
    QVector<int> degrees = {3, 5, 7, 9};

    // Get pitch classes
    QVector<int> pcs;
    for (int deg : degrees) {
        int pc = pcForDegree(chord, deg);
        if (pc >= 0) pcs.push_back(pc);
    }

    if (pcs.size() < 3) {
        // Fallback to simpler degrees
        pcs.clear();
        pcs.push_back(chord.rootPc);
        int third = pcForDegree(chord, 3);
        if (third >= 0) pcs.push_back(third);
        int fifth = pcForDegree(chord, 5);
        if (fifth >= 0) pcs.push_back(fifth);
        int seventh = pcForDegree(chord, 7);
        if (seventh >= 0) pcs.push_back(seventh);
    }

    if (pcs.isEmpty()) return rh;

    // ================================================================
    // DIRECTION-AWARE TOP NOTE SELECTION
    // ================================================================
    int lastTop = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 76;

    // Use unified direction-aware selection
    int targetTop = selectDirectionAwareTop(pcs, c.rhLo, c.rhHi,
                                             lastTop,
                                             m_state.targetMelodicDirection,
                                             m_state.consecutiveSameTop);

    // Build voicing from bottom up, positioning to hit target top
    // Calculate where bottom should start to achieve target top
    int topPc = normalizePc(targetTop);

    // Stack notes: place each PC in order, working up toward target top
    int cursor = targetTop - 14;  // Start roughly an octave below target
    if (cursor < c.rhLo) cursor = c.rhLo;

    for (int pc : pcs) {
        int midi = cursor;
        while (normalizePc(midi) != pc && midi < cursor + 12) midi++;
        if (midi > c.rhHi) midi -= 12;
        if (midi >= c.rhLo && midi <= c.rhHi) {
            rh.midiNotes.push_back(midi);
        }
        cursor = midi + 1;
    }

    std::sort(rh.midiNotes.begin(), rh.midiNotes.end());

    // Apply Drop-2: move 2nd from top down an octave
    if (rh.midiNotes.size() >= 4) {
        int idx = rh.midiNotes.size() - 2;
        rh.midiNotes[idx] -= 12;
        std::sort(rh.midiNotes.begin(), rh.midiNotes.end());
    }

    // Set top note and direction
    if (!rh.midiNotes.isEmpty()) {
        rh.topNoteMidi = rh.midiNotes.last();
        rh.melodicDirection = (rh.topNoteMidi > lastTop) ? 1 :
                              (rh.topNoteMidi < lastTop) ? -1 : 0;
    }

    rh.type = VoicingType::Drop2;
    rh.ontologyKey = "piano_rh_drop2";
    rh.cost = voiceLeadingCost(m_state.lastRhMidi, rh.midiNotes);

    return rh;
}

// =============================================================================
// TRIAD
// =============================================================================

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateTriad(const Context& c) const {
    RhVoicing rh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return rh;

    // Get all available chord tones
    int third = pcForDegree(chord, 3);
    int fifth = pcForDegree(chord, 5);
    int seventh = pcForDegree(chord, 7);
    int ninth = pcForDegree(chord, 9);

    QVector<int> allPcs;
    if (third >= 0) allPcs.push_back(third);
    if (fifth >= 0) allPcs.push_back(fifth);
    if (seventh >= 0) allPcs.push_back(seventh);
    if (ninth >= 0) allPcs.push_back(ninth);
    allPcs.push_back(chord.rootPc);

    if (allPcs.isEmpty()) return rh;

    // ================================================================
    // VOICE LEADING with DIRECTION: Use the unified selection function
    // ================================================================
    int lastTop = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 76;

    // Use direction-aware selection (respects phrase arc + repetition avoidance)
    int bestTopMidi = selectDirectionAwareTop(allPcs, c.rhLo, c.rhHi,
                                               lastTop,
                                               m_state.targetMelodicDirection,
                                               m_state.consecutiveSameTop);

    if (bestTopMidi < 0) return rh;

    rh.topNoteMidi = bestTopMidi;
    int topPc = normalizePc(bestTopMidi);

    // ================================================================
    // BUILD TRIAD: Place other voices below the top
    // ================================================================
    QVector<int> triadPcs;
    // Prefer 3-5-7 or 3-7-9 shapes (guide tone triads)
    if (third >= 0 && third != topPc) triadPcs.push_back(third);
    if (seventh >= 0 && seventh != topPc) triadPcs.push_back(seventh);
    if (fifth >= 0 && fifth != topPc && triadPcs.size() < 2) triadPcs.push_back(fifth);

    // Place inner voices below top note
    for (int pc : triadPcs) {
        int midi = bestTopMidi - 3;  // Start searching below top
        while (normalizePc(midi) != pc && midi > bestTopMidi - 12) {
            midi--;
        }
        if (midi >= c.rhLo && midi < bestTopMidi) {
            rh.midiNotes.push_back(midi);
        }
    }

    rh.midiNotes.push_back(bestTopMidi);
    std::sort(rh.midiNotes.begin(), rh.midiNotes.end());

    rh.melodicDirection = (bestTopMidi > lastTop + 1) ? 1 :
                          (bestTopMidi < lastTop - 1) ? -1 : 0;

    rh.type = VoicingType::Triad;
    rh.ontologyKey = (chord.rootPc == normalizePc(rh.midiNotes.first())) ?
                     "piano_triad_root" : "piano_triad_first_inv";
    rh.cost = voiceLeadingCost(m_state.lastRhMidi, rh.midiNotes);

    return rh;
}

// =============================================================================
// DYAD
// =============================================================================

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateDyad(const Context& c) const {
    RhVoicing rh;
    const auto& chord = c.chord;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return rh;
    
    // Get ALL valid chord tones for voice leading flexibility
    // More options = better chance of stepwise motion
    QVector<int> colorPcs;
    int third = pcForDegree(chord, 3);
    int fifth = pcForDegree(chord, 5);
    int seventh = pcForDegree(chord, 7);
    int ninth = pcForDegree(chord, 9);

    // Include all chord tones for maximum voice leading options
    if (third >= 0) colorPcs.push_back(third);
    if (seventh >= 0) colorPcs.push_back(seventh);
    if (fifth >= 0) colorPcs.push_back(fifth);
    if (ninth >= 0) colorPcs.push_back(ninth);  // 9ths are consonant, always include
    colorPcs.push_back(chord.rootPc);  // Root also valid for voice leading

    if (colorPcs.isEmpty()) return rh;

    // ================================================================
    // STRONG VOICE LEADING with MELODIC CONTOUR
    // ================================================================
    // Evans' top voice creates a coherent melodic line:
    // - Stepwise motion is paramount
    // - But also has DIRECTION (ascending toward climax, descending toward resolution)
    // - Avoids staying on same note too long (repetition penalty)
    // ================================================================
    int lastTop = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 74;

    // Repetition penalty: increases with consecutive same-note beats
    const double repetitionPenalty = m_state.consecutiveSameTop * 1.5;  // 0, 1.5, 3.0, 4.5...

    // Melodic direction: bonus for moving in phrase-appropriate direction
    const int targetDir = c.melodicDirectionHint;  // -1, 0, or +1

    QVector<std::pair<int, double>> candidates;
    for (int pc : colorPcs) {
        for (int oct = 5; oct <= 7; ++oct) {
            int midi = pc + 12 * oct;
            if (midi < c.rhLo || midi > c.rhHi) continue;

            double cost = 0.0;
            int motion = qAbs(midi - lastTop);
            int direction = (midi > lastTop) ? 1 : (midi < lastTop) ? -1 : 0;

            // AGGRESSIVE voice leading costs - stepwise MUST win
            if (motion == 0) {
                // Same note: base penalty PLUS repetition penalty
                cost = 0.5 + repetitionPenalty;
            }
            else if (motion == 1) cost = 0.0;   // Half step - ideal
            else if (motion == 2) cost = 0.0;   // Whole step - ideal
            else if (motion == 3) cost = 2.0;   // Minor 3rd - small leap, acceptable
            else if (motion == 4) cost = 3.0;   // Major 3rd - noticeable leap
            else if (motion <= 7) cost = 8.0;   // 4th-5th - significant leap
            else cost = 15.0;                   // 6th+ - avoid unless necessary

            // ================================================================
            // MELODIC CONTOUR: Bonus for moving in target direction
            // ================================================================
            if (targetDir != 0 && direction != 0) {
                if (direction == targetDir) {
                    // Moving in desired direction: bonus
                    cost -= 1.0;
                } else {
                    // Moving against desired direction: penalty
                    cost += 0.5;
                }
            }

            // Small bonus for guide tones (but NOT enough to override voice leading)
            if (pc == third || pc == seventh) cost -= 0.3;

            // Small bonus for sweet spot
            if (midi >= 72 && midi <= 82) cost -= 0.2;

            candidates.push_back({midi, cost});
        }
    }
    
    if (candidates.isEmpty()) return rh;
    
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    rh.topNoteMidi = candidates.first().first;
    int topPc = normalizePc(rh.topNoteMidi);
    
    rh.melodicDirection = (rh.topNoteMidi > lastTop + 1) ? 1 :
                          (rh.topNoteMidi < lastTop - 1) ? -1 : 0;
    
    // Select second voice with consonance preference (always conservative)
    int secondPc = -1;
    int bestConsonance = 99;

    for (int pc : colorPcs) {
        if (pc == topPc) continue;
        int interval = (topPc - pc + 12) % 12;

        int score = 99;
        if (interval == 3 || interval == 4) score = 0;       // 3rds - best
        else if (interval == 8 || interval == 9) score = 1;  // 6ths - great
        else if (interval == 5) score = 2;                    // 4th - good
        else if (interval == 7) score = 3;                    // 5th - good
        // No tense intervals (7ths, 2nds, tritones) - keep it consonant

        if (score < bestConsonance) {
            bestConsonance = score;
            secondPc = pc;
        }
    }

    if (secondPc < 0 || bestConsonance > 5) {
        secondPc = (seventh >= 0 && seventh != topPc) ? seventh : third;
    }

    if (secondPc >= 0) {
        int secondMidi = rh.topNoteMidi - 3;
        while (normalizePc(secondMidi) != secondPc && secondMidi > rh.topNoteMidi - 10) {
            secondMidi--;
        }

        int actualInterval = rh.topNoteMidi - secondMidi;
        // Only consonant intervals (3-9 semitones)
        bool intervalOk = (actualInterval >= 3 && actualInterval <= 9);

        if (intervalOk && secondMidi >= c.rhLo) {
            rh.midiNotes.push_back(secondMidi);
        }
    }
    
    rh.midiNotes.push_back(rh.topNoteMidi);
    std::sort(rh.midiNotes.begin(), rh.midiNotes.end());
    
    // Set ontology key
    if (topPc == ninth) {
        rh.isColorTone = true;
        rh.ontologyKey = (rh.midiNotes.size() == 2) ? "piano_rh_dyad_color" : "piano_rh_single_color";
    } else {
        rh.isColorTone = false;
        rh.ontologyKey = (rh.midiNotes.size() == 2) ? "piano_rh_dyad_guide" : "piano_rh_single_guide";
    }
    
    rh.type = VoicingType::Dyad;
    rh.cost = voiceLeadingCost(m_state.lastRhMidi, rh.midiNotes);
    
    return rh;
}

// =============================================================================
// SINGLE NOTE
// =============================================================================

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateSingle(const Context& c) const {
    RhVoicing rh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return rh;

    // Get candidate notes
    QVector<int> candidatePcs;
    int third = pcForDegree(chord, 3);
    int seventh = pcForDegree(chord, 7);
    int ninth = pcForDegree(chord, 9);

    if (third >= 0) candidatePcs.push_back(third);
    if (seventh >= 0) candidatePcs.push_back(seventh);
    if (ninth >= 0) candidatePcs.push_back(ninth);  // 9ths always OK (consonant)

    if (candidatePcs.isEmpty()) candidatePcs.push_back(chord.rootPc);

    // Use direction-aware selection (respects phrase arc + repetition avoidance)
    int lastTop = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 74;
    rh.topNoteMidi = selectDirectionAwareTop(candidatePcs, c.rhLo, c.rhHi,
                                              lastTop,
                                              m_state.targetMelodicDirection,
                                              m_state.consecutiveSameTop);

    rh.midiNotes.push_back(rh.topNoteMidi);
    rh.melodicDirection = (rh.topNoteMidi > lastTop + 1) ? 1 :
                          (rh.topNoteMidi < lastTop - 1) ? -1 : 0;

    rh.type = VoicingType::Single;
    rh.ontologyKey = "piano_rh_single_guide";

    return rh;
}

// =============================================================================
// MELODIC DYAD (Evans-style walking 3rds/6ths)
// Creates parallel motion rather than isolated chord tones
// =============================================================================

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateMelodicDyad(
    const Context& c, int direction) const {

    RhVoicing rh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return rh;

    // Determine melodic direction
    int dir = direction;
    if (dir == 0) {
        // Use state to alternate or continue direction
        dir = (m_state.rhMelodicDirection != 0) ? m_state.rhMelodicDirection : 1;
    }

    // Get last top note as starting point
    int lastTop = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 74;

    // Determine interval type based on chord quality
    // Major/Dominant: prefer major 3rds (4 semitones) or major 6ths (9 semitones)
    // Minor: prefer minor 3rds (3 semitones) or minor 6ths (8 semitones)
    int interval = 4;  // Default major 3rd
    if (chord.quality == music::ChordQuality::Minor ||
        chord.quality == music::ChordQuality::HalfDiminished ||
        chord.quality == music::ChordQuality::Diminished) {
        interval = 3;  // Minor 3rd
    }

    // For variety, sometimes use 6ths instead of 3rds
    if ((c.beatInBar + c.barInPhrase) % 4 == 0) {
        interval = (interval == 4) ? 9 : 8;  // 6th instead of 3rd
    }

    // Move by step in the direction
    int step = dir * 2;  // Move by whole step for smooth parallel motion

    // Calculate new top note
    int newTop = lastTop + step;

    // Ensure we stay in register
    if (newTop > c.rhHi) {
        newTop = lastTop - 2;  // Reverse direction
        dir = -1;
    } else if (newTop < c.rhLo + interval) {
        newTop = lastTop + 2;  // Reverse direction
        dir = 1;
    }

    // Snap to nearest chord tone or scale tone
    QVector<int> validPcs;
    int third = pcForDegree(chord, 3);
    int fifth = pcForDegree(chord, 5);
    int seventh = pcForDegree(chord, 7);
    int ninth = pcForDegree(chord, 9);

    if (third >= 0) validPcs.push_back(third);
    if (fifth >= 0) validPcs.push_back(fifth);
    if (seventh >= 0) validPcs.push_back(seventh);
    if (ninth >= 0 && c.energy > 0.3) validPcs.push_back(ninth);

    // Find closest valid PC to newTop
    int bestTop = newTop;
    int bestDist = 99;
    for (int pc : validPcs) {
        for (int oct = 5; oct <= 7; ++oct) {
            int midi = pc + oct * 12;
            if (midi < c.rhLo || midi > c.rhHi) continue;
            int dist = qAbs(midi - newTop);
            if (dist < bestDist) {
                bestDist = dist;
                bestTop = midi;
            }
        }
    }

    rh.topNoteMidi = bestTop;

    // Calculate second note (interval below top)
    int secondMidi = bestTop - interval;
    if (secondMidi < c.rhLo) {
        // Try interval above instead
        secondMidi = bestTop + interval;
        if (secondMidi > c.rhHi) {
            // Can't fit dyad, return single note
            rh.midiNotes.push_back(bestTop);
            rh.type = VoicingType::Single;
            rh.ontologyKey = "piano_rh_melodic_single";
            return rh;
        }
    }

    rh.midiNotes.push_back(qMin(secondMidi, bestTop));
    rh.midiNotes.push_back(qMax(secondMidi, bestTop));
    rh.topNoteMidi = rh.midiNotes.last();
    rh.melodicDirection = dir;

    rh.type = VoicingType::Dyad;
    rh.ontologyKey = (interval <= 4) ? "piano_rh_melodic_3rd" : "piano_rh_melodic_6th";
    rh.cost = voiceLeadingCost(m_state.lastRhMidi, rh.midiNotes);

    return rh;
}

// =============================================================================
// UNISON VOICING (RH synced with LH for reinforced texture)
// Evans' signature: both hands strike together, creating unified texture
// =============================================================================

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateUnisonVoicing(
    const Context& c, const QVector<int>& lhMidi) const {

    RhVoicing rh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return rh;
    if (lhMidi.isEmpty()) return generateDyad(c);  // Fallback

    // For unison comping, RH adds color above LH
    // Typically a 3rd or 6th above the LH top note
    int lhTop = lhMidi.last();

    // Get color tones
    int seventh = pcForDegree(chord, 7);
    int ninth = pcForDegree(chord, 9);
    int thirteenth = pcForDegree(chord, 13);

    QVector<int> colorPcs;
    if (ninth >= 0) colorPcs.push_back(ninth);
    // Skip 13ths - keep voicings simple and consonant
    if (seventh >= 0) colorPcs.push_back(seventh);

    if (colorPcs.isEmpty()) {
        return generateDyad(c);  // Fallback
    }

    // Find a color note that's a 3rd or 6th above LH top
    int bestNote = -1;
    int bestScore = -999;

    for (int pc : colorPcs) {
        for (int oct = 5; oct <= 7; ++oct) {
            int midi = pc + oct * 12;
            if (midi < c.rhLo || midi > c.rhHi) continue;
            if (midi <= lhTop + 2) continue;  // Must be clearly above LH

            int interval = midi - lhTop;
            int score = 0;

            // Prefer 3rds and 6ths above LH
            if (interval == 3 || interval == 4) score = 10;  // 3rd
            else if (interval == 8 || interval == 9) score = 8;  // 6th
            else if (interval == 5) score = 5;  // 4th
            else if (interval >= 10 && interval <= 12) score = 3;  // 7th/octave
            else score = -5;

            // Bonus for being in sweet spot
            if (midi >= 72 && midi <= 82) score += 2;

            if (score > bestScore) {
                bestScore = score;
                bestNote = midi;
            }
        }
    }

    if (bestNote < 0) {
        return generateDyad(c);  // Fallback
    }

    rh.midiNotes.push_back(bestNote);
    rh.topNoteMidi = bestNote;
    rh.melodicDirection = (bestNote > m_state.lastRhTopMidi) ? 1 :
                          (bestNote < m_state.lastRhTopMidi) ? -1 : 0;

    // Optionally add a second note for richer texture
    if (c.energy > 0.4) {
        // Add note a 3rd or 4th below
        int second = bestNote - 4;
        if (second >= c.rhLo && second > lhTop) {
            rh.midiNotes.insert(rh.midiNotes.begin(), second);
        }
    }

    rh.type = VoicingType::Dyad;
    rh.ontologyKey = "piano_rh_unison_color";
    rh.isColorTone = true;
    rh.cost = voiceLeadingCost(m_state.lastRhMidi, rh.midiNotes);

    return rh;
}

// =============================================================================
// BLOCK UPPER (Upper portion of block chord, coordinated with LH)
// Used for climax moments - George Shearing "locked hands" style
// =============================================================================

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateBlockUpper(
    const Context& c, int targetTopMidi) const {

    RhVoicing rh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return rh;

    // Block chord upper: melody note doubled an octave below + inner voices
    // Classic Shearing: melody on top, same note octave below, fills in between

    // Get chord tones for fill
    int third = pcForDegree(chord, 3);
    int fifth = pcForDegree(chord, 5);
    int seventh = pcForDegree(chord, 7);

    // Determine top note
    int topMidi = targetTopMidi;
    if (topMidi < 0) {
        topMidi = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 76;
    }

    // Ensure top is in register
    while (topMidi > c.rhHi) topMidi -= 12;
    while (topMidi < c.rhLo) topMidi += 12;

    // Build block: top note + fill notes + octave below
    rh.midiNotes.push_back(topMidi);

    // Add inner voices between top and octave below
    int octaveBelow = topMidi - 12;
    if (octaveBelow >= c.rhLo) {
        // Fill with chord tones
        QVector<int> fillPcs;
        if (seventh >= 0) fillPcs.push_back(seventh);
        if (fifth >= 0) fillPcs.push_back(fifth);
        if (third >= 0) fillPcs.push_back(third);

        for (int pc : fillPcs) {
            int midi = nearestMidiForPc(pc, (topMidi + octaveBelow) / 2, octaveBelow + 1, topMidi - 1);
            if (midi > octaveBelow && midi < topMidi) {
                rh.midiNotes.push_back(midi);
            }
        }

        // Add octave below
        rh.midiNotes.push_back(octaveBelow);
    }

    std::sort(rh.midiNotes.begin(), rh.midiNotes.end());

    rh.topNoteMidi = rh.midiNotes.isEmpty() ? topMidi : rh.midiNotes.last();
    rh.melodicDirection = (rh.topNoteMidi > m_state.lastRhTopMidi) ? 1 :
                          (rh.topNoteMidi < m_state.lastRhTopMidi) ? -1 : 0;

    rh.type = VoicingType::Drop2;  // Similar voicing density
    rh.ontologyKey = "piano_rh_block";
    rh.cost = voiceLeadingCost(m_state.lastRhMidi, rh.midiNotes);

    return rh;
}

// =============================================================================
// STAGE 9: NEW VOICING TYPES
// =============================================================================

// =============================================================================
// HARMONIZED DYAD: Melody + parallel 3rd or 6th below
// Creates vocal-like, singing texture - Evans/Freeman accompaniment essential
// =============================================================================

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateHarmonizedDyad(const Context& c) const {
    RhVoicing rh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return rh;

    // Select melody target using voice-leading from last position
    int melodyMidi = selectNextMelodicTarget(c);

    // Ensure in register
    while (melodyMidi > c.rhHi) melodyMidi -= 12;
    while (melodyMidi < c.rhLo) melodyMidi += 12;

    // Determine harmony interval: 3rd below (3-4 semitones) or 6th below (8-9 semitones)
    // Choose based on what creates better chord tones
    int melodyPc = melodyMidi % 12;

    // Try minor 3rd below first (most common in jazz)
    int harmonyMidi = melodyMidi - 3;
    int harmonyPc = harmonyMidi % 12;

    // Check if harmony note is a chord tone or good extension
    int third = pcForDegree(chord, 3);
    int fifth = pcForDegree(chord, 5);
    int seventh = pcForDegree(chord, 7);

    bool harmonyIsChordTone = (harmonyPc == chord.rootPc || harmonyPc == third ||
                               harmonyPc == fifth || harmonyPc == seventh);

    // If minor 3rd below isn't a chord tone, try major 3rd
    if (!harmonyIsChordTone) {
        harmonyMidi = melodyMidi - 4;
        harmonyPc = harmonyMidi % 12;
        harmonyIsChordTone = (harmonyPc == chord.rootPc || harmonyPc == third ||
                              harmonyPc == fifth || harmonyPc == seventh);
    }

    // If 3rds don't work, try 6th below (creates warmer sound)
    if (!harmonyIsChordTone && c.energy < 0.5) {
        harmonyMidi = melodyMidi - 8;  // Minor 6th
        harmonyPc = harmonyMidi % 12;
        harmonyIsChordTone = (harmonyPc == chord.rootPc || harmonyPc == third ||
                              harmonyPc == fifth || harmonyPc == seventh);
        if (!harmonyIsChordTone) {
            harmonyMidi = melodyMidi - 9;  // Major 6th
        }
    }

    // Ensure harmony is in register
    if (harmonyMidi < c.rhLo) {
        harmonyMidi += 12;
    }

    // Build the dyad (harmony below melody)
    if (harmonyMidi >= c.rhLo && harmonyMidi < melodyMidi) {
        rh.midiNotes.push_back(harmonyMidi);
    }
    rh.midiNotes.push_back(melodyMidi);

    rh.topNoteMidi = melodyMidi;
    rh.melodicDirection = (melodyMidi > m_state.lastRhTopMidi) ? 1 :
                          (melodyMidi < m_state.lastRhTopMidi) ? -1 : 0;
    rh.type = VoicingType::Dyad;
    rh.ontologyKey = "piano_rh_harmonized_dyad";
    rh.isColorTone = !harmonyIsChordTone;
    rh.cost = voiceLeadingCost(m_state.lastRhMidi, rh.midiNotes);

    return rh;
}

// =============================================================================
// OCTAVE DOUBLE: Melody doubled at octave for powerful, singing sound
// Used at climax moments - creates prominence without full block chord
// =============================================================================

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateOctaveDouble(const Context& c) const {
    RhVoicing rh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return rh;

    // Select melody target - aim for expressive note (3rd, 7th, 9th)
    int melodyMidi = selectNextMelodicTarget(c);

    // For octave doubling, prefer higher register for brilliance
    while (melodyMidi < 72) melodyMidi += 12;  // At least C5
    while (melodyMidi > c.rhHi) melodyMidi -= 12;

    // The octave below
    int octaveBelow = melodyMidi - 12;

    // Build the octave pair
    if (octaveBelow >= c.rhLo) {
        rh.midiNotes.push_back(octaveBelow);
    }
    rh.midiNotes.push_back(melodyMidi);

    rh.topNoteMidi = melodyMidi;
    rh.melodicDirection = (melodyMidi > m_state.lastRhTopMidi) ? 1 :
                          (melodyMidi < m_state.lastRhTopMidi) ? -1 : 0;
    rh.type = VoicingType::Dyad;
    rh.ontologyKey = "piano_rh_octave_double";
    rh.cost = voiceLeadingCost(m_state.lastRhMidi, rh.midiNotes);

    return rh;
}

// =============================================================================
// BLUES GRACE: Main voicing with b3 or b7 grace note approach
// Adds bluesy inflection - essential for jazz-blues feel on dominants
// =============================================================================

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateBluesGrace(const Context& c) const {
    RhVoicing rh;
    const auto& chord = c.chord;

    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return rh;

    // Start with a basic dyad voicing
    rh = generateDyad(c);

    if (rh.midiNotes.isEmpty()) return rh;

    // The blues grace note will be added as timing/ornament data
    // Here we just mark that this voicing should have blues inflection
    // The actual grace note timing is handled by the planner

    // For dominant chords, the classic blues grace is:
    // - Approach the 3rd from b3 (blue note)
    // - Approach the 7th from below

    if (chord.quality == music::ChordQuality::Dominant) {
        int third = pcForDegree(chord, 3);

        // Find if we have the 3rd in our voicing - it will be approached from b3
        for (int i = 0; i < rh.midiNotes.size(); ++i) {
            int notePc = rh.midiNotes[i] % 12;
            if (notePc == third) {
                // This note should be approached from b3 (one semitone below)
                // Store this info in the ontology key for the planner to use
                rh.ontologyKey = "piano_rh_blues_grace_b3";
                rh.isColorTone = true;  // Mark as having color/inflection
                break;
            }
        }
    }

    // Also works on minor chords - approach 3rd from below
    if (chord.quality == music::ChordQuality::Minor) {
        rh.ontologyKey = "piano_rh_blues_grace_minor";
        rh.isColorTone = true;
    }

    return rh;
}

// =============================================================================
// UPPER STRUCTURE TRIADS
// =============================================================================

QVector<RhVoicingGenerator::UpperStructureTriad> RhVoicingGenerator::getUpperStructureTriads(
    const music::ChordSymbol& chord) const {
    
    QVector<UpperStructureTriad> triads;
    
    if (chord.placeholder || chord.noChord || chord.rootPc < 0) return triads;
    
    const int root = chord.rootPc;
    const bool isDominant = (chord.quality == music::ChordQuality::Dominant);
    const bool isMajor = (chord.quality == music::ChordQuality::Major);
    const bool isMinor = (chord.quality == music::ChordQuality::Minor);
    const bool isAlt = chord.alt && isDominant;
    
    // Dominant 7th: most UST options
    if (isDominant) {
        // D/C7 → 9-#11-13 (lydian dominant)
        triads.push_back({normalizePc(root + 2), true, 0.3, "9-#11-13", "piano_ust_II"});
        
        // E/C7 → 3-#5-7 (augmented)
        triads.push_back({normalizePc(root + 4), true, 0.4, "3-#5-7", "piano_ust_III"});
        
        // Eb/C7 → b9-11-b13 (altered)
        if (isAlt) {
            triads.push_back({normalizePc(root + 3), true, 0.6, "b9-11-b13", "piano_ust_bIII"});
        }
        
        // F#/C7 → #11-7-b9 (tritone sub)
        triads.push_back({normalizePc(root + 6), true, 0.5, "#11-7-b9", "piano_ust_tritone"});
        
        // Ab/C7 → b13-1-b9 (very altered)
        if (isAlt) {
            triads.push_back({normalizePc(root + 8), true, 0.7, "b13-1-b9", "piano_ust_bVI"});
        }
    }
    
    // Major 7th
    if (isMajor && chord.seventh == music::SeventhQuality::Major7) {
        // D/Cmaj7 → 9-#11-13 (lydian)
        triads.push_back({normalizePc(root + 2), true, 0.3, "9-#11-13", "piano_ust_II"});
        
        // E/Cmaj7 → 3-#5-7 
        triads.push_back({normalizePc(root + 4), true, 0.4, "3-#5-7", "piano_ust_III"});
    }
    
    // Minor 7th
    if (isMinor) {
        // F/Dm7 → b3-5-b7 (reinforces minor)
        triads.push_back({normalizePc(root + 3), true, 0.2, "b3-5-b7", "piano_ust_bIII"});
        
        // Eb/Dm7 → b9-11-b13 (phrygian)
        triads.push_back({normalizePc(root + 1), true, 0.5, "b2-4-b6", "piano_ust_bII"});
    }
    
    // Sort by tension (lowest first for safe defaults)
    std::sort(triads.begin(), triads.end(),
              [](const auto& a, const auto& b) { return a.tensionLevel < b.tensionLevel; });
    
    return triads;
}

RhVoicingGenerator::RhVoicing RhVoicingGenerator::buildUstVoicing(
    const Context& c, const UpperStructureTriad& ust) const {
    
    RhVoicing rh;
    
    // Build a simple triad from the UST root
    int triadRoot = ust.rootPc;
    int triadThird = normalizePc(triadRoot + (ust.isMajor ? 4 : 3));
    int triadFifth = normalizePc(triadRoot + 7);
    
    // Realize in RH register around MIDI 76
    int targetMidi = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 76;
    
    int rootMidi = nearestMidiForPc(triadRoot, targetMidi - 6, c.rhLo, c.rhHi);
    int thirdMidi = rootMidi + (ust.isMajor ? 4 : 3);
    int fifthMidi = rootMidi + 7;
    
    // Adjust octave if needed
    while (fifthMidi > c.rhHi) {
        rootMidi -= 12;
        thirdMidi -= 12;
        fifthMidi -= 12;
    }
    
    if (rootMidi >= c.rhLo) rh.midiNotes.push_back(rootMidi);
    if (thirdMidi >= c.rhLo && thirdMidi <= c.rhHi) rh.midiNotes.push_back(thirdMidi);
    if (fifthMidi >= c.rhLo && fifthMidi <= c.rhHi) rh.midiNotes.push_back(fifthMidi);
    
    std::sort(rh.midiNotes.begin(), rh.midiNotes.end());
    
    if (!rh.midiNotes.isEmpty()) {
        rh.topNoteMidi = rh.midiNotes.last();
        rh.melodicDirection = (rh.topNoteMidi > m_state.lastRhTopMidi) ? 1 :
                              (rh.topNoteMidi < m_state.lastRhTopMidi) ? -1 : 0;
    }
    
    rh.type = VoicingType::UST;
    rh.ontologyKey = ust.ontologyKey;
    rh.isColorTone = true;
    rh.cost = voiceLeadingCost(m_state.lastRhMidi, rh.midiNotes);
    
    return rh;
}

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateUST(const Context& c) const {
    auto triads = getUpperStructureTriads(c.chord);
    
    if (triads.isEmpty()) {
        return generateDyad(c);  // Fallback
    }
    
    // ALWAYS use safest UST (idx=0) - decoupled from energy
    // No tritone subs or altered voicings regardless of energy
    return buildUstVoicing(c, triads[0]);
}

// =============================================================================
// GENERATE BEST
// =============================================================================

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateBest(const Context& c) const {
    // ================================================================
    // PHASE 1: Update melodic direction based on phrase position
    // ================================================================
    updateMelodicDirection(c);

    // ================================================================
    // PHASE 2: Track chord duration for shimmer
    // ================================================================
    bool sameChord = (c.chord.rootPc == m_state.lastChordForRh.rootPc &&
                      c.chord.quality == m_state.lastChordForRh.quality);

    if (sameChord) {
        m_state.beatsOnSameChord++;
    } else {
        m_state.beatsOnSameChord = 0;
        m_state.shimmerPhase = 0;  // Reset shimmer on chord change
    }

    // ================================================================
    // PHASE 3: Diversified voicing selection (not just Dyad/Triad!)
    // ================================================================
    // Target distribution:
    // - Dyad: 40% (still common, but not dominant)
    // - Triad: 25% (close position color)
    // - HarmonizedDyad: 15% (Evans singing quality)
    // - Drop2: 10% (fuller texture on phrase boundaries)
    // - UST: 5% (dominants only)
    // - Single: 5% (melodic moments)

    // Generate a pseudo-random value from context for variety
    quint32 hash = quint32(c.beatInBar * 17 + c.barInPhrase * 31 +
                           int(c.energy * 100) + c.chord.rootPc * 7);

    // ================================================================
    // CONTEXT-DRIVEN SELECTION
    // ================================================================

    // Phrase boundary / cadence: fuller voicings
    if (c.phraseEndBar || c.cadence01 > 0.5) {
        if (hash % 4 == 0) return generateDrop2(c);
        return generateTriad(c);
    }

    // Dominant chord: UST opportunity (5% chance)
    if (c.chord.quality == music::ChordQuality::Dominant && c.energy > 0.3) {
        if (hash % 20 == 0) return generateUST(c);
    }

    // New chord: triads for clarity (but with variety)
    if (c.chordIsNew) {
        int r = hash % 10;
        if (r < 6) return generateTriad(c);           // 60%
        if (r < 8) return generateHarmonizedDyad(c);  // 20%
        return generateDrop2(c);                       // 20%
    }

    // Melodic/sparse moments (user silent, low energy)
    if (c.userSilence && c.energy < 0.3) {
        int r = hash % 10;
        if (r < 3) return generateHarmonizedDyad(c);  // 30%
        if (r < 5) return generateSingle(c);          // 20%
        return generateDyad(c);                        // 50%
    }

    // Avoid voicing type repetition
    if (m_state.lastVoicingType == VoicingType::Dyad) {
        // If we've been doing dyads, mix it up
        int r = hash % 10;
        if (r < 4) return generateDyad(c);            // 40%
        if (r < 6) return generateTriad(c);           // 20%
        if (r < 8) return generateHarmonizedDyad(c);  // 20%
        return generateSingle(c);                      // 20%
    }

    // Default distribution
    int r = hash % 10;
    RhVoicing result;
    if (r < 4) result = generateDyad(c);            // 40%
    else if (r < 6) result = generateTriad(c);      // 20%
    else if (r < 8) result = generateHarmonizedDyad(c);  // 20%
    else if (r < 9) result = generateDrop2(c);      // 10%
    else result = generateSingle(c);                 // 10%

    // ================================================================
    // PHASE 2: Apply inner voice shimmer when sustaining
    // ================================================================
    if (shouldApplyShimmer(c) && result.midiNotes.size() >= 3) {
        result = applyInnerVoiceShimmer(result, c);
        m_state.shimmerPhase++;  // Advance shimmer phase for next beat
    }

    // ================================================================
    // PHASE 4: Apply velocity shading
    // ================================================================
    result = applyVelocityShading(result, c);

    // ================================================================
    // PHASE 5: Apply micro-timing (BPM-constrained)
    // ================================================================
    result = applyMicroTiming(result, c);

    return result;
}

// =============================================================================
// ACTIVITY LEVEL
// =============================================================================

int RhVoicingGenerator::activityLevel(const Context& c, quint32 hash) const {
    // When user is playing: very sparse
    if (c.userBusy) {
        if (c.chordIsNew) {
            return (hash % 100) < 20 ? 1 : 0;
        }
        return 0;
    }
    
    // Phrase arc affects activity
    double phraseProg = double(c.barInPhrase) / qMax(1, 4);
    
    // Resolving phase: sparse
    if (c.phraseEndBar) {
        if (c.chordIsNew) return 2;
        return (hash % 100) < 60 ? 1 : 2;
    }
    
    // Weak beats: lighter
    bool isWeakBeat = (c.beatInBar == 1 || c.beatInBar == 3);
    if (isWeakBeat && !c.chordIsNew) {
        return (hash % 100) < 65 ? 1 : 2;
    }
    
    // Building phase
    if (phraseProg < 0.5) {
        if (c.chordIsNew) return 2;
        return (hash % 100) < 60 ? 1 : 2;
    }
    
    // Peak phase
    if (c.energy > 0.6) {
        if (c.chordIsNew) {
            return (c.energy > 0.75) ? 4 : 3;  // More notes at higher energy
        }
        return (c.energy > 0.5) ? 3 : 2;
    }
    
    // Cadence
    if (c.cadence01 > 0.6) {
        return (c.beatInBar == 0) ? 3 : 1;
    }
    
    // Default
    return c.chordIsNew ? 2 : 1;
}

// =============================================================================
// SELECT NEXT MELODIC TARGET
// =============================================================================

int RhVoicingGenerator::selectNextMelodicTarget(const Context& c) const {
    QVector<int> candidatePcs;

    int third = pcForDegree(c.chord, 3);
    int seventh = pcForDegree(c.chord, 7);
    int ninth = pcForDegree(c.chord, 9);

    if (third >= 0) candidatePcs.push_back(third);
    if (seventh >= 0) candidatePcs.push_back(seventh);
    if (ninth >= 0) candidatePcs.push_back(ninth);  // 9ths always OK (consonant)

    if (candidatePcs.isEmpty()) candidatePcs.push_back(c.chord.rootPc);

    // Use direction-aware selection (respects phrase arc + repetition avoidance)
    int lastTop = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 74;
    return selectDirectionAwareTop(candidatePcs, c.rhLo, c.rhHi,
                                    lastTop,
                                    m_state.targetMelodicDirection,
                                    m_state.consecutiveSameTop);
}

// =============================================================================
// ORNAMENTS
// =============================================================================

bool RhVoicingGenerator::shouldAddOrnament(const Context& c, quint32 hash) const {
    // HIGH ENERGY: No ornaments! Clean, punchy, rhythmic playing.
    // Ornaments are for intimate, expressive low-energy moments.
    if (c.energy > 0.6) return false;
    
    // At lower energy: ornaments at cadences and expressive moments
    double prob = 0.08 + (0.5 - c.energy) * 0.15;  // MORE ornaments at LOWER energy
    
    if (c.cadence01 > 0.5) prob += 0.12;
    if (c.phraseEndBar) prob += 0.08;
    
    return (hash % 100) < int(prob * 100);
}

RhVoicingGenerator::Ornament RhVoicingGenerator::generateOrnament(
    const Context& c, int targetMidi, quint32 hash) const {
    
    Ornament orn;
    
    int type = hash % 4;
    
    switch (type) {
        case 0:  // Grace note from above
            orn.type = OrnamentType::GraceNote;
            orn.notes.push_back(targetMidi + 2);
            orn.durationsMs.push_back(50);
            orn.velocities.push_back(70);
            orn.mainNoteDelayMs = 50;
            break;
            
        case 1:  // Grace note from below
            orn.type = OrnamentType::GraceNote;
            orn.notes.push_back(targetMidi - 1);
            orn.durationsMs.push_back(50);
            orn.velocities.push_back(70);
            orn.mainNoteDelayMs = 50;
            break;
            
        case 2:  // Mordent (main-upper-main)
            orn.type = OrnamentType::Mordent;
            orn.notes.push_back(targetMidi);
            orn.notes.push_back(targetMidi + 2);
            orn.durationsMs.push_back(40);
            orn.durationsMs.push_back(40);
            orn.velocities.push_back(75);
            orn.velocities.push_back(65);
            orn.mainNoteDelayMs = 80;
            break;
            
        case 3:  // Turn
            orn.type = OrnamentType::Turn;
            orn.notes.push_back(targetMidi + 2);
            orn.notes.push_back(targetMidi);
            orn.notes.push_back(targetMidi - 1);
            orn.durationsMs.push_back(35);
            orn.durationsMs.push_back(35);
            orn.durationsMs.push_back(35);
            orn.velocities.push_back(70);
            orn.velocities.push_back(75);
            orn.velocities.push_back(65);
            orn.mainNoteDelayMs = 105;
            break;
    }
    
    return orn;
}

// =============================================================================
// VOICE LEADING (delegates to utils)
// =============================================================================

double RhVoicingGenerator::voiceLeadingCost(const QVector<int>& prev, const QVector<int>& next) const {
    return voicing_utils::voiceLeadingCost(prev, next);
}

QVector<int> RhVoicingGenerator::realizePcsToMidi(const QVector<int>& pcs, int lo, int hi,
                                                   const QVector<int>& prevVoicing,
                                                   int targetTopMidi) const {
    return voicing_utils::realizePcsToMidi(pcs, lo, hi, prevVoicing, targetTopMidi);
}

int RhVoicingGenerator::selectMelodicTopNote(const QVector<int>& candidatePcs, int lo, int hi,
                                              int lastTopMidi, const Context&) const {
    return voicing_utils::selectMelodicTopNote(candidatePcs, lo, hi, lastTopMidi);
}

// =============================================================================
// STATIC HELPERS
// =============================================================================

int RhVoicingGenerator::pcForDegree(const music::ChordSymbol& c, int degree) {
    return voicing_utils::pcForDegree(c, degree);
}

int RhVoicingGenerator::thirdInterval(music::ChordQuality q) {
    return voicing_utils::thirdInterval(q);
}

int RhVoicingGenerator::fifthInterval(music::ChordQuality q) {
    return voicing_utils::fifthInterval(q);
}

int RhVoicingGenerator::seventhInterval(const music::ChordSymbol& c) {
    return voicing_utils::seventhInterval(c);
}

int RhVoicingGenerator::nearestMidiForPc(int pc, int around, int lo, int hi) {
    return voicing_utils::nearestMidiForPc(pc, around, lo, hi);
}

int RhVoicingGenerator::getDegreeForPc(int pc, const music::ChordSymbol& chord) const {
    return voicing_utils::getDegreeForPc(pc, chord);
}

// =============================================================================
// MICRO-TIMING SYSTEM (Phase 5)
// All offsets in BEATS (BPM-constrained)
// =============================================================================

double RhVoicingGenerator::phraseTimingOffset(const Context& c) const {
    // Map phrase position to timing offset (in beats)
    // - Phrase start: slightly late (+0.02 beats) - settling in
    // - Mid-phrase: on beat (0) - stable groove
    // - Phrase climax: slightly early (-0.015 beats) - forward momentum
    // - Resolution: slightly late (+0.025 beats) - relaxed arrival

    if (c.phraseBars <= 0) return 0.0;

    float progress = float(c.barInPhrase) / float(c.phraseBars);

    if (progress < 0.2) return 0.02;       // Opening: laid back
    if (progress < 0.5) return 0.0;        // Building: on beat
    if (progress < 0.75) return -0.01;     // Approaching climax: push forward
    if (progress < 0.9) return -0.015;     // Near climax: more forward
    return 0.025;                           // Resolution: relax back
}

RhVoicingGenerator::RhVoicing RhVoicingGenerator::applyMicroTiming(
    const RhVoicing& voicing, const Context& c) const {

    RhVoicing result = voicing;

    if (result.midiNotes.isEmpty()) return result;

    // Overall phrase offset
    result.voicingOffset = phraseTimingOffset(c);

    // Per-voice timing (Evans roll effect)
    // Only apply voice spread for Triads and Drop2 (3+ notes)
    // Dyads should strike together
    result.timingOffsets.clear();

    bool applyVoiceSpread = (result.midiNotes.size() >= 3 &&
                             (result.type == VoicingType::Triad ||
                              result.type == VoicingType::Drop2 ||
                              result.type == VoicingType::UST));

    for (int i = 0; i < result.midiNotes.size(); ++i) {
        double offset = 0.0;

        if (applyVoiceSpread) {
            // Evans roll: bottom note on time, each voice slightly later
            // Creates gentle "bloom" rather than block attack
            // Offset: 0.008 beats per voice (roughly 10-15ms at ballad tempo)
            offset = i * 0.008;
        }

        result.timingOffsets.push_back(offset);
    }

    return result;
}

// =============================================================================
// VELOCITY SHADING SYSTEM (Phase 4)
// Evans signature: top voice prominent, inner voices recede
// =============================================================================

int RhVoicingGenerator::phraseVelocityOffset(const Context& c) const {
    // Map phrase position to dynamic arc
    // - Phrase start: soft (-5)
    // - Building: neutral (0)
    // - Approaching climax: louder (+5)
    // - Resolution: soften (-3)

    if (c.phraseBars <= 0) return 0;

    float progress = float(c.barInPhrase) / float(c.phraseBars);

    if (progress < 0.25) return -5;       // Opening: soft
    if (progress < 0.5) return 0;         // Building
    if (progress < 0.75) return 3;        // Approaching climax
    if (progress < 0.9) return 5;         // Near climax
    return -3;                             // Resolution: soften
}

RhVoicingGenerator::RhVoicing RhVoicingGenerator::applyVelocityShading(
    const RhVoicing& voicing, const Context& c) const {

    RhVoicing result = voicing;

    if (result.midiNotes.isEmpty()) return result;

    // Calculate base velocity from energy and phrase position
    // Energy-driven base: 60-85 range
    int energyBase = 60 + int(c.energy * 25);

    // Apply phrase position offset
    int phraseOffset = phraseVelocityOffset(c);
    int baseVel = qBound(50, energyBase + phraseOffset, 95);

    result.baseVelocity = baseVel;
    result.velocities.clear();

    // Voice shading rules (Evans signature):
    // - Top voice: +8 (melody prominence)
    // - Bottom voice: +0 (harmonic anchor)
    // - Inner voices: -5 (recede into texture)

    int topIdx = result.midiNotes.size() - 1;

    for (int i = 0; i < result.midiNotes.size(); ++i) {
        int vel = baseVel;

        if (i == topIdx) {
            // Top voice: prominent
            vel += 8;
        }
        else if (i == 0) {
            // Bottom voice: anchor (no change)
            vel += 0;
        }
        else {
            // Inner voices: recede
            vel -= 5;
        }

        result.velocities.push_back(qBound(30, vel, 127));
    }

    return result;
}

// =============================================================================
// INNER VOICE SHIMMER SYSTEM (Phase 2)
// Evans' signature: inner voices move chromatically while outer anchors
// =============================================================================

bool RhVoicingGenerator::shouldApplyShimmer(const Context& c) const {
    // Shimmer conditions:
    // 1. Same chord for 2+ beats (need time to shimmer)
    // 2. Low-medium energy (shimmer is intimate, not punchy)
    // 3. User not busy (don't distract from soloist)
    // 4. Voicing has at least 3 notes (need inner voice to move)

    if (m_state.beatsOnSameChord < 2) return false;
    if (c.energy > 0.5) return false;
    if (c.userBusy) return false;
    if (m_state.lastRhMidi.size() < 3) return false;

    return true;
}

RhVoicingGenerator::RhVoicing RhVoicingGenerator::applyInnerVoiceShimmer(
    const RhVoicing& base, const Context& c) const {

    // Don't shimmer if conditions aren't met
    if (!shouldApplyShimmer(c)) return base;

    // Need at least 3 notes to have an inner voice
    if (base.midiNotes.size() < 3) return base;

    RhVoicing result = base;

    // Identify voices:
    // - Top voice (melody): NEVER moves - this is the anchor
    // - Bottom voice (bass): rarely moves
    // - Inner voice(s): FREE to move chromatically

    int topIdx = result.midiNotes.size() - 1;
    int bottomIdx = 0;
    int innerIdx = (result.midiNotes.size() >= 3) ? 1 : -1;
    int innerIdx2 = (result.midiNotes.size() >= 4) ? 2 : -1;

    // Shimmer patterns cycle through 4 phases:
    // Phase 0: Base voicing (no change)
    // Phase 1: Inner voice 1 rises half step
    // Phase 2: Inner voice 1 falls back, inner voice 2 rises (if exists)
    // Phase 3: All return to base

    int phase = m_state.shimmerPhase % 4;

    if (innerIdx >= 0 && innerIdx < topIdx) {
        int innerNote = result.midiNotes[innerIdx];
        int topNote = result.midiNotes[topIdx];
        (void)bottomIdx;  // Bottom voice stays anchored

        switch (phase) {
            case 0:
                // Base voicing - no change
                break;

            case 1:
                // Inner voice rises half step (if it doesn't collide with top)
                if (innerNote + 1 < topNote - 1) {
                    result.midiNotes[innerIdx] = innerNote + 1;
                }
                break;

            case 2:
                // Inner voice falls back; if 4+ notes, second inner rises
                if (innerIdx2 >= 0 && innerIdx2 < topIdx) {
                    int inner2Note = result.midiNotes[innerIdx2];
                    if (inner2Note + 1 < topNote - 1) {
                        result.midiNotes[innerIdx2] = inner2Note + 1;
                    }
                }
                break;

            case 3:
                // All return to base (handled by base voicing)
                break;
        }
    }

    // Keep the voicing sorted
    std::sort(result.midiNotes.begin(), result.midiNotes.end());

    // Preserve top note after sort (may have changed position)
    result.topNoteMidi = result.midiNotes.isEmpty() ? base.topNoteMidi : result.midiNotes.last();

    // Mark that this voicing has shimmer applied
    result.ontologyKey = base.ontologyKey + "_shimmer";

    return result;
}

// =============================================================================
// MELODIC DIRECTION SYSTEM (Phase 1)
// =============================================================================

void RhVoicingGenerator::updateMelodicDirection(const Context& c) const {
    // Update targetMelodicDirection based on phrase position
    // This creates the musical arc that Evans voicings follow

    float phraseProgress = (c.phraseBars > 0) ? float(c.barInPhrase) / float(c.phraseBars) : 0.0f;

    if (c.phraseEndBar || c.cadence01 > 0.5) {
        // Phrase end / cadence: descend toward resolution
        m_state.targetMelodicDirection = -1;
    }
    else if (phraseProgress > 0.6) {
        // Approaching climax: ascend
        m_state.targetMelodicDirection = 1;
    }
    else if (phraseProgress < 0.3) {
        // Opening: gentle ascent to build
        m_state.targetMelodicDirection = 1;
    }
    else {
        // Mid-phrase: neutral, let voice-leading decide
        m_state.targetMelodicDirection = 0;
    }

    // Override with explicit hint if provided
    if (c.melodicDirectionHint != 0) {
        m_state.targetMelodicDirection = c.melodicDirectionHint;
    }
}

int RhVoicingGenerator::selectDirectionAwareTop(const QVector<int>& candidatePcs,
                                                  int lo, int hi,
                                                  int lastTopMidi,
                                                  int targetDir,
                                                  int repetitionCount) const {
    // This is the CORE voice-leading function used by ALL voicing methods
    // It balances:
    // 1. Stepwise motion (Evans signature)
    // 2. Melodic direction (phrase arc)
    // 3. Repetition avoidance (force movement after 2+ same notes)

    if (candidatePcs.isEmpty()) return lastTopMidi;

    QVector<std::pair<int, double>> candidates;

    // Repetition penalty: increases with consecutive same-note beats
    // After 2 beats on same note, FORCE movement
    const double repetitionPenalty = (repetitionCount >= 2) ? 10.0 : repetitionCount * 1.5;
    const bool forceMoveFlag = (repetitionCount >= 2);

    for (int pc : candidatePcs) {
        for (int oct = 5; oct <= 7; ++oct) {
            int midi = pc + 12 * oct;
            if (midi < lo || midi > hi) continue;

            double cost = 0.0;
            int motion = qAbs(midi - lastTopMidi);
            int direction = (midi > lastTopMidi) ? 1 : (midi < lastTopMidi) ? -1 : 0;

            // VOICE-LEADING COSTS (stepwise = best)
            if (motion == 0) {
                // Same note: apply repetition penalty
                cost = 0.5 + repetitionPenalty;
                // If force-move flag, make same note very expensive
                if (forceMoveFlag) cost = 50.0;
            }
            else if (motion == 1) cost = 0.0;   // Half step - ideal
            else if (motion == 2) cost = 0.0;   // Whole step - ideal
            else if (motion == 3) cost = 2.0;   // Minor 3rd - acceptable
            else if (motion == 4) cost = 3.0;   // Major 3rd - small leap
            else if (motion <= 7) cost = 8.0;   // 4th-5th - leap
            else cost = 15.0;                   // 6th+ - avoid

            // MELODIC DIRECTION BONUS/PENALTY
            if (targetDir != 0 && direction != 0) {
                if (direction == targetDir) {
                    // Moving in desired direction: bonus
                    cost -= 1.5;
                } else {
                    // Moving against: penalty (but can still happen for good voice-leading)
                    cost += 1.0;
                }
            }

            // REGISTER PREFERENCE (sweet spot around 74-80)
            if (midi >= 72 && midi <= 82) cost -= 0.2;

            candidates.push_back({midi, cost});
        }
    }

    if (candidates.isEmpty()) return lastTopMidi;

    // Find best candidate
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    return candidates.first().first;
}

} // namespace playback
