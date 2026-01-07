#include "playback/InteractionContext.h"

#include <QtGlobal>

namespace playback {

QString InteractionContext::intentsToString(const SemanticMidiAnalyzer::IntentState& i) {
    QStringList out;
    if (i.densityHigh) out << "DENSITY_HIGH";
    if (i.registerHigh) out << "REGISTER_HIGH";
    if (i.intensityPeak) out << "INTENSITY_PEAK";
    if (i.playingOutside) out << "PLAYING_OUTSIDE";
    if (i.silence) out << "SILENCE";
    if (i.questionEnded) out << "QUESTION_END";
    return out.join(",");
}

InteractionContext::Snapshot InteractionContext::snapshot(qint64 nowMsWall, bool debugEnergyAuto, double debugEnergy01) {
    Snapshot s;
    s.nowMsWall = nowMsWall;
    s.intent = m_listener.compute(nowMsWall);
    s.vibe = m_vibe.update(s.intent, nowMsWall);
    s.energy01 = qBound(0.0, debugEnergyAuto ? s.vibe.energy : debugEnergy01, 1.0);
    s.vibeStr = debugEnergyAuto ? VibeStateMachine::vibeName(s.vibe.vibe)
                                : (VibeStateMachine::vibeName(s.vibe.vibe) + " (manual)");
    s.intentStr = intentsToString(s.intent);
    s.userBusy = (s.intent.densityHigh || s.intent.intensityPeak || s.intent.registerHigh);
    return s;
}

} // namespace playback

