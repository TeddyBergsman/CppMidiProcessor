#include "ScaleSnapProcessor.h"

#include <QDebug>
#include <QDateTime>
#include <cmath>
#include <algorithm>

#include "midiprocessor.h"
#include "playback/HarmonyContext.h"
#include "chart/ChartModel.h"
#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/theory/ScaleSuggester.h"

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

        // When Lead mode is active (not Off), suppress raw guitar passthrough in MidiProcessor
        // so that we can output processed notes (with vocal bend, sustain, conformance) on channel 1.
        if (m_midi) {
            m_midi->setSuppressGuitarPassthrough(mode != LeadMode::Off);
        }

        emit leadModeChanged(mode);
    }
}

void ScaleSnapProcessor::setHarmonyModeCompat(HarmonyModeCompat mode)
{
    if (m_harmonyModeCompat != mode) {
        // Clear active notes when mode changes
        reset();
        m_harmonyModeCompat = mode;

        // Map compat mode to new HarmonyMode
        HarmonyMode newMode = HarmonyMode::OFF;
        switch (mode) {
            case HarmonyModeCompat::Off:
                newMode = HarmonyMode::OFF;
                break;
            case HarmonyModeCompat::SmartThirds:
                // SmartThirds is parallel motion
                newMode = HarmonyMode::SINGLE;
                m_harmonyConfig.singleType = HarmonyType::PARALLEL;
                break;
            case HarmonyModeCompat::Contrary:
                // Contrary motion
                newMode = HarmonyMode::SINGLE;
                m_harmonyConfig.singleType = HarmonyType::CONTRARY;
                break;
            case HarmonyModeCompat::Similar:
                // Similar motion - same direction, different intervals
                newMode = HarmonyMode::SINGLE;
                m_harmonyConfig.singleType = HarmonyType::SIMILAR;
                break;
            case HarmonyModeCompat::Single:
                newMode = HarmonyMode::SINGLE;
                break;
            case HarmonyModeCompat::PrePlanned:
                newMode = HarmonyMode::PRE_PLANNED;
                break;
            case HarmonyModeCompat::Voice:
                newMode = HarmonyMode::VOICE;
                break;
        }

        if (m_harmonyMode != newMode) {
            m_harmonyMode = newMode;
            m_harmonyConfig.mode = newMode;
            emit harmonyModeChanged(newMode);
        }
    }
}

void ScaleSnapProcessor::setHarmonyConfig(const HarmonyConfig& config)
{
    m_harmonyConfig = config;
    if (m_harmonyMode != config.mode) {
        reset();
        m_harmonyMode = config.mode;
        emit harmonyModeChanged(config.mode);
    }
}

void ScaleSnapProcessor::setHarmonyType(HarmonyType type)
{
    m_harmonyConfig.singleType = type;
}

void ScaleSnapProcessor::setHarmonyVoiceCount(int count)
{
    m_harmonyConfig.voiceCount = qBound(1, count, 4);
}

void ScaleSnapProcessor::setLeadGravityMultiplier(float multiplier)
{
    m_leadConfig.gravityMultiplier = qBound(0.0f, multiplier, 2.0f);
    m_conformanceEngine.setGravityMultiplier(m_leadConfig.gravityMultiplier);
}

void ScaleSnapProcessor::setVocalBendEnabled(bool enabled)
{
    if (m_vocalBendEnabled != enabled) {
        m_vocalBendEnabled = enabled;
        // Reset pitch bend when toggling
        if (!enabled) {
            emitPitchBend(kChannelLead, 8192);
            emitPitchBend(kChannelHarmony1, 8192);
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

void ScaleSnapProcessor::setHarmonyRange(int minNote, int maxNote)
{
    // Validate and clamp to MIDI range
    minNote = qBound(0, minNote, 127);
    maxNote = qBound(0, maxNote, 127);

    // Ensure min <= max
    if (minNote > maxNote) {
        std::swap(minNote, maxNote);
    }

    m_harmonyRangeMin = minNote;
    m_harmonyRangeMax = maxNote;

    qDebug() << "ScaleSnap: Harmony range set to" << minNote << "-" << maxNote;
}

void ScaleSnapProcessor::setCurrentCellIndex(int cellIndex)
{
    if (m_currentCellIndex == cellIndex) {
        return;  // No change
    }

    const int previousCellIndex = m_currentCellIndex;
    m_currentCellIndex = cellIndex;

    // Check if chord changed and re-conform any active notes
    // This applies to BOTH lead conformance AND harmony - harmony notes need
    // to be re-validated when chords change, regardless of lead mode!
    bool needsReconform = !m_activeNotes.isEmpty() &&
                          (m_leadMode == LeadMode::Conformed || m_harmonyMode != HarmonyMode::OFF);

    if (needsReconform) {
        checkAndReconformOnChordChange(previousCellIndex);
    }
}

void ScaleSnapProcessor::setBeatPosition(float beatPosition)
{
    m_beatPosition = beatPosition;
}

void ScaleSnapProcessor::updateConformance(float deltaMs)
{
    if (m_leadMode != LeadMode::Conformed || m_activeNotes.isEmpty()) {
        return;
    }

    bool needsBendUpdate = false;

    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        ActiveNote& note = it.value();

        // Handle delayed notes
        if (note.isDelayed) {
            note.delayRemainingMs -= deltaMs;
            if (note.delayRemainingMs <= 0.0f) {
                // Delay complete - emit the note now
                note.isDelayed = false;
                emitNoteOn(kChannelLead, note.snappedNote, note.delayedVelocity);
                qDebug() << "ScaleSnap: Delayed note" << note.snappedNote << "now playing after delay";
            }
        }

        // Handle TIMED_SNAP - note held too long, snap to chord tone
        if (note.isTimedSnap) {
            note.timedSnapRemainingMs -= deltaMs;
            if (note.timedSnapRemainingMs <= 0.0f) {
                // Time's up! Snap to the target note
                note.isTimedSnap = false;
                int oldNote = note.snappedNote;
                int newNote = note.timedSnapTarget;

                // If target is different, do the swap
                if (oldNote != newNote) {
                    emitNoteOff(kChannelLead, oldNote);
                    emitNoteOn(kChannelLead, newNote, note.velocity);
                    note.snappedNote = newNote;
                    note.referenceHz = midiNoteToHz(newNote);
                    qDebug() << "ScaleSnap: TIMED_SNAP triggered - snapped" << oldNote << "->" << newNote;
                }
            }
        }

        // Handle TIMED_BEND - smoothly bend to target over duration
        if (note.isTimedBend) {
            note.timedBendElapsedMs += deltaMs;
            float progress = note.timedBendElapsedMs / note.timedBendDurationMs;
            if (progress >= 1.0f) {
                progress = 1.0f;
                note.isTimedBend = false;  // Bend complete
            }
            // Linear interpolation from 0 to target
            note.conformanceBendCurrent = progress * note.timedBendTargetCents;
            needsBendUpdate = true;
        }

        // Handle bend interpolation for BEND behavior
        if (note.behavior == ConformanceBehavior::BEND && !note.isDelayed) {
            float diff = note.conformanceBendTarget - note.conformanceBendCurrent;
            if (std::abs(diff) > 0.5f) {  // More than 0.5 cents difference
                float maxChange = kConformanceBendRatePerMs * deltaMs;
                if (std::abs(diff) <= maxChange) {
                    note.conformanceBendCurrent = note.conformanceBendTarget;
                } else {
                    note.conformanceBendCurrent += (diff > 0 ? maxChange : -maxChange);
                }
                needsBendUpdate = true;
            }
        }
    }

    // If conformance bend changed, we need to update the pitch bend output
    if (needsBendUpdate && !m_activeNotes.isEmpty()) {
        // Get the first active note's conformance bend
        const ActiveNote& note = m_activeNotes.begin().value();

        // Always apply the bend if we have an active conformance bend
        // Pitch bend range is 2 semitones = 200 cents, so divide by 200
        // bendValue: 0 = -200 cents, 8192 = center, 16383 = +200 cents
        int bendValue = 8192 + static_cast<int>((note.conformanceBendCurrent / 200.0) * 8192.0);
        bendValue = qBound(0, bendValue, 16383);
        emitPitchBend(kChannelLead, bendValue);

        qDebug() << "ScaleSnap: Applying bend" << note.conformanceBendCurrent
                 << "cents, MIDI value:" << bendValue;
    }
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
    // Reset fast playing and machine-gun prevention tracking
    m_lastNoteOnTimestamp = 0;
    m_currentlyPlayingNote = -1;
    m_currentNoteWasSnapped = false;
    // Reset chromatic sweep detection
    m_recentIntervals.fill(0);
    m_recentIntervalsIndex = 0;
    m_lastInputNote = -1;
    // Reset lead melody direction tracking
    m_lastHarmonyLeadNote = -1;
    m_leadMelodyDirection = 0;
    m_lastHarmonyOutputNote = -1;
    m_lastGuitarNoteOffTimestamp = 0;  // Reset phrase tracking
    m_guitarNotesHeld = 0;
    // Reset pitch bend to center on all channels
    emitPitchBend(kChannelLead, 8192);
    emitPitchBend(kChannelHarmony1, 8192);
    emitPitchBend(kChannelHarmony2, 8192);
    emitPitchBend(kChannelHarmony3, 8192);
    emitPitchBend(kChannelHarmony4, 8192);
}

void ScaleSnapProcessor::onGuitarNoteOn(int midiNote, int velocity)
{
    qDebug() << "ScaleSnap::onGuitarNoteOn - leadMode:" << static_cast<int>(m_leadMode)
             << "harmonyMode:" << static_cast<int>(m_harmonyMode)
             << "midi:" << (m_midi != nullptr) << "note:" << midiNote << "vel:" << velocity;

    // Both modes off means nothing to do
    if (m_leadMode == LeadMode::Off && m_harmonyMode == HarmonyMode::OFF) {
        qDebug() << "ScaleSnap: Exiting early - both modes are Off";
        return;
    }

    if (!m_midi) {
        qDebug() << "ScaleSnap: Exiting early - no midi processor";
        return;
    }

    // NOTE: We do NOT call releaseVoiceSustainedNotes() here anymore.
    // It will be called later, only if we're actually going to play a new note.
    // This allows repeated wrong notes and fast-playing skips to keep notes sustained.

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
        if (m_harmonyMode != HarmonyMode::OFF) {
            emitPitchBend(kChannelHarmony1, 8192);
        }
    }

    // === LEAD MODE (Channel 1) ===
    if (m_leadMode != LeadMode::Off) {
        if (m_leadMode == LeadMode::Original) {
            // Original mode: pass through unchanged
            releaseVoiceSustainedNotes();
            active.snappedNote = midiNote;
            active.referenceHz = midiNoteToHz(midiNote);
            qDebug() << "ScaleSnap ORIGINAL: Emitting note" << midiNote << "on channel" << kChannelLead;
            emitNoteOn(kChannelLead, midiNote, velocity);
        } else if (m_leadMode == LeadMode::Conformed) {
            // Conformed mode: use PitchConformanceEngine for gravity-based correction
            qDebug() << "ScaleSnap CONFORMED: validPcs.isEmpty()=" << validPcs.isEmpty()
                     << "m_hasLastKnownChord=" << m_hasLastKnownChord;

            if (validPcs.isEmpty() || !m_hasLastKnownChord) {
                // No chord/scale info - pass through unchanged
                qDebug() << "ScaleSnap CONFORMED: No chord/scale info - passing through unchanged";
                releaseVoiceSustainedNotes();
                active.snappedNote = midiNote;
                active.referenceHz = midiNoteToHz(midiNote);
                active.behavior = ConformanceBehavior::ALLOW;
                emitNoteOn(kChannelLead, midiNote, velocity);
            } else {
                // Build ActiveChord for conformance
                ActiveChord activeChord = buildActiveChord();

                // Debug: show actual pitch classes in tier1
                QString tier1Pcs;
                for (int pc : activeChord.tier1Absolute) {
                    static const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                    tier1Pcs += QString("%1(%2) ").arg(noteNames[pc]).arg(pc);
                }
                qDebug() << "ScaleSnap CONFORMED: rootPc=" << activeChord.rootPc
                         << "chordKey=" << activeChord.ontologyChordKey
                         << "T1 notes:" << tier1Pcs
                         << "T1 size=" << activeChord.tier1Absolute.size();

                // ================================================================
                // INTERVAL TRACKING FOR CHROMATIC SWEEP DETECTION
                // Track the interval between consecutive notes to detect
                // chromatic sweeps (±1 semitone runs) vs melodic patterns
                // ================================================================
                if (m_lastInputNote >= 0) {
                    int interval = midiNote - m_lastInputNote;
                    m_recentIntervals[m_recentIntervalsIndex] = interval;
                    m_recentIntervalsIndex = (m_recentIntervalsIndex + 1) % kRecentIntervalsSize;
                }
                m_lastInputNote = midiNote;

                // ================================================================
                // FAST PLAYING DETECTION
                // If notes are coming faster than kFastPlayingThresholdMs AND
                // the pattern looks like a chromatic sweep (consecutive semitones),
                // skip non-chord tones. But if it's a melodic pattern (larger
                // intervals, mixed directions), allow scale tones.
                // ================================================================
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                const qint64 timeSinceLastNote = now - m_lastNoteOnTimestamp;
                m_lastNoteOnTimestamp = now;

                const bool isFastPlaying = (timeSinceLastNote > 0 && timeSinceLastNote < kFastPlayingThresholdMs);
                const int inputPc = normalizePc(midiNote);
                const bool isChordTone = (activeChord.tier1Absolute.count(inputPc) > 0);
                const bool isScaleTone = activeChord.isValidScaleTone(inputPc);  // T1, T2, or T3
                const bool isChromaticSweep = isLikelyChromaticSweep();

                qDebug() << "ScaleSnap: timeSinceLastNote=" << timeSinceLastNote
                         << "isFastPlaying=" << isFastPlaying
                         << "isChordTone=" << isChordTone
                         << "isScaleTone=" << isScaleTone
                         << "isChromaticSweep=" << isChromaticSweep;

                // Fast chromatic sweep + non-chord tone = skip (previous note sustains)
                // Fast melodic pattern + scale tone = allow (it's intentional)
                if (isFastPlaying && !isChordTone) {
                    if (isChromaticSweep) {
                        // Chromatic sweep: skip non-chord tones
                        qDebug() << "ScaleSnap: SKIPPING non-chord tone during chromatic sweep";
                        for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
                            it.value().voiceSustained = true;
                        }
                        return;  // Exit early - don't process this note
                    } else if (isScaleTone) {
                        // Melodic pattern with scale tone: allow it through
                        qDebug() << "ScaleSnap: ALLOWING scale tone during fast melodic pattern";
                        // Fall through to normal processing
                    } else {
                        // Fast playing + chromatic (T4) note = skip
                        qDebug() << "ScaleSnap: SKIPPING chromatic note during fast playing";
                        for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
                            it.value().voiceSustained = true;
                        }
                        return;
                    }
                }

                // ================================================================
                // MACHINE-GUN PREVENTION
                //
                // If a note is already playing (m_currentlyPlayingNote), we check:
                // 1. If playing the SAME output note from a WRONG fret → sustain, don't retrigger
                // 2. If playing the CORRECT fret for the note → allow retrigger
                // 3. If playing a DIFFERENT note → normal behavior
                //
                // This prevents the "machine gun" effect when repeatedly hitting
                // wrong frets that all snap to the same chord tone.
                // ================================================================

                // We need to know what note this input would produce BEFORE deciding
                // Get conformance result early to know the output
                ConformanceContext ctx;
                ctx.currentChord = activeChord;
                ctx.velocity = velocity;
                ctx.beatPosition = m_beatPosition;
                ctx.isStrongBeat = (m_beatPosition < 0.5f) || (m_beatPosition >= 2.0f && m_beatPosition < 2.5f);
                ctx.previousPitch = m_lastPlayedNote;

                ConformanceResult result = m_conformanceEngine.conformPitch(midiNote, ctx);
                int outputNote = qBound(0, result.outputPitch, 127);
                bool wouldBeSnapped = (result.behavior == ConformanceBehavior::SNAP ||
                                       result.behavior == ConformanceBehavior::TIMED_SNAP);

                // Check if this would produce the same note that's already playing
                if (m_currentlyPlayingNote >= 0 && outputNote == m_currentlyPlayingNote) {
                    // Same output note - but is this the "right" way to play it?
                    if (wouldBeSnapped) {
                        // Player is hitting a wrong fret that snaps to the current note
                        // → sustain, don't retrigger
                        qDebug() << "ScaleSnap: Wrong fret" << midiNote << "would snap to already-playing"
                                 << m_currentlyPlayingNote << "- sustaining instead";
                        for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
                            it.value().voiceSustained = true;
                        }
                        return;  // Exit early
                    }
                    // else: Player is playing the correct fret for this note
                    // Allow retrigger (fall through to normal processing)
                    qDebug() << "ScaleSnap: Correct fret" << midiNote << "for note"
                             << outputNote << "- allowing retrigger";
                }

                // (conformance result already computed above for machine-gun check)

                qDebug() << "ScaleSnap: LEAD INPUT" << midiNote << "-> OUTPUT" << outputNote
                         << "behavior:" << static_cast<int>(result.behavior)
                         << "snapTarget:" << result.snapTargetPitch;

                active.snappedNote = outputNote;
                active.referenceHz = midiNoteToHz(outputNote);
                active.behavior = result.behavior;

                // We're about to emit a new note - release any voice-sustained notes first
                releaseVoiceSustainedNotes();

                // Handle behavior-specific actions
                // NOTE: BEND behaviors are now disabled - engine returns ALLOW, SNAP, or TIMED_SNAP
                switch (result.behavior) {
                    case ConformanceBehavior::ALLOW:
                    case ConformanceBehavior::ANTICIPATE:
                        // Emit note immediately (it's already a chord tone or valid scale/tension)
                        emitNoteOn(kChannelLead, outputNote, velocity);
                        // Track: this note was played correctly (not snapped)
                        m_currentlyPlayingNote = outputNote;
                        m_currentNoteWasSnapped = false;
                        break;

                    case ConformanceBehavior::SNAP:
                        // Immediate snap (down direction) - play the snapped note
                        emitNoteOn(kChannelLead, outputNote, velocity);
                        // Track: this note was snapped (wrong fret)
                        m_currentlyPlayingNote = outputNote;
                        m_currentNoteWasSnapped = true;
                        qDebug() << "ScaleSnap: SNAP (down) - note" << midiNote
                                 << "snapped to" << outputNote;
                        break;

                    case ConformanceBehavior::TIMED_SNAP:
                        // Play original note, but set up timer to snap later (up direction)
                        active.snappedNote = midiNote;  // Currently playing original
                        active.referenceHz = midiNoteToHz(midiNote);
                        active.isTimedSnap = true;
                        active.timedSnapRemainingMs = result.snapDelayMs;
                        active.timedSnapTarget = result.snapTargetPitch;
                        active.velocity = velocity;
                        emitNoteOn(kChannelLead, midiNote, velocity);
                        // Track: will snap to target (wrong fret)
                        m_currentlyPlayingNote = result.snapTargetPitch;
                        m_currentNoteWasSnapped = true;

                        qDebug() << "ScaleSnap: TIMED_SNAP (up) - note" << midiNote
                                 << "will snap to" << result.snapTargetPitch
                                 << "after" << result.snapDelayMs << "ms if held";
                        break;

                    // BEND behaviors are disabled but keep code for reference
                    case ConformanceBehavior::TIMED_BEND:
                    case ConformanceBehavior::BEND:
                        // Bends disabled - just emit the note unchanged
                        qDebug() << "ScaleSnap: BEND behavior disabled, emitting note unchanged";
                        emitNoteOn(kChannelLead, midiNote, velocity);
                        break;

                    case ConformanceBehavior::DELAY:
                        // Don't emit yet - schedule for later
                        active.isDelayed = true;
                        active.delayRemainingMs = result.delayMs;
                        active.delayedVelocity = velocity;
                        qDebug() << "ScaleSnap: DELAY behavior - note" << outputNote
                                 << "delayed by" << result.delayMs << "ms";
                        break;
                }

                // Track last played note for melodic analysis
                m_lastPlayedNote = midiNote;
            }
        }
    } else {
        // Lead mode off - still set reference Hz for potential harmony notes
        active.snappedNote = midiNote;
        active.referenceHz = midiNoteToHz(midiNote);
    }

    // === HARMONY MODE (Channels 12-15) ===
    if (m_harmonyMode != HarmonyMode::OFF) {
        qDebug() << "ScaleSnap: HARMONY MODE IS ACTIVE - generating harmony for lead note" << midiNote
                 << "harmonyModeCompat=" << static_cast<int>(m_harmonyModeCompat);
        const QSet<int> chordTones = computeChordTones(m_lastKnownChord);
        qDebug() << "ScaleSnap Harmony: chordTones=" << chordTones << "validPcs=" << validPcs;

        // Check for phrase timeout (new phrase = reset contrary motion)
        // The phrase resets when you STOPPED PLAYING GUITAR for > threshold
        // m_lastGuitarNoteOffTimestamp is set when m_guitarNotesHeld drops to 0
        // This is independent of voice sustain - voice sustain holds the SOUND but
        // we track when you physically stopped playing the guitar
        if (m_guitarNotesHeld == 0 && m_lastGuitarNoteOffTimestamp > 0) {
            // We were silent (no guitar notes held), check how long
            qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
            qint64 silenceDuration = currentTime - m_lastGuitarNoteOffTimestamp;
            qDebug() << "ScaleSnap CONTRARY: was silent for" << silenceDuration << "ms (threshold=" << kPhraseTimeoutMs << ")";
            if (silenceDuration > kPhraseTimeoutMs) {
                // New phrase! Reset contrary motion tracking
                qDebug() << "ScaleSnap CONTRARY: NEW PHRASE detected after" << silenceDuration << "ms silence";
                m_lastHarmonyLeadNote = -1;
                m_lastHarmonyOutputNote = -1;
                m_leadMelodyDirection = 0;
            }
        }
        // We're now holding a guitar note
        m_guitarNotesHeld++;
        m_lastGuitarNoteOffTimestamp = 0;  // Clear since we're playing

        // Store previous lead note before updating (needed for contrary motion)
        int previousLeadNote = m_lastHarmonyLeadNote;
        m_lastHarmonyLeadNote = midiNote;

        // Generate harmony based on harmony mode
        switch (m_harmonyModeCompat) {
            case HarmonyModeCompat::Contrary:
                // Contrary motion uses consonance-aware algorithm
                // Pass previous lead and harmony notes for parallel 5th/octave detection
                active.harmonyNote = generateContraryHarmonyNote(midiNote, previousLeadNote, m_lastHarmonyOutputNote, chordTones, validPcs, false);
                break;

            case HarmonyModeCompat::Similar:
                // Similar motion - both voices move same direction, different intervals
                // Cannot approach perfect consonances (direct 5ths/octaves forbidden)
                active.harmonyNote = generateSimilarHarmonyNote(midiNote, previousLeadNote, m_lastHarmonyOutputNote, chordTones, validPcs, false);
                break;

            case HarmonyModeCompat::SmartThirds:
            default:
                // Parallel motion - both voices move same direction, same interval
                // Maintains constant interval (3rd or 6th) between lead and harmony
                active.harmonyNote = generateParallelHarmonyNote(midiNote, previousLeadNote, m_lastHarmonyOutputNote, chordTones, validPcs, false);
                break;
        }

        // FINAL VALIDATION: Ensure harmony note is T1/T2/T3 (not chromatic T4)
        // This catches edge cases from generator fallbacks that could return invalid notes
        ActiveChord activeChord = buildActiveChord();
        active.harmonyNote = validateHarmonyNote(active.harmonyNote, midiNote, activeChord);

        // Track the harmony output for next iteration (used by CONTRARY mode)
        m_lastHarmonyOutputNote = active.harmonyNote;

        qDebug() << "ScaleSnap Harmony: INPUT" << midiNote << "-> HARMONY" << active.harmonyNote;

        // Apply harmony velocity scaling
        int harmonyVelocity = static_cast<int>(velocity * m_harmonyConfig.velocityRatio);
        harmonyVelocity = qBound(1, harmonyVelocity, 127);

        if (active.harmonyNote >= 0 && active.harmonyNote <= 127) {
            emitNoteOn(kChannelHarmony1, active.harmonyNote, harmonyVelocity);
        }
    }

    m_activeNotes.insert(midiNote, active);
}

void ScaleSnapProcessor::onGuitarNoteOff(int midiNote)
{
    qDebug() << "ScaleSnap::onGuitarNoteOff - note:" << midiNote
             << "activeNotes count:" << m_activeNotes.size()
             << "guitarNotesHeld:" << m_guitarNotesHeld;

    // Track guitar note release for phrase detection (BEFORE checking modes or voice sustain)
    // This tracks when you physically release the guitar string, regardless of voice sustain
    if (m_guitarNotesHeld > 0) {
        m_guitarNotesHeld--;
        if (m_guitarNotesHeld == 0) {
            // All guitar notes released - start silence timer
            m_lastGuitarNoteOffTimestamp = QDateTime::currentMSecsSinceEpoch();
            qDebug() << "ScaleSnap: All guitar notes released - silence timer started";
        }
    }

    // Both modes off means nothing to do
    if (m_leadMode == LeadMode::Off && m_harmonyMode == HarmonyMode::OFF) {
        return;
    }

    if (!m_midi) {
        return;
    }

    auto it = m_activeNotes.find(midiNote);
    if (it == m_activeNotes.end()) {
        qDebug() << "ScaleSnap: Note" << midiNote << "not found in activeNotes, ignoring noteOff";
        return;
    }

    qDebug() << "ScaleSnap: Found note" << midiNote << "in activeNotes, voiceSustained="
             << it.value().voiceSustained << "snappedNote=" << it.value().snappedNote;

    // If note is already marked as voice-sustained (e.g., from repeated wrong note
    // or fast playing skip), don't release it - just return
    if (it.value().voiceSustained) {
        qDebug() << "ScaleSnap: Note" << midiNote << "is voice-sustained, not releasing";
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
        // Clear machine-gun prevention state when all notes released
        m_currentlyPlayingNote = -1;
        m_currentNoteWasSnapped = false;
        qDebug() << "ScaleSnap: All notes released";
        if (m_leadMode != LeadMode::Off) {
            emitPitchBend(kChannelLead, 8192);
        }
        if (m_harmonyMode != HarmonyMode::OFF) {
            emitPitchBend(kChannelHarmony1, 8192);
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
        if (m_harmonyMode != HarmonyMode::OFF) {
            emitPitchBend(kChannelHarmony1, bendValue);
        }
    } else {
        // Solo guitar bend mode
        // Convert to MIDI pitch bend (14-bit, 0-16383, centered at 8192)
        // Assuming standard ±2 semitone (±200 cents) bend range
        const double bendRange = 200.0; // cents
        int bendValue = 8192 + static_cast<int>((m_lastGuitarCents / bendRange) * 8192.0);
        bendValue = qBound(0, bendValue, 16383);

        emitPitchBend(kChannelLead, bendValue);
        if (m_harmonyMode != HarmonyMode::OFF) {
            emitPitchBend(kChannelHarmony1, bendValue);
        }
    }
}

void ScaleSnapProcessor::onVoiceCc2Updated(int value)
{
    // Track CC2 value for voice sustain feature
    const int previousCc2 = m_lastCc2Value;
    m_lastCc2Value = value;

    // Both modes off means nothing to do
    if (m_leadMode == LeadMode::Off && m_harmonyMode == HarmonyMode::OFF) {
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
    if (m_harmonyMode != HarmonyMode::OFF) {
        emitCC(kChannelHarmony1, 2, value);
    }
}

void ScaleSnapProcessor::onVoiceHzUpdated(double hz)
{
    // Only active when vocal bend is enabled, at least one mode is on, and there are active notes
    const bool bothModesOff = (m_leadMode == LeadMode::Off && m_harmonyMode == HarmonyMode::OFF);
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

    // Combine all bend sources based on lead mode
    double totalCents = voiceCents;
    if (m_leadMode == LeadMode::Original) {
        // Original mode: guitar bend + voice vibrato
        totalCents = m_lastGuitarCents + voiceCents;
    } else if (m_leadMode == LeadMode::Conformed && note.behavior == ConformanceBehavior::BEND) {
        // Conformed mode with BEND behavior: conformance bend + voice vibrato
        totalCents = note.conformanceBendCurrent + voiceCents;
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
    if (m_harmonyMode != HarmonyMode::OFF) {
        emitPitchBend(kChannelHarmony1, bendValue);
    }
}

QSet<int> ScaleSnapProcessor::computeValidPitchClasses() const
{
    QSet<int> validPcs;

    qDebug() << "ScaleSnap::computeValidPitchClasses - harmony=" << (m_harmony != nullptr)
             << "ontology=" << (m_ontology != nullptr)
             << "model=" << (m_model != nullptr)
             << "cellIndex=" << m_currentCellIndex
             << "hasLastKnownChord=" << m_hasLastKnownChord;

    if (!m_harmony || !m_ontology || !m_model) {
        qDebug() << "ScaleSnap::computeValidPitchClasses - missing dependency, returning empty";
        return validPcs;
    }

    music::ChordSymbol chord;
    bool isExplicit = false;

    // If we have a valid cell index (playback is active), use it
    if (m_currentCellIndex >= 0) {
        // Try to get chord for current cell
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
    } else {
        // Playback not active - try to use the first chord in the chart as fallback
        // or use last known chord if we have one
        if (m_hasLastKnownChord && m_lastKnownChord.rootPc >= 0) {
            chord = m_lastKnownChord;
        } else {
            // Scan from the beginning to find the first chord (limit to first 32 cells)
            for (int i = 0; i < 32; ++i) {
                music::ChordSymbol firstChord = m_harmony->parseCellChordNoState(
                    *m_model, i, music::ChordSymbol{}, &isExplicit
                );
                if (isExplicit && firstChord.rootPc >= 0 && !firstChord.noChord && !firstChord.placeholder) {
                    chord = firstChord;
                    const_cast<ScaleSnapProcessor*>(this)->m_lastKnownChord = chord;
                    const_cast<ScaleSnapProcessor*>(this)->m_hasLastKnownChord = true;
                    qDebug() << "ScaleSnap: Found first chord at cell" << i << "root=" << chord.rootPc;
                    break;
                }
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

ActiveChord ScaleSnapProcessor::buildActiveChord() const
{
    ActiveChord chord;

    if (!m_hasLastKnownChord || !m_harmony || !m_ontology) {
        return chord;  // Return empty chord
    }

    chord.rootPc = m_lastKnownChord.rootPc;

    // Get chord definition
    const auto* chordDef = m_harmony->chordDefForSymbol(m_lastKnownChord);
    if (chordDef) {
        chord.ontologyChordKey = chordDef->key;
    }

    // Get scale key and key root from local key estimate
    QString scaleKey;
    int keyRootPc = chord.rootPc;  // Default to chord root if no key info

    if (m_currentCellIndex >= 0) {
        const int barIndex = m_currentCellIndex / 4;
        const auto& localKeys = m_harmony->localKeysByBar();

        if (barIndex >= 0 && barIndex < localKeys.size()) {
            scaleKey = localKeys[barIndex].scaleKey;
            keyRootPc = localKeys[barIndex].tonicPc;  // Use the key's tonic!
        } else if (m_harmony->hasKeyPcGuess()) {
            scaleKey = m_harmony->keyScaleKey();
            keyRootPc = m_harmony->keyPcGuess();  // Use the key's tonic!
        }
    }
    chord.ontologyScaleKey = scaleKey;

    // Use ChordOntology to build the full ActiveChord with tiers
    ChordOntology::instance().setOntologyRegistry(m_ontology);

    if (chordDef) {
        // Get chord-type-specific scale hints (e.g., maj7 -> ionian, lydian)
        // This uses music theory rules, not generic pitch class matching
        const auto scaleHints = virtuoso::theory::explicitHintScalesForContext(
            QString(), chordDef->key
        );

        // Build list of scale definitions from hints
        QVector<const virtuoso::ontology::ScaleDef*> scaleDefs;
        QString scaleNames;
        for (const auto& hintKey : scaleHints) {
            if (const auto* scaleDef = m_ontology->scale(hintKey)) {
                scaleDefs.append(scaleDef);
                if (!scaleNames.isEmpty()) scaleNames += ", ";
                scaleNames += scaleDef->name;
            }
        }

        // Fallback: if no hints, use ionian for major-ish, dorian for minor-ish
        if (scaleDefs.isEmpty()) {
            // Check if chord has minor 3rd (interval 3)
            bool hasMinor3rd = false;
            for (int interval : chordDef->intervals) {
                if (interval == 3) { hasMinor3rd = true; break; }
            }
            const QString fallbackKey = hasMinor3rd ? "dorian" : "ionian";
            if (const auto* fallbackScale = m_ontology->scale(fallbackKey)) {
                scaleDefs.append(fallbackScale);
                scaleNames = fallbackScale->name;
            }
        }

        qDebug() << "ScaleSnap buildActiveChord: chordRoot=" << chord.rootPc
                 << "chordKey=" << chordDef->key
                 << "numScales=" << scaleDefs.size()
                 << "scales:" << scaleNames;

        // Create chord with compatible scales (union of scale tones from chord root)
        chord = ChordOntology::instance().createActiveChord(
            chord.rootPc, chordDef, scaleDefs
        );

        // Debug: show all tiers
        QString t1Str, t2Str, t3Str;
        static const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        for (int pc : chord.tier1Absolute) t1Str += QString("%1 ").arg(noteNames[pc]);
        for (int pc : chord.tier2Absolute) t2Str += QString("%1 ").arg(noteNames[pc]);
        for (int pc : chord.tier3Absolute) t3Str += QString("%1 ").arg(noteNames[pc]);

        qDebug() << "ScaleSnap buildActiveChord: T1=" << t1Str
                 << "T2=" << t2Str << "T3=" << t3Str;
    }

    return chord;
}

void ScaleSnapProcessor::checkAndReconformOnChordChange(int previousCellIndex)
{
    // Get the new chord for the current cell
    if (!m_harmony || !m_ontology || !m_model) {
        return;
    }

    // First, force refresh of chord by checking current cell
    // This updates m_lastKnownChord if there's a new chord
    music::ChordSymbol newChord;
    bool isExplicit = false;

    if (m_currentCellIndex >= 0) {
        newChord = m_harmony->parseCellChordNoState(
            *m_model,
            m_currentCellIndex,
            music::ChordSymbol{},
            &isExplicit
        );

        // If current cell has explicit chord, use it
        if (isExplicit && newChord.rootPc >= 0 && !newChord.noChord && !newChord.placeholder) {
            // Check if chord actually changed
            if (m_hasLastKnownChord &&
                m_lastKnownChord.rootPc == newChord.rootPc &&
                m_lastKnownChord.quality == newChord.quality) {
                return;  // Same chord, no re-conformance needed
            }

            // Chord changed - update tracking
            m_lastKnownChord = newChord;
            m_hasLastKnownChord = true;
        } else {
            // No explicit chord in this cell - keep using last known chord
            return;
        }
    } else {
        return;  // No valid cell index
    }

    // Build the new ActiveChord
    ActiveChord activeChord = buildActiveChord();
    if (activeChord.tier1Absolute.empty()) {
        return;  // No valid chord data
    }

    qDebug() << "ScaleSnap: Chord changed at cell" << m_currentCellIndex
             << "- checking" << m_activeNotes.size() << "active notes for re-conformance";

    // Precompute chord tones and valid pitch classes for harmony re-conformance
    const QSet<int> chordTones = computeChordTones(m_lastKnownChord);
    const QSet<int> validPcs = computeValidPitchClasses();

    // Check each active note and re-conform if needed
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        ActiveNote& note = it.value();
        const int currentOutputPc = normalizePc(note.snappedNote);

        // Check if the current output note is still valid (T1 chord tone)
        const int leadTier = ChordOntology::instance().getTier(currentOutputPc, activeChord);

        qDebug() << "ScaleSnap: Lead note" << note.snappedNote << "(pc" << currentOutputPc << ") tier=" << leadTier;

        bool leadChanged = false;
        int currentLeadNote = note.snappedNote;

        // =========================================================================
        // LEAD CHORD-CHANGE CONFORMANCE (only when lead mode is Conformed)
        // =========================================================================
        // If lead note is no longer a chord tone (T1), we need to snap it
        // Only T1 stays, snap T2/T3/T4 (tensions disabled)
        if (m_leadMode == LeadMode::Conformed && leadTier > 1) {
            // Find nearest chord tone
            int nearestTarget = -1;
            int minDistance = 7;

            for (int target : activeChord.tier1Absolute) {
                int dist = ChordOntology::minDistance(currentOutputPc, target);
                if (dist < minDistance) {
                    minDistance = dist;
                    nearestTarget = target;
                }
            }

            if (nearestTarget >= 0) {
                // Compute the new note in the same octave
                int newNote = ChordOntology::findNearestInOctave(note.snappedNote, nearestTarget);

                if (newNote != note.snappedNote) {
                    qDebug() << "ScaleSnap: Re-conforming lead" << note.snappedNote
                             << "->" << newNote << "due to chord change";

                    // Emit note change (note-off old, note-on new)
                    emitNoteOff(kChannelLead, note.snappedNote);
                    emitNoteOn(kChannelLead, newNote, note.velocity);

                    // Update the active note
                    note.snappedNote = newNote;
                    note.referenceHz = midiNoteToHz(newNote);

                    // Update tracking
                    m_currentlyPlayingNote = newNote;
                    m_currentNoteWasSnapped = true;
                    leadChanged = true;
                    currentLeadNote = newNote;
                }
            }
        }

        // =========================================================================
        // INDEPENDENT HARMONY CHORD-CHANGE CONFORMANCE
        // =========================================================================
        // Check if harmony note needs re-conforming, regardless of whether lead changed.
        // This ensures harmony stays consonant even when lead is already a chord tone.

        if (m_harmonyMode != HarmonyMode::OFF && note.harmonyNote >= 0) {
            const int harmonyPc = normalizePc(note.harmonyNote);
            const int harmonyTier = ChordOntology::instance().getTier(harmonyPc, activeChord);

            qDebug() << "ScaleSnap: Harmony note" << note.harmonyNote << "(pc" << harmonyPc << ") tier=" << harmonyTier;

            // Harmony should stay on T1 (chord tones) or T2 (tensions like 9th, 11th, 13th)
            // Re-conform if harmony is T3 (scale tone) or T4 (chromatic)
            bool harmonyNeedsReconform = (harmonyTier > 2);

            // Also re-conform harmony if lead changed (to maintain proper voice leading)
            if (leadChanged || harmonyNeedsReconform) {
                qDebug() << "ScaleSnap: Re-conforming harmony (leadChanged=" << leadChanged
                         << ", harmonyTier=" << harmonyTier << ")";

                // Turn off old harmony note
                emitNoteOff(kChannelHarmony1, note.harmonyNote);

                // Generate new harmony using the CORRECT motion-type generator
                // This preserves voice leading context (parallel/contrary/similar motion)
                int newHarmony = -1;

                switch (m_harmonyModeCompat) {
                    case HarmonyModeCompat::Contrary:
                        // For contrary motion, re-generate with current context
                        // Note: On chord change, we treat this as a "phrase continuation"
                        // so we pass the previous lead/harmony for proper voice leading
                        newHarmony = generateContraryHarmonyNote(
                            currentLeadNote,
                            m_lastHarmonyLeadNote,
                            m_lastHarmonyOutputNote,
                            chordTones,
                            validPcs,
                            false  // harmonyAbove
                        );
                        break;

                    case HarmonyModeCompat::Similar:
                        newHarmony = generateSimilarHarmonyNote(
                            currentLeadNote,
                            m_lastHarmonyLeadNote,
                            m_lastHarmonyOutputNote,
                            chordTones,
                            validPcs,
                            false
                        );
                        break;

                    case HarmonyModeCompat::SmartThirds:
                    default:
                        newHarmony = generateParallelHarmonyNote(
                            currentLeadNote,
                            m_lastHarmonyLeadNote,
                            m_lastHarmonyOutputNote,
                            chordTones,
                            validPcs,
                            false
                        );
                        break;
                }

                // FINAL VALIDATION: Ensure re-conformed harmony is T1/T2/T3 (not chromatic T4)
                newHarmony = validateHarmonyNote(newHarmony, currentLeadNote, activeChord);

                // Emit new harmony note
                int harmonyVelocity = static_cast<int>(note.velocity * m_harmonyConfig.velocityRatio);
                harmonyVelocity = qBound(1, harmonyVelocity, 127);
                emitNoteOn(kChannelHarmony1, newHarmony, harmonyVelocity);

                // Update tracking
                note.harmonyNote = newHarmony;
                m_lastHarmonyOutputNote = newHarmony;

                qDebug() << "ScaleSnap: Harmony re-conformed to" << newHarmony;
            }
        }

        // Update lead tracking if lead changed
        if (leadChanged) {
            m_lastHarmonyLeadNote = currentLeadNote;
        }
    }
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

int ScaleSnapProcessor::generateParallelHarmonyNote(int inputNote, int previousLeadNote, int previousHarmonyNote, const QSet<int>& chordTones, const QSet<int>& validPcs, bool harmonyAbove) const
{
    // =========================================================================
    // TRUE PARALLEL MOTION (Species Counterpoint Rules)
    // =========================================================================
    //
    // Classical parallel motion:
    // - Both voices move in the SAME direction
    // - By the SAME interval amount (maintaining constant interval)
    // - ONLY allowed for IMPERFECT consonances (3rds and 6ths)
    // - Parallel 5ths and octaves are FORBIDDEN
    //
    // Example (parallel 3rds below):
    //   Lead:    C  D  E  F  G
    //   Harmony: A  B  C  D  E  (each a 3rd below)

    qDebug() << "ScaleSnap PARALLEL: inputNote=" << inputNote
             << "prevLead=" << previousLeadNote
             << "prevHarmony=" << previousHarmonyNote
             << "harmonyAbove=" << harmonyAbove;

    // === PHRASE START: Pick initial interval ===
    if (previousLeadNote < 0 || previousHarmonyNote < 0) {
        // First note of phrase - start at a diatonic 3rd
        qDebug() << "ScaleSnap PARALLEL: NEW PHRASE - starting at 3rd";

        // Try to find a 3rd (minor or major) that's in the valid pitch classes
        int targetInterval = harmonyAbove ? 3 : -3;  // 3rd above or below

        // Search for the best starting interval (prefer 3rds, then 6ths)
        static const int kStartingIntervals[] = {-3, -4, 3, 4, -8, -9, 8, 9};  // m3, M3 below/above, m6, M6 below/above

        for (int interval : kStartingIntervals) {
            // Skip intervals that don't match our direction preference
            if (harmonyAbove && interval < 0) continue;
            if (!harmonyAbove && interval > 0) continue;

            int candidate = inputNote + interval;
            if (candidate < m_harmonyRangeMin || candidate > m_harmonyRangeMax) continue;

            int candidatePc = normalizePc(candidate);
            if (chordTones.contains(candidatePc) || validPcs.contains(candidatePc)) {
                qDebug() << "ScaleSnap PARALLEL: starting interval=" << interval << "harmonyNote=" << candidate;
                return qBound(m_harmonyRangeMin, candidate, m_harmonyRangeMax);
            }
        }

        // Fallback: just use a minor 3rd
        int fallback = inputNote + (harmonyAbove ? 3 : -3);
        qDebug() << "ScaleSnap PARALLEL: fallback starting interval, harmonyNote=" << fallback;
        return qBound(m_harmonyRangeMin, fallback, m_harmonyRangeMax);
    }

    // === CONTINUATION: Move harmony by same amount as lead ===
    int leadMovement = inputNote - previousLeadNote;
    int rawHarmonyNote = previousHarmonyNote + leadMovement;

    qDebug() << "ScaleSnap PARALLEL: leadMovement=" << leadMovement
             << "rawHarmonyNote=" << rawHarmonyNote;

    // Check the resulting interval
    int intervalWithLead = getIntervalClass(inputNote, rawHarmonyNote);

    // === VALIDATION: Must be imperfect consonance (3rd or 6th) ===
    if (isImperfectConsonance(intervalWithLead)) {
        // Great! The parallel motion maintains an imperfect consonance
        // Just need to snap to a valid pitch class if needed

        int rawPc = normalizePc(rawHarmonyNote);
        if (chordTones.contains(rawPc) || validPcs.contains(rawPc)) {
            // Already valid
            qDebug() << "ScaleSnap PARALLEL: valid imperfect consonance, harmonyNote=" << rawHarmonyNote;
            return qBound(m_harmonyRangeMin, rawHarmonyNote, m_harmonyRangeMax);
        }

        // Need to snap to nearest valid pitch class while staying close
        int bestCandidate = rawHarmonyNote;
        int bestDistance = 12;

        for (int offset = -2; offset <= 2; ++offset) {
            if (offset == 0) continue;
            int candidate = rawHarmonyNote + offset;
            int candidatePc = normalizePc(candidate);

            if (!chordTones.contains(candidatePc) && !validPcs.contains(candidatePc)) continue;
            if (candidate < m_harmonyRangeMin || candidate > m_harmonyRangeMax) continue;

            // Check that the adjusted note is still an imperfect consonance
            int adjustedInterval = getIntervalClass(inputNote, candidate);
            if (!isImperfectConsonance(adjustedInterval)) continue;

            if (std::abs(offset) < bestDistance) {
                bestDistance = std::abs(offset);
                bestCandidate = candidate;
            }
        }

        qDebug() << "ScaleSnap PARALLEL: snapped to valid pc, harmonyNote=" << bestCandidate;
        return qBound(m_harmonyRangeMin, bestCandidate, m_harmonyRangeMax);
    }

    // === CORRECTION: We've drifted to a non-imperfect consonance ===
    // This can happen when the lead moves chromatically or by unusual intervals
    // Find the nearest imperfect consonance (3rd or 6th) from the lead note

    qDebug() << "ScaleSnap PARALLEL: interval=" << intervalWithLead
             << "is NOT imperfect consonance, correcting...";

    // Target intervals: 3rds and 6ths (semitones: 3, 4, 8, 9)
    // Direction preference based on harmonyAbove and maintaining voice position
    bool harmonyIsCurrentlyAbove = (rawHarmonyNote > inputNote);
    bool shouldBeAbove = harmonyAbove;

    // Search for the best correction
    struct Candidate {
        int note;
        int score;
    };
    QVector<Candidate> candidates;

    // Check all imperfect consonance intervals
    static const int kImperfectIntervals[] = {3, 4, 8, 9, -3, -4, -8, -9};

    for (int interval : kImperfectIntervals) {
        int candidate = inputNote + interval;
        if (candidate < m_harmonyRangeMin || candidate > m_harmonyRangeMax) continue;

        int candidatePc = normalizePc(candidate);

        // Prefer chord tones, then scale tones
        bool isChordTone = chordTones.contains(candidatePc);
        bool isScaleTone = validPcs.contains(candidatePc);
        if (!isChordTone && !isScaleTone) continue;

        // Score the candidate
        int score = 0;

        // Prefer chord tones
        if (isChordTone) score += 4;

        // Prefer staying close to where parallel motion would have gone
        int distanceFromRaw = std::abs(candidate - rawHarmonyNote);
        score -= distanceFromRaw;  // Penalize distance

        // Prefer maintaining above/below relationship
        bool candidateAbove = (candidate > inputNote);
        if (candidateAbove == shouldBeAbove) score += 2;

        // Prefer 3rds over 6ths (tighter harmony)
        int absInterval = std::abs(interval);
        if (absInterval == 3 || absInterval == 4) score += 1;

        candidates.append({candidate, score});
    }

    if (!candidates.isEmpty()) {
        // Sort by score (highest first)
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

        int bestNote = candidates.first().note;
        qDebug() << "ScaleSnap PARALLEL: corrected to imperfect consonance, harmonyNote=" << bestNote
                 << "score=" << candidates.first().score;
        return qBound(m_harmonyRangeMin, bestNote, m_harmonyRangeMax);
    }

    // =========================================================================
    // SMART FALLBACK: Find best consonant chord tone maintaining melodic continuity
    // =========================================================================
    qDebug() << "ScaleSnap PARALLEL: correction search failed, using smart fallback";

    // Determine the direction harmony should be moving (parallel to lead)
    int harmonyDirection = leadMovement > 0 ? 1 : (leadMovement < 0 ? -1 : 0);

    struct FallbackCandidate {
        int note;
        int score;
    };
    QVector<FallbackCandidate> fallbackCandidates;

    // Search entire range for chord tones
    for (int candidate = m_harmonyRangeMin; candidate <= m_harmonyRangeMax; ++candidate) {
        int candidatePc = normalizePc(candidate);

        // Must be a chord tone (T1) for fallback
        if (!chordTones.contains(candidatePc)) continue;

        // Check interval with lead
        int intervalWithLead = getIntervalClass(inputNote, candidate);

        // Prefer imperfect consonances, but accept any consonance
        if (!isConsonant(intervalWithLead)) continue;

        int score = 0;

        // Prefer imperfect consonances
        if (isImperfectConsonance(intervalWithLead)) {
            score += 10;
        } else if (isPerfectConsonance(intervalWithLead)) {
            score += 3;
        }

        // MELODIC CONTINUITY: Prefer continuing in same direction as lead (parallel motion)
        if (previousHarmonyNote >= 0 && harmonyDirection != 0) {
            int movement = candidate - previousHarmonyNote;
            if ((movement > 0 && harmonyDirection > 0) || (movement < 0 && harmonyDirection < 0)) {
                score += 5;  // Moving same direction as lead - good for parallel
            } else if (movement == 0) {
                score += 2;  // Staying put - acceptable
            }
        }

        // Prefer stepwise motion
        if (previousHarmonyNote >= 0) {
            int absMovement = std::abs(candidate - previousHarmonyNote);
            if (absMovement <= 2) {
                score += 4;
            } else if (absMovement <= 4) {
                score += 2;
            } else {
                score -= (absMovement - 4);
            }
        }

        // Prefer staying in correct register
        bool candidateAbove = (candidate > inputNote);
        if (candidateAbove == harmonyAbove) {
            score += 2;
        }

        fallbackCandidates.append({candidate, score});
    }

    if (!fallbackCandidates.isEmpty()) {
        std::sort(fallbackCandidates.begin(), fallbackCandidates.end(),
                  [](const FallbackCandidate& a, const FallbackCandidate& b) { return a.score > b.score; });

        int bestFallback = fallbackCandidates.first().note;
        qDebug() << "ScaleSnap PARALLEL: Smart fallback selected" << bestFallback
                 << "with score" << fallbackCandidates.first().score;
        return bestFallback;
    }

    // Absolute last resort: use the raw parallel motion result clamped to range
    qDebug() << "ScaleSnap PARALLEL: No chord tones in range, using clamped raw result";
    return qBound(m_harmonyRangeMin, rawHarmonyNote, m_harmonyRangeMax);
}

int ScaleSnapProcessor::generateContraryHarmonyNote(int inputNote, int previousLeadNote, int previousHarmonyNote, const QSet<int>& chordTones, const QSet<int>& validPcs, bool harmonyAbove) const
{
    // =========================================================================
    // CONSONANCE-AWARE CONTRARY MOTION (Species Counterpoint Rules)
    // =========================================================================
    //
    // Classical contrary motion prioritizes:
    // 1. CONSONANT INTERVALS with the lead (3rds, 6ths preferred; 5ths, octaves allowed)
    // 2. OPPOSITE DIRECTION movement from lead
    // 3. STEPWISE MOTION when possible (smoother melody)
    // 4. NO PARALLEL 5THS OR OCTAVES (forbidden - destroys voice independence)
    // 5. INSTRUMENT RANGE CONSTRAINTS (stay within playable range)
    //
    // The algorithm finds harmony candidates that satisfy these rules and scores them.

    // Helper lambda to check if a note is within instrument range
    auto isInRange = [this](int note) {
        return note >= m_harmonyRangeMin && note <= m_harmonyRangeMax;
    };

    // First note of phrase: start with an imperfect consonance (3rd or 6th)
    // This establishes separation between the voices from the start
    if (previousHarmonyNote < 0 || previousLeadNote < 0) {
        // Find a consonant starting interval (prefer 3rd below or above based on setting)
        int direction = harmonyAbove ? 1 : -1;

        // Try intervals in order of preference: 3rd, 6th, 5th, octave
        // Also try octave transpositions to find one within range
        static const int preferredIntervals[] = {3, 4, 8, 9, 7, 12};  // m3, M3, m6, M6, P5, P8

        const QSet<int>& validTones = validPcs.isEmpty() ? chordTones : validPcs;

        for (int interval : preferredIntervals) {
            // Try the interval in the preferred direction
            int candidate = inputNote + (direction * interval);

            // Try different octaves to find one in range
            for (int octaveShift = 0; octaveShift <= 2; ++octaveShift) {
                int shifted = candidate + (octaveShift * 12 * direction);
                // Also try the opposite octave direction
                int shiftedOpp = candidate - (octaveShift * 12 * direction);

                for (int c : {shifted, shiftedOpp}) {
                    if (c < 0 || c > 127) continue;
                    if (!isInRange(c)) continue;

                    int candidatePc = normalizePc(c);
                    if (validTones.isEmpty() || validTones.contains(candidatePc)) {
                        qDebug() << "ScaleSnap CONTRARY: PHRASE START - harmony at interval"
                                 << interval << "=" << c << (harmonyAbove ? "(above)" : "(below)")
                                 << "(range:" << m_harmonyRangeMin << "-" << m_harmonyRangeMax << ")";
                        return c;
                    }
                }
            }
        }

        // Fallback: find ANY consonant note within range
        int fallback = inputNote + (direction * 4);  // Major 3rd
        // Shift into range if needed
        while (fallback < m_harmonyRangeMin && fallback + 12 <= 127) fallback += 12;
        while (fallback > m_harmonyRangeMax && fallback - 12 >= 0) fallback -= 12;
        fallback = qBound(m_harmonyRangeMin, fallback, m_harmonyRangeMax);

        qDebug() << "ScaleSnap CONTRARY: PHRASE START fallback - harmony at" << fallback;
        return fallback;
    }

    // Calculate lead movement
    int leadMovement = inputNote - previousLeadNote;

    // No lead movement = use oblique motion (harmony stays, if in range)
    if (leadMovement == 0) {
        if (isInRange(previousHarmonyNote)) {
            qDebug() << "ScaleSnap CONTRARY: no lead movement, keeping harmony at" << previousHarmonyNote;
            return previousHarmonyNote;
        }
        // Previous note is now out of range, need to find new one
    }

    // Determine harmony direction (OPPOSITE to lead)
    int harmonyDir = (leadMovement > 0) ? -1 : 1;

    qDebug() << "ScaleSnap CONTRARY: lead moved" << leadMovement
             << ", harmony should move" << (harmonyDir > 0 ? "UP" : "DOWN")
             << "(range:" << m_harmonyRangeMin << "-" << m_harmonyRangeMax << ")";

    // =========================================================================
    // CANDIDATE SEARCH: Find harmony notes that satisfy counterpoint rules
    // =========================================================================

    const QSet<int>& validTones = validPcs.isEmpty() ? chordTones : validPcs;

    struct Candidate {
        int note;
        int score;
    };
    QVector<Candidate> candidates;

    // Search range: up to 24 semitones (2 octaves) to find candidates within instrument range
    // We want stepwise motion, so prioritize small movements
    for (int delta = 1; delta <= 24; ++delta) {
        int candidateNote = previousHarmonyNote + (delta * harmonyDir);

        // Skip if out of MIDI range
        if (candidateNote < 0 || candidateNote > 127) continue;

        // =====================================================================
        // INSTRUMENT RANGE CHECK: Skip if outside playable range
        // =====================================================================
        if (!isInRange(candidateNote)) continue;

        int candidatePc = normalizePc(candidateNote);

        // Skip if not a valid pitch class (unless we have no chord info)
        if (!validTones.isEmpty() && !validTones.contains(candidatePc)) continue;

        // Check the interval this would form with the lead
        int intervalWithLead = getIntervalClass(inputNote, candidateNote);

        // Skip dissonant intervals
        if (!isConsonant(intervalWithLead)) continue;

        // =====================================================================
        // CRITICAL: Check for parallel 5ths and octaves (FORBIDDEN)
        // =====================================================================
        if (wouldCreateParallelPerfect(previousLeadNote, previousHarmonyNote, inputNote, candidateNote)) {
            qDebug() << "ScaleSnap CONTRARY: REJECTING candidate" << candidateNote
                     << "- would create parallel 5ths/octaves";
            continue;  // Skip this candidate entirely
        }

        // =====================================================================
        // SCORING: Prefer imperfect consonances and stepwise motion
        // =====================================================================
        int score = 0;

        // Strongly prefer imperfect consonances (3rds, 6ths) - the "sweet" intervals
        if (isImperfectConsonance(intervalWithLead)) {
            score += 10;
        } else if (isPerfectConsonance(intervalWithLead)) {
            // Perfect consonances are allowed but less preferred in the middle of phrases
            score += 3;
        }

        // Prefer stepwise motion (delta 1-2 semitones) - sounds more melodic
        if (delta <= 2) {
            score += 8;  // Strong bonus for stepwise
        } else if (delta <= 4) {
            score += 4;  // Moderate bonus for small skip
        } else {
            score -= (delta - 4);  // Penalty for large leaps
        }

        // Prefer staying in the correct register (above or below lead)
        bool isAboveLead = candidateNote > inputNote;
        if (isAboveLead == harmonyAbove) {
            score += 2;
        }

        // Bonus if it's a chord tone (not just a scale tone)
        if (chordTones.contains(candidatePc)) {
            score += 3;
        }

        candidates.append({candidateNote, score});
    }

    // =========================================================================
    // ALSO SEARCH IN THE OPPOSITE DIRECTION (in case we hit range limit)
    // =========================================================================
    // If the natural contrary direction would go out of range, we might need
    // to search the other direction to find any valid consonant note in range.

    if (candidates.isEmpty()) {
        int oppositeDir = -harmonyDir;
        for (int delta = 1; delta <= 24; ++delta) {
            int candidateNote = previousHarmonyNote + (delta * oppositeDir);

            if (candidateNote < 0 || candidateNote > 127) continue;
            if (!isInRange(candidateNote)) continue;

            int candidatePc = normalizePc(candidateNote);
            if (!validTones.isEmpty() && !validTones.contains(candidatePc)) continue;

            int intervalWithLead = getIntervalClass(inputNote, candidateNote);
            if (!isConsonant(intervalWithLead)) continue;

            if (wouldCreateParallelPerfect(previousLeadNote, previousHarmonyNote, inputNote, candidateNote)) {
                continue;
            }

            // Score (with penalty for being in wrong direction)
            int score = 0;
            if (isImperfectConsonance(intervalWithLead)) {
                score += 10;
            } else if (isPerfectConsonance(intervalWithLead)) {
                score += 3;
            }
            score -= 5;  // Penalty for not being contrary motion

            if (delta <= 2) {
                score += 8;
            } else if (delta <= 4) {
                score += 4;
            } else {
                score -= (delta - 4);
            }

            bool isAboveLead = candidateNote > inputNote;
            if (isAboveLead == harmonyAbove) {
                score += 2;
            }

            if (chordTones.contains(candidatePc)) {
                score += 3;
            }

            candidates.append({candidateNote, score});
        }
    }

    // =========================================================================
    // SELECT BEST CANDIDATE
    // =========================================================================

    if (!candidates.isEmpty()) {
        // Sort by score (descending) and pick the best
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

        int bestNote = candidates.first().note;
        int bestScore = candidates.first().score;

        qDebug() << "ScaleSnap CONTRARY: Selected harmony" << bestNote
                 << "with score" << bestScore
                 << "(interval with lead:" << getIntervalClass(inputNote, bestNote) << ")";

        return bestNote;
    }

    // =========================================================================
    // SMART FALLBACK: Find best consonant chord tone maintaining melodic continuity
    // =========================================================================
    // When range constraints prevent true contrary motion, we still want:
    // 1. A consonant interval with the lead (prefer 3rds/6ths)
    // 2. Melodic continuity - prefer continuing in the direction harmony was moving
    // 3. Avoid random jumping back and forth

    qDebug() << "ScaleSnap CONTRARY: Primary searches failed, using smart fallback";

    // Determine the direction harmony was moving (for melodic continuity)
    int harmonyDirection = 0;
    if (previousHarmonyNote >= 0 && m_lastHarmonyOutputNote >= 0 &&
        previousHarmonyNote != m_lastHarmonyOutputNote) {
        // Use the direction from two notes ago to previous note
        // (m_lastHarmonyOutputNote is actually the note before previousHarmonyNote was set)
        harmonyDirection = (previousHarmonyNote > m_lastHarmonyOutputNote) ? 1 : -1;
    } else {
        // No history - default to contrary direction
        harmonyDirection = harmonyDir;
    }

    // Search for any consonant chord tone within range
    struct FallbackCandidate {
        int note;
        int score;
    };
    QVector<FallbackCandidate> fallbackCandidates;

    // Search entire range for chord tones
    for (int candidate = m_harmonyRangeMin; candidate <= m_harmonyRangeMax; ++candidate) {
        int candidatePc = normalizePc(candidate);

        // Must be a chord tone (T1) for fallback - ensures harmonic validity
        if (!chordTones.contains(candidatePc)) continue;

        // Check interval with lead
        int intervalWithLead = getIntervalClass(inputNote, candidate);

        // Must be consonant
        if (!isConsonant(intervalWithLead)) continue;

        // Score the candidate
        int score = 0;

        // Prefer imperfect consonances
        if (isImperfectConsonance(intervalWithLead)) {
            score += 10;
        } else if (isPerfectConsonance(intervalWithLead)) {
            score += 3;
        }

        // MELODIC CONTINUITY: Prefer notes in the direction harmony was moving
        if (previousHarmonyNote >= 0) {
            int movement = candidate - previousHarmonyNote;
            if (harmonyDirection != 0) {
                if ((movement > 0 && harmonyDirection > 0) || (movement < 0 && harmonyDirection < 0)) {
                    // Same direction as harmony was moving - good for continuity
                    score += 5;
                } else if (movement == 0) {
                    // Staying on same note - acceptable
                    score += 2;
                }
                // Opposite direction gets no bonus (but not penalized heavily)
            }

            // Prefer stepwise motion
            int absMovement = std::abs(movement);
            if (absMovement <= 2) {
                score += 4;
            } else if (absMovement <= 4) {
                score += 2;
            } else {
                score -= (absMovement - 4);  // Penalize large leaps
            }
        }

        // Prefer staying in correct register relative to lead
        bool isAboveLead = candidate > inputNote;
        if (isAboveLead == harmonyAbove) {
            score += 2;
        }

        fallbackCandidates.append({candidate, score});
    }

    if (!fallbackCandidates.isEmpty()) {
        std::sort(fallbackCandidates.begin(), fallbackCandidates.end(),
                  [](const FallbackCandidate& a, const FallbackCandidate& b) { return a.score > b.score; });

        int bestFallback = fallbackCandidates.first().note;
        qDebug() << "ScaleSnap CONTRARY: Smart fallback selected" << bestFallback
                 << "with score" << fallbackCandidates.first().score
                 << "(harmonyDirection was" << harmonyDirection << ")";
        return bestFallback;
    }

    // Absolute last resort: clamp previous to range
    int fallback = qBound(m_harmonyRangeMin, previousHarmonyNote, m_harmonyRangeMax);
    qDebug() << "ScaleSnap CONTRARY: No chord tones in range, using clamped fallback" << fallback;
    return fallback;
}

int ScaleSnapProcessor::generateSimilarHarmonyNote(int inputNote, int previousLeadNote, int previousHarmonyNote, const QSet<int>& chordTones, const QSet<int>& validPcs, bool harmonyAbove) const
{
    // =========================================================================
    // SIMILAR MOTION (Species Counterpoint Rules)
    // =========================================================================
    //
    // Similar motion: both voices move in the SAME direction, but by DIFFERENT intervals.
    // The interval between them changes (unlike parallel motion where it stays the same).
    //
    // Rules:
    // 1. OK for approaching IMPERFECT consonances (3rds, 6ths)
    // 2. FORBIDDEN to approach perfect consonances (5ths, octaves) - "direct 5ths/octaves"
    // 3. Prefer stepwise motion in harmony voice
    // 4. Stay within instrument range
    //
    // Similar motion is less independent than contrary, but creates forward momentum.

    // Helper lambda to check if a note is within instrument range
    auto isInRange = [this](int note) {
        return note >= m_harmonyRangeMin && note <= m_harmonyRangeMax;
    };

    // First note of phrase: start with an imperfect consonance (3rd or 6th)
    if (previousHarmonyNote < 0 || previousLeadNote < 0) {
        int direction = harmonyAbove ? 1 : -1;
        static const int preferredIntervals[] = {3, 4, 8, 9};  // m3, M3, m6, M6 (imperfect only for similar)

        const QSet<int>& validTones = validPcs.isEmpty() ? chordTones : validPcs;

        for (int interval : preferredIntervals) {
            int candidate = inputNote + (direction * interval);

            for (int octaveShift = 0; octaveShift <= 2; ++octaveShift) {
                int shifted = candidate + (octaveShift * 12 * direction);
                int shiftedOpp = candidate - (octaveShift * 12 * direction);

                for (int c : {shifted, shiftedOpp}) {
                    if (c < 0 || c > 127) continue;
                    if (!isInRange(c)) continue;

                    int candidatePc = normalizePc(c);
                    if (validTones.isEmpty() || validTones.contains(candidatePc)) {
                        qDebug() << "ScaleSnap SIMILAR: PHRASE START - harmony at interval"
                                 << interval << "=" << c << (harmonyAbove ? "(above)" : "(below)");
                        return c;
                    }
                }
            }
        }

        // Fallback
        int fallback = inputNote + (direction * 4);
        while (fallback < m_harmonyRangeMin && fallback + 12 <= 127) fallback += 12;
        while (fallback > m_harmonyRangeMax && fallback - 12 >= 0) fallback -= 12;
        fallback = qBound(m_harmonyRangeMin, fallback, m_harmonyRangeMax);
        return fallback;
    }

    // Calculate lead movement
    int leadMovement = inputNote - previousLeadNote;

    // No lead movement = use oblique motion (harmony stays)
    if (leadMovement == 0) {
        if (isInRange(previousHarmonyNote)) {
            qDebug() << "ScaleSnap SIMILAR: no lead movement, keeping harmony at" << previousHarmonyNote;
            return previousHarmonyNote;
        }
    }

    // SIMILAR motion: harmony moves in the SAME direction as lead
    int harmonyDir = (leadMovement > 0) ? 1 : -1;

    qDebug() << "ScaleSnap SIMILAR: lead moved" << leadMovement
             << ", harmony should also move" << (harmonyDir > 0 ? "UP" : "DOWN")
             << "(range:" << m_harmonyRangeMin << "-" << m_harmonyRangeMax << ")";

    // =========================================================================
    // CANDIDATE SEARCH
    // =========================================================================

    const QSet<int>& validTones = validPcs.isEmpty() ? chordTones : validPcs;

    struct Candidate {
        int note;
        int score;
    };
    QVector<Candidate> candidates;

    // Search in the same direction as lead
    for (int delta = 1; delta <= 24; ++delta) {
        int candidateNote = previousHarmonyNote + (delta * harmonyDir);

        if (candidateNote < 0 || candidateNote > 127) continue;
        if (!isInRange(candidateNote)) continue;

        int candidatePc = normalizePc(candidateNote);
        if (!validTones.isEmpty() && !validTones.contains(candidatePc)) continue;

        // Check the interval with lead
        int intervalWithLead = getIntervalClass(inputNote, candidateNote);

        // Skip dissonant intervals
        if (!isConsonant(intervalWithLead)) continue;

        // =====================================================================
        // CRITICAL: Similar motion to PERFECT consonances is FORBIDDEN
        // =====================================================================
        if (isPerfectConsonance(intervalWithLead)) {
            qDebug() << "ScaleSnap SIMILAR: REJECTING candidate" << candidateNote
                     << "- similar motion to perfect consonance (direct 5th/octave)";
            continue;
        }

        // Check for parallel 5ths/octaves (still forbidden)
        if (wouldCreateParallelPerfect(previousLeadNote, previousHarmonyNote, inputNote, candidateNote)) {
            qDebug() << "ScaleSnap SIMILAR: REJECTING candidate" << candidateNote
                     << "- would create parallel 5ths/octaves";
            continue;
        }

        // =====================================================================
        // SCORING
        // =====================================================================
        int score = 0;

        // Imperfect consonances are the only valid targets for similar motion
        if (isImperfectConsonance(intervalWithLead)) {
            score += 10;
        }

        // Prefer stepwise motion
        if (delta <= 2) {
            score += 8;
        } else if (delta <= 4) {
            score += 4;
        } else {
            score -= (delta - 4);
        }

        // Prefer correct register
        bool isAboveLead = candidateNote > inputNote;
        if (isAboveLead == harmonyAbove) {
            score += 2;
        }

        // Bonus for chord tones
        if (chordTones.contains(candidatePc)) {
            score += 3;
        }

        candidates.append({candidateNote, score});
    }

    // =========================================================================
    // SELECT BEST CANDIDATE
    // =========================================================================

    if (!candidates.isEmpty()) {
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

        int bestNote = candidates.first().note;
        int bestScore = candidates.first().score;

        qDebug() << "ScaleSnap SIMILAR: Selected harmony" << bestNote
                 << "with score" << bestScore
                 << "(interval with lead:" << getIntervalClass(inputNote, bestNote) << ")";

        return bestNote;
    }

    // =========================================================================
    // FALLBACK: If no valid similar motion candidates, try contrary motion
    // =========================================================================
    // Similar motion is more restricted (can't approach perfect consonances),
    // so fall back to contrary motion which has more options.

    qDebug() << "ScaleSnap SIMILAR: No valid similar motion candidates, falling back to contrary";
    return generateContraryHarmonyNote(inputNote, previousLeadNote, previousHarmonyNote, chordTones, validPcs, harmonyAbove);
}

int ScaleSnapProcessor::validateHarmonyNote(int harmonyNote, int leadNote, const ActiveChord& chord) const
{
    // =========================================================================
    // FINAL HARMONY VALIDATION
    // =========================================================================
    // Ensures harmony note is harmonically valid (T1, T2, or T3 - not chromatic T4).
    // This catches edge cases where generators return invalid notes from fallbacks.
    //
    // Music theory rationale:
    // - T1 (chord tones): Always valid - the foundation of harmony
    // - T2 (tensions): Valid in jazz/pop - add color (9th, 11th, 13th)
    // - T3 (scale tones): Acceptable - diatonic, won't clash badly
    // - T4 (chromatic): INVALID - outside scale, sounds wrong

    if (harmonyNote < 0 || harmonyNote > 127) {
        qDebug() << "ScaleSnap VALIDATE: harmony note" << harmonyNote << "out of MIDI range";
        return qBound(0, harmonyNote, 127);
    }

    // Check if chord data is available
    if (chord.tier1Absolute.empty()) {
        // No chord data - can't validate, return as-is
        qDebug() << "ScaleSnap VALIDATE: no chord data, passing through harmony" << harmonyNote;
        return harmonyNote;
    }

    const int harmonyPc = normalizePc(harmonyNote);
    const int tier = ChordOntology::instance().getTier(harmonyPc, chord);

    // T1, T2, T3 are all acceptable
    if (tier <= 3) {
        qDebug() << "ScaleSnap VALIDATE: harmony" << harmonyNote << "(pc" << harmonyPc << ") tier=" << tier << "- OK";
        return harmonyNote;
    }

    // T4 (chromatic) - snap to nearest T1 (chord tone)
    qDebug() << "ScaleSnap VALIDATE: harmony" << harmonyNote << "(pc" << harmonyPc << ") is T4 (chromatic) - correcting";

    int bestTarget = -1;
    int bestDistance = 12;

    // Find nearest T1 pitch class
    for (int t1Pc : chord.tier1Absolute) {
        int dist = ChordOntology::minDistance(harmonyPc, t1Pc);
        if (dist < bestDistance) {
            bestDistance = dist;
            bestTarget = t1Pc;
        }
    }

    if (bestTarget < 0) {
        // Shouldn't happen if chord has T1, but fallback to root
        qDebug() << "ScaleSnap VALIDATE: no T1 found, using chord root" << chord.rootPc;
        bestTarget = chord.rootPc;
    }

    // Find the MIDI note with bestTarget pitch class, nearest to original harmony note
    int correctedNote = ChordOntology::findNearestInOctave(harmonyNote, bestTarget);

    // Ensure still within instrument range
    correctedNote = qBound(m_harmonyRangeMin, correctedNote, m_harmonyRangeMax);

    qDebug() << "ScaleSnap VALIDATE: corrected harmony" << harmonyNote << "->" << correctedNote
             << "(T4 pc" << harmonyPc << "-> T1 pc" << bestTarget << ")";

    return correctedNote;
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
    // Release lead note on channel 1
    if (m_leadMode != LeadMode::Off) {
        emitNoteOff(kChannelLead, note.snappedNote);
    }

    // Release harmony note on channel 12
    if (m_harmonyMode != HarmonyMode::OFF && note.harmonyNote >= 0) {
        emitNoteOff(kChannelHarmony1, note.harmonyNote);
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
        qDebug() << "ScaleSnap: Voice sustain notes released";
        if (m_leadMode != LeadMode::Off) {
            emitPitchBend(kChannelLead, 8192);
        }
        if (m_harmonyMode != HarmonyMode::OFF) {
            emitPitchBend(kChannelHarmony1, 8192);
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

// ============================================================================
// COUNTERPOINT INTERVAL ANALYSIS UTILITIES
// ============================================================================
// These utilities support classical counterpoint rules for harmony generation.
// Based on species counterpoint principles (Fux, Gradus ad Parnassum).

int ScaleSnapProcessor::getIntervalClass(int note1, int note2)
{
    // Return the interval in semitones (0-11), always positive
    int diff = std::abs(note1 - note2);
    return diff % 12;
}

bool ScaleSnapProcessor::isConsonant(int intervalSemitones)
{
    // Normalize to 0-11 range
    int interval = intervalSemitones % 12;
    if (interval < 0) interval += 12;

    // Consonant intervals:
    // - P1 (unison): 0 semitones
    // - m3 (minor 3rd): 3 semitones
    // - M3 (major 3rd): 4 semitones
    // - P5 (perfect 5th): 7 semitones
    // - m6 (minor 6th): 8 semitones
    // - M6 (major 6th): 9 semitones
    // - P8 (octave): 0 semitones (same as unison in pitch class)
    //
    // Note: Perfect 4th (5 semitones) is dissonant when above bass in two-voice texture
    switch (interval) {
        case 0:  // Unison/Octave
        case 3:  // Minor 3rd
        case 4:  // Major 3rd
        case 7:  // Perfect 5th
        case 8:  // Minor 6th
        case 9:  // Major 6th
            return true;
        default:
            return false;
    }
}

bool ScaleSnapProcessor::isPerfectConsonance(int intervalSemitones)
{
    // Normalize to 0-11 range
    int interval = intervalSemitones % 12;
    if (interval < 0) interval += 12;

    // Perfect consonances:
    // - P1 (unison): 0 semitones
    // - P5 (perfect 5th): 7 semitones
    // - P8 (octave): 0 semitones (same as unison)
    //
    // These are "stable" but should NOT be approached by parallel motion
    return (interval == 0 || interval == 7);
}

bool ScaleSnapProcessor::isImperfectConsonance(int intervalSemitones)
{
    // Normalize to 0-11 range
    int interval = intervalSemitones % 12;
    if (interval < 0) interval += 12;

    // Imperfect consonances:
    // - m3 (minor 3rd): 3 semitones
    // - M3 (major 3rd): 4 semitones
    // - m6 (minor 6th): 8 semitones
    // - M6 (major 6th): 9 semitones
    //
    // These are the "sweet" intervals preferred for harmony - can be approached by any motion
    switch (interval) {
        case 3:  // Minor 3rd
        case 4:  // Major 3rd
        case 8:  // Minor 6th
        case 9:  // Major 6th
            return true;
        default:
            return false;
    }
}

bool ScaleSnapProcessor::wouldCreateParallelPerfect(int prevLead, int prevHarmony, int newLead, int newHarmony)
{
    // Check if moving from (prevLead, prevHarmony) to (newLead, newHarmony)
    // would create forbidden parallel 5ths or octaves.
    //
    // Parallel perfect consonances (parallel 5ths, parallel octaves) are forbidden
    // because they destroy voice independence - the voices sound like one.

    int prevInterval = getIntervalClass(prevLead, prevHarmony);
    int newInterval = getIntervalClass(newLead, newHarmony);

    // Check if both intervals are perfect consonances (0=unison/octave, 7=fifth)
    bool prevIsPerfect = isPerfectConsonance(prevInterval);
    bool newIsPerfect = isPerfectConsonance(newInterval);

    if (!prevIsPerfect || !newIsPerfect) {
        return false;  // Not both perfect consonances, so no parallel perfect
    }

    // Check if both intervals are the SAME type (both 5ths or both unisons/octaves)
    // Parallel 5th→5th or 8ve→8ve is forbidden
    // Moving from 5th to octave (or vice versa) is called "direct" motion to perfect, also problematic
    // but technically different intervals, so we check if same
    if (prevInterval == newInterval) {
        // Both voices must be moving in the same direction for "parallel" motion
        int leadMovement = newLead - prevLead;
        int harmonyMovement = newHarmony - prevHarmony;

        // Parallel = same direction (both positive or both negative)
        // Contrary = opposite direction (one positive, one negative)
        bool sameDirection = (leadMovement > 0 && harmonyMovement > 0) ||
                            (leadMovement < 0 && harmonyMovement < 0);

        if (sameDirection && leadMovement != 0 && harmonyMovement != 0) {
            qDebug() << "ScaleSnap COUNTERPOINT: FORBIDDEN parallel"
                     << (prevInterval == 7 ? "5ths" : "octaves") << "detected!";
            return true;
        }
    }

    return false;
}

bool ScaleSnapProcessor::isLikelyChromaticSweep() const
{
    // A chromatic sweep is characterized by consecutive semitone intervals (±1)
    // We check the recent interval history to detect this pattern.
    //
    // Criteria:
    // - Most intervals are ±1 (chromatic)
    // - Intervals are in the same direction (ascending or descending sweep)
    //
    // A melodic pattern will have:
    // - Larger intervals (2, 3, 4+ semitones)
    // - Mixed directions
    // - Scale-based movement

    int chromaticCount = 0;   // Intervals that are ±1
    int sameDirection = 0;    // Intervals in same direction as first
    int firstDirection = 0;   // +1 for ascending, -1 for descending

    for (int i = 0; i < kRecentIntervalsSize; ++i) {
        int interval = m_recentIntervals[i];
        if (interval == 0) continue;  // Skip uninitialized slots

        // Count chromatic (±1) intervals
        if (std::abs(interval) == 1) {
            ++chromaticCount;
        }

        // Track direction consistency
        int direction = (interval > 0) ? 1 : -1;
        if (firstDirection == 0) {
            firstDirection = direction;
        }
        if (direction == firstDirection) {
            ++sameDirection;
        }
    }

    // It's likely a chromatic sweep if:
    // - At least 3 out of 4 recent intervals are chromatic (±1)
    // - AND they're mostly in the same direction
    const bool mostlyChromatic = (chromaticCount >= 3);
    const bool consistentDirection = (sameDirection >= 3);

    qDebug() << "ChromaticSweep check: chromaticCount=" << chromaticCount
             << "sameDirection=" << sameDirection
             << "result=" << (mostlyChromatic && consistentDirection);

    return mostlyChromatic && consistentDirection;
}

} // namespace playback
