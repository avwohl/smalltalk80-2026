// st80-2026 — st80_gui_test
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Headless "fake GUI" test runner — the st80 analog of
// avwohl/pharo-headless-test (referenced in README). pharo-headless-
// test installs an in-memory Display Form, injects synthetic events at
// the Morphic layer, screenshots the Form, and runs SUnit with no real
// display. The VM-agnostic core of that technique is: drive the image
// through synthetic input + read the rendered framebuffer back, with no
// window system.
//
// st80 already exposes exactly that seam — the pure-C `Bridge.h` API:
// st80_post_mouse_*/st80_post_key_* feed the Sensor, st80_display_*
// hand back the rendered RGBA framebuffer. The platform HALs keep that
// framebuffer purely in memory (no HWND/X11/Metal needed), so this
// harness links one of the real platform bridges and runs completely
// headless — on the host (st80_windows / st80_linux / st80_apple) for
// fast CI, and cross-compiled under DJGPP (st80_dos) to verify the
// FreeDOS port end-to-end inside dosiz.
//
// It boots the Xerox v2 image, drives a synthetic interaction against
// the live Smalltalk-80 environment, writes PNG screenshots at each
// step (self-contained encoder, no libpng/zlib), and asserts the image
// visibly reacts — a real display+input round-trip, not a bytecode
// trace. Exit 0 = PASS, 1 = FAIL, 77 = SKIP (image absent), 2 = error.
//
// Usage:
//   st80_gui_test <image> [out-dir]      (out-dir default ".")

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "Bridge.h"

namespace {

bool fileExists(const char *p) {
    struct stat s;
    return ::stat(p, &s) == 0;
}

// ---- minimal PNG writer (8-bit RGB, zlib "stored" blocks) ----------
// No compression: the Smalltalk display is 1-bit (huge run-length
// redundancy) so a stored stream is large but trivially correct and
// dependency-free. Good enough for a verification artifact.

uint32_t crc32_of(const uint8_t *p, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t tbl[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            tbl[i] = c;
        }
        init = true;
    }
    for (size_t i = 0; i < n; ++i)
        crc = tbl[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

void put32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));  v.push_back(uint8_t(x));
}

void chunk(std::vector<uint8_t> &out, const char tag[4],
           const std::vector<uint8_t> &data) {
    put32(out, uint32_t(data.size()));
    const size_t crcStart = out.size();
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uint32_t crc =
        crc32_of(out.data() + crcStart, out.size() - crcStart) ^ 0xFFFFFFFFu;
    put32(out, crc);
}

bool writePNG(const std::string &path, const uint32_t *rgba, int w, int h) {
    // Raw image: per scanline a filter byte (0) then w*3 RGB bytes.
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (1 + size_t(w) * 3));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        const uint32_t *row = rgba + size_t(y) * w;
        for (int x = 0; x < w; ++x) {
            const uint32_t px = row[x];           // RGBA8 in memory order
            raw.push_back(uint8_t(px));            // R
            raw.push_back(uint8_t(px >> 8));       // G
            raw.push_back(uint8_t(px >> 16));      // B
        }
    }
    // zlib stream: CMF/FLG then stored deflate blocks then Adler-32.
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t off = 0;
    while (off < raw.size()) {
        size_t n = raw.size() - off;
        if (n > 65535) n = 65535;
        z.push_back(off + n >= raw.size() ? 1 : 0);   // BFINAL
        z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n)); z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
    }
    uint32_t a1 = 1, a2 = 0;
    for (uint8_t b : raw) { a1 = (a1 + b) % 65521; a2 = (a2 + a1) % 65521; }
    put32(z, (a2 << 16) | a1);

    std::vector<uint8_t> png;
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), sig, sig + 8);
    std::vector<uint8_t> ihdr;
    put32(ihdr, uint32_t(w)); put32(ihdr, uint32_t(h));
    ihdr.push_back(8); ihdr.push_back(2);            // 8-bit, RGB
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});

    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

// ---- harness helpers ----------------------------------------------

int gW = 0, gH = 0;

void runCycles(int n) {                 // run in small batches
    for (int done = 0; done < n; done += 4000)
        st80_run(4000 < n - done ? 4000 : n - done);
}

const uint32_t *syncPixels() {
    st80_display_sync();
    return st80_display_pixels();
}

bool snap(const std::string &dir, const char *name) {
    const uint32_t *px = syncPixels();
    if (!px || gW <= 0 || gH <= 0) return false;
    const std::string p = dir + "/" + name;
    const bool ok = writePNG(p, px, gW, gH);
    std::fprintf(stderr, "st80_gui_test: wrote %s (%dx%d) %s\n",
                 p.c_str(), gW, gH, ok ? "ok" : "FAILED");
    return ok;
}

long pixelDelta(const std::vector<uint32_t> &a, const uint32_t *b) {
    long d = 0;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++d;
    return d;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: st80_gui_test <image> [out-dir]\n");
        return 2;
    }
    const char *image = argv[1];
    const std::string outDir = argc > 2 ? argv[2] : ".";

    if (!fileExists(image)) {
        std::printf("st80_gui_test: SKIP (%s not present)\n", image);
        return 77;
    }
    if (!st80_init(image)) {
        std::fprintf(stderr, "st80_gui_test: st80_init failed\n");
        return 2;
    }

    // Boot: run until primitiveBeDisplay has configured the screen,
    // then a little longer so the desktop settles.
    for (int tries = 0; tries < 30 && st80_display_width() <= 0; ++tries)
        runCycles(4000);
    gW = st80_display_width();
    gH = st80_display_height();
    if (gW <= 0 || gH <= 0) {
        std::fprintf(stderr, "st80_gui_test: display never configured\n");
        st80_shutdown();
        return 1;
    }
    runCycles(60000);                            // settle the desktop
    if (!snap(outDir, "01-boot.png")) {
        st80_shutdown();
        return 1;
    }

    const uint32_t *px = syncPixels();
    std::vector<uint32_t> baseline(px, px + size_t(gW) * gH);

    // Fake-GUI interaction: park the cursor mid-desktop and press the
    // yellow (operate) button — in the Blue Book MVC desktop this pops
    // the screen menu. Hold it long enough for the image to open the
    // menu, screenshot, release, screenshot, and also exercise the
    // keyboard path. A live VM reacts in many pixels; a dead input
    // path changes nothing.
    const int cx = gW / 2, cy = gH / 2;
    st80_post_mouse_move(cx, cy);
    runCycles(4000);
    st80_post_mouse_down(cx, cy, ST80_BTN_YELLOW);
    runCycles(8000);
    snap(outDir, "02-yellow-down.png");
    const uint32_t *pmenu = syncPixels();
    const long menuDelta = pixelDelta(baseline, pmenu);

    st80_post_mouse_up(cx, cy, ST80_BTN_YELLOW);
    runCycles(4000);
    st80_post_key_down('a', 0);
    st80_post_key_up('a', 0);
    runCycles(4000);
    snap(outDir, "03-after.png");

    st80_shutdown();

    std::printf("st80_gui_test: display=%dx%d  yellow-menu pixel delta=%ld\n",
                gW, gH, menuDelta);
    // A window-level menu paints far more than 100 px; 100 is a low
    // floor that only a non-reacting (dead) input/display path misses.
    if (menuDelta < 100) {
        std::fprintf(stderr,
            "st80_gui_test: FAIL — image did not react to synthetic "
            "yellow-click (delta=%ld, expected >=100)\n", menuDelta);
        return 1;
    }
    std::printf("st80_gui_test: PASS\n");
    return 0;
}
