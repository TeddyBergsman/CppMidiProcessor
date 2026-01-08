#pragma once

#include <QString>
#include <QVector>
#include <QHash>

#include "virtuoso/groove/FeelTemplate.h"
#include "virtuoso/groove/GrooveTemplate.h"
#include "virtuoso/groove/TimingHumanizer.h"
#include "virtuoso/control/PerformanceWeightsV2.h"

namespace virtuoso::groove {

// Stage 1: Groove vocabulary registry (separate from harmonic ontology).
// Goal: make groove vocabulary data-driven and complete over time.
//
// Today it provides:
// - Stable ordering of feel templates for UI
// - Key-based lookup for deterministic selection
//
// Next expansions (scaffold-ready):
// - Subdivision/grid definitions (16th, triplet, 12/8, odd meters)
// - Per-instrument timing/velocity curve templates
// - Style presets mapping: "Jazz Swing Ballad 60" -> {feel + profiles}
class GrooveRegistry {
public:
    static GrooveRegistry builtins();

    const FeelTemplate* feel(const QString& key) const;
    QVector<const FeelTemplate*> allFeels() const; // stable ordering

    // New: GrooveTemplates (richer feel vocabulary)
    const GrooveTemplate* grooveTemplate(const QString& key) const;
    QVector<const GrooveTemplate*> allGrooveTemplates() const; // stable ordering

    // Jazz-only initial style preset vocabulary (expands over time).
    struct StylePreset {
        QString key;
        QString name;
        QString grooveTemplateKey;
        double templateAmount = 1.0;
        QHash<QString, InstrumentGrooveProfile> instrumentProfiles; // agentName -> profile
        // Non-timing performance “hooks” for instrument drivers (e.g., Drums=Brushes).
        // Stage 1: free-form notes, keyed by instrument name ("Drums", "Piano", ...).
        // Later this becomes structured (articulations, limb model params, etc.).
        QHash<QString, QString> articulationNotes;
        int defaultBpm = 120;
        TimeSignature defaultTimeSig{4, 4};

        // Legacy VirtuosityMatrix removed; Weights v2 defaults live here instead.
        virtuoso::control::PerformanceWeightsV2 weightsV2Defaults{};
    };
    const StylePreset* stylePreset(const QString& key) const;
    QVector<const StylePreset*> allStylePresets() const;

private:
    QHash<QString, FeelTemplate> m_feels;
    QVector<QString> m_feelOrder;

    QHash<QString, GrooveTemplate> m_templates;
    QVector<QString> m_templateOrder;

    QHash<QString, StylePreset> m_presets;
    QVector<QString> m_presetOrder;
};

} // namespace virtuoso::groove

