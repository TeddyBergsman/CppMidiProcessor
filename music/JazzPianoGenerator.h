#pragma once

#include <QString>
#include <QVector>
#include <QHash>

#include "music/ChordSymbol.h"
#include "music/PianoProfile.h"

namespace music {

struct PianoEvent {
    enum class Kind {
        Note,
        CC,
    };

    Kind kind = Kind::Note;

    // Note fields
    int midiNote = -1;
    int velocity = 0;
    double lengthBeats = 0.0;  // 0 => derived by playback engine

    // CC fields
    int cc = 0;
    int ccValue = 0;

    // Shared timing
    double offsetBeats = 0.0; // 0.0 = on-beat, 0.5 = upbeat 8th, etc.

    // Explainability (only populated when PianoProfile::reasoningLogEnabled is true)
    QString function;
    QString reasoning;
};

struct PianoBeatContext {
    int barIndex = 0;
    int beatInBar = 0; // 0..3
    int tempoBpm = 120;
    QVector<ChordSymbol> lookaheadChords; // beat-aligned lookahead (0 = current beat)

    int barInSection = 0;
    bool isNewBar = false;
    bool isSectionChange = false;
    bool isPhraseEnd = false;
    bool isNewChord = false;
    int phraseLengthBars = 4;
    quint32 sectionHash = 0;
    int songPass = 0;
    int totalPasses = 1;
};

class JazzPianoGenerator {
public:
    explicit JazzPianoGenerator();

    void setProfile(const PianoProfile& p);
    const PianoProfile& profile() const { return m_profile; }

    QVector<PianoEvent> nextBeat(const PianoBeatContext& ctx, const ChordSymbol* currentChord, const ChordSymbol* nextChord);

    void reset();

private:
    PianoProfile m_profile;

    // Deterministic per-song RNG.
    quint32 m_rngState = 1;
    quint32 nextU32();
    double next01();

    // Voicing memory for voice-leading and repetition control.
    QVector<int> m_lastLh; // MIDI notes
    QVector<int> m_lastRh; // MIDI notes
    quint32 m_lastVoicingHash = 0;

    // Cross-beat planning (so rhythm/phrasing isn't random per beat).
    QHash<int, QVector<PianoEvent>> m_planned; // globalBeat -> events
    int m_lastPlannedGlobalBeat = -1;
    int m_lastPatternId = -1;
    int m_lastTopMidi = -1;

    // Helper methods
    int globalBeatIndex(const PianoBeatContext& ctx) const;
    QVector<int> chooseVoicingPitchClasses(const ChordSymbol& chord, bool rootless, bool& outUsedTension);
    QVector<int> realizeToMidi(const QVector<int>& pcs, int lo, int hi, const QVector<int>& prev, int maxLeap) const;
    static quint32 hashNotes(const QVector<int>& notes);

    struct VoicingPcs {
        QVector<int> lh; // pitch classes
        QVector<int> rh; // pitch classes
        bool usedTension = false;
    };
    VoicingPcs buildBasicChordPcs(const ChordSymbol& chord);
    VoicingPcs buildLiteralChordPcs(const ChordSymbol& chord);
    VoicingPcs buildEvansVoicingPcs(const ChordSymbol& chord, bool ballad);
    VoicingPcs buildTraditionalVoicingPcs(const ChordSymbol& chord,
                                         const ChordSymbol* nextChord,
                                         bool ballad,
                                         bool rootless);
    void planBar(const PianoBeatContext& ctx, const ChordSymbol& cur, const ChordSymbol* nextChord);
};

} // namespace music

