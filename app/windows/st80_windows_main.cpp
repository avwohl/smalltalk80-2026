// st80-2026 — Windows app entry (pure Win32 + GDI).
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Minimal desktop frontend for the Windows port. Mirrors what the
// SwiftUI/Metal frontend does on Apple and what the SDL2 frontend
// does on Linux: open a window at the VM's display size, drive
// the interpreter loop, push the 1-bit display buffer through the
// Bridge.h RGBA accessor onto a GDI DIB via StretchDIBits, and
// translate Win32 WM_* messages into st80_post_* calls.
//
// No SDL, no Direct2D, no D3D — just <windows.h>. This keeps the
// Store submission minimal (no third-party redistributables) and
// matches the project rule of "windows specific platform files,
// no ifdefs in portable code".
//
// Usage:
//   st80-win.exe [--cycles-per-frame N] [--no-window] <path-to-image>
//
// Flags:
//   --cycles-per-frame N  Interpreter cycles run between frames
//                         (default 4000 — same as the other frontends).
//   --no-window           Headless mode: run cycles and exit. Useful
//                         on build agents and inside the MSIX AppX
//                         smoke test.
//
// Three-button mouse mapping:
//   plain left click    = red    (primary / select)
//   Alt  + left click   = yellow (text / operation menu)
//   Ctrl + left click   = blue   (frame / window menu)
//   middle click        = blue
//   right click         = yellow

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Bridge.h"
#include "Launcher.hpp"
#include "AboutDialog.hpp"

#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Menu command IDs. Kept out of any resource header because the
// menubar is built from code via CreateMenu/AppendMenuW — ties the
// IDs to the WM_COMMAND handler and nothing else.
constexpr UINT kIdFileExit   = 0x9001;
constexpr UINT kIdEditCopy   = 0x9002;
constexpr UINT kIdEditPaste  = 0x9003;
constexpr UINT kIdHelpAbout  = 0x9004;

struct Args {
    std::string imagePath;
    int  cyclesPerFrame = 4000;
    bool noWindow       = false;
    bool forceLauncher  = false;   // --launcher flag
};

void usage() {
    // /SUBSYSTEM:WINDOWS apps have no console, so a stderr write
    // disappears and the user sees a silent exit. Show a MessageBox
    // so command-line misuse is at least visible.
    static const wchar_t kMsg[] =
        L"usage: st80-win.exe [--cycles-per-frame N] [--no-window]"
        L" [--launcher] [<path-to-image>]\n"
        L"\n"
        L"With no image path, st80-win opens a launcher to pick or"
        L" download an image.\n"
        L"--launcher forces the launcher even when one was used"
        L" previously.";
    std::fprintf(stderr, "%ls\n", kMsg);
    MessageBoxW(nullptr, kMsg, L"Smalltalk-80",
                MB_ICONINFORMATION | MB_OK);
}

// Convert a UTF-16 argv from CommandLineToArgvW into UTF-8 narrow
// strings. The Windows bridge + ANSI Win32 file API work on 8-bit
// code pages, but the VM only ever sees the image-directory path
// we split out, which is fine under MBCS as long as it's ASCII —
// match the Linux frontend's assumption that image paths are
// plain text.
std::string toUtf8(const wchar_t *w) {
    if (!w) return {};
    const int wlen = static_cast<int>(std::wcslen(w));
    if (wlen == 0) return {};
    const int need = WideCharToMultiByte(CP_UTF8, 0, w, wlen,
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, wlen,
                        out.data(), need, nullptr, nullptr);
    return out;
}

int parseArgs(Args &out) {
    int argc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv) { usage(); return 64; }
    std::vector<std::string> argv;
    argv.reserve(argc);
    for (int i = 0; i < argc; i++) argv.push_back(toUtf8(wargv[i]));
    LocalFree(wargv);

    int i = 1;
    while (i < argc) {
        const std::string &a = argv[i];
        if (a == "--cycles-per-frame" && i + 1 < argc) {
            out.cyclesPerFrame = std::atoi(argv[++i].c_str());
            if (out.cyclesPerFrame < 1) out.cyclesPerFrame = 1;
            ++i;
        } else if (a == "--no-window") {
            out.noWindow = true;
            ++i;
        } else if (a == "--launcher") {
            out.forceLauncher = true;
            ++i;
        } else if (!a.empty() && a[0] == '-' && a.size() > 1) {
            usage();
            return 64;
        } else {
            if (!out.imagePath.empty()) { usage(); return 64; }
            out.imagePath = a;
            ++i;
        }
    }
    // Headless mode demands an image on the command line — there's
    // no UI to pick one. The launcher only runs when there is one.
    if (out.noWindow && out.imagePath.empty()) {
        usage();
        return 64;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Window state
// ----------------------------------------------------------------------------

struct WindowState {
    HWND hwnd = nullptr;
    int  W = 0;           // current VM display width
    int  H = 0;           // current VM display height
    bool running = true;
    bool haveDisplay = false;
    BITMAPINFO bmi{};     // cached DIB header for StretchDIBits

    // Smalltalk-drawn mouse cursor (§BB 30 — the image updates the
    // cursor form when the pointer enters a scroll-bar region, a
    // text-selection region, etc.). We poll `st80_cursor_image`
    // every frame, hash the 16×16 bitmap, and rebuild the HCURSOR
    // only when it changes.
    HCURSOR        cursor     = nullptr;
    std::uint64_t  cursorHash = 0;
};

WindowState *g_state = nullptr;

// ----------------------------------------------------------------------------
// Cursor construction
// ----------------------------------------------------------------------------
//
// The Smalltalk cursor is stored as 16 16-bit words with bit (15 - x)
// of word y giving pixel (x, y). Bit set = black, bit clear =
// transparent. Windows monochrome cursors take an AND mask + XOR mask
// pair:
//
//      AND=0 XOR=0  → black
//      AND=0 XOR=1  → white
//      AND=1 XOR=0  → transparent (screen passes through)
//      AND=1 XOR=1  → inverted
//
// So "Smalltalk bit set" → AND=0, XOR=0 and "Smalltalk bit clear"
// → AND=1, XOR=0. The Blue Book hotspot is the upper-left of the
// form (0, 0); image coordinates already account for it.

std::uint64_t hashCursorBits(const std::uint16_t bits[16]) {
    std::uint64_t h = 14695981039346656037ULL;
    for (int i = 0; i < 16; ++i) {
        h ^= bits[i];
        h *= 1099511628211ULL;
    }
    return h;
}

HCURSOR buildCursorFromBits(const std::uint16_t bits[16]) {
    BYTE andMask[32];
    BYTE xorMask[32];
    for (int y = 0; y < 16; ++y) {
        const std::uint16_t w = bits[y];
        // In a monochrome Win32 bitmap row, bit 7 of byte 0 is the
        // leftmost pixel — matches Smalltalk's bit-15-is-leftmost
        // layout when we split the word hi/lo.
        const BYTE hi = static_cast<BYTE>((w >> 8) & 0xff);
        const BYTE lo = static_cast<BYTE>(w & 0xff);
        andMask[y * 2 + 0] = static_cast<BYTE>(~hi);
        andMask[y * 2 + 1] = static_cast<BYTE>(~lo);
        xorMask[y * 2 + 0] = 0;
        xorMask[y * 2 + 1] = 0;
    }
    HBITMAP hAnd = CreateBitmap(16, 16, 1, 1, andMask);
    HBITMAP hXor = CreateBitmap(16, 16, 1, 1, xorMask);
    if (!hAnd || !hXor) {
        if (hAnd) DeleteObject(hAnd);
        if (hXor) DeleteObject(hXor);
        return nullptr;
    }
    ICONINFO ii{};
    ii.fIcon    = FALSE;   // FALSE → cursor, not icon
    ii.xHotspot = 0;
    ii.yHotspot = 0;
    ii.hbmMask  = hAnd;
    ii.hbmColor = hXor;
    HCURSOR h = CreateIconIndirect(&ii);
    DeleteObject(hAnd);
    DeleteObject(hXor);
    return h;
}

// Called once per frame. Polls the VM, rebuilds the HCURSOR only on
// change, and — if the cursor is currently over our client area —
// applies it immediately. Without the immediate SetCursor we would
// only pick up the new shape on the next WM_SETCURSOR.
void refreshCursorIfChanged(WindowState &st) {
    std::uint16_t bits[16] = {0};
    st80_cursor_image(bits);

    const std::uint64_t h = hashCursorBits(bits);
    if (h == st.cursorHash) return;
    st.cursorHash = h;

    // All-zero form = VM hasn't set a cursor. Drop to the system
    // arrow rather than showing a blank hotspot.
    bool allZero = true;
    for (int i = 0; i < 16; ++i) if (bits[i]) { allZero = false; break; }

    if (st.cursor) { DestroyCursor(st.cursor); st.cursor = nullptr; }
    if (!allZero)  st.cursor = buildCursorFromBits(bits);

    // If the pointer is currently over our client area, apply the
    // new cursor immediately — WM_SETCURSOR won't fire until the
    // pointer moves.
    POINT pt;
    if (GetCursorPos(&pt)) {
        HWND under = WindowFromPoint(pt);
        if (under == st.hwnd) {
            SetCursor(st.cursor ? st.cursor
                                : LoadCursorW(nullptr, IDC_ARROW));
        }
    }
}

void rebuildBitmapInfo(WindowState &st) {
    std::memset(&st.bmi, 0, sizeof(st.bmi));
    st.bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    st.bmi.bmiHeader.biWidth       = st.W;
    st.bmi.bmiHeader.biHeight      = -st.H;  // top-down
    st.bmi.bmiHeader.biPlanes      = 1;
    st.bmi.bmiHeader.biBitCount    = 32;
    st.bmi.bmiHeader.biCompression = BI_RGB;
}

// ----------------------------------------------------------------------------
// Input translation
// ----------------------------------------------------------------------------

St80MouseButton buttonForLeftClick() {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt  = (GetKeyState(VK_MENU)    & 0x8000) != 0;
    if (ctrl) return ST80_BTN_BLUE;
    if (alt)  return ST80_BTN_YELLOW;
    return ST80_BTN_RED;
}

std::uint32_t modifiersFromWin32() {
    std::uint32_t m = 0;
    if (GetKeyState(VK_SHIFT)   & 0x8000) m |= ST80_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= ST80_MOD_CTRL;
    if (GetKeyState(VK_MENU)    & 0x8000) m |= ST80_MOD_OPTION;
    if ((GetKeyState(VK_LWIN)   & 0x8000) ||
        (GetKeyState(VK_RWIN)   & 0x8000)) m |= ST80_MOD_COMMAND;
    return m;
}

// ----------------------------------------------------------------------------
// Menu construction
// ----------------------------------------------------------------------------
//
// File     → Exit                            Alt+F4
// Edit     → Copy Selection... (info dialog) — OS-clipboard copy-out
//            requires a HAL primitive (see note below)
//          → Paste from Clipboard            Ctrl+Shift+V
// Help     → About Smalltalk-80...

HMENU buildMenuBar() {
    HMENU bar = CreateMenu();

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, kIdFileExit, L"E&xit\tAlt+F4");
    AppendMenuW(bar, MF_POPUP | MF_STRING,
                reinterpret_cast<UINT_PTR>(fileMenu), L"&File");

    HMENU editMenu = CreatePopupMenu();
    AppendMenuW(editMenu, MF_STRING, kIdEditCopy,
                L"&Copy Selection...");
    AppendMenuW(editMenu, MF_STRING, kIdEditPaste,
                L"&Paste from Clipboard\tCtrl+Shift+V");
    AppendMenuW(bar, MF_POPUP | MF_STRING,
                reinterpret_cast<UINT_PTR>(editMenu), L"&Edit");

    HMENU helpMenu = CreatePopupMenu();
    AppendMenuW(helpMenu, MF_STRING, kIdHelpAbout,
                L"&About Smalltalk-80...");
    AppendMenuW(bar, MF_POPUP | MF_STRING,
                reinterpret_cast<UINT_PTR>(helpMenu), L"&Help");

    return bar;
}

// ----------------------------------------------------------------------------
// Clipboard → VM
// ----------------------------------------------------------------------------
//
// Mirrors app/apple-catalyst/St80MTKView.swift:pasteFromSystemClipboard.
// One-way bridge: the OS clipboard is injected as a stream of key
// events, so the VM's text editor sees the same input it would see
// if the user typed the characters. No clipboard-change notifications
// feed back into the VM. Copy-out (VM selection → OS clipboard) is
// not implemented — see `showCopyUnsupportedDialog` below.
void pasteFromClipboard(HWND owner) {
    if (!OpenClipboard(owner)) return;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return; }
    const wchar_t *text = static_cast<const wchar_t *>(GlobalLock(h));
    if (!text) { CloseClipboard(); return; }

    for (const wchar_t *p = text; *p; ++p) {
        unsigned code = static_cast<unsigned>(*p);
        // Skip surrogates — the Blue Book image speaks 7-bit ASCII.
        if (code >= 0xD800 && code <= 0xDFFF) continue;
        if (code == 0x0A) code = 13;             // LF → CR
        else if (code == 0x09) { /* TAB as-is */ }
        else if (code == 0x0D) { /* CR as-is */ }
        else if (code < 0x20 || code > 0x7E) continue;  // clip
        st80_post_key_down(static_cast<int>(code), 0);
    }

    GlobalUnlock(h);
    CloseClipboard();
}

void showCopyUnsupportedDialog(HWND owner) {
    // Honest about the limitation rather than pretending the menu
    // item does something. Mirrors the comment at
    // app/apple-catalyst/St80MTKView.swift:461 — Mac Catalyst has
    // the same shape (paste-in works; copy-out would need a HAL
    // primitive + image modification).
    MessageBoxW(owner,
        L"Copying Smalltalk-selected text to the Windows clipboard"
        L" requires a VM-side primitive that isn't wired up yet.\n\n"
        L"For now, use Smalltalk's own copy command from the"
        L" yellow-button (operate) menu inside a text pane — it"
        L" copies to the image's internal clipboard, which paste"
        L" inside Smalltalk will read.\n\n"
        L"Pasting the Windows clipboard INTO Smalltalk does work"
        L" (Ctrl+Shift+V or Edit \x2192 Paste from Clipboard).",
        L"Copy to Windows Clipboard",
        MB_ICONINFORMATION | MB_OK);
}

// ----------------------------------------------------------------------------
// Window procedure
// ----------------------------------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WindowState *st = g_state;
    if (!st) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_CLOSE:
            st->running = false;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_ERASEBKGND:
            // Prevent GDI from painting white before WM_PAINT — we
            // draw every pixel of the client area ourselves.
            return 1;

        case WM_SETCURSOR:
            // Only override for the client area. Title-bar, frame,
            // and resize borders need to show the system cursors
            // Windows normally draws for them.
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(st->cursor ? st->cursor
                                     : LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (st->haveDisplay && st->W > 0 && st->H > 0) {
                const uint32_t *pixels = st80_display_pixels();
                if (pixels) {
                    RECT rc;
                    GetClientRect(hwnd, &rc);
                    StretchDIBits(
                        hdc,
                        0, 0, rc.right - rc.left, rc.bottom - rc.top,
                        0, 0, st->W, st->H,
                        pixels,
                        &st->bmi,
                        DIB_RGB_COLORS,
                        SRCCOPY);
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            // Translate client coordinates into VM coordinates.
            // We stretch the DIB across the whole client area, so
            // undo that here.
            RECT rc; GetClientRect(hwnd, &rc);
            const LONG cw = std::max<LONG>(1, rc.right - rc.left);
            const LONG ch = std::max<LONG>(1, rc.bottom - rc.top);
            const int mx = GET_X_LPARAM(lp);
            const int my = GET_Y_LPARAM(lp);
            const int vx = static_cast<int>(
                st->W > 0 ? (mx * st->W) / cw : mx);
            const int vy = static_cast<int>(
                st->H > 0 ? (my * st->H) / ch : my);
            st80_post_mouse_move(vx, vy);
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: {
            RECT rc; GetClientRect(hwnd, &rc);
            const LONG cw = std::max<LONG>(1, rc.right - rc.left);
            const LONG ch = std::max<LONG>(1, rc.bottom - rc.top);
            const int mx = GET_X_LPARAM(lp);
            const int my = GET_Y_LPARAM(lp);
            const int vx = static_cast<int>(
                st->W > 0 ? (mx * st->W) / cw : mx);
            const int vy = static_cast<int>(
                st->H > 0 ? (my * st->H) / ch : my);

            St80MouseButton b = ST80_BTN_RED;
            const bool down = (msg == WM_LBUTTONDOWN) ||
                              (msg == WM_MBUTTONDOWN) ||
                              (msg == WM_RBUTTONDOWN);
            if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP)
                b = buttonForLeftClick();
            else if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP)
                b = ST80_BTN_BLUE;
            else
                b = ST80_BTN_YELLOW;

            if (down) {
                SetCapture(hwnd);
                st80_post_mouse_down(vx, vy, b);
            } else {
                ReleaseCapture();
                st80_post_mouse_up(vx, vy, b);
            }
            return 0;
        }

        case WM_CHAR: {
            // wp is the character code (UTF-16). Feed 7-bit ASCII
            // through — including BS(8), TAB(9), CR(13), ESC(27)
            // which Windows already delivers via WM_CHAR. Multi-
            // byte glyphs are ignored for the Blue Book image's
            // decoded-keyboard path.
            const unsigned ch = static_cast<unsigned>(wp);
            if (ch < 0x80) {
                st80_post_key_down(static_cast<int>(ch),
                                   modifiersFromWin32());
            }
            return 0;
        }

        case WM_KEYDOWN: {
            // VK_DELETE (forward delete) has no WM_CHAR counterpart
            // in most layouts — route it manually as ASCII 127.
            if (wp == VK_DELETE) {
                st80_post_key_down(127, modifiersFromWin32());
                return 0;
            }
            // Ctrl+Shift+V → paste OS clipboard as keystrokes. We
            // intentionally avoid plain Ctrl+V so Smalltalk's own
            // Ctrl+V binding (whatever the image maps it to) keeps
            // working.
            if (wp == 'V' &&
                (GetKeyState(VK_CONTROL) & 0x8000) &&
                (GetKeyState(VK_SHIFT)   & 0x8000)) {
                pasteFromClipboard(hwnd);
                return 0;
            }
            return 0;
        }

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case kIdFileExit:
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
                case kIdEditCopy:
                    showCopyUnsupportedDialog(hwnd);
                    return 0;
                case kIdEditPaste:
                    pasteFromClipboard(hwnd);
                    return 0;
                case kIdHelpAbout:
                    st80::ShowAboutDialog(hwnd);
                    return 0;
            }
            break;

        case WM_KILLFOCUS:
            // Alt-Tab and click-away can leave us mid-drag with a
            // captured mouse and a half-sent button event. Release
            // the capture so the next click on return to our window
            // starts cleanly. Posted VM state is not rolled back —
            // the VM tracks its own button state via post_mouse_*,
            // and the user reported text-selection oddities after
            // alt-tabbing (see docs/changes.md build 28).
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ----------------------------------------------------------------------------
// Frame loop
// ----------------------------------------------------------------------------

int runHeadless(const Args &args) {
    // On headless we have no UI; just run a batch and exit. Used
    // by the MSIX smoke test and by CI on Windows runners without
    // an interactive desktop.
    if (!st80_init(args.imagePath.c_str())) {
        std::fprintf(stderr, "st80-win: st80_init FAILED\n");
        return 2;
    }
    const int total = args.cyclesPerFrame * 10;
    const int ran = st80_run(total);
    std::fprintf(stderr, "st80-win: ran %d cycles, quit=%d\n",
                 ran, st80_quit_requested());
    st80_shutdown();
    return 0;
}

int runWindowed(HINSTANCE hInstance, const Args &args) {
    if (!st80_init(args.imagePath.c_str())) {
        MessageBoxW(nullptr,
            L"st80-win: failed to load the Smalltalk-80 image.",
            L"Smalltalk-80", MB_ICONERROR | MB_OK);
        return 2;
    }

    // Prime the VM so primitiveBeDisplay sets the display size
    // before we create the window. Matches Linux SDL frontend.
    st80_run(4000);
    int W = st80_display_width();
    int H = st80_display_height();
    if (W <= 0 || H <= 0) { W = 640; H = 480; }

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"St80WindowClass";
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm       = wc.hIcon;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"RegisterClassEx failed.",
                    L"Smalltalk-80", MB_ICONERROR | MB_OK);
        st80_shutdown();
        return 3;
    }

    WindowState state;
    state.W = W;
    state.H = H;
    state.haveDisplay = true;
    rebuildBitmapInfo(state);
    g_state = &state;

    // Add non-client padding so the VM's inner area stays the
    // advertised size. AdjustWindowRect accounts for the title
    // bar + frame + menu bar for the chosen WS_* style.
    const DWORD style   = WS_OVERLAPPEDWINDOW;
    const DWORD exStyle = 0;
    RECT wantedClient{0, 0, W, H};
    AdjustWindowRectEx(&wantedClient, style, TRUE, exStyle);

    HMENU menuBar = buildMenuBar();

    HWND hwnd = CreateWindowExW(
        exStyle, wc.lpszClassName, L"Smalltalk-80",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wantedClient.right - wantedClient.left,
        wantedClient.bottom - wantedClient.top,
        nullptr, menuBar, hInstance, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"CreateWindow failed.",
                    L"Smalltalk-80", MB_ICONERROR | MB_OK);
        st80_shutdown();
        return 3;
    }
    state.hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Main loop: drain the Win32 queue, run VM cycles, push dirty
    // rect into a WM_PAINT via InvalidateRect. PeekMessage keeps
    // the loop non-blocking; WaitMessage would starve the VM.
    while (state.running && !st80_quit_requested()) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                state.running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!state.running) break;

        st80_run(args.cyclesPerFrame);

        // Pick up any cursor form the image may have written this
        // frame (scroll-bar regions, text selection, etc.).
        refreshCursorIfChanged(state);

        // Handle VM-driven display resize.
        const int nw = st80_display_width();
        const int nh = st80_display_height();
        if (nw > 0 && nh > 0 && (nw != state.W || nh != state.H)) {
            state.W = nw;
            state.H = nh;
            rebuildBitmapInfo(state);
            RECT r{0, 0, nw, nh};
            AdjustWindowRectEx(&r, style, TRUE, exStyle);
            SetWindowPos(hwnd, nullptr, 0, 0,
                         r.right - r.left, r.bottom - r.top,
                         SWP_NOMOVE | SWP_NOZORDER);
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        // Flush pixels and invalidate the dirty rect so the next
        // WM_PAINT redraws the changed region only.
        const St80Rect d = st80_display_sync();
        if (d.w > 0 && d.h > 0) {
            // Map VM coords back to client coords for the rect
            // we invalidate. If the VM display is being stretched,
            // err on the side of a larger rect — GDI can handle it.
            RECT rc; GetClientRect(hwnd, &rc);
            const LONG cw = std::max<LONG>(1, rc.right - rc.left);
            const LONG ch = std::max<LONG>(1, rc.bottom - rc.top);
            RECT dirty{
                (d.x * cw) / state.W,
                (d.y * ch) / state.H,
                ((d.x + d.w) * cw + state.W - 1) / state.W,
                ((d.y + d.h) * ch + state.H - 1) / state.H
            };
            InvalidateRect(hwnd, &dirty, FALSE);
        }
    }

    g_state = nullptr;
    if (state.cursor) { DestroyCursor(state.cursor); state.cursor = nullptr; }
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);
    st80_shutdown();
    return 0;
}

}  // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    Args args;
    if (int rc = parseArgs(args); rc != 0) return rc;

    // Headless path is for CI / smoke tests only; never invoke UI.
    if (args.noWindow) return runHeadless(args);

    // If no image path was passed on the command line, run the
    // launcher (matching app/apple-catalyst/ContentView.swift's
    // ImageLibraryView path). Skip it when the user already
    // launched an image previously and isn't holding Shift, so
    // ordinary double-click-to-launch doesn't show the picker
    // every time.
    if (args.imagePath.empty()) {
        const bool shiftHeld =
            (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!args.forceLauncher && !shiftHeld) {
            std::string displayName;
            std::string remembered = st80::LoadAutoLaunchInfo(displayName);
            if (!remembered.empty()) {
                // 3-second countdown with a "Show Library" escape
                // hatch so a damaged starred image doesn't lock the
                // user out. Mirrors AutoLaunchSplashView on Catalyst.
                if (st80::ShowAutoLaunchSplash(hInstance, remembered,
                                               displayName)) {
                    args.imagePath = remembered;
                }
            }
        }
        if (args.imagePath.empty()) {
            std::string picked;
            if (!st80::ShowLauncher(hInstance, picked)) return 0;
            args.imagePath = picked;
        }
    }

    int rc = runWindowed(hInstance, args);
    if (rc == 0) st80::RememberLastImage(args.imagePath);
    return rc;
}
