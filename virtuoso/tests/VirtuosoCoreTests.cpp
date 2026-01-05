#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/constraints/PianoDriver.h"
#include "virtuoso/constraints/BassDriver.h"
#include "virtuoso/theory/TheoryEvent.h"
#include "virtuoso/theory/NegativeHarmony.h"
#include "virtuoso/theory/ScaleSuggester.h"
#include "virtuoso/theory/GrooveEngine.h"
#include "virtuoso/theory/FunctionalHarmony.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

using virtuoso::ontology::OntologyRegistry;
using virtuoso::ontology::InstrumentKind;

using virtuoso::constraints::CandidateGesture;
using virtuoso::constraints::PerformanceState;
using virtuoso::constraints::PianoDriver;
using virtuoso::constraints::BassDriver;

namespace {

static int g_failures = 0;

static void expect(bool cond, const QString& msg) {
    if (!cond) {
        ++g_failures;
        qWarning().noquote() << "FAIL:" << msg;
    }
}

static void expectEq(int a, int b, const QString& msg) {
    expect(a == b, msg + QString(" (got %1 expected %2)").arg(a).arg(b));
}

static void expectStrEq(const QString& a, const QString& b, const QString& msg) {
    expect(a == b, msg + QString(" (got '%1' expected '%2')").arg(a, b));
}

} // namespace

static void testOntology() {
    const OntologyRegistry reg = OntologyRegistry::builtins();

    // Chords
    const auto* maj7 = reg.chord("maj7");
    expect(maj7 != nullptr, "Chord Major7 exists");
    if (maj7) {
        expectStrEq(maj7->name, "maj7", "Chord Major7 name");
        expectEq(maj7->intervals.size(), 4, "Chord Major7 interval count");
        expectEq(maj7->intervals[0], 0, "Chord Major7 interval 0");
        expectEq(maj7->intervals[3], 11, "Chord Major7 interval 11");
    }

    const auto sevenths = reg.chordsWithTag("seventh");
    expect(sevenths.size() >= 4, "At least a few 7th-chord primitives exist");

    // Scales
    const auto* ionian = reg.scale("ionian");
    expect(ionian != nullptr, "Scale Ionian exists");
    if (ionian) {
        expect(ionian->name.contains("Ionian"), "Scale Ionian name");
        expectEq(ionian->intervals.size(), 7, "Scale Ionian interval count");
        expectEq(ionian->intervals[0], 0, "Scale Ionian interval 0");
        expectEq(ionian->intervals[6], 11, "Scale Ionian interval 11");
    }

    const auto diatonic = reg.scalesWithTag("diatonic");
    expect(diatonic.size() >= 7, "All 7 diatonic modes exist");

    // Voicings
    const auto* rootlessA = reg.voicing("piano_rootless_a");
    expect(rootlessA != nullptr, "Voicing RootlessA exists");
    if (rootlessA) {
        expect(rootlessA->instrument == InstrumentKind::Piano, "RootlessA instrument == Piano");
        expectStrEq(rootlessA->category, "Rootless", "RootlessA category");
        expectEq(rootlessA->chordDegrees.size(), 4, "RootlessA degree count");
        expectEq(rootlessA->chordDegrees[0], 3, "RootlessA first degree");
    }

    const auto pianoVoicings = reg.voicingsFor(InstrumentKind::Piano);
    expect(pianoVoicings.size() >= 4, "At least a few piano voicings exist");
}

static void testPianoConstraints() {
    PianoDriver piano;

    // OK: triad close position
    CandidateGesture g1;
    g1.midiNotes = {60, 64, 67};
    PerformanceState s;
    auto r1 = piano.evaluateFeasibility(s, g1);
    expect(r1.ok, "Piano: close triad is feasible");

    // FAIL: too many notes
    CandidateGesture g2;
    for (int i = 0; i < 11; ++i) g2.midiNotes.push_back(48 + i);
    auto r2 = piano.evaluateFeasibility(s, g2);
    expect(!r2.ok, "Piano: >10 notes rejected");

    // FAIL: span too wide
    CandidateGesture g3;
    g3.midiNotes = {48, 72}; // 24 semitones
    auto r3 = piano.evaluateFeasibility(s, g3);
    expect(!r3.ok, "Piano: span > 10th rejected");
}

static void testBassConstraints() {
    BassDriver bass;
    PerformanceState s;

    // OK: open E1
    CandidateGesture g1;
    g1.midiNotes = {40};
    auto r1 = bass.evaluateFeasibility(s, g1);
    expect(r1.ok, "Bass: open E1 is feasible");

    // FAIL: below lowest string
    CandidateGesture g2;
    g2.midiNotes = {30};
    auto r2 = bass.evaluateFeasibility(s, g2);
    expect(!r2.ok, "Bass: below range rejected");

    // FAIL: too large a shift (given lastFret)
    s.ints.insert("lastFret", 0);
    CandidateGesture g3;
    g3.midiNotes = {55 + 12}; // G2 open is 55; 67 requires fret 12 on G string
    auto r3 = bass.evaluateFeasibility(s, g3);
    expect(!r3.ok, "Bass: excessive fret shift rejected");
}

static void testTheoryStream() {
    virtuoso::theory::TheoryEvent e;
    e.agent = "Piano";
    e.timestamp = "12.3.1.0";
    e.chord_context = "G7alt";
    e.scale_used = "Ab Melodic Minor (7th Mode)";
    e.voicing_type = "UST bVI (Eb Major Triad)";
    e.logic_tag = "Tritone Substitution Response";
    e.target_note = "B (3rd of Cmaj7)";
    e.dynamic_marking = "mf";

    const QString json = e.toJsonString(true);
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    expect(doc.isObject(), "TheoryEvent JSON parses to object");
    const QJsonObject o = doc.object();
    expectStrEq(o.value("agent").toString(), "Piano", "TheoryEvent.agent");
    expectStrEq(o.value("timestamp").toString(), "12.3.1.0", "TheoryEvent.timestamp");
    expectStrEq(o.value("chord_context").toString(), "G7alt", "TheoryEvent.chord_context");
    expectStrEq(o.value("dynamic_marking").toString(), "mf", "TheoryEvent.dynamic_marking");
}

static void testNegativeHarmony() {
    using virtuoso::theory::negativeHarmonyMirrorPc;
    // In C (tonic=0): D(2)->Bb(10), E(4)->Ab(8), F(5)->G(7)
    expectEq(negativeHarmonyMirrorPc(2, 0), 10, "NegativeHarmony: D -> Bb (pc)");
    expectEq(negativeHarmonyMirrorPc(4, 0), 8, "NegativeHarmony: E -> Ab (pc)");
    expectEq(negativeHarmonyMirrorPc(5, 0), 7, "NegativeHarmony: F -> G (pc)");
    expectEq(negativeHarmonyMirrorPc(0, 0), 0, "NegativeHarmony: C -> C (pc)");
}

static void testScaleSuggester() {
    const OntologyRegistry reg = OntologyRegistry::builtins();
    // For a dominant alt-ish pitch set, Altered should appear very high.
    // Example set: {G, B, F, Ab, Bb, Db} pcs {7,11,5,8,10,1}
    QSet<int> pcs = {7, 11, 5, 8, 10, 1};
    const auto sug = virtuoso::theory::suggestScalesForPitchClasses(reg, pcs, 6);
    expect(!sug.isEmpty(), "ScaleSuggester returns suggestions");
    bool hasAltered = false;
    for (const auto& s : sug) if (s.key == "altered") hasAltered = true;
    expect(hasAltered, "ScaleSuggester includes Altered for altered-ish dominant set");
}

static void testGrooveEngine() {
    using virtuoso::theory::GrooveEngine;
    using virtuoso::theory::GrooveTemplate;

    GrooveTemplate straight;
    straight.swing = 0.50;
    auto dueStraight = GrooveEngine::scheduleDueMs(/*steps=*/8, /*baseStepMs=*/100, /*stepsPerBeat=*/2, straight, /*seed=*/123);
    expectEq(dueStraight.size(), 8, "GrooveEngine: due size");
    expectEq(dueStraight[0], 0, "GrooveEngine: straight step0");
    expectEq(dueStraight[1], 100, "GrooveEngine: straight step1");
    expectEq(dueStraight[2], 200, "GrooveEngine: straight step2");

    GrooveTemplate swing;
    swing.swing = 0.666;
    auto dueSwing = GrooveEngine::scheduleDueMs(/*steps=*/8, /*baseStepMs=*/100, /*stepsPerBeat=*/2, swing, /*seed=*/123);
    expectEq(dueSwing.size(), 8, "GrooveEngine: swing due size");
    expect(dueSwing[1] > dueStraight[1], "GrooveEngine: offbeat delayed");
    expect(dueSwing[3] > dueStraight[3], "GrooveEngine: offbeat delayed (2)");
    expectEq(dueSwing[2], dueStraight[2], "GrooveEngine: downbeat on grid");
    expectEq(dueSwing[4], dueStraight[4], "GrooveEngine: downbeat on grid (2)");
    for (int i = 1; i < dueSwing.size(); ++i) {
        expect(dueSwing[i] > dueSwing[i - 1], "GrooveEngine: monotonic schedule");
    }
}

static void testFunctionalHarmony() {
    const OntologyRegistry reg = OntologyRegistry::builtins();
    const auto* maj7 = reg.chord("maj7");
    const auto* dom7 = reg.chord("7");
    expect(maj7 != nullptr, "FunctionalHarmony: have maj7 chord def");
    expect(dom7 != nullptr, "FunctionalHarmony: have 7 chord def");
    if (!maj7 || !dom7) return;

    // In C major: Cmaj7 -> Imaj7 (Tonic)
    {
        const auto r = virtuoso::theory::analyzeChordInMajorKey(/*tonicPc=*/0, /*chordRootPc=*/0, *maj7);
        expect(r.roman.startsWith("I"), "FunctionalHarmony: Cmaj7 is I...");
        expectStrEq(r.function, "Tonic", "FunctionalHarmony: I is Tonic");
    }

    // In C major: G7 -> V7 (Dominant)
    {
        const auto r = virtuoso::theory::analyzeChordInMajorKey(/*tonicPc=*/0, /*chordRootPc=*/7, *dom7);
        expect(r.roman.startsWith("V"), "FunctionalHarmony: G7 is V...");
        expectStrEq(r.function, "Dominant", "FunctionalHarmony: V is Dominant");
    }

    // In C major: D7 -> V/V (secondary dominant heuristic)
    {
        const auto r = virtuoso::theory::analyzeChordInMajorKey(/*tonicPc=*/0, /*chordRootPc=*/2, *dom7);
        expect(r.roman.startsWith("V/"), "FunctionalHarmony: D7 is V/...");
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    testOntology();
    testPianoConstraints();
    testBassConstraints();
    testTheoryStream();
    testNegativeHarmony();
    testScaleSuggester();
    testGrooveEngine();
    testFunctionalHarmony();

    if (g_failures == 0) {
        qInfo("VirtuosoCoreTests: PASS");
        return 0;
    }

    qWarning("VirtuosoCoreTests: FAIL (%d failures)", g_failures);
    return 1;
}

