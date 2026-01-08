#include "chart/ChartModel.h"

#include "playback/HarmonyContext.h"
#include "playback/LookaheadPlanner.h"
#include "playback/SemanticMidiAnalyzer.h"
#include "playback/VibeStateMachine.h"
#include "playback/JazzBalladBassPlanner.h"
#include "playback/JazzBalladPianoPlanner.h"
#include "playback/BrushesBalladDrummer.h"
#include "playback/AgentCoordinator.h"
#include "playback/AutoWeightController.h"
#include "playback/WeightNegotiator.h"
#include "playback/StoryState.h"

#include "music/ChordSymbol.h"
#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/memory/MotifTransform.h"
#include "virtuoso/engine/VirtuosoEngine.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QElapsedTimer>
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

static void testPianoPlannerCompOnlyBasics() {
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

    // Comp-only basics: allow pedal CC64, but no topline/gesture notes.
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
    {
        auto plan0 = piano.planBeatWithActions(c, /*midiChannel=*/4, ts);
        // Pedal may emit CC64 depending on strategy; ensure no non-comp notes.
        for (const auto& n : plan0.notes) {
            if (n.agent != "Piano") continue;
            expect(n.logic_tag.startsWith("ballad_comp"), "Piano basics: only ballad_comp notes emitted");
            expect(!n.logic_tag.contains("rh_gesture"), "Piano basics: no RH gesture notes");
            expect(!n.logic_tag.contains("piano_topline"), "Piano basics: no topline notes");
        }
    }

    // Library IDs should be populated when vocab is available.
    JazzBalladPianoPlanner::Context c2 = c;
    c2.chordIsNew = true;
    c2.beatInBar = 1;
    const auto planA = piano.planBeatWithActions(c2, /*midiChannel=*/4, ts);
    expect(!planA.performance.compPhraseId.trimmed().isEmpty(), "Piano: comp_phrase_id is set");
    expect(planA.performance.toplinePhraseId.trimmed().isEmpty(), "Piano basics: topline_phrase_id is empty");
    expect(!planA.performance.pedalId.trimmed().isEmpty(), "Piano basics: pedal_id is set");
    expect(planA.performance.gestureId.trimmed().isEmpty(), "Piano basics: gesture_id is empty");

    // Determinism: same context should choose same library IDs.
    const auto planB = piano.planBeatWithActions(c2, /*midiChannel=*/4, ts);
    expect(planA.performance.compPhraseId == planB.performance.compPhraseId, "Piano: comp_phrase_id deterministic");
    expect(planA.performance.toplinePhraseId == planB.performance.toplinePhraseId, "Piano basics: topline_phrase_id deterministic");
    expect(planA.performance.pedalId == planB.performance.pedalId, "Piano basics: pedal_id deterministic");
    expect(planA.performance.gestureId == planB.performance.gestureId, "Piano basics: gesture_id deterministic");

    // Phrase coherence: comp phrase id should remain stable across bars within the phrase,
    // even if chord text changes (phrase uses anchor chord for selection).
    JazzBalladPianoPlanner::Context c4 = c;
    c4.chordIsNew = true;
    music::ChordSymbol chordF;
    expect(music::parseChordSymbol("F7", chordF), "ParseChordSymbol F7");
    const auto p0 = piano.planBeatWithActions(c4, /*midiChannel=*/4, ts);
    c4.playbackBarIndex = 5;
    c4.barInPhrase = 2;
    c4.chordIsNew = true;
    c4.chord = chordF;
    c4.chordText = "F7";
    const auto p1 = piano.planBeatWithActions(c4, /*midiChannel=*/4, ts);
    expect(!p0.performance.compPhraseId.trimmed().isEmpty(), "Piano: comp phrase chosen (bar0)");
    expect(p0.performance.compPhraseId.trimmed() == p1.performance.compPhraseId.trimmed(), "Piano: comp phrase stable across phrase bars");

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
}

static void testAutoWeightsV2DeterminismAndBounds() {
    using playback::AutoWeightController;
    AutoWeightController::Inputs in;
    in.sectionLabel = "Chorus";
    in.repeatIndex = 0;
    in.repeatsTotal = 2;
    in.playbackBarIndex = 7;
    in.phraseBars = 4;
    in.barInPhrase = 3;
    in.phraseEndBar = true;
    in.cadence01 = 0.8;
    in.userSilence = false;
    in.userBusy = false;
    in.userRegisterHigh = true;
    in.userIntensityPeak = true;

    const auto a = AutoWeightController::compute(in);
    const auto b = AutoWeightController::compute(in);
    expectStrEq(QString::fromUtf8(QJsonDocument(a.toJson()).toJson(QJsonDocument::Compact)),
                QString::fromUtf8(QJsonDocument(b.toJson()).toJson(QJsonDocument::Compact)),
                "AutoWeightController is deterministic for fixed inputs");

    auto ok01 = [](double v) { return v >= 0.0 && v <= 1.0; };
    expect(ok01(a.density), "Auto weights: density in [0,1]");
    expect(ok01(a.rhythm), "Auto weights: rhythm in [0,1]");
    expect(ok01(a.intensity), "Auto weights: intensity in [0,1]");
    expect(ok01(a.dynamism), "Auto weights: dynamism in [0,1]");
    expect(ok01(a.emotion), "Auto weights: emotion in [0,1]");
    expect(ok01(a.creativity), "Auto weights: creativity in [0,1]");
    expect(ok01(a.tension), "Auto weights: tension in [0,1]");
    expect(ok01(a.interactivity), "Auto weights: interactivity in [0,1]");
    expect(ok01(a.variability), "Auto weights: variability in [0,1]");
    expect(ok01(a.warmth), "Auto weights: warmth in [0,1]");
}

static void testWeightNegotiatorDeterminismAndBounds() {
    using playback::WeightNegotiator;
    WeightNegotiator::Inputs in;
    in.sectionLabel = "Bridge";
    in.userBusy = false;
    in.userSilence = false;
    in.cadence = true;
    in.phraseEnd = true;
    in.global.density = 0.55;
    in.global.rhythm = 0.70;
    in.global.intensity = 0.80;
    in.global.dynamism = 0.60;
    in.global.emotion = 0.35;
    in.global.creativity = 0.65;
    in.global.tension = 0.75;
    in.global.interactivity = 0.50;
    in.global.variability = 0.55;
    in.global.warmth = 0.60;
    in.global.clamp01();

    WeightNegotiator::State s1;
    WeightNegotiator::State s2;
    const auto a = WeightNegotiator::negotiate(in, s1, /*smoothingAlpha=*/0.22);
    const auto b = WeightNegotiator::negotiate(in, s2, /*smoothingAlpha=*/0.22);
    expectStrEq(QString::fromUtf8(QJsonDocument(a.toJson()).toJson(QJsonDocument::Compact)),
                QString::fromUtf8(QJsonDocument(b.toJson()).toJson(QJsonDocument::Compact)),
                "WeightNegotiator is deterministic for fixed inputs + fresh state");

    auto ok01 = [](double v) { return v >= 0.0 && v <= 1.0; };
    auto chkAgent = [&](const WeightNegotiator::AgentWeights& aw, const QString& tag) {
        expect(ok01(aw.w.density), tag + ": density in [0,1]");
        expect(ok01(aw.w.rhythm), tag + ": rhythm in [0,1]");
        expect(ok01(aw.w.intensity), tag + ": intensity in [0,1]");
        expect(ok01(aw.w.dynamism), tag + ": dynamism in [0,1]");
        expect(ok01(aw.w.emotion), tag + ": emotion in [0,1]");
        expect(ok01(aw.w.creativity), tag + ": creativity in [0,1]");
        expect(ok01(aw.w.tension), tag + ": tension in [0,1]");
        expect(ok01(aw.w.interactivity), tag + ": interactivity in [0,1]");
        expect(ok01(aw.w.variability), tag + ": variability in [0,1]");
        expect(ok01(aw.w.warmth), tag + ": warmth in [0,1]");
        expect(ok01(aw.virt.harmonicRisk), tag + ": virt.harmonicRisk in [0,1]");
        expect(ok01(aw.virt.rhythmicComplexity), tag + ": virt.rhythmicComplexity in [0,1]");
        expect(ok01(aw.virt.interaction), tag + ": virt.interaction in [0,1]");
        expect(ok01(aw.virt.toneDark), tag + ": virt.toneDark in [0,1]");
    };
    chkAgent(a.piano, "Negotiator:piano");
    chkAgent(a.bass, "Negotiator:bass");
    chkAgent(a.drums, "Negotiator:drums");
}

static void testCandidatePoolIncludesWeightsV2() {
    using namespace playback;
    const virtuoso::ontology::OntologyRegistry ont = virtuoso::ontology::OntologyRegistry::builtins();

    // Minimal 1-bar chart.
    const chart::ChartModel model = makeOneBarChart("Cmaj7");
    QVector<int> sequence;
    sequence << 0 << 1 << 2 << 3;

    HarmonyContext harmony;
    harmony.setOntology(&ont);
    harmony.rebuildFromModel(model);

    InteractionContext interaction;

    JazzBalladBassPlanner bassPlanner;
    JazzBalladPianoPlanner pianoPlanner;
    pianoPlanner.setOntology(&ont);
    BrushesBalladDrummer drummer;

    virtuoso::memory::MotivicMemory mem;
    StoryState story;

    virtuoso::engine::VirtuosoEngine engine;
    engine.setTempoBpm(120);
    engine.setTimeSignature({4, 4});
    engine.start();

    QString captured;
    QObject::connect(&engine, &virtuoso::engine::VirtuosoEngine::theoryEventJson,
                     &engine, [&](const QString& json) {
                         if (json.contains("\"event_kind\":\"candidate_pool\"")) {
                             captured = json;
                         }
                     });

    // Provide v2 weights + negotiated allocation.
    WeightNegotiator::Inputs ni;
    ni.sectionLabel = "Verse";
    ni.global.density = 0.40;
    ni.global.rhythm = 0.35;
    ni.global.intensity = 0.45;
    ni.global.dynamism = 0.50;
    ni.global.emotion = 0.40;
    ni.global.creativity = 0.25;
    ni.global.tension = 0.45;
    ni.global.interactivity = 0.55;
    ni.global.variability = 0.35;
    ni.global.warmth = 0.60;
    ni.global.clamp01();
    WeightNegotiator::State ns;
    const auto negotiated = WeightNegotiator::negotiate(ni, ns, 0.0);

    AgentCoordinator::Inputs in;
    in.model = &model;
    in.sequence = &sequence;
    in.repeats = 1;
    in.bpm = 120;
    in.stylePresetKey = "jazz_brushes_ballad_60_evans";
    in.debugEnergyAuto = false;
    in.debugEnergy = 0.35;
    in.chDrums = 6;
    in.chBass = 3;
    in.chPiano = 4;
    in.harmony = &harmony;
    in.interaction = &interaction;
    in.engine = &engine;
    in.ontology = &ont;
    in.bassPlanner = &bassPlanner;
    in.pianoPlanner = &pianoPlanner;
    in.drummer = &drummer;
    in.motivicMemory = &mem;
    in.story = &story;
    in.weightsV2Auto = false;
    in.weightsV2 = ni.global;
    in.negotiated = negotiated;

    AgentCoordinator::scheduleStep(in, /*stepIndex=*/0);

    // Pump the event loop until the scheduler dispatches candidate_pool, or timeout.
    QElapsedTimer t;
    t.start();
    while (captured.isEmpty() && t.elapsed() < 250) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    expect(!captured.isEmpty(), "candidate_pool JSON emitted by engine");
    if (captured.isEmpty()) return;

    const auto doc = QJsonDocument::fromJson(captured.toUtf8());
    expect(doc.isObject(), "candidate_pool parses as JSON object");
    if (!doc.isObject()) return;
    const QJsonObject o = doc.object();
    expect(o.value("event_kind").toString() == "candidate_pool", "candidate_pool: event_kind=candidate_pool");
    expect(o.contains("weights_v2"), "candidate_pool includes weights_v2");
    expect(o.contains("negotiated_v2"), "candidate_pool includes negotiated_v2");
    expect(o.value("weights_v2").isObject(), "weights_v2 is object");
    expect(o.value("negotiated_v2").isObject(), "negotiated_v2 is object");
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testLookaheadPlannerJsonDeterminism();
    testHarmonyContextKeyWindowAndFunctionalTagging();
    testMotifTransformDeterminism();
    testPianoPlannerCompOnlyBasics();
    testAutoWeightsV2DeterminismAndBounds();
    testWeightNegotiatorDeterminismAndBounds();
    testCandidatePoolIncludesWeightsV2();
    if (g_failures > 0) {
        qWarning() << "VirtuosoPlaybackTests failures:" << g_failures;
        return 1;
    }
    qInfo() << "VirtuosoPlaybackTests OK";
    return 0;
}

