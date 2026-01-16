#pragma once

#include <QObject>
#include <QSet>
#include <QHash>
#include <QVector>

#include "music/ChordSymbol.h"

class MidiProcessor;

namespace chart { class ChartModel; }
namespace virtuoso::ontology { class OntologyRegistry; }

namespace playback {

class HarmonyContext;

/**
 * ScaleSnapProcessor - MIDI processor that snaps guitar notes to scale/chord tones
 *
 * Listens to guitar MIDI input and outputs processed notes on dedicated channels:
 * - "As Played" mode: Snaps wrong notes to nearest valid tone -> channel 12
 * - "Harmony" mode: Generates consonant harmony note -> channel 12
 * - "As Played + Harmony": As Played -> channel 11, Harmony -> channel 12
 *
 * Valid pitch classes are computed as:
 *   (key scale tones from dynamic key detection) + (chord tones) - (avoid notes)
 *
 * Avoid notes are any pitch class that creates a minor 2nd (1 semitone) with a chord tone,
 * preventing clashes like F against E on a C7 chord.
 *
 * Harmony is always generated relative to the original input note (not the snapped note).
 */
class ScaleSnapProcessor : public QObject {
    Q_OBJECT

public:
    enum class Mode {
        Off = 0,
        AsPlayed,            // Snap to nearest scale tone -> channel 12
        Harmony,             // Generate harmony note -> channel 12
        AsPlayedPlusHarmony, // As Played -> channel 11, Harmony -> channel 12
        AsPlayedPlusBend     // Snap + apply vocal vibrato as pitch bend -> channel 12
    };
    Q_ENUM(Mode)

    explicit ScaleSnapProcessor(QObject* parent = nullptr);
    ~ScaleSnapProcessor() override;

    // Dependencies (must be set before use)
    void setMidiProcessor(MidiProcessor* midi);
    void setHarmonyContext(HarmonyContext* harmony);
    void setOntology(const virtuoso::ontology::OntologyRegistry* ontology);
    void setChartModel(const chart::ChartModel* model);

    // Mode control
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    // Cell index tracking (called by engine on each step)
    void setCurrentCellIndex(int cellIndex);
    int currentCellIndex() const { return m_currentCellIndex; }

signals:
    void modeChanged(Mode newMode);

public slots:
    // Guitar input handlers (connected to MidiProcessor signals)
    void onGuitarNoteOn(int midiNote, int velocity);
    void onGuitarNoteOff(int midiNote);
    void onGuitarHzUpdated(double hz);

    // CC2 (breath) forwarding from voice
    void onVoiceCc2Updated(int value);

    // Voice Hz (for AsPlayedPlusBend mode - measures delta from snapped note for vibrato)
    void onVoiceHzUpdated(double hz);

    // Clear all active notes
    void reset();

private:
    // Core snapping logic
    int snapToNearestValidPc(int inputPc, const QSet<int>& validPcs) const;
    int generateHarmonyNote(int inputNote, const QSet<int>& chordTones, const QSet<int>& scaleTones) const;
    QSet<int> computeValidPitchClasses() const;
    QSet<int> computeChordTones(const music::ChordSymbol& chord) const;
    QSet<int> computeKeyScaleTones() const;  // Uses dynamic key detection

    // MIDI output helpers
    void emitNoteOn(int channel, int note, int velocity);
    void emitNoteOff(int channel, int note);
    void emitPitchBend(int channel, int bendValue);
    void emitCC(int channel, int cc, int value);
    void emitAllNotesOff();

    // Pitch conversion utilities
    static int normalizePc(int pc);
    static int noteToOctave(int midiNote);
    static int pcToMidiNote(int pc, int targetOctave);
    static double midiNoteToHz(int midiNote);
    static double hzToCents(double hz, double referenceHz);

    // Dependencies (not owned)
    MidiProcessor* m_midi = nullptr;
    HarmonyContext* m_harmony = nullptr;
    const virtuoso::ontology::OntologyRegistry* m_ontology = nullptr;
    const chart::ChartModel* m_model = nullptr;

    // State
    Mode m_mode = Mode::Off;
    int m_currentCellIndex = -1;

    // Track last known chord (to persist across empty cells)
    music::ChordSymbol m_lastKnownChord;
    bool m_hasLastKnownChord = false;

    // Active note tracking (for note-off routing and pitch bend)
    struct ActiveNote {
        int originalNote = 0;
        int snappedNote = 0;      // Channel 11 (AsPlayedPlusHarmony) or channel 12 (AsPlayed)
        int harmonyNote = -1;     // Channel 12 (Harmony modes), -1 if not active
        double referenceHz = 0.0; // Hz of the snapped note (for pitch bend calculation)
    };
    QHash<int, ActiveNote> m_activeNotes;  // key = original input note

    // Hz tracking for pitch bend
    double m_lastGuitarHz = 0.0;

    // Voice pitch tracking (for AsPlayedPlusBend mode)
    double m_lastVoiceCents = 0.0;

    // Output channels (1-indexed, matching sendVirtualNoteOn expectations)
    // - AsPlayed mode: snapped notes -> channel 12
    // - Harmony mode: harmony notes -> channel 12
    // - AsPlayedPlusHarmony mode: snapped notes -> channel 11, harmony notes -> channel 12
    static constexpr int kChannelAsPlayed = 11;  // MIDI channel 11 (for dual mode)
    static constexpr int kChannelHarmony = 12;   // MIDI channel 12 (main output)
};

} // namespace playback
