// st80-2026 — Win32 About dialog.
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Mirrors app/apple-catalyst/AboutView.swift: project link,
// references list, Blue Book footnote. Implemented as a
// TaskDialogIndirect so we get hyperlink-aware rich text without
// shipping a .rc dialog template.

#ifndef ST80_WIN_ABOUT_DIALOG_HPP
#define ST80_WIN_ABOUT_DIALOG_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace st80 {

void ShowAboutDialog(HWND owner);

}  // namespace st80

#endif  // ST80_WIN_ABOUT_DIALOG_HPP
