// st80-2026 — Launcher.hpp (Linux / GTK4)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// First-launch picker mirroring app/windows/Launcher.hpp:
//
//   - sortable GtkColumnView with filter box
//   - detail panel for the selected row
//   - rename / duplicate / show-in-Files / star-auto-launch /
//     delete from a button strip and right-click context menu
//   - download dialog driven by manifest.json (SHA256-verified)
//     plus a custom-URL field
//   - "Add from file…" imports a user-picked image plus companion
//     sources/changes files
//
// Persistent state under XDG_DATA_HOME (default ~/.local/share):
//
//   $XDG_DATA_HOME/st80-2026/Images/<slug>/
//       <VirtualImage | *.image>
//       Smalltalk-80.sources (optional)
//       Smalltalk-80.changes (optional)
//
//   $XDG_DATA_HOME/st80-2026/library.json
//       catalog of images with user-chosen display names, timestamps,
//       and the explicit auto-launch pointer.
//
//   $XDG_DATA_HOME/st80-2026/manifest-cache.json
//       cached copy of the latest manifest fetch — used as fallback
//       if the network is down on the next launch.

#pragma once

#include <string>

namespace st80 {

// Show the launcher window. On success, fills `outImagePath` and
// returns true. Returns false if the user closed the window or
// pressed Cancel.
bool ShowLauncher(int argc, char **argv, std::string &outImagePath);

// Stamp lastLaunchedAt for the matching catalog entry — called by
// the main entry after the VM exits cleanly.
void RememberLastImage(const std::string &imagePath);

// Return the path of the explicitly-starred auto-launch image, or
// empty if none / the file no longer exists.
std::string LoadLastImage();

// Same, but also returns the catalog display name in
// outDisplayName. Used by the splash so it can show the user-chosen
// name rather than the bare filename.
std::string LoadAutoLaunchInfo(std::string &outDisplayName);

// 3-second countdown splash shown before booting an auto-launch
// image. Returns true if the countdown completed (caller should
// proceed) or false if the user clicked "Show Library".
bool ShowAutoLaunchSplash(int argc, char **argv,
                          const std::string &imagePath,
                          const std::string &displayName);

}  // namespace st80
