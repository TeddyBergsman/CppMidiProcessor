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
        (void)harmony.chooseScaleUsedForChord(/*keyPc=*/0, virtuoso::theory::KeyMode::Major, g7, *def, &roman, &func);
        expect(func == "Dominant", "FunctionalHarmony: G7 is Dominant in C");
        expect(!roman.trimmed().isEmpty(), "FunctionalHarmony: roman populated");
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testLookaheadPlannerJsonDeterminism();
    testHarmonyContextKeyWindowAndFunctionalTagging();
    if (g_failures > 0) {
        qWarning() << "VirtuosoPlaybackTests failures:" << g_failures;
        return 1;
    }
    qInfo() << "VirtuosoPlaybackTests OK";
    return 0;
}

