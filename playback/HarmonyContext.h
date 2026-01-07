#pragma once

#include <QSet>
#include <QString>
#include <QVector>

#include "chart/ChartModel.h"
#include "music/ChordSymbol.h"
#include "playback/HarmonyTypes.h"
#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/theory/FunctionalHarmony.h"

namespace playback {

// HarmonyContext: parses chart harmony, tracks current chord, computes key/local-key context,
// and provides analysis helpers (roman/function, scale suggestions).
class HarmonyContext final {
public:
    void setOntology(const virtuoso::ontology::OntologyRegistry* ont) { m_ont = ont; }

    void resetRuntimeState();

    void rebuildFromModel(const chart::ChartModel& model);

    // Runtime chord tracking (mutates internal last-chord state).
    bool chordForCellIndex(const chart::ChartModel& model, int cellIndex, music::ChordSymbol& outChord, bool& isNewChord);

    // Stateless parse: never mutates last-chord state.
    music::ChordSymbol parseCellChordNoState(const chart::ChartModel& model,
                                             int cellIndex,
                                             const music::ChordSymbol& fallback,
                                             bool* outIsExplicit = nullptr) const;

    // Key context accessors
    bool hasKeyPcGuess() const { return m_hasKeyPcGuess; }
    int keyPcGuess() const { return m_keyPcGuess; }
    const QString& keyScaleKey() const { return m_keyScaleKey; }
    const QString& keyScaleName() const { return m_keyScaleName; }
    virtuoso::theory::KeyMode keyMode() const { return m_keyMode; }
    const QVector<LocalKeyEstimate>& localKeysByBar() const { return m_localKeysByBar; }

    // Sliding-window key estimate starting at barIndex (uses forward window of `windowBars`).
    // This is the canonical "lookahead key window" used at runtime.
    LocalKeyEstimate estimateLocalKeyWindow(const chart::ChartModel& model, int barIndex, int windowBars) const;

    bool hasLastChord() const { return m_hasLastChord; }
    const music::ChordSymbol& lastChord() const { return m_lastChord; }

    // Analysis helpers
    static bool sameChordKey(const music::ChordSymbol& a, const music::ChordSymbol& b);
    static int normalizePc(int pc);
    static QString pcName(int pc);

    const virtuoso::ontology::ChordDef* chordDefForSymbol(const music::ChordSymbol& c) const;

    struct ScaleChoice {
        QString key;       // ontology scale key (e.g. "altered")
        QString name;      // ontology scale name (e.g. "Altered")
        int transposePc = 0; // 0..11, best transposition/root for display
        QString display;   // preformatted, e.g. "Altered (Ab)"
    };

    // Suggests the best scale choice, also fills roman/function (if provided).
    ScaleChoice chooseScaleForChord(int keyPc,
                                   virtuoso::theory::KeyMode keyMode,
                                   const music::ChordSymbol& chordSym,
                                   const virtuoso::ontology::ChordDef& chordDef,
                                   QString* outRoman = nullptr,
                                   QString* outFunction = nullptr) const;

    // Back-compat: display string only.
    QString chooseScaleUsedForChord(int keyPc,
                                   virtuoso::theory::KeyMode keyMode,
                                   const music::ChordSymbol& chordSym,
                                   const virtuoso::ontology::ChordDef& chordDef,
                                   QString* outRoman = nullptr,
                                   QString* outFunction = nullptr) const {
        return chooseScaleForChord(keyPc, keyMode, chordSym, chordDef, outRoman, outFunction).display;
    }

private:
    static QVector<const chart::Bar*> flattenBarsFrom(const chart::ChartModel& model);
    static QString ontologyChordKeyFor(const music::ChordSymbol& c);
    static QSet<int> pitchClassesForChordDef(int rootPc, const virtuoso::ontology::ChordDef& chord);
    static virtuoso::theory::KeyMode keyModeForScaleKey(const QString& k);

    void estimateGlobalKeyByScale(const QVector<music::ChordSymbol>& chords, int fallbackPc);
    QVector<LocalKeyEstimate> estimateLocalKeysByBar(const QVector<const chart::Bar*>& bars,
                                                     int windowBars,
                                                     int fallbackTonicPc,
                                                     const QString& fallbackScaleKey,
                                                     const QString& fallbackScaleName,
                                                     virtuoso::theory::KeyMode fallbackMode) const;

    const virtuoso::ontology::OntologyRegistry* m_ont = nullptr; // not owned

    // Runtime chord tracking
    music::ChordSymbol m_lastChord;
    bool m_hasLastChord = false;

    // Chart-derived key context
    int m_keyPcGuess = 0;
    bool m_hasKeyPcGuess = false;
    QString m_keyScaleKey;
    QString m_keyScaleName;
    virtuoso::theory::KeyMode m_keyMode = virtuoso::theory::KeyMode::Major;
    QVector<LocalKeyEstimate> m_localKeysByBar;
};

} // namespace playback

