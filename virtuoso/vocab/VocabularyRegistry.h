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

    bool loadFromJsonBytes(const QByteArray& json, QString* outError = nullptr);
    bool loadFromResourcePath(const QString& resourcePath, QString* outError = nullptr);

    bool isLoaded() const { return m_loaded; }
    QString lastError() const { return m_lastError; }

    PianoBeatChoice choosePianoBeat(const PianoBeatQuery& q) const;
    BassBeatChoice chooseBassBeat(const BassBeatQuery& q) const;
    DrumsBeatChoice chooseDrumsBeat(const DrumsBeatQuery& q) const;

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
};

} // namespace virtuoso::vocab

