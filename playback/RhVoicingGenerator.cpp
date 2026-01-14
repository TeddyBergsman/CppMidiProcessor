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
    
    // Realize in RH register
    int targetTop = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 76;
    
    // Stack notes upward from around MIDI 70
    int cursor = 70;
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
    
    // Set top note
    if (!rh.midiNotes.isEmpty()) {
        rh.topNoteMidi = rh.midiNotes.last();
        rh.melodicDirection = (rh.topNoteMidi > m_state.lastRhTopMidi) ? 1 :
                              (rh.topNoteMidi < m_state.lastRhTopMidi) ? -1 : 0;
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
    
    // Close position triad: 3-5-7 or 1-3-5
    QVector<int> pcs;
    int third = pcForDegree(chord, 3);
    int fifth = pcForDegree(chord, 5);
    int seventh = pcForDegree(chord, 7);
    
    if (third >= 0) pcs.push_back(third);
    if (fifth >= 0) pcs.push_back(fifth);
    if (seventh >= 0) pcs.push_back(seventh);
    
    if (pcs.size() < 2) {
        pcs.clear();
        pcs.push_back(chord.rootPc);
        if (third >= 0) pcs.push_back(third);
        if (fifth >= 0) pcs.push_back(fifth);
    }
    
    if (pcs.isEmpty()) return rh;
    
    // Realize near previous top note for voice-leading
    int targetMidi = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 76;
    
    for (int pc : pcs) {
        int midi = nearestMidiForPc(pc, targetMidi - 4, c.rhLo, c.rhHi);
        rh.midiNotes.push_back(midi);
    }
    
    std::sort(rh.midiNotes.begin(), rh.midiNotes.end());
    
    if (!rh.midiNotes.isEmpty()) {
        rh.topNoteMidi = rh.midiNotes.last();
        rh.melodicDirection = (rh.topNoteMidi > m_state.lastRhTopMidi) ? 1 :
                              (rh.topNoteMidi < m_state.lastRhTopMidi) ? -1 : 0;
    }
    
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
    
    // Get color tones for dyad
    QVector<int> colorPcs;
    int third = pcForDegree(chord, 3);
    int fifth = pcForDegree(chord, 5);
    int seventh = pcForDegree(chord, 7);
    int ninth = pcForDegree(chord, 9);
    int thirteenth = pcForDegree(chord, 13);
    
    // Priority: guide tones first
    if (third >= 0) colorPcs.push_back(third);
    if (seventh >= 0) colorPcs.push_back(seventh);
    if (fifth >= 0) colorPcs.push_back(fifth);
    
    // Extensions based on energy (tension derived from energy)
    double tensionLevel = c.energy;  // Direct correlation with energy
    if (tensionLevel > 0.3 && ninth >= 0) colorPcs.push_back(ninth);
    if (tensionLevel > 0.5 && thirteenth >= 0) colorPcs.push_back(thirteenth);
    
    if (colorPcs.isEmpty()) return rh;
    
    // Select top note with stepwise motion preference
    int lastTop = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 74;
    
    QVector<std::pair<int, double>> candidates;
    for (int pc : colorPcs) {
        for (int oct = 5; oct <= 7; ++oct) {
            int midi = pc + 12 * oct;
            if (midi < c.rhLo || midi > c.rhHi) continue;
            
            double cost = 0.0;
            int motion = qAbs(midi - lastTop);
            
            if (motion <= 2) cost = 0.0;
            else if (motion <= 4) cost = 1.0;
            else if (motion <= 7) cost = 3.0;
            else cost = 6.0;
            
            // Prefer guide tones
            if (pc == third || pc == seventh) cost -= 0.8;
            else if ((pc == ninth || pc == thirteenth) && tensionLevel > 0.5) cost -= 0.3;
            
            // Prefer sweet spot
            if (midi >= 72 && midi <= 82) cost -= 0.3;
            
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
    
    // Select second voice with consonance preference
    int secondPc = -1;
    int bestConsonance = 99;
    const bool isDominant = (chord.quality == music::ChordQuality::Dominant);
    
    for (int pc : colorPcs) {
        if (pc == topPc) continue;
        int interval = (topPc - pc + 12) % 12;
        
        int score = 99;
        if (interval == 3 || interval == 4) score = 0;       // 3rds
        else if (interval == 8 || interval == 9) score = 1;  // 6ths
        else if (interval == 5) score = 2;                    // 4th
        else if (interval == 7) score = 3;                    // 5th
        else if ((interval == 10 || interval == 11) && tensionLevel > 0.5) score = 5;
        else if ((interval == 1 || interval == 2) && tensionLevel > 0.7) score = 7;
        else if (interval == 6 && isDominant && tensionLevel > 0.6) score = 6;
        
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
        bool intervalOk = (actualInterval >= 3 && actualInterval <= 9) ||
                          (actualInterval == 10 && tensionLevel > 0.5);
        
        if (intervalOk && secondMidi >= c.rhLo) {
            rh.midiNotes.push_back(secondMidi);
        }
    }
    
    rh.midiNotes.push_back(rh.topNoteMidi);
    std::sort(rh.midiNotes.begin(), rh.midiNotes.end());
    
    // Set ontology key
    if (topPc == ninth || topPc == thirteenth) {
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
    if (ninth >= 0 && c.energy > 0.3) candidatePcs.push_back(ninth);
    
    if (candidatePcs.isEmpty()) candidatePcs.push_back(chord.rootPc);
    
    // Select with stepwise motion
    int lastTop = m_state.lastRhTopMidi > 0 ? m_state.lastRhTopMidi : 74;
    rh.topNoteMidi = selectMelodicTopNote(candidatePcs, c.rhLo, c.rhHi, lastTop, c);
    
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
    if (thirteenth >= 0 && c.energy > 0.4) colorPcs.push_back(thirteenth);
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
    
    // Choose based on energy (tension correlates with energy)
    int idx = 0;
    if (c.energy > 0.7 && triads.size() > 2) {
        idx = 2;  // Most colorful UST at high energy
    } else if (c.energy > 0.4 && triads.size() > 1) {
        idx = 1;  // More color at medium-high energy
    }
    
    return buildUstVoicing(c, triads[idx]);
}

// =============================================================================
// GENERATE BEST
// =============================================================================

RhVoicingGenerator::RhVoicing RhVoicingGenerator::generateBest(const Context& c) const {
    const bool isDominant = (c.chord.quality == music::ChordQuality::Dominant);
    
    // UST for dominants at medium-high energy
    if (isDominant && c.energy > 0.4) {
        return generateUST(c);
    }
    
    // Drop-2 for high energy (always at high energy for full sound)
    if (c.energy > 0.5) {
        return generateDrop2(c);
    }
    
    // Triad for moderate moments
    if (c.energy > 0.4 || c.chordIsNew) {
        return generateTriad(c);
    }
    
    // Dyad for lower energy/ballad texture
    if (c.energy < 0.4) {
        return generateDyad(c);
    }
    
    // Default: dyad
    return generateDyad(c);
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
    if (ninth >= 0 && c.energy > 0.3) candidatePcs.push_back(ninth);
    
    if (candidatePcs.isEmpty()) candidatePcs.push_back(c.chord.rootPc);
    
    return selectMelodicTopNote(candidatePcs, c.rhLo, c.rhHi, m_state.lastRhTopMidi, c);
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

} // namespace playback
