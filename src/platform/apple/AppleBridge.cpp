// st80-2026 — AppleBridge.cpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Implements Bridge.h on top of an AppleHal + Interpreter +
// PosixFileSystem. A single global runtime is instantiated by
// st80_init and torn down by st80_shutdown; all other entry points
// route through it.
//
// Threading (Phase 2a): the Swift frontend is expected to call
// st80_post_* and st80_run on the same thread (e.g. the Metal
// display-link callback, where you do N cycles then yield). That
// keeps Interpreter::asynchronousSignal safe without adding locks
// around the Interpreter's semaphoreList. A multi-threaded layout
// is possible but needs a thread-safe signal path and is left for
// later.

#include "Bridge.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "AppleHal.hpp"
#include "Interpreter.hpp"
#include "ObjectMemory.hpp"
#include "Oops.hpp"
#include "PosixFileSystem.hpp"

namespace {

struct Runtime {
    st80::AppleHal hal;
    std::unique_ptr<st80::PosixFileSystem> fs;
    std::unique_ptr<st80::Interpreter> vm;
    std::atomic<bool> stopRequested{false};

    // Event-stream timestamp tracking. A type-0 "delta time" word
    // precedes each posted event in BB §29.12; after >4095 ms we
    // emit an absolute timestamp instead.
    std::uint32_t lastEventMs = 0;
    bool firstEvent = true;
};

Runtime *g_runtime = nullptr;

// ----------------------------------------------------------------------------
// Input word encoding (BB §29.12 / dbanay main.cpp:532-580).
// ----------------------------------------------------------------------------

constexpr std::uint16_t EV_DELTA_TIME = 0;
constexpr std::uint16_t EV_X_COORD    = 1;
constexpr std::uint16_t EV_Y_COORD    = 2;
constexpr std::uint16_t EV_BI_DOWN    = 3;
constexpr std::uint16_t EV_BI_UP      = 4;
constexpr std::uint16_t EV_ABS_TIME   = 5;

// Mouse-button Smalltalk codes (dbanay main.cpp:693-696).
// "The bluebook got these wrong!" — dbanay. We follow his values.
constexpr int BUTTON_RED    = 130;  // primary (select)
constexpr int BUTTON_YELLOW = 129;  // middle  (do-it, print-it)
constexpr int BUTTON_BLUE   = 128;  // tertiary (frame, close)

std::uint16_t encodeWord(std::uint16_t type, std::uint16_t parameter) {
    return static_cast<std::uint16_t>(
        ((type & 0xF) << 12) | (parameter & 0xFFF));
}

// dbanay main.cpp:532 signals the input semaphore after every queued
// word, not per logical event. Each signal wakes the image's input
// process just once; a multi-word event would strand the trailing
// words in the queue if we only signal once. Match that pattern.
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

// Emit a type-0 delta or type-5 absolute-time preamble so the image
// can order events. Call once before each logical event.
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

// ----------------------------------------------------------------------------
// 1-bit DisplayBitmap → RGBA8 expansion (BB §20.5 display Form layout).
// ----------------------------------------------------------------------------
//
// Each scanline is `(width + 15) / 16` 16-bit words. Bit 0 of a word
// is the LEFTMOST pixel of that 16-pixel group (RealWordMemory
// convention: bit 0 == MSB). A set bit = black ink.

constexpr std::uint32_t PIXEL_BLACK = 0xFF000000u;
constexpr std::uint32_t PIXEL_WHITE = 0xFFFFFFFFu;

void expandDirtyRectToRGBA(Runtime &rt,
                           const st80::AppleHal::DirtyRect &d,
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
    const auto slash = full.find_last_of('/');
    if (slash != std::string::npos) {
        dir = full.substr(0, slash);
        name = full.substr(slash + 1);
    }

    rt->fs = std::make_unique<st80::PosixFileSystem>(dir);
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
        // Fire any due `signal_at` scheduled semaphore before the cycle.
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
        // No display form yet (or the VM said it changed shape).
        // Report the dirty rect anyway so the caller triggers a redraw
        // when the form arrives.
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
    // "Decoded keyboard" per dbanay comment at main.cpp:593: emit a
    // type-3/type-4 pair for each ASCII keystroke. The image's input
    // sensor treats the pair as a single down-up event.
    pushWord(rt, encodeWord(EV_BI_DOWN, static_cast<std::uint16_t>(charCode)));
    pushWord(rt, encodeWord(EV_BI_UP,   static_cast<std::uint16_t>(charCode)));
}

extern "C" void st80_post_key_up(int /*charCode*/, uint32_t /*modifiers*/) {
    // Decoded keyboard has no separate up event — the down-up pair is
    // emitted by post_key_down. This hook exists for future undecoded
    // / meta-key handling (shift, ctrl, alpha-lock — BB codes 136-139).
}
