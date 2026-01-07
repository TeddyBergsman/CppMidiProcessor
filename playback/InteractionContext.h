#pragma once

#include "playback/SemanticMidiAnalyzer.h"
#include "playback/VibeStateMachine.h"

#include <QString>
#include <QtGlobal>

namespace playback {

// InteractionContext: wraps listening + macro-dynamics into a single component.
// It owns the SemanticMidiAnalyzer and VibeStateMachine and produces a per-step snapshot.
class InteractionContext final {
public:
    struct Snapshot {
        qint64 nowMsWall = 0;
        SemanticMidiAnalyzer::IntentState intent;
        VibeStateMachine::Output vibe;

        // Convenience (derived)
        double energy01 = 0.25;   // respects debug override
        QString vibeStr;
        QString intentStr;
        bool userBusy = false;
    };

    void reset() {
        m_listener.reset();
        m_vibe.reset();
    }

    // Wire harmonic context into outside detection.
    void setChordContext(const music::ChordSymbol& chord) { m_listener.setChordContext(chord); }

    // Ingest live events (thread-safe usage depends on caller; engine uses queued slots).
    void ingestGuitarNoteOn(int note, int vel, qint64 tsMs) { m_listener.ingestGuitarNoteOn(note, vel, tsMs); }
    void ingestGuitarNoteOff(int note, qint64 tsMs) { m_listener.ingestGuitarNoteOff(note, tsMs); }
    void ingestCc2(int cc2, qint64 tsMs) { m_listener.ingestCc2(cc2, tsMs); }
    void ingestVoiceNoteOn(int note, int vel, qint64 tsMs) { m_listener.ingestVoiceNoteOn(note, vel, tsMs); }
    void ingestVoiceNoteOff(int note, qint64 tsMs) { m_listener.ingestVoiceNoteOff(note, tsMs); }

    Snapshot snapshot(qint64 nowMsWall, bool debugEnergyAuto, double debugEnergy01);

    // Expose internals for modules like LookaheadPlanner (temporary until deeper refactor).
    SemanticMidiAnalyzer& listener() { return m_listener; }
    const SemanticMidiAnalyzer& listener() const { return m_listener; }
    VibeStateMachine& vibe() { return m_vibe; }
    const VibeStateMachine& vibe() const { return m_vibe; }

    static QString intentsToString(const SemanticMidiAnalyzer::IntentState& i);

private:
    SemanticMidiAnalyzer m_listener;
    VibeStateMachine m_vibe;
};

} // namespace playback

