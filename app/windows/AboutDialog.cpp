// st80-2026 — Win32 About dialog.
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Ports the content of app/apple-catalyst/AboutView.swift to a
// Win32 TaskDialogIndirect. TaskDialog understands the <a href="">
// syntax for hyperlinks; clicks fire TDN_HYPERLINK_CLICKED which
// we hand off to ShellExecuteW.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "AboutDialog.hpp"

#include <commctrl.h>
#include <shellapi.h>

#include <string>

namespace st80 {
namespace {

HRESULT CALLBACK aboutTaskDialogCallback(HWND hwnd, UINT msg,
                                        WPARAM /*wp*/, LPARAM lp,
                                        LONG_PTR /*refData*/) {
    if (msg == TDN_HYPERLINK_CLICKED) {
        const wchar_t *url = reinterpret_cast<const wchar_t *>(lp);
        if (url && *url) {
            ShellExecuteW(hwnd, L"open", url, nullptr, nullptr,
                          SW_SHOWNORMAL);
        }
    }
    return S_OK;
}

}  // namespace

void ShowAboutDialog(HWND owner) {
    // Content mirrors AboutView.swift's sections: title + subtitle,
    // Project link, References, Blue Book footnote. The string
    // stays wide because TaskDialogIndirect speaks UTF-16.
    static const wchar_t kTitle[]   = L"About Smalltalk-80";
    static const wchar_t kHeader[]  = L"Smalltalk-80";
    static const wchar_t kContent[] =
        L"Blue Book VM, 1983 Xerox virtual image.\n"
        L"\n"
        L"<b>Project</b>\n"
        L"<a href=\"https://github.com/avwohl/smalltalk80-2026\">"
        L"avwohl/smalltalk80-2026</a>\n"
        L"\n"
        L"<b>References</b>\n"
        L"<a href=\"https://github.com/dbanay/Smalltalk\">dbanay/Smalltalk</a>"
        L" — MIT; primary C++ port source for object memory, BitBlt,"
        L" interpreter dispatch, and primitives.\n"
        L"\n"
        L"<a href=\"https://github.com/rochus-keller/Smalltalk\">"
        L"rochus-keller/Smalltalk</a> — GPL; read-only reference."
        L" Its image viewer is useful for inspecting the Xerox v2"
        L" image format.\n"
        L"\n"
        L"<a href=\"https://github.com/iriyak/Smalltalk\">iriyak/Smalltalk</a>"
        L" — additional reference implementation of the Blue Book VM.\n"
        L"\n"
        L"Implements the Smalltalk-80 virtual machine as specified in"
        L" Goldberg \x0026 Robson, \x201CSmalltalk-80: The Language"
        L" and its Implementation\x201D (Addison-Wesley, 1983),"
        L" chapters 26\x2013" L"30.";

    TASKDIALOGCONFIG cfg{};
    cfg.cbSize             = sizeof(cfg);
    cfg.hwndParent         = owner;
    cfg.dwFlags            = TDF_ENABLE_HYPERLINKS |
                             TDF_ALLOW_DIALOG_CANCELLATION |
                             TDF_SIZE_TO_CONTENT;
    cfg.dwCommonButtons    = TDCBF_OK_BUTTON;
    cfg.pszWindowTitle     = kTitle;
    cfg.pszMainInstruction = kHeader;
    cfg.pszContent         = kContent;
    cfg.pfCallback         = aboutTaskDialogCallback;

    TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr);
}

}  // namespace st80
