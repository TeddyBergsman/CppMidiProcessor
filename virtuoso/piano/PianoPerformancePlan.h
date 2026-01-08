#pragma once

#include <QString>
#include <QVector>

#include "virtuoso/groove/GrooveGrid.h"

namespace virtuoso::piano {

enum class Hand {
    Unknown = 0,
    Left,
    Right,
};

struct PianoNoteIntent {
    int midi = 60;
    int velocity = 90;
    groove::GridPos startPos;
    groove::Rational durationWhole{1, 4};

    Hand hand = Hand::Unknown;
    QString voiceId; // e.g. "lh", "rh", "top"
    QString role;    // e.g. "comp", "topline", "gesture"
};

enum class PedalActionKind {
    Set = 0,       // set CC64 value (0/64/127)
    Lift,          // explicitly lift (0)
    Repedal,       // down-up-down within a small window (represented as multiple Set actions)
    ClearOnChange, // semantic tag for explainability
};

struct PedalAction {
    int cc64Value = 0; // 0..127, we use 0/64/127 (up/half/down)
    groove::GridPos startPos;
    PedalActionKind kind = PedalActionKind::Set;
};

enum class RollKind {
    None = 0,
    RolledHands,
    Arpeggiated,
    Strum,
};

struct PianoGesture {
    RollKind kind = RollKind::None;
    int strumDelayMs = 0;
    QString accentProfile; // freeform tag for explainability
};

struct PianoPerformancePlan {
    QVector<PianoNoteIntent> notes;
    QVector<PedalAction> pedal;
    QVector<PianoGesture> gestures;

    // Explainability (used in candidate_pool).
    QString pedalProfile;   // e.g. "HalfPedal+Repedal"
    QString gestureProfile; // e.g. "RolledHands"
    QString toplineSummary; // e.g. "target=7 resolve"

    // Library IDs (auditable).
    QString compPhraseId;
    QString compBeatId; // beat-level cell used for this beat (if any)
    QString toplinePhraseId;
    QString gestureId;
    QString pedalId;
};

} // namespace virtuoso::piano

