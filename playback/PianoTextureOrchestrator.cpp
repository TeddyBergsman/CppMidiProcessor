#include "PianoTextureOrchestrator.h"

namespace playback {

PianoTextureOrchestrator::PianoTextureOrchestrator() = default;

// ========== Main Entry Point ==========

PianoTextureOrchestrator::TextureDecision
PianoTextureOrchestrator::decide(const OrchestratorInput& input) const
{
    TextureDecision decision;

    // Step 1: Determine primary mode based on soloist state
    if (input.context.isClimaxPoint) {
        decision.mode = selectModeForStructuralMoment(input);
        decision.rationale = "Structural moment (climax/cadence)";
    }
    else if (input.soloist.userBusy) {
        decision.mode = selectModeWhenUserBusy(input);
        decision.rationale = "User is playing - supportive mode";
    }
    else {
        decision.mode = selectModeWhenUserSilent(input);
        decision.rationale = "User is silent - opportunity for color";
    }

    // Step 2: Assign hand roles based on selected mode
    decision.leftHand = assignLeftHandRole(decision.mode, input);
    decision.rightHand = assignRightHandRole(decision.mode, input);

    // Step 3: Calculate timing offsets
    decision.lhTimingOffsetMs = calculateLhTimingOffset(decision.mode, input);
    decision.rhTimingOffsetMs = calculateRhTimingOffset(decision.mode, input);

    // Step 4: Set additional directives
    decision.omitRoot = shouldOmitRoot(input);

    // Inner voice motion: on sustained chords when there's time before next change
    // Evans used inner voice motion at ALL energy levels - it's subtle shading, not dramatic
    // Allow in most modes except Space, BlockChord (those have different purposes)
    bool modeAllowsInnerVoice = (decision.mode == TextureMode::Support ||
                                  decision.mode == TextureMode::BalancedComp ||
                                  decision.mode == TextureMode::SupportWithColor ||
                                  decision.mode == TextureMode::Fill ||
                                  decision.mode == TextureMode::Dialogue ||
                                  decision.mode == TextureMode::Resolution);
    decision.innerVoiceMotion = modeAllowsInnerVoice &&
                                 input.context.beatsUntilChordChange >= 2;

    // Hemiola: VERY RARE, only when expression is safe and energy is moderate
    decision.useHemiola = isExpressionSafe(input) &&
                          input.context.energy > 0.5 &&
                          input.context.energy < 0.8 &&
                          !input.soloist.userBusy;

    // Dramatic pause: after climactic moments
    decision.dramaticPause = input.context.isPhraseEnd &&
                             input.context.energy > 0.6 &&
                             !input.soloist.userBusy;

    // Step 5: Apply safety constraints (Accompaniment Hierarchy)
    decision = applySafetyConstraints(decision, input);

    return decision;
}

// ========== Root Omission ==========

bool PianoTextureOrchestrator::shouldOmitRoot(const OrchestratorInput& input) const
{
    // If bass is active, ALWAYS omit root from piano voicing
    if (input.rhythmSection.bassIsPlaying) {
        return true;
    }

    // If bass just played root, still omit for 1-2 beats
    if (input.rhythmSection.beatsSinceBassRoot < 2) {
        return true;
    }

    // Default: omit root (we're accompanying, bass handles roots)
    return true;
}

// ========== Timing for Bass Coordination ==========

PianoTextureOrchestrator::TimingRole
PianoTextureOrchestrator::getTimingForBass(const OrchestratorInput& input, double energy) const
{
    // If strong beat coming and moderate+ energy, anticipate
    if (input.rhythmSection.strongBeatComing && energy > 0.5) {
        return TimingRole::Anticipate;  // Shell on "&4" before bass root on "1"
    }

    // If bass is playing, delay to let bass establish
    if (input.rhythmSection.bassIsPlaying) {
        return TimingRole::Delay;  // "&1" after "1"
    }

    return TimingRole::OnBeat;
}

// ========== Anticipation Amount ==========

double PianoTextureOrchestrator::getAnticipationBeats(const OrchestratorInput& input) const
{
    // CONSERVATIVE DEFAULT for accompaniment
    // Extreme anticipation only in very specific conditions

    if (!input.context.isChordChange) {
        return 0.0;  // No anticipation if not approaching chord change
    }

    // Default: very subtle anticipation
    double anticipation = 0.3;  // Less than half a beat

    // Only increase if expression is safe AND we're at a climax
    if (isExpressionSafe(input) && isClimaxMoment(input)) {
        if (input.context.energy > 0.7) {
            anticipation = 0.5;  // Half beat - noticeable but not extreme
        }
        // NOTE: We intentionally cap at 0.5 for accompaniment
        // Evans' 1-2 beat anticipation is for SOLO piano, not accompaniment
    }

    return anticipation;
}

// ========== Mode Selection: User Busy ==========

PianoTextureOrchestrator::TextureMode
PianoTextureOrchestrator::selectModeWhenUserBusy(const OrchestratorInput& input) const
{
    const double energy = input.context.energy;

    // If cymbal crash, back off completely
    if (input.rhythmSection.cymbalCrash) {
        return TextureMode::Space;
    }

    // Low energy: minimal support
    if (energy < 0.3) {
        // Only play on chord changes
        if (input.context.isChordChange) {
            return TextureMode::Support;
        }
        return TextureMode::Space;
    }

    // Medium energy: support with occasional color
    if (energy < 0.6) {
        if (input.context.isChordChange) {
            return TextureMode::SupportWithColor;
        }
        return TextureMode::Support;
    }

    // Higher energy: more active support
    // But still NOT competing with soloist
    if (input.context.isChordChange) {
        return TextureMode::SupportWithColor;
    }

    // Check if shell anticipation is appropriate
    if (input.rhythmSection.strongBeatComing && input.context.beatsUntilChordChange <= 1) {
        return TextureMode::ShellAnticipation;
    }

    return TextureMode::Support;
}

// ========== Mode Selection: User Silent ==========

PianoTextureOrchestrator::TextureMode
PianoTextureOrchestrator::selectModeWhenUserSilent(const OrchestratorInput& input) const
{
    const double silenceDuration = input.soloist.userSilenceDuration;
    const double energy = input.context.energy;

    // Brief silence (< 2 beats): don't rush in
    if (silenceDuration < 2.0) {
        return TextureMode::Support;
    }

    // Short silence (2-4 beats): balanced comping
    if (silenceDuration < 4.0) {
        if (energy > 0.4) {
            return TextureMode::BalancedComp;
        }
        return TextureMode::Support;
    }

    // Extended silence (4+ beats): can do more
    if (isExtendedSilence(input)) {
        if (energy > 0.5) {
            return TextureMode::Fill;
        }
        if (energy > 0.3) {
            return TextureMode::Dialogue;
        }
        return TextureMode::BalancedComp;
    }

    return TextureMode::Support;
}

// ========== Mode Selection: Structural Moment ==========

PianoTextureOrchestrator::TextureMode
PianoTextureOrchestrator::selectModeForStructuralMoment(const OrchestratorInput& input) const
{
    // Phrase ending
    if (input.context.isPhraseEnd) {
        if (input.context.cadence01 > 0.7) {
            // Strong cadence: resolution
            return TextureMode::Resolution;
        }
        return TextureMode::SupportWithColor;
    }

    // Climax point
    if (input.context.isClimaxPoint) {
        if (input.context.energy > 0.8 && !input.soloist.userBusy) {
            // High energy climax without soloist: block chord OK
            return TextureMode::BlockChord;
        }
        return TextureMode::SupportWithColor;
    }

    return TextureMode::Support;
}

// ========== Left Hand Role Assignment ==========

PianoTextureOrchestrator::HandRole
PianoTextureOrchestrator::assignLeftHandRole(TextureMode mode, const OrchestratorInput& input) const
{
    HandRole role;

    // Default register for LH
    role.registerLow = 48;
    role.registerHigh = 64;

    // Avoid collision with bass
    if (input.rhythmSection.bassIsPlaying) {
        role.registerLow = std::max(role.registerLow,
                                    input.rhythmSection.bassRegisterHigh + 3);
    }

    switch (mode) {
    case TextureMode::Space:
        role.voicing = VoicingRole::None;
        role.timing = TimingRole::Rest;
        break;

    case TextureMode::Support:
    case TextureMode::SupportWithColor:
    case TextureMode::BalancedComp:
        role.voicing = VoicingRole::Rootless;
        role.timing = TimingRole::OnBeat;
        break;

    case TextureMode::Fill:
    case TextureMode::Dialogue:
        role.voicing = VoicingRole::Rootless;
        role.timing = TimingRole::OnBeat;
        role.durationMult = 1.5;  // Sustain longer for fills
        break;

    case TextureMode::ShellAnticipation:
        role.voicing = VoicingRole::Shell;
        role.timing = TimingRole::Anticipate;
        break;

    case TextureMode::DelayedEntry:
        role.voicing = VoicingRole::Rootless;
        role.timing = TimingRole::Delay;
        break;

    case TextureMode::BlockChord:
        role.voicing = VoicingRole::Block;
        role.timing = TimingRole::WithOther;
        break;

    case TextureMode::Resolution:
        role.voicing = VoicingRole::Rootless;
        role.timing = TimingRole::OnBeat;
        role.velocityMult = 0.9;  // Slightly softer for resolution
        role.durationMult = 1.3;  // Sustain for warmth
        break;
    }

    return role;
}

// ========== Right Hand Role Assignment ==========

PianoTextureOrchestrator::HandRole
PianoTextureOrchestrator::assignRightHandRole(TextureMode mode, const OrchestratorInput& input) const
{
    HandRole role;

    // Default register for RH (above LH)
    role.registerLow = 65;
    role.registerHigh = 84;

    switch (mode) {
    case TextureMode::Space:
    case TextureMode::Support:
        // RH defaults to REST in accompaniment mode
        role.voicing = VoicingRole::None;
        role.timing = TimingRole::Rest;
        break;

    case TextureMode::SupportWithColor:
        // Color dyad on chord changes only
        if (input.context.isChordChange) {
            role.voicing = VoicingRole::Dyad;
            role.timing = TimingRole::OnBeat;
            role.accentTop = true;  // Bring out color tone
        } else {
            role.voicing = VoicingRole::None;
            role.timing = TimingRole::Rest;
        }
        break;

    case TextureMode::BalancedComp:
        role.voicing = VoicingRole::Triad;
        role.timing = TimingRole::OnBeat;
        break;

    case TextureMode::Fill:
        role.voicing = VoicingRole::MelodicDyad;
        role.timing = TimingRole::OnBeat;
        role.targetTopMidi = input.soloist.userMeanMidi + 7;  // Above user's range
        break;

    case TextureMode::Dialogue:
        role.voicing = VoicingRole::Dyad;
        role.timing = TimingRole::Respond;  // After LH statement
        break;

    case TextureMode::ShellAnticipation:
    case TextureMode::DelayedEntry:
        // RH rests during bass coordination modes
        role.voicing = VoicingRole::None;
        role.timing = TimingRole::Rest;
        break;

    case TextureMode::BlockChord:
        role.voicing = VoicingRole::Block;
        role.timing = TimingRole::WithOther;
        break;

    case TextureMode::Resolution:
        role.voicing = VoicingRole::Triad;
        role.timing = TimingRole::OnBeat;
        role.velocityMult = 0.85;  // Softer than LH
        break;
    }

    return role;
}

// ========== Timing Offset Calculation ==========

int PianoTextureOrchestrator::calculateLhTimingOffset(TextureMode mode,
                                                       const OrchestratorInput& input) const
{
    const double energy = input.context.energy;

    switch (mode) {
    case TextureMode::ShellAnticipation:
        // Anticipate by about half a beat (assuming ~120 BPM, ~250ms per beat)
        return -100;  // 100ms early

    case TextureMode::DelayedEntry:
        // Delay to let bass establish
        return 80;  // 80ms late

    default:
        // Slight lay-back at low energy, on-beat at high energy
        if (energy < 0.3) {
            return 12;  // Very slight lay-back
        }
        if (energy > 0.7) {
            return 0;   // On beat for driving feel
        }
        return 8;       // Default subtle lay-back
    }
}

int PianoTextureOrchestrator::calculateRhTimingOffset(TextureMode mode,
                                                       const OrchestratorInput& input) const
{
    // RH typically slightly after LH (Evans signature)
    int lhOffset = calculateLhTimingOffset(mode, input);

    if (mode == TextureMode::BlockChord) {
        // Block chords: simultaneous
        return lhOffset;
    }

    if (mode == TextureMode::Dialogue) {
        // Dialogue: RH responds after LH
        return lhOffset + 150;  // 150ms after LH
    }

    // Default: RH 5-10ms after LH for depth
    return lhOffset + 7;
}

// ========== Safety Constraints ==========

PianoTextureOrchestrator::TextureDecision
PianoTextureOrchestrator::applySafetyConstraints(TextureDecision decision,
                                                  const OrchestratorInput& input) const
{
    // PRIORITY 1: CLARITY
    // If soloist is busy, ensure we're not doing anything confusing

    if (input.soloist.userBusy) {
        // Never use hemiola when user is playing
        decision.useHemiola = false;

        // Cap anticipation when user is playing
        if (decision.lhTimingOffsetMs < -80) {
            decision.lhTimingOffsetMs = -80;  // Max 80ms early
        }

        // RH should mostly rest
        if (decision.rightHand.voicing != VoicingRole::None &&
            decision.rightHand.voicing != VoicingRole::Dyad) {
            // Downgrade to dyad at most
            if (!input.context.isChordChange) {
                decision.rightHand.voicing = VoicingRole::None;
                decision.rightHand.timing = TimingRole::Rest;
            }
        }
    }

    // PRIORITY 2: SUPPORT
    // Ensure stable foundation

    // Don't do dramatic pauses if user might be confused
    if (decision.dramaticPause && input.soloist.userBusy) {
        decision.dramaticPause = false;
    }

    // PRIORITY 3: COLOR (allow if 1 & 2 satisfied)
    // Inner voice motion OK if we have clarity and support

    // PRIORITY 4: EXPRESSION (very restrictive)
    if (!isExpressionSafe(input)) {
        decision.useHemiola = false;
        // Already handled anticipation above
    }

    return decision;
}

// ========== Expression Safety ==========

bool PianoTextureOrchestrator::isExpressionSafe(const OrchestratorInput& input) const
{
    // Expression is safe when ALL of these are true:
    // 1. User is not playing
    if (input.soloist.userBusy) {
        return false;
    }

    // 2. We've had enough silence to be sure user isn't about to play
    if (input.soloist.userSilenceDuration < 2.0) {
        return false;
    }

    // 3. Not at a confusing moment (like just after a chord change)
    if (input.context.isChordChange) {
        return false;
    }

    // 4. Drummer isn't doing something that would clash
    if (input.rhythmSection.drumFillInProgress) {
        return false;
    }

    return true;
}

bool PianoTextureOrchestrator::isClimaxMoment(const OrchestratorInput& input) const
{
    return input.context.isClimaxPoint ||
           (input.context.isPhraseEnd && input.context.cadence01 > 0.6);
}

bool PianoTextureOrchestrator::isExtendedSilence(const OrchestratorInput& input) const
{
    return input.soloist.userSilenceDuration >= 4.0;  // 4+ beats of silence
}

// =============================================================================
// STAGE 6: RHYTHMIC PHRASE SYSTEM
// =============================================================================

// ========== Main Phrase Generation ==========

PianoTextureOrchestrator::RhythmicPhrase
PianoTextureOrchestrator::generateRhythmicPhrase(const OrchestratorInput& input, quint32 hash) const
{
    // Select the appropriate phrase type for context
    RhythmicPhraseType type = selectPhraseType(input, hash);

    // Generate the specific phrase pattern
    switch (type) {
    case RhythmicPhraseType::Sustained:
        return generateSustainedPhrase(input);
    case RhythmicPhraseType::Punctuation:
        return generatePunctuationPhrase(input);
    case RhythmicPhraseType::Hemiola:
        return generateHemiolaPhrase(input, hash);
    case RhythmicPhraseType::DisplacedShell:
        return generateDisplacedShellPhrase(input);
    case RhythmicPhraseType::Conversational:
        return generateConversationalPhrase(input, hash);
    case RhythmicPhraseType::Unison:
        return generateUnisonPhrase(input);
    case RhythmicPhraseType::DramaticPause:
    default:
        // Dramatic pause: nothing plays
        RhythmicPhrase pause;
        pause.type = RhythmicPhraseType::DramaticPause;
        pause.density = 0.0;
        pause.description = "Dramatic pause - intentional silence";
        return pause;
    }
}

// ========== Phrase Type Selection ==========

PianoTextureOrchestrator::RhythmicPhraseType
PianoTextureOrchestrator::selectPhraseType(const OrchestratorInput& input, quint32 hash) const
{
    const double energy = input.context.energy;
    const bool userBusy = input.soloist.userBusy;
    const bool isPhraseEnd = input.context.isPhraseEnd;
    const bool isChordChange = input.context.isChordChange;
    const double silenceDuration = input.soloist.userSilenceDuration;

    // =====================================================================
    // DRAMATIC PAUSE: After climactic moments, give space
    // =====================================================================
    if (isPhraseEnd && energy > 0.6 && !userBusy && (hash % 100) < 30) {
        return RhythmicPhraseType::DramaticPause;
    }

    // =====================================================================
    // USER BUSY: Conservative patterns that support, don't compete
    // =====================================================================
    if (userBusy) {
        // Very low energy: sustained (minimal activity)
        if (energy < 0.25) {
            return RhythmicPhraseType::Sustained;
        }

        // Phrase ending with chord change: punctuation
        if (isPhraseEnd && isChordChange) {
            return RhythmicPhraseType::Punctuation;
        }

        // Medium energy with chord change: displaced shell (subtle anticipation)
        if (energy > 0.4 && isChordChange && input.context.beatsUntilChordChange <= 1) {
            return RhythmicPhraseType::DisplacedShell;
        }

        // Higher energy: unison comping (reinforced texture)
        if (energy > 0.55 && (hash % 100) < int(energy * 40)) {
            return RhythmicPhraseType::Unison;
        }

        // Default when user is busy: sustained (safe)
        return RhythmicPhraseType::Sustained;
    }

    // =====================================================================
    // USER SILENT: More freedom for expression (but still careful)
    // =====================================================================

    // Brief silence: still be conservative
    if (silenceDuration < 2.0) {
        return RhythmicPhraseType::Sustained;
    }

    // Extended silence with moderate energy: conversational
    if (silenceDuration >= 4.0 && energy > 0.35 && energy < 0.7) {
        if ((hash % 100) < 50) {
            return RhythmicPhraseType::Conversational;
        }
    }

    // Hemiola: VERY RARE - only with extended silence, moderate energy, not at phrase boundaries
    // This creates the "floating" Evans feel but can confuse the user if overused
    if (silenceDuration >= 4.0 && energy > 0.45 && energy < 0.75 &&
        !isPhraseEnd && !isChordChange && (hash % 100) < 15) {  // Only 15% chance!
        return RhythmicPhraseType::Hemiola;
    }

    // Higher energy silence: unison for power
    if (energy > 0.6 && (hash % 100) < int(energy * 50)) {
        return RhythmicPhraseType::Unison;
    }

    // Phrase boundary: punctuation
    if (isPhraseEnd) {
        return RhythmicPhraseType::Punctuation;
    }

    // Default: sustained
    return RhythmicPhraseType::Sustained;
}

// ========== Individual Phrase Generators ==========

PianoTextureOrchestrator::RhythmicPhrase
PianoTextureOrchestrator::generateSustainedPhrase(const OrchestratorInput& input) const
{
    RhythmicPhrase phrase;
    phrase.type = RhythmicPhraseType::Sustained;
    phrase.description = "Sustained - held voicing with inner motion";

    const double energy = input.context.energy;
    phrase.density = calculatePhraseDensity(energy, input.soloist.userBusy);

    // LH: Play on beat 1, maybe beat 3 at higher energy
    phrase.lhPlays[0] = true;  // Always beat 1
    phrase.lhPlays[1] = false;
    phrase.lhPlays[2] = energy > 0.5;  // Beat 3 at moderate+ energy
    phrase.lhPlays[3] = false;

    // RH: Mostly rests, color on chord changes only
    phrase.rhPlays[0] = input.context.isChordChange && energy > 0.3;
    phrase.rhPlays[1] = false;
    phrase.rhPlays[2] = false;
    phrase.rhPlays[3] = false;

    // Subtle lay-back for relaxed feel
    phrase.lhTimingMs[0] = (energy < 0.4) ? 10 : 5;
    phrase.lhTimingMs[2] = 8;  // Beat 3 slightly laid back

    return phrase;
}

PianoTextureOrchestrator::RhythmicPhrase
PianoTextureOrchestrator::generatePunctuationPhrase(const OrchestratorInput& input) const
{
    RhythmicPhrase phrase;
    phrase.type = RhythmicPhraseType::Punctuation;
    phrase.description = "Punctuation - accent then space";

    const double energy = input.context.energy;
    phrase.density = 0.25;  // Just one hit

    // Single strong hit on beat 1, then rest
    phrase.lhPlays[0] = true;
    phrase.lhPlays[1] = false;
    phrase.lhPlays[2] = false;
    phrase.lhPlays[3] = false;

    // RH joins for emphasis at higher energy
    phrase.rhPlays[0] = energy > 0.4;
    phrase.rhPlays[1] = false;
    phrase.rhPlays[2] = false;
    phrase.rhPlays[3] = false;

    // Slightly early for definitive feel at phrase boundaries
    phrase.lhTimingMs[0] = -5;
    phrase.rhTimingMs[0] = -3;  // RH slightly after LH

    return phrase;
}

PianoTextureOrchestrator::RhythmicPhrase
PianoTextureOrchestrator::generateHemiolaPhrase(const OrchestratorInput& input, quint32 hash) const
{
    RhythmicPhrase phrase;
    phrase.type = RhythmicPhraseType::Hemiola;
    phrase.description = "Hemiola - 3-against-4 floating feel (RARE)";

    const double energy = input.context.energy;
    phrase.density = 0.5;  // Medium density

    // Hemiola: 3 notes spread across 4 beats creates floating tension
    // Pattern: hit on beats 1, 2.5-ish, 4 (or variations)
    // This is approximate - the "feel" matters more than exact placement

    int pattern = hash % 3;
    switch (pattern) {
    case 0:
        // Pattern A: 1, (2&), 4
        phrase.lhPlays[0] = true;
        phrase.lhPlays[1] = true;   // This will be offset to feel like "2&"
        phrase.lhPlays[2] = false;
        phrase.lhPlays[3] = true;
        phrase.lhTimingMs[1] = 80;  // Push beat 2 late to feel like "&2"
        break;
    case 1:
        // Pattern B: 1, 3, (4&)
        phrase.lhPlays[0] = true;
        phrase.lhPlays[1] = false;
        phrase.lhPlays[2] = true;
        phrase.lhPlays[3] = true;
        phrase.lhTimingMs[3] = 60;  // Push beat 4 late
        break;
    default:
        // Pattern C: (1&), 2, 4
        phrase.lhPlays[0] = true;
        phrase.lhPlays[1] = true;
        phrase.lhPlays[2] = false;
        phrase.lhPlays[3] = true;
        phrase.lhTimingMs[0] = 50;  // Push beat 1 late
        break;
    }

    // RH: minimal involvement in hemiola (LH drives the rhythm)
    phrase.rhPlays[0] = energy > 0.5 && input.context.isChordChange;
    phrase.rhPlays[1] = false;
    phrase.rhPlays[2] = false;
    phrase.rhPlays[3] = false;

    return phrase;
}

PianoTextureOrchestrator::RhythmicPhrase
PianoTextureOrchestrator::generateDisplacedShellPhrase(const OrchestratorInput& input) const
{
    RhythmicPhrase phrase;
    phrase.type = RhythmicPhraseType::DisplacedShell;
    phrase.description = "Displaced shell - anticipates bass root";

    phrase.density = 0.35;

    // Shell on beat 4 anticipating the next bar's beat 1 (where bass plays root)
    // This creates forward motion without extreme anticipation
    phrase.lhPlays[0] = false;  // Let bass establish on beat 1
    phrase.lhPlays[1] = false;
    phrase.lhPlays[2] = false;
    phrase.lhPlays[3] = true;   // Shell on beat 4

    // Beat 4 is slightly early (anticipation of next bar)
    phrase.lhTimingMs[3] = -30;  // 30ms early - subtle but noticeable

    // RH rests during displacement (don't muddy the texture)
    phrase.rhPlays[0] = false;
    phrase.rhPlays[1] = false;
    phrase.rhPlays[2] = false;
    phrase.rhPlays[3] = false;

    phrase.hasAnticipation = true;
    phrase.anticipationBeat = 3;  // Beat 4 (0-indexed)

    return phrase;
}

PianoTextureOrchestrator::RhythmicPhrase
PianoTextureOrchestrator::generateConversationalPhrase(const OrchestratorInput& input, quint32 hash) const
{
    RhythmicPhrase phrase;
    phrase.type = RhythmicPhraseType::Conversational;
    phrase.description = "Conversational - LH/RH alternate in dialogue";

    const double energy = input.context.energy;
    phrase.density = calculatePhraseDensity(energy, false);

    // Conversational: hands take turns, filling each other's gaps
    int pattern = hash % 4;
    switch (pattern) {
    case 0:
        // LH: 1, 3; RH: 2
        phrase.lhPlays[0] = true;
        phrase.lhPlays[1] = false;
        phrase.lhPlays[2] = true;
        phrase.lhPlays[3] = false;
        phrase.rhPlays[0] = false;
        phrase.rhPlays[1] = true;
        phrase.rhPlays[2] = false;
        phrase.rhPlays[3] = false;
        break;
    case 1:
        // LH: 1; RH: 2, 4
        phrase.lhPlays[0] = true;
        phrase.lhPlays[1] = false;
        phrase.lhPlays[2] = false;
        phrase.lhPlays[3] = false;
        phrase.rhPlays[0] = false;
        phrase.rhPlays[1] = true;
        phrase.rhPlays[2] = false;
        phrase.rhPlays[3] = energy > 0.5;
        break;
    case 2:
        // LH: 1, 4; RH: 3
        phrase.lhPlays[0] = true;
        phrase.lhPlays[1] = false;
        phrase.lhPlays[2] = false;
        phrase.lhPlays[3] = true;
        phrase.rhPlays[0] = false;
        phrase.rhPlays[1] = false;
        phrase.rhPlays[2] = true;
        phrase.rhPlays[3] = false;
        break;
    default:
        // LH: 1; RH: 3
        phrase.lhPlays[0] = true;
        phrase.lhPlays[1] = false;
        phrase.lhPlays[2] = false;
        phrase.lhPlays[3] = false;
        phrase.rhPlays[0] = false;
        phrase.rhPlays[1] = false;
        phrase.rhPlays[2] = true;
        phrase.rhPlays[3] = false;
        break;
    }

    // RH responds slightly after where LH would be (call-response feel)
    phrase.rhTimingMs[1] = 50;
    phrase.rhTimingMs[2] = 40;
    phrase.rhTimingMs[3] = 30;

    return phrase;
}

PianoTextureOrchestrator::RhythmicPhrase
PianoTextureOrchestrator::generateUnisonPhrase(const OrchestratorInput& input) const
{
    RhythmicPhrase phrase;
    phrase.type = RhythmicPhraseType::Unison;
    phrase.description = "Unison - LH/RH together for reinforced texture";

    const double energy = input.context.energy;
    phrase.density = calculatePhraseDensity(energy, input.soloist.userBusy);

    // Unison: both hands strike together
    // Pattern depends on energy
    if (energy > 0.7) {
        // High energy: 1, 2, 3 (driving)
        phrase.lhPlays[0] = true;
        phrase.lhPlays[1] = true;
        phrase.lhPlays[2] = true;
        phrase.lhPlays[3] = false;
    } else if (energy > 0.5) {
        // Medium-high: 1, 3
        phrase.lhPlays[0] = true;
        phrase.lhPlays[1] = false;
        phrase.lhPlays[2] = true;
        phrase.lhPlays[3] = false;
    } else {
        // Medium: 1 only
        phrase.lhPlays[0] = true;
        phrase.lhPlays[1] = false;
        phrase.lhPlays[2] = false;
        phrase.lhPlays[3] = false;
    }

    // RH mirrors LH exactly for unison
    for (int i = 0; i < 4; ++i) {
        phrase.rhPlays[i] = phrase.lhPlays[i];
    }

    // Simultaneous attack (no offset between hands)
    // Both slightly on-beat or slightly early for punch
    int baseOffset = (energy > 0.6) ? -5 : 3;
    for (int i = 0; i < 4; ++i) {
        phrase.lhTimingMs[i] = baseOffset;
        phrase.rhTimingMs[i] = baseOffset;
    }

    return phrase;
}

// ========== Phrase Query Methods ==========

bool PianoTextureOrchestrator::shouldPlayBeatInPhrase(const RhythmicPhrase& phrase,
                                                       int beatInBar, bool isLH) const
{
    if (beatInBar < 0 || beatInBar > 3) return false;
    return isLH ? phrase.lhPlays[beatInBar] : phrase.rhPlays[beatInBar];
}

int PianoTextureOrchestrator::getTimingOffsetForBeat(const RhythmicPhrase& phrase,
                                                      int beatInBar, bool isLH) const
{
    if (beatInBar < 0 || beatInBar > 3) return 0;
    return isLH ? phrase.lhTimingMs[beatInBar] : phrase.rhTimingMs[beatInBar];
}

// ========== Extreme Anticipation (VERY RESTRICTIVE) ==========

bool PianoTextureOrchestrator::isExtremeAnticipationAppropriate(const OrchestratorInput& input) const
{
    // ===========================================================================
    // EXTREME ANTICIPATION: Only appropriate 1-2 times per song, at most.
    // This is the Evans signature move, but in ACCOMPANIMENT context it must be
    // used EXTREMELY sparingly or it will confuse the soloist.
    //
    // ALL of the following conditions must be TRUE:
    // ===========================================================================

    // 1. User must NOT be playing (they need to hear this coming)
    if (input.soloist.userBusy) {
        return false;
    }

    // 2. Extended silence (4+ beats) - user is clearly resting
    if (input.soloist.userSilenceDuration < 4.0) {
        return false;
    }

    // 3. Approaching a STRONG cadence (V-I or similar)
    if (input.context.cadence01 < 0.7) {
        return false;
    }

    // 4. At a phrase boundary
    if (!input.context.isPhraseEnd) {
        return false;
    }

    // 5. High energy (building to climax)
    if (input.context.energy < 0.65) {
        return false;
    }

    // 6. Next chord exists and is a resolution target (I or i)
    // (We're anticipating the resolution, not just any chord)
    if (!input.context.hasNextChord) {
        return false;
    }

    // 7. Drummer is not filling (don't compete)
    if (input.rhythmSection.drumFillInProgress) {
        return false;
    }

    // 8. No cymbal crash (would mask the anticipation)
    if (input.rhythmSection.cymbalCrash) {
        return false;
    }

    // ALL conditions met - this is a rare, special moment
    return true;
}

// ========== Density Calculation ==========

double PianoTextureOrchestrator::calculatePhraseDensity(double energy, bool userBusy) const
{
    // When user is busy: always sparse
    if (userBusy) {
        return 0.15 + energy * 0.25;  // Range: 0.15 - 0.40
    }

    // When user is silent: can be more active
    if (energy < 0.3) {
        return 0.20 + energy * 0.30;  // Range: 0.20 - 0.29
    }
    if (energy < 0.6) {
        return 0.30 + (energy - 0.3) * 0.50;  // Range: 0.30 - 0.45
    }

    // High energy: more filled but never overwhelming
    return 0.45 + (energy - 0.6) * 0.40;  // Range: 0.45 - 0.61
}

} // namespace playback
