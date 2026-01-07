#pragma once

#include <QString>
#include <QVector>

#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/groove/GrooveGrid.h"
#include "virtuoso/drums/FluffyAudioJazzDrumsBrushesMapping.h"

namespace playback {

// Brushes Ballad Drummer v1:
// - Deterministic-by-default, with seeded stochastic variation (via qHash + bar/beat).
// - Generates a continuous snare brush loop texture plus sparse accents (swish) and feather kick.
// - Phrase awareness is MVP-simple: default phrase length is 4 bars.
class BrushesBalladDrummer {
public:
    struct Profile {
        // Channels (1..16). Drums must be channel 6 (1-based) for VST routing.
        int channel = 6;

        // FluffyAudio mapping defaults
        int noteKick = virtuoso::drums::fluffy_brushes::kKickLooseNormal_G0;
        int noteSnareSwish = virtuoso::drums::fluffy_brushes::kSnareRightHand_D1;
        int noteBrushLoopA = virtuoso::drums::fluffy_brushes::kBrushCircleTwoHands_Fs3; // looping
        int noteBrushLoopB = virtuoso::drums::fluffy_brushes::kBrushCircleRightHand_G3; // looping
        int noteBrushShort = virtuoso::drums::fluffy_brushes::kBrushShortRightHand_Gs3;
        int noteRideHit = virtuoso::drums::fluffy_brushes::kRideHitBorder_Ds2;
        int noteRideSwish = virtuoso::drums::fluffy_brushes::kRideSwish2_E2;

        // Phrase model (bars)
        int phraseBars = 4;       // 4-bar phrasing for MVP
        int loopRetriggerBars = 4; // retrigger loop once per phrase to avoid constant re-articulation

        // Texture behavior
        int minLoopHoldMs = 6000; // must be long enough to actually hit the loop body
        int loopHoldBars = 4;     // also hold for at least N bars at current tempo

        // Probabilities (0..1)
        double kickProbOnBeat1 = 0.08;       // feather kick <10%
        double swishProbOn2And4 = 0.90;      // usually present
        double swishAltShortProb = 0.20;     // sometimes use a short brush hand articulation instead of a snare hit
        double phraseEndSwishProb = 0.35;    // a longer swish/ride at phrase end

        // Velocities (baseVelocity before humanizer)
        int velLoop = 28;
        int velSwish = 34;
        int velKick = 18;
        int velPhraseEnd = 26;
    };

    struct Context {
        int bpm = 60;
        virtuoso::groove::TimeSignature ts{4, 4};
        int playbackBarIndex = 0; // absolute playback bar index
        int beatInBar = 0;        // 0-based
        bool structural = false;  // strong beat/chord arrival proxy
        quint32 determinismSeed = 1;

        // Interaction/macro-dynamics (MVP)
        double energy = 0.25;       // 0..1
        bool intensityPeak = false; // user peak -> drummer supports with brief cymbal pattern

        // Phrase model (lightweight, deterministic): 4-bar phrases by default.
        int phraseBars = 4;
        int barInPhrase = 0;     // 0..phraseBars-1
        bool phraseEndBar = false;
        double cadence01 = 0.0;  // 0..1 (stronger at phrase end / turnarounds)

        // Joint-solver knobs (embodiment controls):
        // - gestureBias: -1=dry/minimal, +1=gesture-forward (more phrase pickups/swell).
        // - allowRide: if false, suppress ride pattern switching (keep snare brush texture only).
        // - allowPhraseGestures: if false, suppress phrase setup/end gestures entirely.
        double gestureBias = 0.0; // -1..+1
        bool allowRide = true;
        bool allowPhraseGestures = true;
    };

    BrushesBalladDrummer() = default;
    explicit BrushesBalladDrummer(const Profile& p) : m_p(p) {}

    const Profile& profile() const { return m_p; }
    void setProfile(const Profile& p) { m_p = p; }

    // Returns AgentIntentNotes with:
    // - agent="Drums"
    // - channel=profile.channel
    // - startPos set by caller (or left at default) — this function sets it.
    QVector<virtuoso::engine::AgentIntentNote> planBeat(const Context& ctx) const;

private:
    static double unitRand01(quint32 x);
    static quint32 mixSeed(quint32 a, quint32 b);
    static virtuoso::groove::Rational durationWholeFromHoldMs(int holdMs, int bpm);
    static int msForBars(int bpm, const virtuoso::groove::TimeSignature& ts, int bars);

    Profile m_p;
};

} // namespace playback

