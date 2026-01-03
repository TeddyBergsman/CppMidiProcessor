#pragma once

#include <QString>

#include "music/ChordSymbol.h"
#include "music/BassProfile.h"

namespace music {

struct BassDecision {
    int midiNote = -1; // 0..127
    int velocity = 0;
};

struct BassEvent {
    int midiNote = -1;
    int velocity = 0;
    // Offset within the beat: 0.0 = on-beat, 0.5 = upbeat 8th, etc.
    double offsetBeats = 0.0;
    // Length as fraction of a beat (0 => use profile gate/noteLength)
    double lengthBeats = 0.0;
    // If true, treat as “dead/ghost” note (short, quiet).
    bool ghost = false;
};

struct BassBeatContext {
    int barIndex = 0;
    int beatInBar = 0; // 0..3
    int barInSection = 0;
    bool isNewBar = false;
    bool isSectionChange = false;
    bool isPhraseEnd = false;
    int phraseLengthBars = 4;
    quint32 sectionHash = 0;
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

    // Higher-level API: returns 1..N events for this beat (supports pickups, ghost notes, syncopation).
    QVector<BassEvent> nextBeat(const BassBeatContext& ctx, const ChordSymbol* currentChord, const ChordSymbol* nextChord);

    void reset();

private:
    BassProfile m_profile;
    int m_lastMidi = -1;
    int m_lastBarBeat = -1;
    int m_lastStepPc = -1;
    quint32 m_rngState = 1;

    // Evolving state
    double m_intensity = 0.35; // 0..1
    quint32 m_lastSectionHash = 0;
};

} // namespace music

