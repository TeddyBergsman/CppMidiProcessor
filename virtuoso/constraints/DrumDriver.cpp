#include "virtuoso/constraints/DrumDriver.h"

#include <QtGlobal>

namespace virtuoso::constraints {

DrumDriver::HitClass DrumDriver::classify(int midiNote) {
    // Minimal GM-ish heuristic classification.
    // Feet:
    if (midiNote == 35 || midiNote == 36) return {LimbKind::Foot, 0, "Kick"};
    if (midiNote == 44) return {LimbKind::Foot, 1, "HH Pedal"};

    // Hands:
    if (midiNote == 38 || midiNote == 40) return {LimbKind::Hand, 2, "Snare"};
    if (midiNote == 42 || midiNote == 46) return {LimbKind::Hand, 3, "HiHat"};
    if (midiNote == 51 || midiNote == 59) return {LimbKind::Hand, 4, "Ride"};
    if (midiNote == 49 || midiNote == 57) return {LimbKind::Hand, 5, "Crash"};
    if (midiNote >= 41 && midiNote <= 48) return {LimbKind::Hand, 6, "Tom"};

    return {LimbKind::Hand, 7, "Other"};
}

FeasibilityResult DrumDriver::evaluateFeasibility(const PerformanceState& state,
                                                  const CandidateGesture& candidate) const {
    FeasibilityResult r;
    if (candidate.midiNotes.isEmpty()) {
        r.ok = true;
        r.reasons.push_back("OK: empty gesture");
        return r;
    }

    int hands = 0;
    int feet = 0;
    int zone = -1;
    double cost = 0.0;

    for (int n : candidate.midiNotes) {
        const auto hc = classify(n);
        if (hc.limb == LimbKind::Hand) hands++;
        else feet++;
        // Use the first hit as the representative zone for traversal; multi-zone clusters cost extra.
        if (zone < 0) zone = hc.zone;
        else if (hc.zone != zone) cost += 0.15;
    }

    if (hands > m_c.maxSimultaneousHands) {
        r.ok = false;
        r.reasons.push_back(QString("FAIL: hands=%1 exceeds maxSimultaneousHands=%2").arg(hands).arg(m_c.maxSimultaneousHands));
        return r;
    }
    if (feet > m_c.maxSimultaneousFeet) {
        r.ok = false;
        r.reasons.push_back(QString("FAIL: feet=%1 exceeds maxSimultaneousFeet=%2").arg(feet).arg(m_c.maxSimultaneousFeet));
        return r;
    }

    const int lastZone = state.ints.value("lastDrumZone", -1);
    if (lastZone >= 0 && zone >= 0 && lastZone != zone) {
        cost += m_c.zoneChangeCost;
        r.reasons.push_back(QString("INFO: zone change %1->%2 cost=%3").arg(lastZone).arg(zone).arg(m_c.zoneChangeCost, 0, 'f', 3));
    }

    r.ok = true;
    r.cost = cost;
    r.reasons.push_back(QString("OK: hits=%1 hands=%2 feet=%3 zone=%4 cost=%5")
                            .arg(candidate.midiNotes.size())
                            .arg(hands)
                            .arg(feet)
                            .arg(zone)
                            .arg(cost, 0, 'f', 3));
    if (zone >= 0) r.stateUpdates.insert("lastDrumZone", zone);
    return r;
}

} // namespace virtuoso::constraints

