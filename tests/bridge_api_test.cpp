// st80-2026 — bridge_api_test
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Smoke test against the Bridge.h C API on top of libst80_apple.
// Boots the Xerox v2 image, runs a batch of cycles, verifies display
// metadata and input-word queueing survive a round trip.
//
// Skips (exit 77) if the Xerox image isn't on disk.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <vector>

#include "Bridge.h"

static bool fileExists(const char *p) {
    struct stat s;
    return ::stat(p, &s) == 0;
}

int main() {
    const char *image = "reference/xerox-image/VirtualImage";
    if (!fileExists(image)) {
        std::printf("bridge_api_test: SKIP (%s not present)\n", image);
        return 77;  // CTest SKIP_RETURN_CODE
    }

    if (!st80_init(image)) {
        std::fprintf(stderr, "bridge_api_test: st80_init failed\n");
        return 1;
    }

    // Display should not be configured until the image runs and the
    // `primitiveBeDisplay` call lands. Empty sync on boot returns 0s.
    St80Rect r0 = st80_display_sync();
    if (r0.w != 0 || r0.h != 0) {
        std::fprintf(stderr, "bridge_api_test: expected empty dirty rect "
                             "before running, got %dx%d\n", r0.w, r0.h);
        return 2;
    }

    // Run 2000 cycles — enough to get past startup and into the main
    // scheduler loop. (trace2 covers 499 cycles; we go deeper to make
    // sure nothing asserts a bit later.)
    const int ran = st80_run(2000);
    if (ran != 2000) {
        std::fprintf(stderr, "bridge_api_test: expected 2000 cycles, ran %d\n", ran);
        return 3;
    }

    // Exercise the event API — boundary checks, no crash, queue accepts.
    st80_post_mouse_move(100, 200);
    st80_post_mouse_down(100, 200, ST80_BTN_RED);
    st80_post_mouse_up(100, 200, ST80_BTN_RED);
    st80_post_key_down('a', 0);
    st80_post_key_up('a', 0);

    // A second run batch lets the image process the queued events.
    const int ran2 = st80_run(1000);
    if (ran2 != 1000) {
        std::fprintf(stderr, "bridge_api_test: second batch expected 1000, ran %d\n", ran2);
        return 4;
    }

    // Display metadata is available after `primitiveBeDisplay` fires.
    const int w = st80_display_width();
    const int h = st80_display_height();
    std::printf("bridge_api_test: display=%dx%d\n", w, h);
    if (w <= 0 || h <= 0) {
        std::fprintf(stderr, "bridge_api_test: display never configured\n");
        return 5;
    }

    // Drive the image past the snapshot point so the normal event
    // dispatch is running. 20 K cycles is plenty.
    st80_run(20000);
    st80_display_sync();

    // Capture a baseline snapshot of the display RGBA buffer.
    const std::uint32_t *pixelsPtr = st80_display_pixels();
    if (!pixelsPtr) {
        std::fprintf(stderr, "bridge_api_test: no display pixels\n");
        return 6;
    }
    std::vector<std::uint32_t> before(pixelsPtr, pixelsPtr + w * h);

    // Post a yellow (option-click) sequence in the middle of the
    // desktop. Hold the button for a few thousand cycles so the image
    // has time to open a menu. Then release.
    const int cx = w / 2;
    const int cy = h / 2;
    st80_post_mouse_move(cx, cy);
    st80_run(2000);
    st80_post_mouse_down(cx, cy, ST80_BTN_YELLOW);
    st80_run(6000);
    st80_post_mouse_up(cx, cy, ST80_BTN_YELLOW);
    st80_run(4000);
    st80_display_sync();

    // Diff the new pixels against the baseline. A visible reaction
    // (menu appearing, cursor moving) flips many pixels. Zero diff
    // means the event path isn't reaching the image.
    const std::uint32_t *after = st80_display_pixels();
    int diffs = 0;
    for (int i = 0; i < w * h; ++i) if (before[i] != after[i]) ++diffs;
    std::printf("bridge_api_test: pixel diffs after yellow-click = %d\n", diffs);

    st80_shutdown();
    // Hard-fail if the event path produced zero reaction. 100 is an
    // arbitrarily-low floor: any window-level chrome change will blow
    // past it.
    if (diffs < 100) {
        std::fprintf(stderr,
                     "bridge_api_test: expected image to react to yellow-click "
                     "(>=100 pixel diffs); got %d\n", diffs);
        return 7;
    }
    return 0;
}
