#pragma once

#include <QString>
#include <QVector>

#include "music/ChordSymbol.h"
#include "virtuoso/constraints/PianoDriver.h"
#include "virtuoso/engine/VirtuosoEngine.h"
#include "virtuoso/groove/GrooveGrid.h"
#include "virtuoso/theory/FunctionalHarmony.h"

namespace playback {

// Deterministic ballad piano comp planner:
// - Rootless A/B + shells
// - Simple voice-leading via "nearest pitch class to previous voicing" heuristic
// - Constraint-gated by PianoDriver (polyphony + span)
class JazzBalladPianoPlanner {
public:
    struct CompHit {
        int beatInBar = 0;     // 0-based
        int sub = 0;           // sub-index within count
        int count = 1;         // subdivision count (2=eighths, 4=sixteenths, 3=triplets)
        virtuoso::groove::Rational dur{1, 4};
        int velDelta = 0;
        QString density = "guide"; // "guide" or "full"
        QString rhythmTag;         // e.g. "charleston", "push4", "delay2"
    };

    struct TopHit {
        int beatInBar = 0;     // 0-based
        int sub = 0;           // sub-index within count
        int count = 1;         // subdivision count
        virtuoso::groove::Rational dur{1, 8};
        int velDelta = -10;
        int pc = -1;           // 0..11
        bool resolve = false;  // true if this is the resolution note of a motif
        QString tag;           // e.g. "neighbor_resolve", "enclosure"
    };

    struct TopTemplateHit {
        int beatInBar = 0;
        int sub = 0;
        int count = 1;
        virtuoso::groove::Rational dur{1, 8};
        int velDelta = -10;
        int degree = 9;      // preferred degree (1,3,5,7,9,11,13)
        int neighborDir = 0; // -1/+1 when this is a neighbor/enclosure tone; 0 for direct tones
        bool resolve = false;
        QString tag;
    };

    struct Context {
        int bpm = 60;
        int playbackBarIndex = 0;
        int beatInBar = 0;
        bool chordIsNew = false;
        music::ChordSymbol chord;
        QString chordText;
        quint32 determinismSeed = 1;

        // Reference tuning knobs (Chet Baker – My Funny Valentine: sparse, airy, gentle).
        // Ranges are MIDI note numbers.
        int lhLo = 50, lhHi = 66;      // guide tones
        int rhLo = 67, rhHi = 84;      // main color tones
        int sparkleLo = 84, sparkleHi = 96; // optional top sparkle

        double skipBeat2ProbStable = 0.45;   // if chord is stable, often skip beat 2
        double addSecondColorProb = 0.25;    // add a second color tone sometimes
        double sparkleProbBeat4 = 0.18;      // occasional high sparkle on beat 4
        bool preferShells = true;            // favor shells over thicker rootless

        // Listening MVP (optional): comping space + interaction.
        bool userDensityHigh = false;
        bool userIntensityPeak = false;
        bool userRegisterHigh = false;
        bool userSilence = false;

        // Macro dynamics / debug forcing
        bool forceClimax = false;
        double energy = 0.25; // 0..1

        // Phrase model (lightweight, deterministic): 4-bar phrases by default.
        int phraseBars = 4;
        int barInPhrase = 0;     // 0..phraseBars-1
        bool phraseEndBar = false;
        double cadence01 = 0.0;  // 0..1 (stronger at phrase end / turnarounds)

        // Key context (optional; used to keep RH line and colors tonal).
        bool hasKey = false;
        int keyTonicPc = 0; // 0..11
        virtuoso::theory::KeyMode keyMode = virtuoso::theory::KeyMode::Major;

        // Lookahead for anticipations (optional).
        music::ChordSymbol nextChord;
        bool hasNextChord = false;
        bool nextChanges = false;

        // Stage 3 solver weights (Virtuosity Matrix-style, 0..1).
        double harmonicRisk = 0.20;        // 0=triads/shells, 1=more tensions/UST-ish
        double rhythmicComplexity = 0.25;  // 0=simpler placement, 1=more motion
        double interaction = 0.50;         // 0=backing track, 1=conversational
        double toneDark = 0.60;            // 0=bright/open, 1=dark/warm (bias register + density)
    };

    JazzBalladPianoPlanner();

    void reset();

    // Deprecated (kept for compatibility): pianist is fully procedural.
    void setVocabulary(const void*) {}

    QVector<virtuoso::engine::AgentIntentNote> planBeat(const Context& c,
                                                        int midiChannel,
                                                        const virtuoso::groove::TimeSignature& ts);

private:
    static int thirdIntervalForQuality(music::ChordQuality q);
    static int fifthIntervalForQuality(music::ChordQuality q);
    static int seventhIntervalFor(const music::ChordSymbol& c);

    static int pcForDegree(const music::ChordSymbol& c, int degree);
    static int nearestMidiForPc(int pc, int around, int lo, int hi);
    static int bestNearestToPrev(int pc, const QVector<int>& prev, int lo, int hi);

    static void sortUnique(QVector<int>& v);
    static QVector<int> makeRootlessA(const music::ChordSymbol& c);
    static QVector<int> makeRootlessB(const music::ChordSymbol& c);
    static QVector<int> makeShell(const music::ChordSymbol& c);

    bool feasible(const QVector<int>& midiNotes) const;
    QVector<int> repairToFeasible(QVector<int> midiNotes) const;

    void ensureBarRhythmPlanned(const Context& c);
    QVector<CompHit> chooseBarCompRhythm(const Context& c) const;
    QVector<TopHit> chooseBarTopLine(const Context& c) const;
    void ensureMotifBlockPlanned(const Context& c);
    void buildMotifBlockTemplates(const Context& c);
    QVector<TopHit> realizeTopTemplate(const Context& c, const QVector<TopTemplateHit>& tmpl) const;
    void ensurePhraseGuideLinePlanned(const Context& c);
    int chooseGuidePcForChord(const music::ChordSymbol& chord, int prevGuidePc, bool preferResolve) const;

    virtuoso::constraints::PianoDriver m_driver;
    QVector<int> m_lastVoicing;

    int m_lastRhythmBar = -1;
    QVector<CompHit> m_barHits;
    int m_lastTopMidi = -1; // right-hand top-line continuity
    QVector<TopHit> m_barTopHits;

    // Phrase-spanning guide-tone line (3rds/7ths) for "tonal + intentional" RH.
    int m_phraseGuideStartBar = -1;
    int m_phraseGuideBars = 4;
    QVector<int> m_phraseGuidePcByBar; // size=m_phraseGuideBars, values 0..11

    // Upper-structure memory (for resolutions).
    int m_lastUpperBar = -1;
    QVector<int> m_lastUpperPcs; // 0..11 (size 2-3)

    int m_motifBlockStartBar = -1; // even bar index of current 2-bar block
    QVector<TopTemplateHit> m_motifA;
    QVector<TopTemplateHit> m_motifB;
    QVector<TopTemplateHit> m_motifC;
    QVector<TopTemplateHit> m_motifD;
    int m_phraseMotifStartBar = -1; // multiple of 4 (phrase start)

    // Coherence: keep a stable "hand position" pitch-class set across a 2-bar block.
    int m_anchorBlockStartBar = -1;
    QString m_anchorChordText;
    QVector<int> m_anchorPcs; // pitch classes 0..11

    // Arpeggiation anti-repeat (deterministic).
    int m_lastArpBar = -1;
    int m_lastArpStyle = -1;
};

} // namespace playback

