#pragma once

#include <QString>
#include <QVector>

#include "virtuoso/groove/GrooveGrid.h"

namespace virtuoso::vocab {

// A small, data-driven "phrase/pattern vocabulary" layer.
// MVP scope:
// - Beat-scoped patterns (per beat-in-bar) for 4/4, tuned for cool jazz ballad language.
// - Deterministic selection: no RNG state; selection is derived from a stable hash of the query.
class VocabularyRegistry {
public:
    struct PianoHit {
        // GrooveGrid::fromBarBeatTuplet(bar, beatInBar, sub, count)
        int sub = 0;
        int count = 1;

        // durationWhole = dur_num / dur_den (whole notes)
        int dur_num = 1;
        int dur_den = 4;

        int vel_delta = 0;         // added to planner baseVelocity (before macro scaling / humanizer)
        QString density = "full";  // "full" | "guide"
    };

    struct PianoBeatQuery {
        virtuoso::groove::TimeSignature ts{4, 4};
        int playbackBarIndex = 0;
        int beatInBar = 0; // 0-based
        QString chordText;
        QString chordFunction; // "Tonic" | "Subdominant" | "Dominant" | "Other" (optional)
        bool chordIsNew = false;
        bool userSilence = false;
        double energy = 0.25; // 0..1
        quint32 determinismSeed = 1;
    };

    struct PianoBeatChoice {
        QString id;
        QVector<PianoHit> hits;
        QString notes; // description for debugging / future TheoryEvent fields
    };

    // --- Phrase-level rhythmic vocabulary (multi-bar) ---
    struct PianoPhraseHit {
        int barOffset = 0; // 0..phraseBars-1
        int beatInBar = 0; // 0-based
        PianoHit hit;
    };

    struct PianoPhraseQuery {
        virtuoso::groove::TimeSignature ts{4, 4};
        int playbackBarIndex = 0;
        int beatInBar = 0;
        QString chordText;
        QString chordFunction; // optional
        bool chordIsNew = false;
        bool userSilence = false;
        double energy = 0.25;
        quint32 determinismSeed = 1;
        int phraseBars = 4;
    };

    struct PianoPhraseChoice {
        QString id;
        int phraseBars = 4;
        QVector<PianoPhraseHit> hits;
        QString notes;
    };

    // --- Piano top-line vocabulary (phrase-level) ---
    // This is the "melodic mind": a named library of phrase-level top-line cells with rhythm+degree intent.
    struct PianoTopLineHit {
        int barOffset = 0; // 0..phraseBars-1
        int beatInBar = 0; // 0-based
        int sub = 0;
        int count = 1;
        int dur_num = 1;
        int dur_den = 8;
        int vel_delta = -10;
        int degree = 9;      // 1,3,5,7,9,11,13
        int neighborDir = 0; // -1/+1 for neighbor/enclosure; 0 for direct tones
        bool resolve = false;
        QString tag;         // e.g. "a", "b", "resolve", "mem:sequence"
    };

    struct PianoTopLineQuery {
        virtuoso::groove::TimeSignature ts{4, 4};
        int playbackBarIndex = 0;
        int beatInBar = 0;
        QString chordText;
        QString chordFunction; // optional
        bool chordIsNew = false;
        bool userSilence = false;
        double energy = 0.25;
        double rhythmicComplexity = 0.25;
        double interaction = 0.50;
        quint32 determinismSeed = 1;
        int phraseBars = 4;
    };

    struct PianoTopLineChoice {
        QString id;
        int phraseBars = 4;
        QVector<PianoTopLineHit> hits;
        QString notes;
    };

    struct PianoTopLinePatternDef {
        QString id;
        int phraseBars = 4;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool allowWhenUserSilence = true;
        QVector<QString> chordFunctions; // empty => any
        QVector<PianoTopLineHit> hits;
        QString notes;
    };

    // --- Piano gesture vocabulary (roll/arp/touch) ---
    struct PianoGestureQuery {
        virtuoso::groove::TimeSignature ts{4, 4};
        int bpm = 60;
        int playbackBarIndex = 0;
        int beatInBar = 0;
        QString chordText;
        QString chordFunction;
        bool chordIsNew = false;
        bool userSilence = false;
        bool cadence = false;
        double energy = 0.25;
        double rhythmicComplexity = 0.25;
        quint32 determinismSeed = 1;
        int noteCount = 3; // size of voicing hit
    };

    struct PianoGestureChoice {
        QString id;
        QString kind;      // "none"|"roll"|"arp"|"broken"|"strum"
        QString style;     // "up"|"down"|"inside_out"|...
        int spreadMs = 0;  // timing spread for roll/arp
        QString notes;
    };

    struct PianoGesturePatternDef {
        QString id;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool cadenceOnly = false;
        bool chordIsNewOnly = false;
        bool allowWhenUserSilence = true;
        int minNoteCount = 2;
        int maxNoteCount = 10;
        int maxBpm = 999;
        QString kind;
        QString style;
        int spreadMs = 0;
        QString notes;
    };

    // --- Piano pedal strategy vocabulary ---
    struct PianoPedalQuery {
        virtuoso::groove::TimeSignature ts{4, 4};
        int playbackBarIndex = 0;
        int beatInBar = 0;
        QString chordText;
        QString chordFunction;
        bool chordIsNew = false;
        bool userBusy = false;
        bool userSilence = false;
        bool nextChanges = false;
        int beatsUntilChordChange = 0;
        double energy = 0.25;
        double toneDark = 0.60;
        quint32 determinismSeed = 1;
    };

    struct PianoPedalChoice {
        QString id;
        QString defaultState; // "up"|"half"|"down"
        bool repedalOnNewChord = false;
        int repedalProbPct = 50;
        bool clearBeforeChange = false;
        int clearSub = 3;   // 16th index within beat (count=4)
        int clearCount = 4; // typically 4
        QString notes;
    };

    struct PianoPedalPatternDef {
        QString id;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool allowWhenUserSilence = true;
        QString defaultState;
        bool repedalOnNewChord = false;
        int repedalProbPct = 50;
        bool clearBeforeChange = true;
        int clearSub = 3;
        int clearCount = 4;
        QString notes;
    };

    // UI browsing helpers (copy out loaded definitions)
    struct PianoPatternDef {
        QString id;
        QVector<int> beats;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool chordIsNewOnly = false;
        bool stableOnly = false;
        bool allowWhenUserSilence = true;
        QVector<QString> chordFunctions; // empty => any
        QVector<PianoHit> hits;
        QString notes;
    };

    enum class BassAction {
        None,
        Rest,
        Root,
        Fifth,
        Third,
        ApproachToNext, // half-step approach into next bar
        PickupToNext    // upbeat pickup into next bar
    };

    struct BassBeatQuery {
        virtuoso::groove::TimeSignature ts{4, 4};
        int playbackBarIndex = 0;
        int beatInBar = 0; // 0-based
        QString chordText;
        bool chordIsNew = false;
        bool hasNextChord = false;
        bool nextChanges = false;
        bool userDenseOrPeak = false;
        double energy = 0.25; // 0..1
        quint32 determinismSeed = 1;
    };

    struct BassBeatChoice {
        QString id;
        BassAction action = BassAction::None;
        // Placement within the beat (only relevant for PickupToNext)
        int sub = 0;
        int count = 1;
        int dur_num = 1;
        int dur_den = 4;
        int vel_delta = 0;
        QString notes;
    };

    struct BassPhraseHit {
        int barOffset = 0;
        int beatInBar = 0;
        BassAction action = BassAction::None;
        int sub = 0;
        int count = 1;
        int dur_num = 1;
        int dur_den = 4;
        int vel_delta = 0;
        QString notes;
    };

    struct BassPhraseQuery {
        virtuoso::groove::TimeSignature ts{4, 4};
        int playbackBarIndex = 0;
        int beatInBar = 0;
        QString chordText;
        bool chordIsNew = false;
        bool hasNextChord = false;
        bool nextChanges = false;
        bool userDenseOrPeak = false;
        double energy = 0.25;
        quint32 determinismSeed = 1;
        int phraseBars = 4;
    };

    struct BassPhraseChoice {
        QString id;
        int phraseBars = 4;
        QVector<BassPhraseHit> hits;
        QString notes;
    };

    struct BassPatternDef {
        QString id;
        QVector<int> beats;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool chordIsNewOnly = false;
        bool stableOnly = false;
        bool nextChangesOnly = false;
        bool forbidWhenUserDenseOrPeak = false;
        BassAction action = BassAction::None;
        int sub = 0;
        int count = 1;
        int dur_num = 1;
        int dur_den = 4;
        int vel_delta = 0;
        QString notes;
    };

    enum class DrumArticulation {
        RideHit,
        RideBell,
        SnareSwish,
        BrushShort
    };

    struct DrumHit {
        DrumArticulation articulation = DrumArticulation::RideHit;
        int sub = 0;
        int count = 1;
        int dur_num = 1;
        int dur_den = 16;
        int vel_delta = 0;
    };

    struct DrumsBeatQuery {
        virtuoso::groove::TimeSignature ts{4, 4};
        int playbackBarIndex = 0;
        int beatInBar = 0; // 0-based
        double energy = 0.25; // 0..1
        bool intensityPeak = false;
        quint32 determinismSeed = 1;
    };

    struct DrumsBeatChoice {
        QString id;
        QVector<DrumHit> hits;
        QString notes;
    };

    struct DrumsPhraseHit {
        int barOffset = 0;
        int beatInBar = 0;
        DrumHit hit;
    };

    struct DrumsPhraseQuery {
        virtuoso::groove::TimeSignature ts{4, 4};
        int playbackBarIndex = 0;
        int beatInBar = 0;
        double energy = 0.25;
        bool intensityPeak = false;
        quint32 determinismSeed = 1;
        int phraseBars = 4;
    };

    struct DrumsPhraseChoice {
        QString id;
        int phraseBars = 4;
        QVector<DrumsPhraseHit> hits;
        QString notes;
    };

    struct DrumsPatternDef {
        QString id;
        QVector<int> beats;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool intensityPeakOnly = false;
        QVector<DrumHit> hits;
        QString notes;
    };

    bool loadFromJsonBytes(const QByteArray& json, QString* outError = nullptr);
    bool loadFromResourcePath(const QString& resourcePath, QString* outError = nullptr);

    bool isLoaded() const { return m_loaded; }
    QString lastError() const { return m_lastError; }

    PianoBeatChoice choosePianoBeat(const PianoBeatQuery& q) const;
    BassBeatChoice chooseBassBeat(const BassBeatQuery& q) const;
    DrumsBeatChoice chooseDrumsBeat(const DrumsBeatQuery& q) const;

    PianoPhraseChoice choosePianoPhrase(const PianoPhraseQuery& q) const;
    PianoTopLineChoice choosePianoTopLine(const PianoTopLineQuery& q) const;
    PianoGestureChoice choosePianoGesture(const PianoGestureQuery& q) const;
    PianoPedalChoice choosePianoPedal(const PianoPedalQuery& q) const;
    BassPhraseChoice chooseBassPhrase(const BassPhraseQuery& q) const;
    DrumsPhraseChoice chooseDrumsPhrase(const DrumsPhraseQuery& q) const;

    QVector<PianoHit> pianoPhraseHitsForBeat(const PianoPhraseQuery& q, QString* outPhraseId = nullptr, QString* outPhraseNotes = nullptr) const;
    QVector<BassPhraseHit> bassPhraseHitsForBeat(const BassPhraseQuery& q, QString* outPhraseId = nullptr, QString* outPhraseNotes = nullptr) const;
    QVector<DrumHit> drumsPhraseHitsForBeat(const DrumsPhraseQuery& q, QString* outPhraseId = nullptr, QString* outPhraseNotes = nullptr) const;

    QVector<PianoPatternDef> pianoPatterns() const;
    QVector<BassPatternDef> bassPatterns() const;
    QVector<DrumsPatternDef> drumsPatterns() const;

    QVector<PianoPhraseChoice> pianoPhrasePatterns() const;
    QVector<PianoTopLinePatternDef> pianoTopLinePatterns() const;
    QVector<PianoGesturePatternDef> pianoGesturePatterns() const;
    QVector<PianoPedalPatternDef> pianoPedalPatterns() const;
    QVector<BassPhraseChoice> bassPhrasePatterns() const;
    QVector<DrumsPhraseChoice> drumsPhrasePatterns() const;

private:
    struct PianoBeatPattern {
        QString id;
        QVector<int> beats; // allowed beatInBar values
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool chordIsNewOnly = false;
        bool stableOnly = false;
        bool allowWhenUserSilence = true;
        QVector<QString> chordFunctions; // empty => any
        QVector<PianoHit> hits;
        QString notes;
    };

    struct BassBeatPattern {
        QString id;
        QVector<int> beats;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool chordIsNewOnly = false;
        bool stableOnly = false;
        bool nextChangesOnly = false;
        bool forbidWhenUserDenseOrPeak = false;
        BassAction action = BassAction::None;
        int sub = 0;
        int count = 1;
        int dur_num = 1;
        int dur_den = 4;
        int vel_delta = 0;
        QString notes;
    };

    struct DrumsBeatPattern {
        QString id;
        QVector<int> beats;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool intensityPeakOnly = false;
        QVector<DrumHit> hits;
        QString notes;
    };

    struct PianoPhrasePattern {
        QString id;
        int phraseBars = 4;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool allowWhenUserSilence = true;
        QVector<QString> chordFunctions; // empty => any
        QVector<PianoPhraseHit> hits;
        QString notes;
    };

    struct PianoTopLinePattern {
        QString id;
        int phraseBars = 4;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool allowWhenUserSilence = true;
        QVector<QString> chordFunctions; // empty => any
        QVector<PianoTopLineHit> hits;
        QString notes;
    };

    struct PianoGesturePattern {
        QString id;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool cadenceOnly = false;
        bool chordIsNewOnly = false;
        bool allowWhenUserSilence = true;
        int minNoteCount = 2;
        int maxNoteCount = 10;
        int maxBpm = 999;
        QString kind;
        QString style;
        int spreadMs = 0;
        QString notes;
    };

    struct PianoPedalPattern {
        QString id;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool allowWhenUserSilence = true;
        QString defaultState;
        bool repedalOnNewChord = false;
        int repedalProbPct = 50;
        bool clearBeforeChange = true;
        int clearSub = 3;
        int clearCount = 4;
        QString notes;
    };

    struct BassPhrasePattern {
        QString id;
        int phraseBars = 4;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool forbidWhenUserDenseOrPeak = false;
        QVector<BassPhraseHit> hits;
        QString notes;
    };

    struct DrumsPhrasePattern {
        QString id;
        int phraseBars = 4;
        double minEnergy = 0.0;
        double maxEnergy = 1.0;
        double weight = 1.0;
        bool intensityPeakOnly = false;
        QVector<DrumsPhraseHit> hits;
        QString notes;
    };

    static quint32 fnv1a32(const QByteArray& bytes);
    static bool energyMatches(double e, double minE, double maxE);
    static int clampBeat(int beatInBar);

    template <typename TPattern, typename TChoice, typename TMakeChoiceFn>
    static TChoice chooseWeighted(const QVector<TPattern>& patterns,
                                  quint32 pickHash,
                                  const TMakeChoiceFn& makeChoice);

    bool m_loaded = false;
    QString m_lastError;

    QVector<PianoBeatPattern> m_piano;
    QVector<BassBeatPattern> m_bass;
    QVector<DrumsBeatPattern> m_drums;

    QVector<PianoPhrasePattern> m_pianoPhrases;
    QVector<PianoTopLinePattern> m_pianoTopLines;
    QVector<PianoGesturePattern> m_pianoGestures;
    QVector<PianoPedalPattern> m_pianoPedals;
    QVector<BassPhrasePattern> m_bassPhrases;
    QVector<DrumsPhrasePattern> m_drumsPhrases;
};

} // namespace virtuoso::vocab

