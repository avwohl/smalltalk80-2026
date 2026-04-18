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

struct Args {
    std::string imagePath;
    int cyclesPerFrame = 4000;
    bool noWindow = false;
};

void usage() {
    std::fprintf(stderr,
        "usage: st80-win.exe [--cycles-per-frame N] [--no-window]"
        " <path-to-image>\n");
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
        } else if (!a.empty() && a[0] == '-' && a.size() > 1) {
            usage();
            return 64;
        } else {
            if (!out.imagePath.empty()) { usage(); return 64; }
            out.imagePath = a;
            ++i;
        }
    }
    if (out.imagePath.empty()) { usage(); return 64; }
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
};

WindowState *g_state = nullptr;

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
            }
            return 0;
        }

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
    // bar + frame for the chosen WS_* style.
    const DWORD style   = WS_OVERLAPPEDWINDOW;
    const DWORD exStyle = 0;
    RECT wantedClient{0, 0, W, H};
    AdjustWindowRectEx(&wantedClient, style, FALSE, exStyle);

    HWND hwnd = CreateWindowExW(
        exStyle, wc.lpszClassName, L"Smalltalk-80",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wantedClient.right - wantedClient.left,
        wantedClient.bottom - wantedClient.top,
        nullptr, nullptr, hInstance, nullptr);
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

        // Handle VM-driven display resize.
        const int nw = st80_display_width();
        const int nh = st80_display_height();
        if (nw > 0 && nh > 0 && (nw != state.W || nh != state.H)) {
            state.W = nw;
            state.H = nh;
            rebuildBitmapInfo(state);
            RECT r{0, 0, nw, nh};
            AdjustWindowRectEx(&r, style, FALSE, exStyle);
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
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);
    st80_shutdown();
    return 0;
}

}  // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    Args args;
    if (int rc = parseArgs(args); rc != 0) return rc;
    if (args.noWindow) return runHeadless(args);
    return runWindowed(hInstance, args);
}
