#pragma once

#include <QString>

#include "music/ChordSymbol.h"
#include "music/BassProfile.h"

namespace music {

struct BassDecision {
    int midiNote = -1; // 0..127
    int velocity = 0;
};

class WalkingBassGenerator {
public:
    explicit WalkingBassGenerator();

    void setProfile(const BassProfile& p);
    const BassProfile& profile() const { return m_profile; }

    // Generates the next bass note for a 4/4 bar, quarter-note walking feel.
    // beatInBar: 0..3
    // currentChord / nextChord can be nullptr meaning "no info".
    BassDecision nextNote(int beatInBar, const ChordSymbol* currentChord, const ChordSymbol* nextChord);

    void reset();

private:
    BassProfile m_profile;
    int m_lastMidi = -1;
    int m_lastBarBeat = -1;
    int m_lastStepPc = -1;
    quint32 m_rngState = 1;
};

} // namespace music

