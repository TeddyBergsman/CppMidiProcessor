#pragma once

#include <QString>

#include "virtuoso/groove/GrooveGrid.h"

namespace virtuoso::groove {

enum class FeelKind {
    Straight = 0,
    Swing2to1,
    Swing3to1,
    LaidBackPocket,
};

// A minimal feel template (offset-map) for Stage 1 groove.
// Later this can become a data-driven library and include per-subdivision maps.
struct FeelTemplate {
    QString key;          // stable id, e.g. "swing_2to1"
    QString name;         // display name
    FeelKind kind = FeelKind::Straight;

    // 0..1 scaling of the template effect (swing/pocket), where 0 disables template offsets.
    double amount = 1.0;

    // For pocket templates: ms to add on weak positions (positive = laid back).
    int pocketMs = 0;

    static FeelTemplate straight() {
        FeelTemplate t;
        t.key = "straight";
        t.name = "Straight";
        t.kind = FeelKind::Straight;
        t.amount = 1.0;
        return t;
    }

    static FeelTemplate swing2to1(double amount = 1.0) {
        FeelTemplate t;
        t.key = "swing_2to1";
        t.name = "Swing (2:1)";
        t.kind = FeelKind::Swing2to1;
        t.amount = amount;
        return t;
    }

    static FeelTemplate swing3to1(double amount = 1.0) {
        FeelTemplate t;
        t.key = "swing_3to1";
        t.name = "Swing (3:1)";
        t.kind = FeelKind::Swing3to1;
        t.amount = amount;
        return t;
    }

    static FeelTemplate laidBackPocket(int pocketMs, double amount = 1.0) {
        FeelTemplate t;
        t.key = "laid_back_pocket";
        t.name = "Laid-back pocket";
        t.kind = FeelKind::LaidBackPocket;
        t.amount = amount;
        t.pocketMs = pocketMs;
        return t;
    }

    // Returns *template-only* timing offset in ms (does not include instrument push/jitter/drift).
    int offsetMsFor(const GridPos& pos, const TimeSignature& ts, int bpm) const {
        if (amount <= 0.0) return 0;

        int beatInBar = 0;
        Rational withinBeat{0, 1};
        GrooveGrid::splitWithinBar(pos, ts, beatInBar, withinBeat);
        const Rational beat = GrooveGrid::beatDurationWhole(ts);

        // withinBeatNormalized = withinBeat / beat
        // Since beat is (1/den) whole notes, dividing is multiplying by den.
        const Rational withinBeatNormalized(withinBeat.num * beat.den, withinBeat.den * beat.num);

        auto beatMs = [&]() -> double {
            // beat unit is 1/den whole notes.
            if (bpm <= 0) bpm = 120;
            const double quarterMs = 60000.0 / double(bpm);
            return quarterMs * (4.0 / double(ts.den));
        };

        switch (kind) {
        case FeelKind::Straight:
            return 0;

        case FeelKind::Swing2to1:
        case FeelKind::Swing3to1: {
            // MVP: only swing the upbeat 8th (exactly half the beat).
            if (withinBeatNormalized != Rational(1, 2)) return 0;
            const double ratio = (kind == FeelKind::Swing3to1) ? 3.0 : 2.0;
            const double newFrac = ratio / (ratio + 1.0); // e.g. 2/3, 3/4
            const double deltaFrac = newFrac - 0.5;
            return int(llround(deltaFrac * beatMs() * amount));
        }

        case FeelKind::LaidBackPocket: {
            // MVP: lay back beats 2 and 4, and slightly lay back the upbeat 8th.
            int ms = 0;
            if ((beatInBar % 2) == 1) ms += pocketMs;
            if (withinBeatNormalized == Rational(1, 2)) ms += int(llround(0.5 * double(pocketMs)));
            return int(llround(double(ms) * amount));
        }
        }

        return 0;
    }
};

} // namespace virtuoso::groove

