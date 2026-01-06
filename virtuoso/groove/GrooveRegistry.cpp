#include "virtuoso/groove/GrooveRegistry.h"

#include <algorithm>

namespace virtuoso::groove {

GrooveRegistry GrooveRegistry::builtins() {
    GrooveRegistry r;

    auto addFeel = [&](const FeelTemplate& t, int order) {
        r.m_feels.insert(t.key, t);
        if (r.m_feelOrder.size() <= order) r.m_feelOrder.resize(order + 1);
        r.m_feelOrder[order] = t.key;
    };

    auto addTemplate = [&](const GrooveTemplate& t, int order) {
        r.m_templates.insert(t.key, t);
        if (r.m_templateOrder.size() <= order) r.m_templateOrder.resize(order + 1);
        r.m_templateOrder[order] = t.key;
    };

    auto addPreset = [&](const StylePreset& p, int order) {
        r.m_presets.insert(p.key, p);
        if (r.m_presetOrder.size() <= order) r.m_presetOrder.resize(order + 1);
        r.m_presetOrder[order] = p.key;
    };

    // Stable ordering for UI.
    addFeel(FeelTemplate::straight(), 0);
    addFeel(FeelTemplate::swing2to1(/*amount=*/0.80), 1);
    addFeel(FeelTemplate::swing3to1(/*amount=*/0.80), 2);
    addFeel(FeelTemplate::laidBackPocket(/*pocketMs=*/18, /*amount=*/1.0), 3);

    // --- Jazz groove templates (initial vocabulary) ---
    // Note: offset values are expressed in BeatFraction for tempo-scaled swing.
    {
        GrooveTemplate t;
        t.key = "jazz_swing_2to1";
        t.name = "Jazz Swing (2:1)";
        t.category = "Jazz/Swing";
        t.gridKind = GrooveGridKind::Swing8;
        t.amount = 0.80;
        // Swing the upbeat 8th later: from 1/2 to 2/3 => +1/6 beat.
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::BeatFraction, /*+*/(1.0 / 6.0)});
        addTemplate(t, 0);
    }
    {
        GrooveTemplate t;
        t.key = "jazz_swing_3to1";
        t.name = "Jazz Swing (3:1)";
        t.category = "Jazz/Swing";
        t.gridKind = GrooveGridKind::Swing8;
        t.amount = 0.75;
        // 1/2 to 3/4 => +1/4 beat.
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::BeatFraction, /*+*/(1.0 / 4.0)});
        addTemplate(t, 1);
    }
    {
        GrooveTemplate t;
        t.key = "jazz_ballad_laidback";
        t.name = "Jazz Ballad (Laid back)";
        t.category = "Jazz/Ballad";
        t.gridKind = GrooveGridKind::Swing8;
        t.amount = 1.0;
        // Subtle swing + laid-back pocket on upbeat.
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::BeatFraction, /*+*/(1.0 / 7.0)});
        // Also lightly lay back the end of beat (triplet 2/3) for a dragging ballad feel.
        t.offsetMap.push_back({Rational(2, 3), OffsetUnit::BeatFraction, /*+*/(1.0 / 18.0)});
        addTemplate(t, 2);
    }
    // Ballad pocket templates: these are "global pocket" (withinBeat=0) + upbeat shaping.
    // They intentionally affect quarter-note patterns too (because ballad pocket is not swing-only).
    {
        GrooveTemplate t;
        t.key = "jazz_ballad_pocket_light";
        t.name = "Ballad pocket (light)";
        t.category = "Jazz/Ballad";
        t.gridKind = GrooveGridKind::Straight;
        t.amount = 1.0;
        t.offsetMap.push_back({Rational(0, 1), OffsetUnit::Ms, 10.0});  // all beat-starts slightly late
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::Ms, 14.0});  // upbeat a bit later
        addTemplate(t, 3);
    }
    {
        GrooveTemplate t;
        t.key = "jazz_ballad_pocket_medium";
        t.name = "Ballad pocket (medium)";
        t.category = "Jazz/Ballad";
        t.gridKind = GrooveGridKind::Straight;
        t.amount = 1.0;
        t.offsetMap.push_back({Rational(0, 1), OffsetUnit::Ms, 18.0});
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::Ms, 26.0});
        addTemplate(t, 4);
    }
    {
        GrooveTemplate t;
        t.key = "jazz_ballad_pocket_deep";
        t.name = "Ballad pocket (deep)";
        t.category = "Jazz/Ballad";
        t.gridKind = GrooveGridKind::Straight;
        t.amount = 1.0;
        t.offsetMap.push_back({Rational(0, 1), OffsetUnit::Ms, 28.0});
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::Ms, 40.0});
        addTemplate(t, 5);
    }
    {
        GrooveTemplate t;
        t.key = "jazz_ballad_swing_soft";
        t.name = "Ballad swing (soft)";
        t.category = "Jazz/Ballad";
        t.gridKind = GrooveGridKind::Swing8;
        t.amount = 0.55;
        // Classic 2:1 swing mapping, but softened.
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::BeatFraction, (1.0 / 6.0)});
        // Add a tiny late end-of-beat for breath.
        t.offsetMap.push_back({Rational(2, 3), OffsetUnit::Ms, 6.0});
        addTemplate(t, 6);
    }
    {
        GrooveTemplate t;
        t.key = "jazz_ballad_swing_deep";
        t.name = "Ballad swing (deep)";
        t.category = "Jazz/Ballad";
        t.gridKind = GrooveGridKind::Swing8;
        t.amount = 0.85;
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::BeatFraction, (1.0 / 6.0)});
        t.offsetMap.push_back({Rational(2, 3), OffsetUnit::Ms, 10.0});
        addTemplate(t, 7);
    }
    {
        GrooveTemplate t;
        t.key = "jazz_ballad_triplet_drag";
        t.name = "Ballad triplet drag";
        t.category = "Jazz/Ballad";
        t.gridKind = GrooveGridKind::Triplet8;
        t.amount = 1.0;
        // Late last triplet and slightly late mid triplet: slow, dragging ballad triplet feel.
        t.offsetMap.push_back({Rational(1, 3), OffsetUnit::Ms, 4.0});
        t.offsetMap.push_back({Rational(2, 3), OffsetUnit::Ms, 12.0});
        addTemplate(t, 8);
    }

    // Brushes ballad family (Chet Baker / Bill Evans vibe).
    // These combine: deep pocket on beat-start + gentle late upbeats + a touch of triplet drag.
    {
        GrooveTemplate t;
        t.key = "jazz_ballad_brushes_chet";
        t.name = "Brushes Ballad (Chet)";
        t.category = "Jazz/Ballad/Brushes";
        t.gridKind = GrooveGridKind::Swing8;
        t.amount = 1.0;
        // Deep pocket: downbeats late, upbeats later, plus gentle triplet drag.
        t.offsetMap.push_back({Rational(0, 1), OffsetUnit::Ms, 22.0});
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::Ms, 30.0});
        t.offsetMap.push_back({Rational(2, 3), OffsetUnit::Ms, 10.0});
        addTemplate(t, 9);
    }
    {
        GrooveTemplate t;
        t.key = "jazz_ballad_brushes_evans";
        t.name = "Brushes Ballad (Evans)";
        t.category = "Jazz/Ballad/Brushes";
        t.gridKind = GrooveGridKind::Swing8;
        t.amount = 1.0;
        // Slightly tighter than Chet: still laid back, but more centered/tidy.
        t.offsetMap.push_back({Rational(0, 1), OffsetUnit::Ms, 16.0});
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::Ms, 22.0});
        t.offsetMap.push_back({Rational(2, 3), OffsetUnit::Ms, 8.0});
        addTemplate(t, 10);
    }
    {
        GrooveTemplate t;
        t.key = "jazz_ecm_straight8";
        t.name = "ECM Straight 8 (soft pocket)";
        t.category = "Jazz/ECM";
        t.gridKind = GrooveGridKind::Straight;
        t.amount = 1.0;
        // Slightly lay back upbeat 8th without full swing.
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::Ms, /*+*/8.0});
        addTemplate(t, 20);
    }
    {
        GrooveTemplate t;
        t.key = "jazz_elvin_triplet_roll";
        t.name = "Elvin Triplet Roll (hint)";
        t.category = "Jazz/Triplet";
        t.gridKind = GrooveGridKind::Triplet8;
        t.amount = 1.0;
        // Make middle triplet slightly early and last triplet slightly late (rolling feel).
        t.offsetMap.push_back({Rational(1, 3), OffsetUnit::Ms, /*-*/-6.0});
        t.offsetMap.push_back({Rational(2, 3), OffsetUnit::Ms, /*+*/6.0});
        addTemplate(t, 4);
    }

    // Swing intensity variants (same underlying mapping, different amounts).
    {
        GrooveTemplate t;
        t.key = "jazz_swing_light";
        t.name = "Jazz Swing (light)";
        t.category = "Jazz/Swing";
        t.gridKind = GrooveGridKind::Swing8;
        t.amount = 0.55;
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::BeatFraction, (1.0 / 6.0)});
        addTemplate(t, 5);
    }
    {
        GrooveTemplate t;
        t.key = "jazz_swing_heavy";
        t.name = "Jazz Swing (heavy)";
        t.category = "Jazz/Swing";
        t.gridKind = GrooveGridKind::Swing8;
        t.amount = 0.95;
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::BeatFraction, (1.0 / 6.0)});
        addTemplate(t, 6);
    }

    // Triplet feel variants.
    {
        GrooveTemplate t;
        t.key = "jazz_triplet_tight";
        t.name = "Triplet feel (tight)";
        t.category = "Jazz/Triplet";
        t.gridKind = GrooveGridKind::Triplet8;
        t.amount = 0.65;
        t.offsetMap.push_back({Rational(1, 3), OffsetUnit::Ms, -3.0});
        t.offsetMap.push_back({Rational(2, 3), OffsetUnit::Ms, 3.0});
        addTemplate(t, 7);
    }

    // Shuffle (12/8) family.
    {
        GrooveTemplate t;
        t.key = "jazz_shuffle_12_8";
        t.name = "Shuffle (12/8)";
        t.category = "Jazz/Shuffle";
        t.gridKind = GrooveGridKind::Shuffle12_8;
        t.amount = 1.0;
        // If generator plays straight 8ths (1/2), map them to the shuffle 3rd triplet (2/3).
        // Same time shift as classic 2:1 swing (+1/6 beat), but categorized explicitly as shuffle.
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::BeatFraction, (1.0 / 6.0)});
        // Hint of drag on the last triplet.
        t.offsetMap.push_back({Rational(2, 3), OffsetUnit::Ms, 4.0});
        addTemplate(t, 8);
    }

    // Waltz swing (3/4) — uses the same upbeat-8th mapping.
    {
        GrooveTemplate t;
        t.key = "jazz_waltz_swing_2to1";
        t.name = "Jazz Waltz Swing (2:1)";
        t.category = "Jazz/Waltz";
        t.gridKind = GrooveGridKind::Swing8;
        t.amount = 0.75;
        t.offsetMap.push_back({Rational(1, 2), OffsetUnit::BeatFraction, (1.0 / 6.0)});
        addTemplate(t, 9);
    }

    // --- Jazz style presets (initial vocabulary) ---
    // These map a high-level style to per-instrument profiles and a groove template.
    {
        StylePreset p;
        p.key = "jazz_swing_medium";
        p.name = "Jazz Swing Medium";
        p.grooveTemplateKey = "jazz_swing_2to1";
        p.templateAmount = 0.80;
        p.defaultBpm = 130;
        p.defaultTimeSig = TimeSignature{4, 4};

        InstrumentGrooveProfile piano;
        piano.instrument = "Piano";
        piano.humanizeSeed = 1;
        piano.laidBackMs = 6;
        piano.microJitterMs = 3;
        piano.attackVarianceMs = 2;
        piano.driftMaxMs = 10;
        piano.driftRate = 0.15;
        piano.velocityJitter = 10;
        piano.accentDownbeat = 1.05;
        piano.accentBackbeat = 0.95;
        p.instrumentProfiles.insert("Piano", piano);

        InstrumentGrooveProfile bass;
        bass.instrument = "Bass";
        bass.humanizeSeed = 2;
        bass.laidBackMs = 2;
        bass.microJitterMs = 2;
        bass.attackVarianceMs = 1;
        bass.driftMaxMs = 8;
        bass.driftRate = 0.12;
        bass.velocityJitter = 6;
        bass.accentDownbeat = 1.10;
        bass.accentBackbeat = 0.85;
        p.instrumentProfiles.insert("Bass", bass);

        addPreset(p, 0);
    }
    {
        StylePreset p;
        p.key = "jazz_ballad_60";
        p.name = "Jazz Swing Ballad (60)";
        p.grooveTemplateKey = "jazz_ballad_laidback";
        p.templateAmount = 1.0;
        p.defaultBpm = 60;
        p.defaultTimeSig = TimeSignature{4, 4};

        InstrumentGrooveProfile piano;
        piano.instrument = "Piano";
        piano.humanizeSeed = 3;
        piano.laidBackMs = 18;
        piano.microJitterMs = 5;
        piano.attackVarianceMs = 4;
        piano.driftMaxMs = 20;
        piano.driftRate = 0.18;
        piano.velocityJitter = 8;
        piano.accentDownbeat = 1.08;
        piano.accentBackbeat = 0.95;
        p.instrumentProfiles.insert("Piano", piano);

        InstrumentGrooveProfile bass;
        bass.instrument = "Bass";
        bass.humanizeSeed = 4;
        bass.laidBackMs = 10;
        bass.microJitterMs = 3;
        bass.attackVarianceMs = 3;
        bass.driftMaxMs = 18;
        bass.driftRate = 0.20;
        bass.velocityJitter = 5;
        bass.accentDownbeat = 1.12;
        bass.accentBackbeat = 0.82;
        p.instrumentProfiles.insert("Bass", bass);

        addPreset(p, 1);
    }
    {
        StylePreset p;
        p.key = "jazz_ballad_50";
        p.name = "Jazz Ballad (50, deep pocket)";
        p.grooveTemplateKey = "jazz_ballad_pocket_deep";
        p.templateAmount = 1.0;
        p.defaultBpm = 50;
        p.defaultTimeSig = TimeSignature{4, 4};

        InstrumentGrooveProfile piano;
        piano.instrument = "Piano";
        piano.humanizeSeed = 31;
        piano.laidBackMs = 20;
        piano.microJitterMs = 6;
        piano.attackVarianceMs = 5;
        piano.driftMaxMs = 26;
        piano.driftRate = 0.20;
        piano.velocityJitter = 6;
        piano.accentDownbeat = 1.08;
        piano.accentBackbeat = 0.96;
        p.instrumentProfiles.insert("Piano", piano);

        InstrumentGrooveProfile bass;
        bass.instrument = "Bass";
        bass.humanizeSeed = 32;
        bass.laidBackMs = 14;
        bass.microJitterMs = 4;
        bass.attackVarianceMs = 4;
        bass.driftMaxMs = 24;
        bass.driftRate = 0.22;
        bass.velocityJitter = 4;
        bass.accentDownbeat = 1.14;
        bass.accentBackbeat = 0.84;
        p.instrumentProfiles.insert("Bass", bass);

        addPreset(p, 10);
    }
    {
        StylePreset p;
        p.key = "jazz_ballad_72";
        p.name = "Jazz Ballad (72, medium pocket)";
        p.grooveTemplateKey = "jazz_ballad_pocket_medium";
        p.templateAmount = 1.0;
        p.defaultBpm = 72;
        p.defaultTimeSig = TimeSignature{4, 4};

        InstrumentGrooveProfile piano;
        piano.instrument = "Piano";
        piano.humanizeSeed = 33;
        piano.laidBackMs = 14;
        piano.microJitterMs = 5;
        piano.attackVarianceMs = 4;
        piano.driftMaxMs = 20;
        piano.driftRate = 0.18;
        piano.velocityJitter = 7;
        piano.accentDownbeat = 1.06;
        piano.accentBackbeat = 0.96;
        p.instrumentProfiles.insert("Piano", piano);

        InstrumentGrooveProfile bass;
        bass.instrument = "Bass";
        bass.humanizeSeed = 34;
        bass.laidBackMs = 8;
        bass.microJitterMs = 3;
        bass.attackVarianceMs = 3;
        bass.driftMaxMs = 16;
        bass.driftRate = 0.18;
        bass.velocityJitter = 4;
        bass.accentDownbeat = 1.12;
        bass.accentBackbeat = 0.86;
        p.instrumentProfiles.insert("Bass", bass);

        addPreset(p, 11);
    }
    {
        StylePreset p;
        p.key = "jazz_ballad_90";
        p.name = "Jazz Ballad (90, soft swing)";
        p.grooveTemplateKey = "jazz_ballad_swing_soft";
        p.templateAmount = 0.70;
        p.defaultBpm = 90;
        p.defaultTimeSig = TimeSignature{4, 4};

        InstrumentGrooveProfile piano;
        piano.instrument = "Piano";
        piano.humanizeSeed = 35;
        piano.laidBackMs = 10;
        piano.microJitterMs = 4;
        piano.attackVarianceMs = 3;
        piano.driftMaxMs = 16;
        piano.driftRate = 0.16;
        piano.velocityJitter = 7;
        piano.accentDownbeat = 1.05;
        piano.accentBackbeat = 0.97;
        p.instrumentProfiles.insert("Piano", piano);

        InstrumentGrooveProfile bass;
        bass.instrument = "Bass";
        bass.humanizeSeed = 36;
        bass.laidBackMs = 6;
        bass.microJitterMs = 2;
        bass.attackVarianceMs = 2;
        bass.driftMaxMs = 12;
        bass.driftRate = 0.14;
        bass.velocityJitter = 4;
        bass.accentDownbeat = 1.10;
        bass.accentBackbeat = 0.88;
        p.instrumentProfiles.insert("Bass", bass);

        addPreset(p, 12);
    }

    // Brushes ballad presets (Chet Baker / Bill Evans).
    {
        StylePreset p;
        p.key = "jazz_brushes_ballad_60_chet";
        p.name = "Brushes Ballad (Chet Baker Sings, 60)";
        p.grooveTemplateKey = "jazz_ballad_brushes_chet";
        p.templateAmount = 1.0;
        p.defaultBpm = 60;
        p.defaultTimeSig = TimeSignature{4, 4};

        InstrumentGrooveProfile piano;
        piano.instrument = "Piano";
        piano.humanizeSeed = 41;
        piano.laidBackMs = 14;
        piano.microJitterMs = 4;
        piano.attackVarianceMs = 4;
        piano.driftMaxMs = 18;
        piano.driftRate = 0.18;
        piano.velocityJitter = 8;
        piano.accentDownbeat = 1.06;
        piano.accentBackbeat = 0.98;
        p.instrumentProfiles.insert("Piano", piano);

        InstrumentGrooveProfile bass;
        bass.instrument = "Bass";
        bass.humanizeSeed = 42;
        bass.laidBackMs = 10;
        bass.microJitterMs = 3;
        bass.attackVarianceMs = 3;
        bass.driftMaxMs = 16;
        bass.driftRate = 0.18;
        bass.velocityJitter = 5;
        bass.accentDownbeat = 1.12;
        bass.accentBackbeat = 0.85;
        p.instrumentProfiles.insert("Bass", bass);

        InstrumentGrooveProfile drums;
        drums.instrument = "Drums";
        drums.humanizeSeed = 43;
        drums.laidBackMs = 8;
        drums.microJitterMs = 2;
        drums.attackVarianceMs = 2;
        drums.driftMaxMs = 14;
        drums.driftRate = 0.15;
        drums.velocityJitter = 6;
        drums.accentDownbeat = 1.00;
        drums.accentBackbeat = 1.00;
        p.instrumentProfiles.insert("Drums", drums);

        p.articulationNotes.insert("Drums",
            "Brushes: Snare stir (continuous), light swishes on 2&4, feather kick (<10%), "
            "no rimshots; if intensity rises, switch to ride pattern briefly then resolve.");

        addPreset(p, 20);
    }
    {
        StylePreset p;
        p.key = "jazz_brushes_ballad_60_evans";
        p.name = "Brushes Ballad (Bill Evans, 60)";
        p.grooveTemplateKey = "jazz_ballad_brushes_evans";
        p.templateAmount = 1.0;
        p.defaultBpm = 60;
        p.defaultTimeSig = TimeSignature{4, 4};

        InstrumentGrooveProfile piano;
        piano.instrument = "Piano";
        piano.humanizeSeed = 44;
        piano.laidBackMs = 10;
        piano.microJitterMs = 3;
        piano.attackVarianceMs = 3;
        piano.driftMaxMs = 14;
        piano.driftRate = 0.16;
        piano.velocityJitter = 7;
        piano.accentDownbeat = 1.05;
        piano.accentBackbeat = 0.99;
        p.instrumentProfiles.insert("Piano", piano);

        InstrumentGrooveProfile bass;
        bass.instrument = "Bass";
        bass.humanizeSeed = 45;
        bass.laidBackMs = 6;
        bass.microJitterMs = 2;
        bass.attackVarianceMs = 2;
        bass.driftMaxMs = 12;
        bass.driftRate = 0.14;
        bass.velocityJitter = 4;
        bass.accentDownbeat = 1.10;
        bass.accentBackbeat = 0.88;
        p.instrumentProfiles.insert("Bass", bass);

        InstrumentGrooveProfile drums;
        drums.instrument = "Drums";
        drums.humanizeSeed = 46;
        drums.laidBackMs = 6;
        drums.microJitterMs = 2;
        drums.attackVarianceMs = 2;
        drums.driftMaxMs = 12;
        drums.driftRate = 0.14;
        drums.velocityJitter = 6;
        drums.accentDownbeat = 1.00;
        drums.accentBackbeat = 1.00;
        p.instrumentProfiles.insert("Drums", drums);

        p.articulationNotes.insert("Drums",
            "Brushes: Snare stir + soft sweep, more space, very light feather kick, "
            "avoid cymbal wash; prioritize breath/room and micro-dynamic shaping.");

        addPreset(p, 21);
    }
    {
        StylePreset p;
        p.key = "jazz_ecm_90";
        p.name = "ECM (Straight 8, 90)";
        p.grooveTemplateKey = "jazz_ecm_straight8";
        p.templateAmount = 1.0;
        p.defaultBpm = 90;
        p.defaultTimeSig = TimeSignature{4, 4};

        InstrumentGrooveProfile piano;
        piano.instrument = "Piano";
        piano.humanizeSeed = 5;
        piano.laidBackMs = 8;
        piano.microJitterMs = 4;
        piano.attackVarianceMs = 3;
        piano.driftMaxMs = 18;
        piano.driftRate = 0.20;
        piano.velocityJitter = 9;
        piano.accentDownbeat = 1.02;
        piano.accentBackbeat = 0.98;
        p.instrumentProfiles.insert("Piano", piano);

        InstrumentGrooveProfile bass;
        bass.instrument = "Bass";
        bass.humanizeSeed = 6;
        bass.laidBackMs = 4;
        bass.microJitterMs = 2;
        bass.attackVarianceMs = 2;
        bass.driftMaxMs = 10;
        bass.driftRate = 0.12;
        bass.velocityJitter = 6;
        bass.accentDownbeat = 1.08;
        bass.accentBackbeat = 0.90;
        p.instrumentProfiles.insert("Bass", bass);

        addPreset(p, 2);
    }

    // More jazz presets (still deterministic, groove-only; generation logic comes later).
    {
        StylePreset p;
        p.key = "jazz_bebop_240";
        p.name = "Bebop Up-tempo (240)";
        p.grooveTemplateKey = "jazz_swing_light";
        p.templateAmount = 0.55;
        p.defaultBpm = 240;
        p.defaultTimeSig = TimeSignature{4, 4};

        InstrumentGrooveProfile piano;
        piano.instrument = "Piano";
        piano.humanizeSeed = 7;
        piano.laidBackMs = 1;
        piano.microJitterMs = 1;
        piano.attackVarianceMs = 1;
        piano.driftMaxMs = 5;
        piano.driftRate = 0.10;
        piano.velocityJitter = 6;
        piano.accentDownbeat = 1.03;
        piano.accentBackbeat = 0.97;
        p.instrumentProfiles.insert("Piano", piano);

        InstrumentGrooveProfile bass;
        bass.instrument = "Bass";
        bass.humanizeSeed = 8;
        bass.laidBackMs = 0;
        bass.microJitterMs = 1;
        bass.attackVarianceMs = 0;
        bass.driftMaxMs = 4;
        bass.driftRate = 0.08;
        bass.velocityJitter = 4;
        bass.accentDownbeat = 1.08;
        bass.accentBackbeat = 0.88;
        p.instrumentProfiles.insert("Bass", bass);

        addPreset(p, 3);
    }
    {
        StylePreset p;
        p.key = "jazz_hardbop_160";
        p.name = "Hard Bop (160)";
        p.grooveTemplateKey = "jazz_swing_heavy";
        p.templateAmount = 0.95;
        p.defaultBpm = 160;
        p.defaultTimeSig = TimeSignature{4, 4};

        InstrumentGrooveProfile piano;
        piano.instrument = "Piano";
        piano.humanizeSeed = 9;
        piano.laidBackMs = 4;
        piano.microJitterMs = 3;
        piano.attackVarianceMs = 2;
        piano.driftMaxMs = 10;
        piano.driftRate = 0.15;
        piano.velocityJitter = 10;
        piano.accentDownbeat = 1.06;
        piano.accentBackbeat = 0.94;
        p.instrumentProfiles.insert("Piano", piano);

        InstrumentGrooveProfile bass;
        bass.instrument = "Bass";
        bass.humanizeSeed = 10;
        bass.laidBackMs = 1;
        bass.microJitterMs = 2;
        bass.attackVarianceMs = 1;
        bass.driftMaxMs = 8;
        bass.driftRate = 0.12;
        bass.velocityJitter = 6;
        bass.accentDownbeat = 1.12;
        bass.accentBackbeat = 0.86;
        p.instrumentProfiles.insert("Bass", bass);

        addPreset(p, 4);
    }
    {
        StylePreset p;
        p.key = "jazz_waltz_180";
        p.name = "Jazz Waltz (180)";
        p.grooveTemplateKey = "jazz_waltz_swing_2to1";
        p.templateAmount = 0.75;
        p.defaultBpm = 180;
        p.defaultTimeSig = TimeSignature{3, 4};

        InstrumentGrooveProfile piano;
        piano.instrument = "Piano";
        piano.humanizeSeed = 11;
        piano.laidBackMs = 3;
        piano.microJitterMs = 2;
        piano.attackVarianceMs = 2;
        piano.driftMaxMs = 8;
        piano.driftRate = 0.12;
        piano.velocityJitter = 8;
        piano.accentDownbeat = 1.10;
        piano.accentBackbeat = 1.00;
        p.instrumentProfiles.insert("Piano", piano);

        InstrumentGrooveProfile bass;
        bass.instrument = "Bass";
        bass.humanizeSeed = 12;
        bass.laidBackMs = 1;
        bass.microJitterMs = 2;
        bass.attackVarianceMs = 1;
        bass.driftMaxMs = 7;
        bass.driftRate = 0.10;
        bass.velocityJitter = 5;
        bass.accentDownbeat = 1.12;
        bass.accentBackbeat = 1.00;
        p.instrumentProfiles.insert("Bass", bass);

        addPreset(p, 5);
    }
    {
        StylePreset p;
        p.key = "jazz_shuffle_120";
        p.name = "Jazz Shuffle (120)";
        p.grooveTemplateKey = "jazz_shuffle_12_8";
        p.templateAmount = 1.0;
        p.defaultBpm = 120;
        p.defaultTimeSig = TimeSignature{4, 4};

        InstrumentGrooveProfile piano;
        piano.instrument = "Piano";
        piano.humanizeSeed = 13;
        piano.laidBackMs = 5;
        piano.microJitterMs = 3;
        piano.attackVarianceMs = 3;
        piano.driftMaxMs = 14;
        piano.driftRate = 0.16;
        piano.velocityJitter = 9;
        piano.accentDownbeat = 1.07;
        piano.accentBackbeat = 0.96;
        p.instrumentProfiles.insert("Piano", piano);

        InstrumentGrooveProfile bass;
        bass.instrument = "Bass";
        bass.humanizeSeed = 14;
        bass.laidBackMs = 2;
        bass.microJitterMs = 2;
        bass.attackVarianceMs = 2;
        bass.driftMaxMs = 10;
        bass.driftRate = 0.12;
        bass.velocityJitter = 5;
        bass.accentDownbeat = 1.14;
        bass.accentBackbeat = 0.88;
        p.instrumentProfiles.insert("Bass", bass);

        addPreset(p, 6);
    }

    // Clean up any gaps (defensive).
    QVector<QString> compact;
    compact.reserve(r.m_feelOrder.size());
    for (const auto& k : r.m_feelOrder) {
        if (!k.trimmed().isEmpty() && r.m_feels.contains(k)) compact.push_back(k);
    }
    r.m_feelOrder = compact;

    QVector<QString> compactT;
    compactT.reserve(r.m_templateOrder.size());
    for (const auto& k : r.m_templateOrder) {
        if (!k.trimmed().isEmpty() && r.m_templates.contains(k)) compactT.push_back(k);
    }
    r.m_templateOrder = compactT;

    QVector<QString> compactP;
    compactP.reserve(r.m_presetOrder.size());
    for (const auto& k : r.m_presetOrder) {
        if (!k.trimmed().isEmpty() && r.m_presets.contains(k)) compactP.push_back(k);
    }
    r.m_presetOrder = compactP;

    return r;
}

const FeelTemplate* GrooveRegistry::feel(const QString& key) const {
    auto it = m_feels.find(key);
    if (it == m_feels.end()) return nullptr;
    return &it.value();
}

QVector<const FeelTemplate*> GrooveRegistry::allFeels() const {
    QVector<const FeelTemplate*> out;
    out.reserve(m_feelOrder.size());
    for (const QString& k : m_feelOrder) {
        auto it = m_feels.find(k);
        if (it != m_feels.end()) out.push_back(&it.value());
    }
    return out;
}

const GrooveTemplate* GrooveRegistry::grooveTemplate(const QString& key) const {
    auto it = m_templates.find(key);
    if (it == m_templates.end()) return nullptr;
    return &it.value();
}

QVector<const GrooveTemplate*> GrooveRegistry::allGrooveTemplates() const {
    QVector<const GrooveTemplate*> out;
    out.reserve(m_templateOrder.size());
    for (const QString& k : m_templateOrder) {
        auto it = m_templates.find(k);
        if (it != m_templates.end()) out.push_back(&it.value());
    }
    return out;
}

const GrooveRegistry::StylePreset* GrooveRegistry::stylePreset(const QString& key) const {
    auto it = m_presets.find(key);
    if (it == m_presets.end()) return nullptr;
    return &it.value();
}

QVector<const GrooveRegistry::StylePreset*> GrooveRegistry::allStylePresets() const {
    QVector<const StylePreset*> out;
    out.reserve(m_presetOrder.size());
    for (const QString& k : m_presetOrder) {
        auto it = m_presets.find(k);
        if (it != m_presets.end()) out.push_back(&it.value());
    }
    return out;
}

} // namespace virtuoso::groove

