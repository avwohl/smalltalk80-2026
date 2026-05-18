// st80-2026 — app/dos/Launcher.hpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Text-mode banner + error reporting for the DOS frontend. CP437
// only (the image carries its own font; these are just the early
// boot/teardown messages the user sees before/after graphics).

#pragma once

namespace st80dos {

void showBanner(const char *imagePath, int cyclesPerFrame);

// Print an error in text mode and wait briefly for a keypress so a
// double-clicked .EXE in a freshly-closed DOS box doesn't vanish
// before the user can read why it failed.
void showError(const char *message);

void showProbeHeader();

}  // namespace st80dos
