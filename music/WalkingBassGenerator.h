#pragma once

#include <QString>
#include <QHash>

#include "music/ChordSymbol.h"
#include "music/BassProfile.h"

namespace music {

struct BassDecision {
    int midiNote = -1; // 0..127
    int velocity = 0;
};

struct BassEvent {
    enum class Role {
        MusicalNote, // normal pitched bass notes (will be octave-shifted by the playback engine)
        KeySwitch,   // articulation keyswitch notes (must never be transposed)
        FxSound      // FX sound notes (must never be transposed)
    };

    int midiNote = -1;
    int velocity = 0;
    // Offset within the beat: 0.0 = on-beat, 0.5 = upbeat 8th, etc.
    double offsetBeats = 0.0;
    // Length as fraction of a beat (0 => use profile gate/noteLength)
    double lengthBeats = 0.0;
    // If true, treat as “dead/ghost” note (short, quiet).
    bool ghost = false;
    // If true, do not re-articulate if same note continues (tie across beats).
    bool tie = false;
    // If true, explicit rest (no note). This event is ignored by the engine.
    bool rest = false;
    // Event role (musical vs keyswitch/fx).
    Role role = Role::MusicalNote;
    // If true, the engine should NOT clamp this note shorter to avoid overlap with the next musical event.
    // Used for legato/overlap articulations (e.g., E0/F0-based legato).
    bool allowOverlap = false;
};

struct BassBeatContext {
    int barIndex = 0;
    int beatInBar = 0; // 0..3
    int barInSection = 0;
    bool isNewBar = false;
    bool isSectionChange = false;
    bool isPhraseEnd = false;
    bool isNewChord = false;
    int phraseLengthBars = 4;
    quint32 sectionHash = 0;
    int songPass = 0;     // 0..(totalPasses-1)
    int totalPasses = 1;
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

    // Multi-beat planning and resolution targeting
    QHash<int, QVector<BassEvent>> m_planned; // globalBeat -> events
    int m_forcedStrongPc = -1;               // if set, prefer this pc on next strong beat

    // Phrase-level planner state (so it doesn't feel static)
    int m_phraseMode = 1; // 0=sparse, 1=normal, 2=busy

    // Motif memory (phrase-level melodic identity)
    QVector<int> m_motifSteps; // semitone steps (in pitch classes, signed)
    int m_motifIndex = 0;
    bool m_hasMotif = false;

    // Cross-beat legato support: we extend note lengths slightly and, on the next beat,
    // decide whether to trigger HP/Legato Slide based on the actual interval.
    bool m_pendingCrossBeatLegato = false;
    int m_pendingCrossBeatFromMidi = -1;
};

} // namespace music

