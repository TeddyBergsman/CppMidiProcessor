#include "chart/ChartModel.h"

#include "playback/HarmonyContext.h"
#include "playback/LookaheadPlanner.h"
#include "playback/SemanticMidiAnalyzer.h"
#include "playback/VibeStateMachine.h"
#include "playback/JazzBalladBassPlanner.h"
#include "playback/JazzBalladPianoPlanner.h"
#include "playback/BrushesBalladDrummer.h"

#include "music/ChordSymbol.h"
#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/memory/MotifTransform.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QtGlobal>

namespace {

static int g_failures = 0;

static void expect(bool cond, const QString& msg) {
    if (!cond) {
        ++g_failures;
        qWarning().noquote() << "FAIL:" << msg;
    }
}

static void expectStrEq(const QString& a, const QString& b, const QString& msg) {
    expect(a == b, msg + QString(" (got '%1' expected '%2')").arg(a, b));
}

static chart::ChartModel makeOneBarChart(const QString& chord0) {
    chart::ChartModel m;
    chart::Line l;
    chart::Bar b;
    b.cells.resize(4);
    b.cells[0].chord = chord0;
    l.bars.push_back(b);
    m.lines.push_back(l);
    m.timeSigNum = 4;
    m.timeSigDen = 4;
    return m;
}

} // namespace

static void testLookaheadPlannerJsonDeterminism() {
    using namespace playback;

    const virtuoso::ontology::OntologyRegistry ont = virtuoso::ontology::OntologyRegistry::builtins();

    HarmonyContext harmony;
    harmony.setOntology(&ont);

    const chart::ChartModel model = makeOneBarChart("Cmaj7");
    harmony.rebuildFromModel(model);

    QVector<int> sequence;
    sequence << 0 << 1 << 2 << 3; // 4 beats -> 4 cells

    SemanticMidiAnalyzer listener;
    VibeStateMachine vibe;
    JazzBalladBassPlanner bass;
    JazzBalladPianoPlanner piano;
    piano.setOntology(&ont);
    BrushesBalladDrummer drummer;

    LookaheadPlanner::Inputs in;
    in.bpm = 120;
    in.ts = {4, 4};
    in.repeats = 1;
    in.model = &model;
    in.sequence = &sequence;
    in.hasLastChord = false;
    in.harmonyCtx = &harmony;
    in.keyWindowBars = 4;
    in.listener = &listener;
    in.vibe = &vibe;
    in.bassPlanner = &bass;
    in.pianoPlanner = &piano;
    in.drummer = &drummer;
    in.stylePresetKey = "jazz_brushes_ballad_60_evans";
    in.debugEnergyAuto = false;
    in.debugEnergy = 0.25;
    in.virtAuto = false;
    in.virtHarmonicRisk = 0.2;
    in.virtRhythmicComplexity = 0.2;
    in.virtInteraction = 0.2;
    in.virtToneDark = 0.6;
    in.engineNowMs = 123;
    in.nowMs = 1234567890;

    const QString a = LookaheadPlanner::buildLookaheadPlanJson(in, /*stepNow=*/0, /*horizonBars=*/1);
    const QString b = LookaheadPlanner::buildLookaheadPlanJson(in, /*stepNow=*/0, /*horizonBars=*/1);
    expectStrEq(a, b, "LookaheadPlanner JSON is stable for fixed inputs");

    // Sanity: parse to JSON array.
    const auto doc = QJsonDocument::fromJson(a.toUtf8());
    expect(doc.isArray(), "LookaheadPlanner output parses as JSON array");
    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        expect(!arr.isEmpty(), "LookaheadPlanner output array non-empty");
    }
}

static void testHarmonyContextKeyWindowAndFunctionalTagging() {
    using namespace playback;

    const virtuoso::ontology::OntologyRegistry ont = virtuoso::ontology::OntologyRegistry::builtins();
    HarmonyContext harmony;
    harmony.setOntology(&ont);

    // II-V-I in C major across 4 bars.
    chart::ChartModel model;
    model.timeSigNum = 4;
    model.timeSigDen = 4;
    chart::Line l;
    l.bars.resize(4);
    for (auto& b : l.bars) b.cells.resize(4);
    l.bars[0].cells[0].chord = "Cmaj7";
    l.bars[1].cells[0].chord = "Dmin7";
    l.bars[2].cells[0].chord = "G7";
    l.bars[3].cells[0].chord = "Cmaj7";
    model.lines.push_back(l);

    harmony.rebuildFromModel(model);

    const auto lk = harmony.estimateLocalKeyWindow(model, /*barIndex=*/0, /*windowBars=*/4);
    expect(!lk.scaleKey.trimmed().isEmpty(), "HarmonyContext key window: scaleKey populated");
    expect(!lk.scaleName.trimmed().isEmpty(), "HarmonyContext key window: scaleName populated");
    const auto* sc = ont.scale(lk.scaleKey);
    expect(sc != nullptr, "HarmonyContext key window: scale exists in ontology");
    if (sc) {
        // For a II-V-I in C, we accept any *mode* of the C-major pitch-class set.
        const QSet<int> expected = QSet<int>{0, 2, 4, 5, 7, 9, 11};
        QSet<int> got;
        for (int iv : sc->intervals) got.insert((lk.tonicPc + iv + 1200) % 12);
        bool okSet = (got.size() == expected.size());
        for (int pc : expected) okSet = okSet && got.contains(pc);
        expect(okSet, "HarmonyContext key window: diatonic pitch-class set matches C-major collection");
    }

    // Functional tagging sanity: G7 in C major should be Dominant.
    music::ChordSymbol g7;
    expect(music::parseChordSymbol("G7", g7), "ParseChordSymbol G7");
    const auto* def = harmony.chordDefForSymbol(g7);
    expect(def != nullptr, "HarmonyContext chordDefForSymbol(G7) exists");
    if (def) {
        QString roman;
        QString func;
        const auto sc = harmony.chooseScaleForChord(/*keyPc=*/0, virtuoso::theory::KeyMode::Major, g7, *def, &roman, &func);
        expect(func == "Dominant", "FunctionalHarmony: G7 is Dominant in C");
        expect(!roman.trimmed().isEmpty(), "FunctionalHarmony: roman populated");
        expect(!sc.key.trimmed().isEmpty(), "HarmonyContext chooseScaleForChord: returns scale key");
        expect(!sc.name.trimmed().isEmpty(), "HarmonyContext chooseScaleForChord: returns scale name");
        expect(!sc.display.trimmed().isEmpty(), "HarmonyContext chooseScaleForChord: returns display string");
        expect(ont.scale(sc.key) != nullptr, "HarmonyContext chooseScaleForChord: key exists in ontology");
    }
}

static void testMotifTransformDeterminism() {
    using namespace virtuoso::memory;
    const QVector<int> pcs = {0, 4, 7}; // C-E-G
    const quint32 seed = 1234567u;
    const auto a = transformPitchMotif(pcs, /*resolvePc=*/2, seed);
    const auto b = transformPitchMotif(pcs, /*resolvePc=*/2, seed);
    expect(a.kind == b.kind, "MotifTransform is deterministic (kind)");
    expect(a.displaceRhythm == b.displaceRhythm, "MotifTransform is deterministic (displace flag)");
    expect(a.tag == b.tag, "MotifTransform is deterministic (tag)");
    expect(a.pcs == b.pcs, "MotifTransform is deterministic (pcs)");
}

static void testPianoPlannerPedalAndTopline() {
    using namespace playback;
    const virtuoso::ontology::OntologyRegistry ont = virtuoso::ontology::OntologyRegistry::builtins();

    JazzBalladPianoPlanner piano;
    piano.setOntology(&ont);
    // Provide a minimal piano vocabulary so the data-driven pedal/topline libraries are active in tests.
    {
        static virtuoso::vocab::VocabularyRegistry vocab;
        QString err;
        const QByteArray js = QByteArray(R"JSON(
{
  "version": 1,
  "piano": [ { "id": "EVANS_SHELL_2", "beats": [1], "minEnergy": 0.0, "maxEnergy": 1.0, "weight": 1.0,
               "hits": [ { "sub": 0, "count": 1, "dur_num": 1, "dur_den": 4, "vel_delta": 0, "density": "guide" } ],
               "notes": "test" } ],
  "piano_phrases": [ { "id": "EVANS_4BAR_SPARSE_2_4", "phraseBars": 4, "minEnergy": 0.0, "maxEnergy": 1.0, "weight": 1.0,
                      "allowWhenUserSilence": true,
                      "hits": [ { "bar": 0, "beat": 1, "sub": 0, "count": 1, "dur_num": 1, "dur_den": 4, "vel_delta": 0, "density": "guide" } ],
                      "notes": "test" } ],
  "piano_topline": [ { "id": "TL_4BAR_GUIDE_RESOLVE", "phraseBars": 4, "minEnergy": 0.0, "maxEnergy": 1.0, "weight": 1.0,
                      "allowWhenUserSilence": true,
                      "hits": [ { "bar": 0, "beat": 1, "sub": 1, "count": 2, "dur_num": 1, "dur_den": 16, "vel_delta": -18, "degree": 7, "neighborDir": 0, "resolve": false, "tag": "a" },
                                { "bar": 0, "beat": 3, "sub": 0, "count": 1, "dur_num": 1, "dur_den": 8, "vel_delta": -10, "degree": 7, "neighborDir": 0, "resolve": true, "tag": "resolve" } ],
                      "notes": "test" } ],
  "piano_gestures": [ { "id": "G_NONE", "minEnergy": 0.0, "maxEnergy": 1.0, "weight": 1.0, "kind": "none", "style": "", "spreadMs": 0, "notes": "test" } ],
  "piano_pedals": [ { "id": "P_HALF_DEFAULT", "minEnergy": 0.0, "maxEnergy": 1.0, "weight": 1.0,
                     "defaultState": "half", "repedalOnNewChord": true, "repedalProbPct": 100, "clearBeforeChange": true, "clearSub": 3, "clearCount": 4,
                     "notes": "test" } ],
  "bass": [],
  "drums": []
}
)JSON");
        const bool ok = vocab.loadFromJsonBytes(js, &err);
        expect(ok, "Load minimal piano vocab JSON for tests");
        if (ok) piano.setVocabulary(&vocab);
    }
    piano.reset();

    virtuoso::groove::TimeSignature ts{4, 4};

    // Prefer some sustain (half or down) in a non-busy, stable moment.
    JazzBalladPianoPlanner::Context c;
    c.bpm = 120;
    c.playbackBarIndex = 4;
    c.beatInBar = 0;
    c.chordIsNew = false;
    music::ChordSymbol chord;
    expect(music::parseChordSymbol("Cmaj7", chord), "ParseChordSymbol Cmaj7");
    c.chord = chord;
    c.chordText = "Cmaj7";
    c.determinismSeed = 1337;
    c.userDensityHigh = false;
    c.userIntensityPeak = false;
    c.userSilence = false;
    c.nextChanges = false;
    c.beatsUntilChordChange = 0;
    c.energy = 0.55;
    c.toneDark = 0.35;
    c.rhythmicComplexity = 0.35;
    c.interaction = 0.35;
    c.phraseBars = 4;
    c.barInPhrase = 1;
    c.phraseEndBar = false;
    c.cadence01 = 0.25;

    (void)piano.planBeatWithActions(c, /*midiChannel=*/4, ts);
    const auto st = piano.snapshotState();
    const int cc64 = st.perf.ints.value("cc64", 0);
    expect(cc64 >= 32, "Piano: sustain used (CC64 >= 32)");

    // Topline should be emitted somewhere in the bar on chord arrivals/cadence-ish landmarks.
    JazzBalladPianoPlanner::Context c2 = c;
    c2.chordIsNew = true;
    c2.cadence01 = 0.55;
    bool hasTop = false;
    for (int beat = 0; beat < 4; ++beat) {
        c2.beatInBar = beat;
        const auto plan2 = piano.planBeatWithActions(c2, /*midiChannel=*/4, ts);
        for (const auto& n : plan2.notes) {
            if (n.agent != "Piano") continue;
            if (n.target_note.trimmed().toLower().contains("topline")) { hasTop = true; break; }
        }
        if (hasTop) break;
    }
    expect(hasTop, "Piano: topline notes are emitted on landmarks");

    // Library IDs should be populated when vocab is available.
    c2.beatInBar = 1;
    const auto planA = piano.planBeatWithActions(c2, /*midiChannel=*/4, ts);
    expect(!planA.performance.compPhraseId.trimmed().isEmpty(), "Piano: comp_phrase_id is set");
    expect(!planA.performance.toplinePhraseId.trimmed().isEmpty(), "Piano: topline_phrase_id is set");
    expect(!planA.performance.pedalId.trimmed().isEmpty(), "Piano: pedal_id is set");
    expect(!planA.performance.gestureId.trimmed().isEmpty(), "Piano: gesture_id is set");

    // Determinism: same context should choose same library IDs.
    const auto planB = piano.planBeatWithActions(c2, /*midiChannel=*/4, ts);
    expect(planA.performance.compPhraseId == planB.performance.compPhraseId, "Piano: comp_phrase_id deterministic");
    expect(planA.performance.toplinePhraseId == planB.performance.toplinePhraseId, "Piano: topline_phrase_id deterministic");
    expect(planA.performance.pedalId == planB.performance.pedalId, "Piano: pedal_id deterministic");
    expect(planA.performance.gestureId == planB.performance.gestureId, "Piano: gesture_id deterministic");

    // No-ring invariant: when the next chord change is one beat away, comp notes should not ring past that boundary.
    JazzBalladPianoPlanner::Context c3 = c;
    c3.playbackBarIndex = 8;
    c3.beatInBar = 3;
    c3.chordIsNew = false;
    c3.hasNextChord = true;
    c3.nextChanges = true;
    c3.beatsUntilChordChange = 1;
    const auto plan3 = piano.planBeatWithActions(c3, /*midiChannel=*/4, ts);
    const auto beatDur = virtuoso::groove::GrooveGrid::beatDurationWhole(ts);
    const auto boundary = beatDur * (c3.beatInBar + c3.beatsUntilChordChange);
    for (const auto& n : plan3.notes) {
        if (n.agent != "Piano") continue;
        if (!n.logic_tag.trimmed().startsWith("ballad_comp")) continue;
        const auto end = n.startPos.withinBarWhole + n.durationWhole;
        expect(end <= boundary, "Piano: comp note does not ring into next chord");
    }

    // Repedal should be possible deterministically when sustain is already down and chord is new.
    bool foundRepedal = false;
    for (int seed = 1; seed <= 240; ++seed) {
        JazzBalladPianoPlanner p3;
        p3.setOntology(&ont);
        p3.reset();
        auto s3 = p3.snapshotState();
        s3.perf.ints.insert("cc64", 127);
        s3.perf.heldNotes = {60, 64, 67};
        p3.restoreState(s3);

        JazzBalladPianoPlanner::Context cx = c;
        cx.determinismSeed = quint32(seed);
        cx.chordIsNew = true;
        cx.cadence01 = 0.65;
        const auto px = p3.planBeatWithActions(cx, /*midiChannel=*/4, ts);
        bool haveLift = false;
        bool haveDown = false;
        for (const auto& ci : px.ccs) {
            if (ci.cc != 64) continue;
            if (ci.logic_tag.contains("repedal", Qt::CaseInsensitive) && ci.value <= 1) haveLift = true;
            if (ci.logic_tag.contains("repedal", Qt::CaseInsensitive) && ci.value >= 32) haveDown = true;
        }
        if (haveLift && haveDown) { foundRepedal = true; break; }
    }
    expect(foundRepedal, "Piano: repedal (lift+re-engage) occurs for some deterministic seeds");
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testLookaheadPlannerJsonDeterminism();
    testHarmonyContextKeyWindowAndFunctionalTagging();
    testMotifTransformDeterminism();
    testPianoPlannerPedalAndTopline();
    if (g_failures > 0) {
        qWarning() << "VirtuosoPlaybackTests failures:" << g_failures;
        return 1;
    }
    qInfo() << "VirtuosoPlaybackTests OK";
    return 0;
}

