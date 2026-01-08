#include "virtuoso/piano/PianoPerformanceModel.h"

#include <QtGlobal>

namespace virtuoso::piano {
namespace {

static Hand inferHandFromTargetNote(const QString& t) {
    const QString s = t.trimmed().toLower();
    if (s.contains("lh")) return Hand::Left;
    if (s.contains("rh")) return Hand::Right;
    return Hand::Unknown;
}

static QString inferVoiceId(const virtuoso::engine::AgentIntentNote& n) {
    const QString t = n.target_note.trimmed().toLower();
    if (t.contains("top")) return "top";
    if (t.contains("lh")) return "lh";
    if (t.contains("rh")) return "rh";
    // Fall back to voicing_type hint.
    if (n.voicing_type.trimmed().toLower().contains("gesture")) return "gesture";
    return "comp";
}

static QString inferRole(const virtuoso::engine::AgentIntentNote& n) {
    const QString t = n.target_note.trimmed().toLower();
    if (t.contains("gesture")) return "gesture";
    if (t.contains("top")) return "topline";
    return "comp";
}

static QString pedalProfileFor(const QVector<PedalAction>& ped) {
    if (ped.isEmpty()) return {};
    bool haveHalf = false;
    bool haveDown = false;
    bool haveLift = false;
    for (const auto& a : ped) {
        if (a.cc64Value <= 1) haveLift = true;
        else if (a.cc64Value < 96) haveHalf = true;
        else haveDown = true;
    }
    QStringList parts;
    if (haveDown) parts << "Down";
    if (haveHalf) parts << "Half";
    if (haveLift) parts << "Lift";
    return parts.join('+');
}

} // namespace

PianoPerformancePlan PianoPerformanceModel::inferFromLegacy(const QVector<virtuoso::engine::AgentIntentNote>& notes,
                                                            const QVector<LegacyCc64Intent>& cc64) {
    PianoPerformancePlan out;
    out.notes.reserve(notes.size());

    for (const auto& n : notes) {
        PianoNoteIntent pn;
        pn.midi = qBound(0, n.note, 127);
        pn.velocity = qBound(1, n.baseVelocity, 127);
        pn.startPos = n.startPos;
        pn.durationWhole = n.durationWhole;
        pn.hand = inferHandFromTargetNote(n.target_note);
        pn.voiceId = inferVoiceId(n);
        pn.role = inferRole(n);
        out.notes.push_back(pn);
    }

    out.pedal.reserve(cc64.size());
    for (const auto& c : cc64) {
        PedalAction a;
        a.cc64Value = qBound(0, c.value, 127);
        a.startPos = c.startPos;
        a.kind = (a.cc64Value <= 1) ? PedalActionKind::Lift : PedalActionKind::Set;
        out.pedal.push_back(a);
    }

    // Gesture profile (very coarse v1; refined when gesture generation moves into model).
    out.gestureProfile.clear();
    for (const auto& n : notes) {
        const QString vt = n.voicing_type.trimmed().toLower();
        if (vt.contains("arpegg")) { out.gestureProfile = "Arpeggiated"; break; }
        if (vt.contains("rolled")) { out.gestureProfile = "RolledHands"; break; }
    }
    out.pedalProfile = pedalProfileFor(out.pedal);

    // Topline summary: if any note is tagged as top voice.
    for (const auto& n : notes) {
        const QString tn = n.target_note.trimmed().toLower();
        if (tn.contains("top")) { out.toplineSummary = "top_voice"; break; }
    }

    // Library IDs can be encoded into logic_tag tokens; we keep this optional and best-effort.
    for (const auto& n : notes) {
        const QString lg = n.logic_tag;
        if (out.compPhraseId.isEmpty() && lg.contains("vocab_phrase:", Qt::CaseInsensitive)) {
            const int idx = lg.indexOf("vocab_phrase:", 0, Qt::CaseInsensitive);
            if (idx >= 0) {
                const int start = idx + int(QString("vocab_phrase:").size());
                int end = lg.indexOf('|', start);
                if (end < 0) end = lg.size();
                out.compPhraseId = lg.mid(start, end - start).trimmed();
            }
        }
        if (out.compBeatId.isEmpty() && lg.contains("vocab:", Qt::CaseInsensitive)) {
            const int idx = lg.indexOf("vocab:", 0, Qt::CaseInsensitive);
            if (idx >= 0) {
                const int start = idx + int(QString("vocab:").size());
                int end = lg.indexOf('|', start);
                if (end < 0) end = lg.size();
                const QString id = lg.mid(start, end - start).trimmed();
                // Avoid capturing vocab_phrase as vocab.
                if (!id.startsWith("phrase", Qt::CaseInsensitive)) out.compBeatId = id;
            }
        }
        if (out.gestureId.isEmpty() && lg.contains("gesture:", Qt::CaseInsensitive)) {
            const int idx = lg.indexOf("gesture:", 0, Qt::CaseInsensitive);
            const int start = idx + int(QString("gesture:").size());
            int end = lg.indexOf('|', start);
            if (end < 0) end = lg.size();
            out.gestureId = lg.mid(start, end - start).trimmed();
        }
        if (out.toplinePhraseId.isEmpty() && lg.contains("topline_phrase:", Qt::CaseInsensitive)) {
            const int idx = lg.indexOf("topline_phrase:", 0, Qt::CaseInsensitive);
            const int start = idx + int(QString("topline_phrase:").size());
            int end = lg.indexOf('|', start);
            if (end < 0) end = lg.size();
            out.toplinePhraseId = lg.mid(start, end - start).trimmed();
        }
        if (out.pedalId.isEmpty() && lg.contains("pedal:", Qt::CaseInsensitive)) {
            const int idx = lg.indexOf("pedal:", 0, Qt::CaseInsensitive);
            const int start = idx + int(QString("pedal:").size());
            int end = lg.indexOf('|', start);
            if (end < 0) end = lg.size();
            out.pedalId = lg.mid(start, end - start).trimmed();
        }
    }

    return out;
}

} // namespace virtuoso::piano

