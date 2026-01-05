#include "virtuoso/ontology/OntologyRegistry.h"
#include "virtuoso/constraints/PianoDriver.h"
#include "virtuoso/constraints/BassDriver.h"
#include "virtuoso/theory/TheoryEvent.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

using virtuoso::ontology::OntologyRegistry;
using virtuoso::ontology::ChordId;
using virtuoso::ontology::ScaleId;
using virtuoso::ontology::VoicingId;
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
    const auto* maj7 = reg.chord(ChordId::Major7);
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
    const auto* ionian = reg.scale(ScaleId::Ionian);
    expect(ionian != nullptr, "Scale Ionian exists");
    if (ionian) {
        expectStrEq(ionian->name, "Ionian", "Scale Ionian name");
        expectEq(ionian->intervals.size(), 7, "Scale Ionian interval count");
        expectEq(ionian->intervals[0], 0, "Scale Ionian interval 0");
        expectEq(ionian->intervals[6], 11, "Scale Ionian interval 11");
    }

    const auto diatonic = reg.scalesWithTag("diatonic");
    expect(diatonic.size() >= 7, "All 7 diatonic modes exist");

    // Voicings
    const auto* rootlessA = reg.voicing(VoicingId::PianoRootlessA_3_5_7_9);
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

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    testOntology();
    testPianoConstraints();
    testBassConstraints();
    testTheoryStream();

    if (g_failures == 0) {
        qInfo("VirtuosoCoreTests: PASS");
        return 0;
    }

    qWarning("VirtuosoCoreTests: FAIL (%d failures)", g_failures);
    return 1;
}

