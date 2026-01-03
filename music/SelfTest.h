#pragma once

namespace music {

// Runs lightweight debug-only self tests (chord parsing, chord dictionary).
// Safe to call multiple times; does nothing in release builds.
void runMusicSelfTests();

} // namespace music

