// st80-2026 — DosBridge.cpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Implements Bridge.h on top of DosHal + Interpreter + PosixFileSystem
// (via DosHostFileSystem.hpp). Structurally identical to
// WindowsBridge.cpp / LinuxBridge.cpp; the only differences are (1)
// the HAL subclass and (2) file-system aliasing. The frontend
// (app/dos — VBE + INT 33h mouse + INT 16h keyboard) drives the same
// C API that the Win32/SDL/Swift frontends drive, so everything
// stays in lockstep.
//
// DOS runtime path: our DJGPP binary ships with a go32-v2 stub; when
// dosiz (or CWSDPMI on real DOS) loads it, the stub switches into
// PM, our `main()` runs in ring-3 32-bit flat, and every Bridge.h
// entry point runs from that single thread. Bridge.h's same-thread
// contract is trivially satisfied.

#include "Bridge.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "DosHal.hpp"
#include "DosHostFileSystem.hpp"
#include "Interpreter.hpp"
#include "ObjectMemory.hpp"
#include "Oops.hpp"

namespace {

struct Runtime {
    st80::DosHal hal;
    std::unique_ptr<st80::HostFileSystem> fs;
    std::unique_ptr<st80::Interpreter> vm;
    std::atomic<bool> stopRequested{false};

    std::uint32_t lastEventMs = 0;
    bool firstEvent = true;
};

Runtime *g_runtime = nullptr;

// ----------------------------------------------------------------------------
// Input word encoding (BB §29.12 / dbanay main.cpp:532-580). Identical
// constants and packing as WindowsBridge / LinuxBridge.
// ----------------------------------------------------------------------------

constexpr std::uint16_t EV_DELTA_TIME = 0;
constexpr std::uint16_t EV_X_COORD    = 1;
constexpr std::uint16_t EV_Y_COORD    = 2;
constexpr std::uint16_t EV_BI_DOWN    = 3;
constexpr std::uint16_t EV_BI_UP      = 4;
constexpr std::uint16_t EV_ABS_TIME   = 5;

constexpr int BUTTON_RED    = 130;
constexpr int BUTTON_YELLOW = 129;
constexpr int BUTTON_BLUE   = 128;

std::uint16_t encodeWord(std::uint16_t type, std::uint16_t parameter) {
    return static_cast<std::uint16_t>(
        ((type & 0xF) << 12) | (parameter & 0xFFF));
}

void pushWord(Runtime &rt, std::uint16_t word) {
    rt.hal.postInputWord(word);
    const int sem = rt.hal.inputSemaphore();
    if (sem > 0) rt.vm->asynchronousSignal(sem);
}

int smalltalkButton(St80MouseButton b) {
    switch (b) {
        case ST80_BTN_RED:    return BUTTON_RED;
        case ST80_BTN_YELLOW: return BUTTON_YELLOW;
        case ST80_BTN_BLUE:   return BUTTON_BLUE;
    }
    return BUTTON_RED;
}

void emitTimestamp(Runtime &rt) {
    const std::uint32_t now = rt.hal.get_msclock();
    const std::uint32_t delta = rt.firstEvent ? 0 : (now - rt.lastEventMs);
    rt.lastEventMs = now;
    rt.firstEvent = false;

    if (delta <= 0xFFF) {
        pushWord(rt, encodeWord(EV_DELTA_TIME,
                                static_cast<std::uint16_t>(delta)));
    } else {
        const std::uint32_t abs = rt.hal.get_smalltalk_epoch_time();
        pushWord(rt, encodeWord(EV_ABS_TIME, 0));
        pushWord(rt, static_cast<std::uint16_t>((abs >> 16) & 0xFFFF));
        pushWord(rt, static_cast<std::uint16_t>(abs & 0xFFFF));
    }
}

constexpr std::uint32_t PIXEL_BLACK = 0xFF000000u;
constexpr std::uint32_t PIXEL_WHITE = 0xFFFFFFFFu;

void expandDirtyRectToRGBA(Runtime &rt,
                           const st80::DosHal::DirtyRect &d,
                           int displayBits) {
    const int W = rt.hal.displayWidth();
    const int H = rt.hal.displayHeight();
    if (W <= 0 || H <= 0 || d.w <= 0 || d.h <= 0) return;

    const int wordsPerRow = (W + 15) / 16;
    std::uint32_t *out = rt.hal.mutablePixels();

    const int x0 = std::max(0, d.x);
    const int y0 = std::max(0, d.y);
    const int x1 = std::min(W, d.x + d.w);
    const int y1 = std::min(H, d.y + d.h);

    const int firstWord = x0 / 16;
    const int lastWord  = (x1 + 15) / 16;

    for (int y = y0; y < y1; y++) {
        const int rowBase = y * wordsPerRow;
        const int pixelRow = y * W;
        for (int wx = firstWord; wx < lastWord; wx++) {
            const std::uint16_t word = static_cast<std::uint16_t>(
                rt.vm->fetchWord_ofDislayBits(rowBase + wx, displayBits));
            const int pxBase = wx * 16;
            for (int b = 0; b < 16; b++) {
                const int px = pxBase + b;
                if (px < x0 || px >= x1) continue;
                const bool on = (word >> (15 - b)) & 1;
                out[pixelRow + px] = on ? PIXEL_BLACK : PIXEL_WHITE;
            }
        }
    }
}

}  // namespace

// ============================================================================
// Lifecycle
// ============================================================================

extern "C" bool st80_init(const char *imagePath) {
    if (g_runtime) return false;
    if (!imagePath) return false;

    auto rt = std::make_unique<Runtime>();

    std::string full(imagePath);
    std::string dir = ".";
    std::string name = full;
    const auto slash = full.find_last_of("\\/");
    if (slash != std::string::npos) {
        dir = full.substr(0, slash);
        name = full.substr(slash + 1);
    }

    rt->fs = std::make_unique<st80::HostFileSystem>(dir);
    rt->hal.set_image_name(name.c_str());
    rt->vm = std::make_unique<st80::Interpreter>(&rt->hal, rt->fs.get());

    if (!rt->vm->init()) return false;

    g_runtime = rt.release();
    return true;
}

extern "C" int st80_run(int maxCycles) {
    if (!g_runtime) return 0;
    Runtime &rt = *g_runtime;

    rt.stopRequested.store(false);
    int n = 0;
    while (!rt.stopRequested.load() &&
           !rt.hal.quitRequested() &&
           (maxCycles <= 0 || n < maxCycles)) {
        int dueSem = 0;
        if (rt.hal.scheduledSemaphoreDue(&dueSem) && dueSem > 0) {
            rt.vm->asynchronousSignal(dueSem);
        }
        rt.vm->cycle();
        ++n;
    }
    return n;
}

extern "C" void st80_stop(void) {
    if (g_runtime) g_runtime->stopRequested.store(true);
}

extern "C" void st80_shutdown(void) {
    delete g_runtime;
    g_runtime = nullptr;
}

extern "C" int st80_quit_requested(void) {
    return g_runtime && g_runtime->hal.quitRequested() ? 1 : 0;
}

// ============================================================================
// Display
// ============================================================================

extern "C" int st80_display_width(void) {
    return g_runtime ? g_runtime->hal.displayWidth() : 0;
}

extern "C" int st80_display_height(void) {
    return g_runtime ? g_runtime->hal.displayHeight() : 0;
}

extern "C" const uint32_t *st80_display_pixels(void) {
    return g_runtime ? g_runtime->hal.displayPixels() : nullptr;
}

extern "C" void st80_cursor_image(uint16_t out[16]) {
    if (!g_runtime || !out) {
        for (int i = 0; i < 16; ++i) if (out) out[i] = 0;
        return;
    }
    g_runtime->hal.copyCursorImage(out);
}

extern "C" St80Rect st80_display_sync(void) {
    St80Rect out{0, 0, 0, 0};
    if (!g_runtime) return out;
    Runtime &rt = *g_runtime;

    const auto dirty = rt.hal.takeDirtyRect();
    if (dirty.w == 0 || dirty.h == 0) return out;

    const int W = rt.hal.displayWidth();
    const int H = rt.hal.displayHeight();
    const int displayBits = rt.vm->getDisplayBits(W, H);
    if (displayBits == 0) {
        out.x = dirty.x; out.y = dirty.y;
        out.w = dirty.w; out.h = dirty.h;
        return out;
    }

    expandDirtyRectToRGBA(rt, dirty, displayBits);

    out.x = std::max(0, dirty.x);
    out.y = std::max(0, dirty.y);
    out.w = std::min(W, dirty.x + dirty.w) - out.x;
    out.h = std::min(H, dirty.y + dirty.h) - out.y;
    return out;
}

// ============================================================================
// Input
// ============================================================================

extern "C" void st80_post_mouse_move(int x, int y) {
    if (!g_runtime) return;
    Runtime &rt = *g_runtime;
    rt.hal.setShadowCursor(x, y);
    emitTimestamp(rt);
    pushWord(rt, encodeWord(EV_X_COORD, static_cast<std::uint16_t>(x & 0xFFF)));
    pushWord(rt, encodeWord(EV_Y_COORD, static_cast<std::uint16_t>(y & 0xFFF)));
}

extern "C" void st80_post_mouse_down(int x, int y, St80MouseButton b) {
    if (!g_runtime) return;
    Runtime &rt = *g_runtime;
    rt.hal.setShadowCursor(x, y);
    emitTimestamp(rt);
    pushWord(rt, encodeWord(EV_X_COORD, static_cast<std::uint16_t>(x & 0xFFF)));
    pushWord(rt, encodeWord(EV_Y_COORD, static_cast<std::uint16_t>(y & 0xFFF)));
    pushWord(rt, encodeWord(EV_BI_DOWN,
                            static_cast<std::uint16_t>(smalltalkButton(b))));
}

extern "C" void st80_post_mouse_up(int x, int y, St80MouseButton b) {
    if (!g_runtime) return;
    Runtime &rt = *g_runtime;
    rt.hal.setShadowCursor(x, y);
    emitTimestamp(rt);
    pushWord(rt, encodeWord(EV_X_COORD, static_cast<std::uint16_t>(x & 0xFFF)));
    pushWord(rt, encodeWord(EV_Y_COORD, static_cast<std::uint16_t>(y & 0xFFF)));
    pushWord(rt, encodeWord(EV_BI_UP,
                            static_cast<std::uint16_t>(smalltalkButton(b))));
}

extern "C" void st80_post_key_down(int charCode, uint32_t /*modifiers*/) {
    if (!g_runtime) return;
    if (charCode < 0 || charCode > 0xFFF) return;
    Runtime &rt = *g_runtime;
    emitTimestamp(rt);
    pushWord(rt, encodeWord(EV_BI_DOWN, static_cast<std::uint16_t>(charCode)));
    pushWord(rt, encodeWord(EV_BI_UP,   static_cast<std::uint16_t>(charCode)));
}

extern "C" void st80_post_key_up(int /*charCode*/, uint32_t /*modifiers*/) {
    // Decoded keyboard — no separate up event. Symmetry with the
    // other four frontends.
}

extern "C" const char *st80_clipboard_read(void) {
    static std::string buf;
    buf.clear();
    if (!g_runtime || !g_runtime->vm) return "";
    buf = g_runtime->vm->getClipboardText();
    return buf.c_str();
}
