#pragma once

// Virtuoso determinism policy (code-level documentation).
//
// Goal: given the same inputs (chart + live MIDI event stream with timestamps),
// all agent decisions and humanization are reproducible across platforms and builds.
//
// Rules:
// 1) Do not use qHash() for decision-making or seeding.
// 2) Do not use Qt RNGs (QRandomGenerator) in decision-making or timing humanization.
// 3) Derive all seeds via StableHash::fnv1a32() from a namespaced string:
//    seed = fnv1a32("domain|agent|preset|bar|beat|...".toUtf8()).
// 4) Planning code must not call wall-clock APIs directly. If it needs \"now\",
//    accept it as an input (e.g., LookaheadPlanner::Inputs.nowMs).
//
// Notes:
// - Stochastic humanization is allowed, but MUST be seeded deterministically.
// - Live MIDI ingestion timestamps define the interaction-time boundary.

