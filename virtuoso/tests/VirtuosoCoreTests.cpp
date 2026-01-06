#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/constraints/PianoDriver.h"
#include "virtuoso/constraints/BassDriver.h"
#include "virtuoso/theory/TheoryEvent.h"
#include "virtuoso/theory/NegativeHarmony.h"
#include "virtuoso/theory/ScaleSuggester.h"
#include "virtuoso/theory/FunctionalHarmony.h"
#include "virtuoso/groove/GrooveGrid.h"
#include "virtuoso/groove/FeelTemplate.h"
#include "virtuoso/groove/GrooveRegistry.h"
#include "virtuoso/groove/GrooveTemplate.h"
#include "virtuoso/groove/TimingHumanizer.h"
#include "virtuoso/drums/FluffyAudioJazzDrumsBrushesMapping.h"

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
    e.groove_template = "swing_2to1";
    e.grid_pos = "12.3@1/8w";
    e.timing_offset_ms = 17;
    e.velocity_adjustment = -3;
    e.humanize_seed = 123;

    const QString json = e.toJsonString(true);
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    expect(doc.isObject(), "TheoryEvent JSON parses to object");
    const QJsonObject o = doc.object();
    expectStrEq(o.value("agent").toString(), "Piano", "TheoryEvent.agent");
    expectStrEq(o.value("timestamp").toString(), "12.3.1.0", "TheoryEvent.timestamp");
    expectStrEq(o.value("chord_context").toString(), "G7alt", "TheoryEvent.chord_context");
    expectStrEq(o.value("dynamic_marking").toString(), "mf", "TheoryEvent.dynamic_marking");
    expectStrEq(o.value("groove_template").toString(), "swing_2to1", "TheoryEvent.groove_template");
    expectStrEq(o.value("grid_pos").toString(), "12.3@1/8w", "TheoryEvent.grid_pos");
    expectEq(o.value("timing_offset_ms").toInt(), 17, "TheoryEvent.timing_offset_ms");
    expectEq(o.value("velocity_adjustment").toInt(), -3, "TheoryEvent.velocity_adjustment");
    expectEq(o.value("humanize_seed").toInt(), 123, "TheoryEvent.humanize_seed");
}

static void testGrooveGridAndFeel() {
    using namespace virtuoso::groove;
    TimeSignature ts{4, 4};

    // Triplet within beat 1: bar1 beat1 subdiv 1/3 => withinBeat = 1/12 whole notes.
    {
        const GridPos p = GrooveGrid::fromBarBeatTuplet(/*bar=*/0, /*beat=*/0, /*sub=*/1, /*count=*/3, ts);
        expectEq(p.barIndex, 0, "GrooveGrid: bar index");
        expect(p.withinBarWhole == Rational(1, 12), "GrooveGrid: triplet position exact (1/12 whole notes)");
    }

    // Swing: upbeat 8th (1/2 beat) should be delayed.
    {
        const int bpm = 120;
        const GridPos upbeat8 = GrooveGrid::fromBarBeatTuplet(/*bar=*/0, /*beat=*/0, /*sub=*/1, /*count=*/2, ts); // 1/2 beat
        const FeelTemplate swing = FeelTemplate::swing2to1(1.0);
        const int off = swing.offsetMsFor(upbeat8, ts, bpm);
        expect(off > 0, "FeelTemplate Swing(2:1): upbeat delayed");
    }
}

static void testTimingHumanizerDeterminism() {
    using namespace virtuoso::groove;
    TimeSignature ts{4, 4};
    const int bpm = 120;

    InstrumentGrooveProfile p;
    p.instrument = "Test";
    p.humanizeSeed = 777;
    p.microJitterMs = 5;
    p.attackVarianceMs = 3;
    p.driftMaxMs = 10;
    p.driftRate = 0.25;
    p.velocityJitter = 4;
    p.accentDownbeat = 1.10;
    p.accentBackbeat = 0.95;
    p.laidBackMs = 6;
    p.pushMs = 1;

    TimingHumanizer h1(p);
    h1.setFeelTemplate(FeelTemplate::swing2to1(0.8));
    TimingHumanizer h2(p);
    h2.setFeelTemplate(FeelTemplate::swing2to1(0.8));

    const GridPos pos = GrooveGrid::fromBarBeatTuplet(/*bar=*/2, /*beat=*/1, /*sub=*/1, /*count=*/2, ts); // bar3 beat2 upbeat
    const Rational dur(1, 8); // eighth note in whole-note units
    const auto a = h1.humanizeNote(pos, ts, bpm, /*baseVel=*/90, dur, /*structural=*/false);
    const auto b = h2.humanizeNote(pos, ts, bpm, /*baseVel=*/90, dur, /*structural=*/false);

    expectEq(int(a.onMs), int(b.onMs), "TimingHumanizer determinism: onMs");
    expectEq(int(a.offMs), int(b.offMs), "TimingHumanizer determinism: offMs");
    expectEq(a.velocity, b.velocity, "TimingHumanizer determinism: velocity");
    expectEq(a.timing_offset_ms, b.timing_offset_ms, "TimingHumanizer determinism: timing_offset_ms");
    expectStrEq(a.groove_template, b.groove_template, "TimingHumanizer determinism: template");
    expectStrEq(a.grid_pos, b.grid_pos, "TimingHumanizer determinism: grid_pos");
}

static void testGrooveRegistry() {
    const auto reg = virtuoso::groove::GrooveRegistry::builtins();
    const auto feels = reg.allFeels();
    expect(!feels.isEmpty(), "GrooveRegistry has feel templates");
    const auto* straight = reg.feel("straight");
    expect(straight != nullptr, "GrooveRegistry: straight exists");
    const auto* swing = reg.feel("swing_2to1");
    expect(swing != nullptr, "GrooveRegistry: swing_2to1 exists");

    const auto* jazzSwing = reg.grooveTemplate("jazz_swing_2to1");
    expect(jazzSwing != nullptr, "GrooveRegistry: jazz_swing_2to1 exists");
    const auto presets = reg.allStylePresets();
    expect(!presets.isEmpty(), "GrooveRegistry: style presets exist");
}

static void testJazzSwingTemplateOffsets() {
    using namespace virtuoso::groove;
    const auto reg = GrooveRegistry::builtins();
    const auto* t = reg.grooveTemplate("jazz_swing_2to1");
    expect(t != nullptr, "Jazz swing template exists");
    if (!t) return;

    TimeSignature ts{4, 4};
    const int bpm = 120;

    // Upbeat 8th within beat 1 should be delayed (>0 ms).
    const GridPos upbeat8 = GrooveGrid::fromBarBeatTuplet(/*bar=*/0, /*beat=*/0, /*sub=*/1, /*count=*/2, ts);
    const int off = t->offsetMsFor(upbeat8, ts, bpm);
    expect(off > 0, "Jazz swing: upbeat 8th delayed");
}

static void testExpandedJazzLibraryExists() {
    const auto reg = virtuoso::groove::GrooveRegistry::builtins();
    expect(reg.grooveTemplate("jazz_swing_light") != nullptr, "Jazz template: swing_light exists");
    expect(reg.grooveTemplate("jazz_swing_heavy") != nullptr, "Jazz template: swing_heavy exists");
    expect(reg.grooveTemplate("jazz_shuffle_12_8") != nullptr, "Jazz template: shuffle_12_8 exists");
    expect(reg.grooveTemplate("jazz_waltz_swing_2to1") != nullptr, "Jazz template: waltz_swing exists");

    expect(reg.stylePreset("jazz_bebop_240") != nullptr, "Jazz preset: bebop_240 exists");
    expect(reg.stylePreset("jazz_hardbop_160") != nullptr, "Jazz preset: hardbop_160 exists");
    expect(reg.stylePreset("jazz_waltz_180") != nullptr, "Jazz preset: waltz_180 exists");
    expect(reg.stylePreset("jazz_shuffle_120") != nullptr, "Jazz preset: shuffle_120 exists");
}

static void testBalladLibraryExists() {
    using namespace virtuoso::groove;
    const auto reg = GrooveRegistry::builtins();

    expect(reg.grooveTemplate("jazz_ballad_pocket_light") != nullptr, "Ballad template: pocket_light exists");
    expect(reg.grooveTemplate("jazz_ballad_pocket_medium") != nullptr, "Ballad template: pocket_medium exists");
    expect(reg.grooveTemplate("jazz_ballad_pocket_deep") != nullptr, "Ballad template: pocket_deep exists");
    expect(reg.grooveTemplate("jazz_ballad_swing_soft") != nullptr, "Ballad template: swing_soft exists");
    expect(reg.grooveTemplate("jazz_ballad_swing_deep") != nullptr, "Ballad template: swing_deep exists");
    expect(reg.grooveTemplate("jazz_ballad_triplet_drag") != nullptr, "Ballad template: triplet_drag exists");

    expect(reg.stylePreset("jazz_ballad_50") != nullptr, "Ballad preset: ballad_50 exists");
    expect(reg.stylePreset("jazz_ballad_60") != nullptr, "Ballad preset: ballad_60 exists");
    expect(reg.stylePreset("jazz_ballad_72") != nullptr, "Ballad preset: ballad_72 exists");
    expect(reg.stylePreset("jazz_ballad_90") != nullptr, "Ballad preset: ballad_90 exists");

    // Basic sanity: pocket templates should delay beat start (>0ms) at withinBeat=0.
    TimeSignature ts{4, 4};
    const int bpm = 60;
    const GridPos beatStart = GrooveGrid::fromBarBeatTuplet(/*bar=*/0, /*beat=*/0, /*sub=*/0, /*count=*/1, ts);
    const auto* pocket = reg.grooveTemplate("jazz_ballad_pocket_deep");
    expect(pocket != nullptr, "Ballad pocket_deep exists");
    if (pocket) {
        const int off = pocket->offsetMsFor(beatStart, ts, bpm);
        expect(off > 0, "Ballad pocket_deep: beat-start delayed");
    }
}

static void testBrushesBalladLibraryExists() {
    using namespace virtuoso::groove;
    const auto reg = GrooveRegistry::builtins();

    expect(reg.grooveTemplate("jazz_ballad_brushes_chet") != nullptr, "Brushes ballad template: chet exists");
    expect(reg.grooveTemplate("jazz_ballad_brushes_evans") != nullptr, "Brushes ballad template: evans exists");
    expect(reg.stylePreset("jazz_brushes_ballad_60_chet") != nullptr, "Brushes ballad preset: chet exists");
    expect(reg.stylePreset("jazz_brushes_ballad_60_evans") != nullptr, "Brushes ballad preset: evans exists");

    // Non-timing driver hooks should be present for drums.
    const auto* pc = reg.stylePreset("jazz_brushes_ballad_60_chet");
    expect(pc != nullptr, "Brushes chet preset exists");
    if (pc) {
        expect(!pc->articulationNotes.value("Drums").trimmed().isEmpty(), "Brushes chet preset has Drums articulation notes");
    }

    // Upbeat 8th should be delayed (positive ms) for these templates.
    TimeSignature ts{4, 4};
    const int bpm = 60;
    const GridPos upbeat8 = GrooveGrid::fromBarBeatTuplet(/*bar=*/0, /*beat=*/0, /*sub=*/1, /*count=*/2, ts);
    const auto* t = reg.grooveTemplate("jazz_ballad_brushes_chet");
    expect(t != nullptr, "Brushes chet template exists");
    if (t) {
        const int off = t->offsetMsFor(upbeat8, ts, bpm);
        expect(off > 0, "Brushes chet: upbeat delayed");
    }
}

static void testFluffyAudioBrushesMappingBasics() {
    const auto notes = virtuoso::drums::fluffyAudioJazzDrumsBrushesNotes();
    expect(!notes.isEmpty(), "FluffyAudio Jazz Drums - Brushes mapping is non-empty");

    auto hasMidi = [&](int midi) -> bool {
        for (const auto& n : notes) {
            if (n.midi == midi) return true;
        }
        return false;
    };

    // MVP-required notes we schedule today:
    expect(hasMidi(virtuoso::drums::fluffy_brushes::kKickLooseNormal_G0), "Mapping includes Kick/Loose Normal (G0)");
    expect(hasMidi(virtuoso::drums::fluffy_brushes::kSnareRightHand_D1), "Mapping includes Snare Right Hand (D1)");
    expect(hasMidi(virtuoso::drums::fluffy_brushes::kSnareBrushing_E3), "Mapping includes Snare Brushing (E3)");
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
    testGrooveGridAndFeel();
    testTimingHumanizerDeterminism();
    testGrooveRegistry();
    testJazzSwingTemplateOffsets();
    testExpandedJazzLibraryExists();
    testBalladLibraryExists();
    testBrushesBalladLibraryExists();
    testFluffyAudioBrushesMappingBasics();
    testNegativeHarmony();
    testScaleSuggester();
    testFunctionalHarmony();

    if (g_failures == 0) {
        qInfo("VirtuosoCoreTests: PASS");
        return 0;
    }

    qWarning("VirtuosoCoreTests: FAIL (%d failures)", g_failures);
    return 1;
}

