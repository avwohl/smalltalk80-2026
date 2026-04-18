// st80-2026 — Launcher.hpp (Windows)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// First-launch picker mirroring the ImageLibraryView from
// ../iospharo/iospharo/Views/ImageLibraryView.swift:
//
//   - sortable image table with filter box
//   - detail panel for the selected row
//   - rename / duplicate / show in Explorer / star-auto-launch /
//     delete from a button strip and right-click context menu
//   - download-picker sheet driven by a manifest.json listing each
//     downloadable image with its SHA256 digest
//   - "Add from file…" imports a user-picked image plus companion
//     sources/changes files
//
// Persistent state:
//
//   %USERPROFILE%\Documents\Smalltalk-80\Images\<slug>\
//       <VirtualImage | *.image>
//       Smalltalk-80.sources (optional)
//       Smalltalk-80.changes (optional)
//
//   %USERPROFILE%\Documents\Smalltalk-80\library.json
//       catalog of images with user-chosen display names, timestamps,
//       and the explicit auto-launch pointer (was previously a
//       HKCU\...\LastImagePath registry value; that legacy value is
//       migrated once on first run).
//
// The manifest lives outside this repo at a URL configurable below;
// a cached copy is kept next to library.json.

#pragma once

#include <windows.h>
#include <string>

namespace st80 {

// Show the modal launcher. On success, fills `outImagePath` (UTF-8,
// as `st80_init` expects) and returns true. Returns false if the user
// cancelled or closed the window.
bool ShowLauncher(HINSTANCE hInstance, std::string &outImagePath);

// Record that `imagePath` was just launched — bumps `lastLaunchedAt`
// for the corresponding image in the library catalog.
void RememberLastImage(const std::string &imagePath);

// Return the path of the image the user has starred for auto-launch,
// or empty if none is starred (or the starred image is missing).
std::string LoadLastImage();

}  // namespace st80
