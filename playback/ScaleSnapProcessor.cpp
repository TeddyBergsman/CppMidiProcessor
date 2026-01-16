#include "ScaleSnapProcessor.h"

#include <QDebug>
#include <cmath>
#include <algorithm>

#include "midiprocessor.h"
#include "playback/HarmonyContext.h"
#include "chart/ChartModel.h"
#include "virtuoso/ontology/OntologyRegistry.h"

namespace playback {

ScaleSnapProcessor::ScaleSnapProcessor(QObject* parent)
    : QObject(parent)
{
}

ScaleSnapProcessor::~ScaleSnapProcessor()
{
    reset();
}

void ScaleSnapProcessor::setMidiProcessor(MidiProcessor* midi)
{
    if (m_midi) {
        disconnect(m_midi, nullptr, this, nullptr);
    }
    m_midi = midi;
}

void ScaleSnapProcessor::setHarmonyContext(HarmonyContext* harmony)
{
    m_harmony = harmony;
}

void ScaleSnapProcessor::setOntology(const virtuoso::ontology::OntologyRegistry* ontology)
{
    m_ontology = ontology;
}

void ScaleSnapProcessor::setChartModel(const chart::ChartModel* model)
{
    m_model = model;
    // Reset chord tracking when chart changes
    m_lastKnownChord = music::ChordSymbol{};
    m_hasLastKnownChord = false;
}

void ScaleSnapProcessor::setLeadMode(LeadMode mode)
{
    if (m_leadMode != mode) {
        // Clear active notes when mode changes
        reset();
        m_leadMode = mode;
        emit leadModeChanged(mode);
    }
}

void ScaleSnapProcessor::setHarmonyMode(HarmonyMode mode)
{
    if (m_harmonyMode != mode) {
        // Clear active notes when mode changes
        reset();
        m_harmonyMode = mode;
        emit harmonyModeChanged(mode);
    }
}

void ScaleSnapProcessor::setVocalBendEnabled(bool enabled)
{
    if (m_vocalBendEnabled != enabled) {
        m_vocalBendEnabled = enabled;
        // Reset pitch bend when toggling
        if (!enabled) {
            emitPitchBend(kChannelLead, 8192);
            emitPitchBend(kChannelHarmony, 8192);
        }
        emit vocalBendEnabledChanged(enabled);
    }
}

void ScaleSnapProcessor::setVocalVibratoRangeCents(double cents)
{
    // Clamp to valid range (100 or 200 cents)
    cents = qBound(100.0, cents, 200.0);
    if (m_vocalVibratoRangeCents != cents) {
        m_vocalVibratoRangeCents = cents;
        emit vocalVibratoRangeCentsChanged(cents);
    }
}

void ScaleSnapProcessor::setVibratoCorrectionEnabled(bool enabled)
{
    if (m_vibratoCorrectionEnabled != enabled) {
        m_vibratoCorrectionEnabled = enabled;
        // Reset the tracking state when toggling
        m_voiceCentsAverage = 0.0;
        m_voiceCentsAverageInitialized = false;
        m_settlingCounter = 0;
        m_vibratoFadeInSamples = 0;
        m_oscillationDetected = false;
        m_lastOscillation = 0.0;
        emit vibratoCorrectionEnabledChanged(enabled);
    }
}

void ScaleSnapProcessor::setVoiceSustainEnabled(bool enabled)
{
    if (m_voiceSustainEnabled != enabled) {
        m_voiceSustainEnabled = enabled;
        // Release any currently voice-sustained notes when disabling
        if (!enabled) {
            releaseVoiceSustainedNotes();
        }
        emit voiceSustainEnabledChanged(enabled);
    }
}

void ScaleSnapProcessor::setCurrentCellIndex(int cellIndex)
{
    m_currentCellIndex = cellIndex;
}

void ScaleSnapProcessor::reset()
{
    emitAllNotesOff();
    m_activeNotes.clear();
    m_lastGuitarHz = 0.0;
    m_lastGuitarCents = 0.0;
    m_lastVoiceCents = 0.0;
    m_voiceCentsAverage = 0.0;
    m_voiceCentsAverageInitialized = false;
    m_settlingCounter = 0;
    m_vibratoFadeInSamples = 0;
    m_oscillationDetected = false;
    m_lastOscillation = 0.0;
    m_lastCc2Value = 0;
    // Reset pitch bend to center on both channels
    emitPitchBend(kChannelLead, 8192);
    emitPitchBend(kChannelHarmony, 8192);
}

void ScaleSnapProcessor::onGuitarNoteOn(int midiNote, int velocity)
{
    qDebug() << "ScaleSnap::onGuitarNoteOn - leadMode:" << static_cast<int>(m_leadMode)
             << "harmonyMode:" << static_cast<int>(m_harmonyMode)
             << "midi:" << (m_midi != nullptr) << "note:" << midiNote << "vel:" << velocity;

    // Both modes off means nothing to do
    if (m_leadMode == LeadMode::Off && m_harmonyMode == HarmonyMode::Off) {
        qDebug() << "ScaleSnap: Exiting early - both modes are Off";
        return;
    }

    if (!m_midi) {
        qDebug() << "ScaleSnap: Exiting early - no midi processor";
        return;
    }

    // Release any voice-sustained notes before starting a new note
    // This ensures clean transitions when playing a new note while singing
    releaseVoiceSustainedNotes();

    qDebug() << "ScaleSnap: cellIndex=" << m_currentCellIndex
             << "hasChord=" << m_hasLastKnownChord
             << "harmony=" << (m_harmony != nullptr)
             << "ontology=" << (m_ontology != nullptr)
             << "model=" << (m_model != nullptr);

    // Compute valid pitch classes from current chord/scale
    const QSet<int> validPcs = computeValidPitchClasses();
    qDebug() << "ScaleSnap: validPcs size=" << validPcs.size() << "pcs=" << validPcs;

    ActiveNote active;
    active.originalNote = midiNote;

    // Reset pitch bend before new note (unless vocal bend will control it)
    if (!m_vocalBendEnabled) {
        if (m_leadMode != LeadMode::Off) {
            emitPitchBend(kChannelLead, 8192);
        }
        if (m_harmonyMode != HarmonyMode::Off) {
            emitPitchBend(kChannelHarmony, 8192);
        }
    }

    // === LEAD MODE (Channel 12) ===
    if (m_leadMode != LeadMode::Off) {
        if (m_leadMode == LeadMode::Original) {
            // Original mode: pass through unchanged
            active.snappedNote = midiNote;
            active.referenceHz = midiNoteToHz(midiNote);
            emitNoteOn(kChannelLead, midiNote, velocity);
        } else if (m_leadMode == LeadMode::Constrained) {
            // Constrained mode: snap to nearest scale/chord tone
            if (validPcs.isEmpty()) {
                // No chord/scale info - pass through unchanged
                active.snappedNote = midiNote;
                active.referenceHz = midiNoteToHz(midiNote);
                emitNoteOn(kChannelLead, midiNote, velocity);
            } else {
                const int inputPc = normalizePc(midiNote);
                const int inputOctave = noteToOctave(midiNote);
                const int snappedPc = snapToNearestValidPc(inputPc, validPcs);
                int snappedNote = pcToMidiNote(snappedPc, inputOctave);

                // Handle octave boundary crossing - keep snapped note close to original
                if (snappedNote - midiNote > 6) {
                    snappedNote -= 12;
                } else if (midiNote - snappedNote > 6) {
                    snappedNote += 12;
                }
                snappedNote = qBound(0, snappedNote, 127);

                qDebug() << "ScaleSnap: LEAD INPUT" << midiNote << "-> OUTPUT" << snappedNote;

                active.snappedNote = snappedNote;
                active.referenceHz = midiNoteToHz(snappedNote);
                emitNoteOn(kChannelLead, snappedNote, velocity);
            }
        }
    } else {
        // Lead mode off - still set reference Hz for potential harmony notes
        active.snappedNote = midiNote;
        active.referenceHz = midiNoteToHz(midiNote);
    }

    // === HARMONY MODE (Channel 11) ===
    if (m_harmonyMode != HarmonyMode::Off) {
        const QSet<int> chordTones = computeChordTones(m_lastKnownChord);
        qDebug() << "ScaleSnap Harmony: chordTones=" << chordTones << "validPcs=" << validPcs;

        // Generate harmony based on original input note
        if (validPcs.isEmpty()) {
            // No chord/scale info - default major 3rd
            active.harmonyNote = midiNote + 4;
        } else {
            active.harmonyNote = generateHarmonyNote(midiNote, chordTones, validPcs);
        }

        qDebug() << "ScaleSnap Harmony: INPUT" << midiNote << "-> HARMONY" << active.harmonyNote;

        if (active.harmonyNote >= 0 && active.harmonyNote <= 127) {
            emitNoteOn(kChannelHarmony, active.harmonyNote, velocity);
        }
    }

    m_activeNotes.insert(midiNote, active);
}

void ScaleSnapProcessor::onGuitarNoteOff(int midiNote)
{
    // Both modes off means nothing to do
    if (m_leadMode == LeadMode::Off && m_harmonyMode == HarmonyMode::Off) {
        return;
    }

    if (!m_midi) {
        return;
    }

    auto it = m_activeNotes.find(midiNote);
    if (it == m_activeNotes.end()) {
        return;
    }

    // Voice sustain: if enabled and singing (CC2 > threshold), mark as sustained instead of releasing
    if (m_voiceSustainEnabled && m_lastCc2Value > kVoiceSustainCc2Threshold) {
        // Mark note as voice-sustained instead of releasing
        it.value().voiceSustained = true;
        qDebug() << "ScaleSnap: Voice sustaining note" << midiNote << "CC2=" << m_lastCc2Value;
        return;
    }

    // Release the note immediately
    releaseNote(it.value());
    m_activeNotes.erase(it);

    // Reset state when no notes are active
    if (m_activeNotes.isEmpty()) {
        m_lastGuitarHz = 0.0;
        m_lastGuitarCents = 0.0;
        m_lastVoiceCents = 0.0;
        m_voiceCentsAverage = 0.0;
        m_voiceCentsAverageInitialized = false;
        m_settlingCounter = 0;
        m_vibratoFadeInSamples = 0;
        m_oscillationDetected = false;
        m_lastOscillation = 0.0;
        if (m_leadMode != LeadMode::Off) {
            emitPitchBend(kChannelLead, 8192);
        }
        if (m_harmonyMode != HarmonyMode::Off) {
            emitPitchBend(kChannelHarmony, 8192);
        }
    }
}

void ScaleSnapProcessor::onGuitarHzUpdated(double hz)
{
    // Only track/forward guitar pitch bend in Original lead mode
    if (m_leadMode != LeadMode::Original || !m_midi || m_activeNotes.isEmpty() || hz <= 0.0) {
        return;
    }

    m_lastGuitarHz = hz;

    const ActiveNote& note = m_activeNotes.begin().value();
    if (note.referenceHz <= 0.0) {
        return;
    }

    // Calculate cents deviation from the original note's reference frequency
    m_lastGuitarCents = hzToCents(hz, note.referenceHz);

    // If vocal bend is enabled, let onVoiceHzUpdated handle the combined output
    // Otherwise, output just the guitar bend
    if (m_vocalBendEnabled) {
        // Combine guitar + voice cents and emit
        const double combinedCents = m_lastGuitarCents + m_lastVoiceCents;
        // Clamp combined to reasonable range (±200 cents = ±2 semitones)
        const double clampedCents = qBound(-200.0, combinedCents, 200.0);

        int bendValue = 8192 + static_cast<int>((clampedCents / 200.0) * 8192.0);
        bendValue = qBound(0, bendValue, 16383);

        // Apply to lead channel (and harmony channel if harmony is active)
        emitPitchBend(kChannelLead, bendValue);
        if (m_harmonyMode != HarmonyMode::Off) {
            emitPitchBend(kChannelHarmony, bendValue);
        }
    } else {
        // Solo guitar bend mode
        // Convert to MIDI pitch bend (14-bit, 0-16383, centered at 8192)
        // Assuming standard ±2 semitone (±200 cents) bend range
        const double bendRange = 200.0; // cents
        int bendValue = 8192 + static_cast<int>((m_lastGuitarCents / bendRange) * 8192.0);
        bendValue = qBound(0, bendValue, 16383);

        emitPitchBend(kChannelLead, bendValue);
        if (m_harmonyMode != HarmonyMode::Off) {
            emitPitchBend(kChannelHarmony, bendValue);
        }
    }
}

void ScaleSnapProcessor::onVoiceCc2Updated(int value)
{
    // Track CC2 value for voice sustain feature
    const int previousCc2 = m_lastCc2Value;
    m_lastCc2Value = value;

    // Both modes off means nothing to do
    if (m_leadMode == LeadMode::Off && m_harmonyMode == HarmonyMode::Off) {
        return;
    }

    if (!m_midi) {
        return;
    }

    // Voice sustain: release sustained notes when CC2 drops below threshold
    if (m_voiceSustainEnabled && previousCc2 > kVoiceSustainCc2Threshold && value <= kVoiceSustainCc2Threshold) {
        qDebug() << "ScaleSnap: CC2 dropped below threshold, releasing voice-sustained notes";
        releaseVoiceSustainedNotes();
    }

    // Forward CC2 (breath control) to active channels
    if (m_leadMode != LeadMode::Off) {
        emitCC(kChannelLead, 2, value);
    }
    if (m_harmonyMode != HarmonyMode::Off) {
        emitCC(kChannelHarmony, 2, value);
    }
}

void ScaleSnapProcessor::onVoiceHzUpdated(double hz)
{
    // Only active when vocal bend is enabled, at least one mode is on, and there are active notes
    const bool bothModesOff = (m_leadMode == LeadMode::Off && m_harmonyMode == HarmonyMode::Off);
    if (!m_vocalBendEnabled || bothModesOff || !m_midi || m_activeNotes.isEmpty() || hz <= 0.0) {
        return;
    }

    // Get the reference Hz of the snapped note (what we're bending around)
    const ActiveNote& note = m_activeNotes.begin().value();
    if (note.referenceHz <= 0.0) {
        return;
    }

    // Calculate cents deviation: how far is the voice from the snapped note?
    // Positive = voice is sharp, negative = voice is flat
    double rawVoiceCents = 1200.0 * std::log2(hz / note.referenceHz);

    // Clamp raw voice cents to configurable range before processing
    rawVoiceCents = qBound(-m_vocalVibratoRangeCents, rawVoiceCents, m_vocalVibratoRangeCents);

    double voiceCents = rawVoiceCents;

    // Vibrato correction: filter out DC offset, keep only the oscillation
    // Algorithm:
    // 1. Settling period (~300ms): Track average but output zero bend
    // 2. Detect oscillation: Look for zero-crossings with sufficient amplitude
    // 3. Fade-in (~500ms): Once oscillation detected, gradually ramp up vibrato
    if (m_vibratoCorrectionEnabled) {
        // On first voice sample after note attack, initialize average to current pitch
        if (!m_voiceCentsAverageInitialized) {
            m_voiceCentsAverage = rawVoiceCents;
            m_voiceCentsAverageInitialized = true;
            m_settlingCounter = 0;
            m_vibratoFadeInSamples = 0;
            m_oscillationDetected = false;
            m_lastOscillation = 0.0;
        }

        // Update exponential moving average (tracks the "center" of the voice pitch)
        m_voiceCentsAverage = kVibratoCorrectionAlpha * rawVoiceCents + (1.0 - kVibratoCorrectionAlpha) * m_voiceCentsAverage;

        // Subtract the average to get just the oscillation (AC component)
        double oscillation = rawVoiceCents - m_voiceCentsAverage;

        // During settling period: output zero bend, just track the average
        if (m_settlingCounter < kSettlingDuration) {
            ++m_settlingCounter;
            voiceCents = 0.0;
        } else {
            // After settling: detect oscillation via zero-crossing with threshold
            if (!m_oscillationDetected) {
                // Check for zero-crossing with sufficient amplitude
                // (sign change AND both values exceed threshold)
                bool signChange = (m_lastOscillation > 0.0 && oscillation < 0.0) ||
                                  (m_lastOscillation < 0.0 && oscillation > 0.0);
                bool sufficientAmplitude = std::abs(m_lastOscillation) > kOscillationThreshold ||
                                           std::abs(oscillation) > kOscillationThreshold;

                if (signChange && sufficientAmplitude) {
                    m_oscillationDetected = true;
                    m_vibratoFadeInSamples = 0;
                }
            }

            m_lastOscillation = oscillation;

            // Only apply vibrato if oscillation has been detected
            if (m_oscillationDetected) {
                // Fade-in: vibrato ramps up from 0 over kVibratoFadeInDuration samples
                if (m_vibratoFadeInSamples < kVibratoFadeInDuration) {
                    ++m_vibratoFadeInSamples;
                }
                const double fadeGain = static_cast<double>(m_vibratoFadeInSamples) / kVibratoFadeInDuration;
                voiceCents = oscillation * fadeGain;
            } else {
                // No oscillation detected yet - output zero bend
                voiceCents = 0.0;
            }
        }
    }

    m_lastVoiceCents = voiceCents;

    // In Original lead mode, combine guitar bend + voice vibrato
    // In other modes, just use voice cents
    double totalCents = voiceCents;
    if (m_leadMode == LeadMode::Original) {
        totalCents = m_lastGuitarCents + voiceCents;
    }

    // Clamp combined to reasonable range (±200 cents = ±2 semitones)
    totalCents = qBound(-200.0, totalCents, 200.0);

    // Convert to 14-bit MIDI pitch bend
    // Assuming synth has standard ±2 semitone (±200 cents) bend range
    int bendValue = 8192 + static_cast<int>((totalCents / 200.0) * 8192.0);
    bendValue = qBound(0, bendValue, 16383);

    qDebug() << "ScaleSnap VoiceHz: voiceHz=" << hz << "refHz=" << note.referenceHz
             << "rawCents=" << rawVoiceCents << "oscillation=" << (rawVoiceCents - m_voiceCentsAverage)
             << "settling=" << m_settlingCounter << "/" << kSettlingDuration
             << "oscDetected=" << m_oscillationDetected
             << "fadeGain=" << (m_oscillationDetected ? static_cast<double>(m_vibratoFadeInSamples) / kVibratoFadeInDuration : 0.0)
             << "voiceCents=" << voiceCents << "bendValue=" << bendValue;

    // Apply pitch bend to all active output channels
    if (m_leadMode != LeadMode::Off) {
        emitPitchBend(kChannelLead, bendValue);
    }
    if (m_harmonyMode != HarmonyMode::Off) {
        emitPitchBend(kChannelHarmony, bendValue);
    }
}

QSet<int> ScaleSnapProcessor::computeValidPitchClasses() const
{
    QSet<int> validPcs;

    if (!m_harmony || !m_ontology || !m_model || m_currentCellIndex < 0) {
        return validPcs;
    }

    // Try to get chord for current cell
    music::ChordSymbol chord;
    bool isExplicit = false;
    chord = m_harmony->parseCellChordNoState(
        *m_model,
        m_currentCellIndex,
        music::ChordSymbol{},
        &isExplicit
    );

    // If current cell has explicit chord, update our tracking
    if (isExplicit && chord.rootPc >= 0 && !chord.noChord && !chord.placeholder) {
        // Use this chord
        const_cast<ScaleSnapProcessor*>(this)->m_lastKnownChord = chord;
        const_cast<ScaleSnapProcessor*>(this)->m_hasLastKnownChord = true;
    } else if (m_hasLastKnownChord && m_lastKnownChord.rootPc >= 0) {
        // Use last known chord
        chord = m_lastKnownChord;
    } else {
        // Scan backward to find most recent chord
        for (int i = m_currentCellIndex - 1; i >= 0; --i) {
            music::ChordSymbol prevChord = m_harmony->parseCellChordNoState(
                *m_model, i, music::ChordSymbol{}, &isExplicit
            );
            if (isExplicit && prevChord.rootPc >= 0 && !prevChord.noChord && !prevChord.placeholder) {
                chord = prevChord;
                const_cast<ScaleSnapProcessor*>(this)->m_lastKnownChord = chord;
                const_cast<ScaleSnapProcessor*>(this)->m_hasLastKnownChord = true;
                break;
            }
        }
    }

    // If still no chord, return empty (will pass through)
    if (chord.rootPc < 0 || chord.noChord || chord.placeholder) {
        return validPcs;
    }

    // Get chord tones (always valid)
    const QSet<int> chordTones = computeChordTones(chord);
    validPcs.unite(chordTones);

    // Get key scale tones from dynamic key detection
    const QSet<int> keyTones = computeKeyScaleTones();
    validPcs.unite(keyTones);

    // Smart avoid notes filter: only remove the most problematic clashes
    // The main rule: the natural 4th is an avoid note on chords with a major 3rd
    // (it creates a minor 2nd above the 3rd, which sounds harsh)

    QSet<int> avoidPcs;

    // Find the 3rd of the chord (if present)
    const int root = chord.rootPc;
    const int major3rd = normalizePc(root + 4);  // Major 3rd
    const int minor3rd = normalizePc(root + 3);  // Minor 3rd

    // Check if chord has a major 3rd
    bool hasMajor3rd = chordTones.contains(major3rd);
    bool hasMinor3rd = chordTones.contains(minor3rd);

    if (hasMajor3rd && !hasMinor3rd) {
        // Chord has major 3rd - the natural 4th (semitone above) is an avoid note
        int natural4th = normalizePc(root + 5);
        // Only avoid if it's not already a chord tone (e.g., sus4 chords)
        if (!chordTones.contains(natural4th)) {
            avoidPcs.insert(natural4th);
        }
    }

    // Filter out avoid notes
    QSet<int> safePcs;
    for (int pc : validPcs) {
        if (!avoidPcs.contains(pc)) {
            safePcs.insert(pc);
        }
    }

    qDebug() << "ScaleSnap: chordTones=" << chordTones << "keyTones=" << keyTones
             << "avoidPcs=" << avoidPcs << "safePcs=" << safePcs;

    return safePcs;
}

QSet<int> ScaleSnapProcessor::computeChordTones(const music::ChordSymbol& chord) const
{
    QSet<int> chordTones;

    if (!m_harmony || chord.rootPc < 0) {
        return chordTones;
    }

    const auto* chordDef = m_harmony->chordDefForSymbol(chord);
    if (!chordDef) {
        // Fallback: just the root
        chordTones.insert(normalizePc(chord.rootPc));
        return chordTones;
    }

    // Add root and all intervals
    const int root = chord.rootPc;
    chordTones.insert(normalizePc(root));
    for (int interval : chordDef->intervals) {
        chordTones.insert(normalizePc(root + interval));
    }

    return chordTones;
}

QSet<int> ScaleSnapProcessor::computeKeyScaleTones() const
{
    QSet<int> keyTones;

    if (!m_harmony || !m_ontology || !m_model || m_currentCellIndex < 0) {
        return keyTones;
    }

    // Get local key estimate for current bar (dynamic key detection)
    const int barIndex = m_currentCellIndex / 4;
    const auto& localKeys = m_harmony->localKeysByBar();

    int keyPc = 0;
    QString scaleKey;

    if (barIndex >= 0 && barIndex < localKeys.size()) {
        // Use local key estimate for this bar
        const auto& localKey = localKeys[barIndex];
        keyPc = localKey.tonicPc;
        scaleKey = localKey.scaleKey;
    } else if (m_harmony->hasKeyPcGuess()) {
        // Fall back to global key
        keyPc = m_harmony->keyPcGuess();
        scaleKey = m_harmony->keyScaleKey();
    } else {
        // No key info available
        return keyTones;
    }

    // Look up scale definition
    const auto* scaleDef = m_ontology->scale(scaleKey);
    if (!scaleDef) {
        // Fallback to major scale if scale key not found
        scaleDef = m_ontology->scale("major");
        if (!scaleDef) {
            return keyTones;
        }
    }

    // Build pitch class set from key scale intervals
    for (int interval : scaleDef->intervals) {
        keyTones.insert(normalizePc(keyPc + interval));
    }

    return keyTones;
}

int ScaleSnapProcessor::snapToNearestValidPc(int inputPc, const QSet<int>& validPcs) const
{
    if (validPcs.contains(inputPc)) {
        return inputPc;
    }

    if (validPcs.isEmpty()) {
        return inputPc;
    }

    int bestPc = inputPc;
    int minDistance = 12;

    for (int pc : validPcs) {
        // Compute circular distance on pitch class circle (0-11)
        int dist = std::abs(pc - inputPc);
        if (dist > 6) {
            dist = 12 - dist;
        }

        if (dist < minDistance) {
            minDistance = dist;
            bestPc = pc;
        } else if (dist == minDistance && pc < bestPc) {
            // Tie-break: prefer lower pitch class (closer to root in many cases)
            bestPc = pc;
        }
    }

    return bestPc;
}

int ScaleSnapProcessor::generateHarmonyNote(int inputNote, const QSet<int>& chordTones, const QSet<int>& scaleTones) const
{
    // Strategy: Find a chord tone close to the input note (within 3rd-5th range)
    // Keep harmony tight - prefer minor/major 3rds, avoid large jumps

    const int inputPc = normalizePc(inputNote);

    // Preferred intervals in order: minor 3rd up, major 3rd up, perfect 4th up,
    // perfect 5th up, minor 3rd down, major 3rd down
    // These are the most musical harmony intervals
    static const int kPreferredIntervals[] = {3, 4, 5, 7, -3, -4, -5};

    // First pass: look for chord tones at preferred intervals
    for (int interval : kPreferredIntervals) {
        int harmonyPc = normalizePc(inputPc + interval);
        if (chordTones.contains(harmonyPc)) {
            int harmonyNote = inputNote + interval;
            qDebug() << "ScaleSnap Harmony: found chord tone at interval" << interval
                     << "harmonyNote=" << harmonyNote;
            return qBound(0, harmonyNote, 127);
        }
    }

    // Second pass: accept scale tones at preferred intervals
    for (int interval : kPreferredIntervals) {
        int harmonyPc = normalizePc(inputPc + interval);
        if (scaleTones.contains(harmonyPc)) {
            int harmonyNote = inputNote + interval;
            qDebug() << "ScaleSnap Harmony: found scale tone at interval" << interval
                     << "harmonyNote=" << harmonyNote;
            return qBound(0, harmonyNote, 127);
        }
    }

    // Fallback: find nearest chord tone that isn't unison
    if (!chordTones.isEmpty()) {
        int bestInterval = 4;  // default major 3rd
        int minDistance = 12;

        for (int pc : chordTones) {
            if (pc == inputPc) continue;  // Skip unison

            // Compute interval (prefer going up slightly)
            int upInterval = (pc - inputPc + 12) % 12;
            int downInterval = (inputPc - pc + 12) % 12;

            if (upInterval <= 7 && upInterval < minDistance) {
                minDistance = upInterval;
                bestInterval = upInterval;
            }
            if (downInterval <= 5 && downInterval < minDistance) {
                minDistance = downInterval;
                bestInterval = -downInterval;
            }
        }

        int harmonyNote = inputNote + bestInterval;
        qDebug() << "ScaleSnap Harmony: fallback nearest chord tone, interval=" << bestInterval
                 << "harmonyNote=" << harmonyNote;
        return qBound(0, harmonyNote, 127);
    }

    // Last resort: major 3rd above
    qDebug() << "ScaleSnap Harmony: last resort major 3rd above";
    return qBound(0, inputNote + 4, 127);
}

void ScaleSnapProcessor::emitNoteOn(int channel, int note, int velocity)
{
    if (m_midi && note >= 0 && note <= 127) {
        m_midi->sendVirtualNoteOn(channel, note, velocity);
    }
}

void ScaleSnapProcessor::emitNoteOff(int channel, int note)
{
    if (m_midi && note >= 0 && note <= 127) {
        m_midi->sendVirtualNoteOff(channel, note);
    }
}

void ScaleSnapProcessor::emitPitchBend(int channel, int bendValue)
{
    if (m_midi) {
        m_midi->sendVirtualPitchBend(channel, bendValue);
    }
}

void ScaleSnapProcessor::emitCC(int channel, int cc, int value)
{
    if (m_midi) {
        m_midi->sendVirtualCC(channel, cc, value);
    }
}

void ScaleSnapProcessor::emitAllNotesOff()
{
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        releaseNote(it.value());
    }
}

void ScaleSnapProcessor::releaseNote(const ActiveNote& note)
{
    // Release lead note on channel 12
    if (m_leadMode != LeadMode::Off) {
        emitNoteOff(kChannelLead, note.snappedNote);
    }

    // Release harmony note on channel 11
    if (m_harmonyMode != HarmonyMode::Off && note.harmonyNote >= 0) {
        emitNoteOff(kChannelHarmony, note.harmonyNote);
    }
}

void ScaleSnapProcessor::releaseVoiceSustainedNotes()
{
    // Release all notes that are being held by voice sustain
    QList<int> toRemove;
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        if (it.value().voiceSustained) {
            releaseNote(it.value());
            toRemove.append(it.key());
        }
    }
    for (int key : toRemove) {
        m_activeNotes.remove(key);
    }

    // Reset state when no notes are active
    if (m_activeNotes.isEmpty()) {
        m_lastGuitarHz = 0.0;
        m_lastGuitarCents = 0.0;
        m_lastVoiceCents = 0.0;
        m_voiceCentsAverage = 0.0;
        m_voiceCentsAverageInitialized = false;
        m_settlingCounter = 0;
        m_vibratoFadeInSamples = 0;
        m_oscillationDetected = false;
        m_lastOscillation = 0.0;
        if (m_leadMode != LeadMode::Off) {
            emitPitchBend(kChannelLead, 8192);
        }
        if (m_harmonyMode != HarmonyMode::Off) {
            emitPitchBend(kChannelHarmony, 8192);
        }
    }
}

int ScaleSnapProcessor::normalizePc(int pc)
{
    pc = pc % 12;
    if (pc < 0) pc += 12;
    return pc;
}

int ScaleSnapProcessor::noteToOctave(int midiNote)
{
    return midiNote / 12;
}

int ScaleSnapProcessor::pcToMidiNote(int pc, int targetOctave)
{
    return targetOctave * 12 + pc;
}

double ScaleSnapProcessor::midiNoteToHz(int midiNote)
{
    // A4 = MIDI 69 = 440 Hz
    return 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
}

double ScaleSnapProcessor::hzToCents(double hz, double referenceHz)
{
    if (referenceHz <= 0.0 || hz <= 0.0) {
        return 0.0;
    }
    return 1200.0 * std::log2(hz / referenceHz);
}

} // namespace playback
