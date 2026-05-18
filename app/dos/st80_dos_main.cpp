// st80-2026 — app/dos/st80_dos_main.cpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// MS-DOS / FreeDOS frontend entry point. A DJGPP COFF binary with a
// go32-v2 stub: the stub switches into 32-bit protected mode via a
// DPMI 0.9+ host (dosiz primary; CWSDPMI / HDPMI32 / a Win9x DOS box
// on real hardware secondary), then main() runs ring-3 flat. Every
// Bridge.h call is on this one thread, so the Phase-2a same-thread
// contract is trivially satisfied.
//
// This mirrors what app/windows/st80_windows_main.cpp does on Win32
// and the SDL frontend does on Linux: prime the VM, open a display
// at its size, then a cooperative loop — run a batch of cycles,
// flush the dirty rect to the framebuffer, composite the cursor,
// pump mouse + keyboard back through Bridge.h. No window manager, no
// threads, no event ISRs: DOS is the layering stress test for the
// IHal / Bridge.h seam (docs/dos-plan.md).
//
// Usage:
//   st80.exe [options] [image]
//
//   image                 Xerox v2 image (default: snapshot.im)
//   --cycles-per-frame N  VM cycles between frames (default 4000;
//                         same as the other four frontends)
//   --scale NUM/DEN       mouse pixels-per-mickey (default 1/1)
//   --probe               print the VBE/mouse probe result and exit
//                         (stays in text mode — Risk #2 tool)
//   --no-display          headless: run cycles, print quit state,
//                         exit (CI / trace2 parity with --no-window)
//   --help

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Bridge.h"

#include "KbdInt16.hpp"
#include "Launcher.hpp"
#include "MouseInt33.hpp"
#include "VbeDisplay.hpp"

namespace {

struct Args {
    const char *imagePath    = "snapshot.im";
    int  cyclesPerFrame      = 4000;
    int  scaleNum            = 1;
    int  scaleDen            = 1;
    bool probe               = false;
    bool noDisplay           = false;
};

void usage() {
    std::printf(
        "usage: st80.exe [options] [image]\n"
        "  image                 Xerox v2 image (default snapshot.im)\n"
        "  --cycles-per-frame N  VM cycles between frames (default 4000)\n"
        "  --scale NUM/DEN       mouse pixels per mickey (default 1/1)\n"
        "  --probe               print VBE/mouse probe, stay in text\n"
        "  --no-display          headless run (CI / trace parity)\n"
        "  --help\n");
}

int parseArgs(int argc, char **argv, Args &out) {
    bool gotImage = false;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (std::strcmp(a, "--cycles-per-frame") == 0 && i + 1 < argc) {
            out.cyclesPerFrame = std::atoi(argv[++i]);
            if (out.cyclesPerFrame < 1) out.cyclesPerFrame = 1;
        } else if (std::strcmp(a, "--scale") == 0 && i + 1 < argc) {
            int n = 1, d = 1;
            if (std::sscanf(argv[++i], "%d/%d", &n, &d) >= 1) {
                out.scaleNum = n > 0 ? n : 1;
                out.scaleDen = d > 0 ? d : 1;
            }
        } else if (std::strcmp(a, "--probe") == 0) {
            out.probe = true;
        } else if (std::strcmp(a, "--no-display") == 0) {
            out.noDisplay = true;
        } else if (std::strcmp(a, "--help") == 0 ||
                   std::strcmp(a, "-h") == 0) {
            usage();
            return 1;
        } else if (a[0] == '-' && a[1] != '\0') {
            usage();
            return 64;
        } else {
            out.imagePath = a;
            gotImage = true;
        }
    }
    (void)gotImage;
    return 0;
}

// Prime the interpreter so primitiveBeDisplay publishes the display
// size before we touch VESA. Matches the Win32/SDL frontends.
bool primeAndSize(const Args &args, int &W, int &H) {
    if (!st80_init(args.imagePath)) {
        st80dos::showError("failed to load the Smalltalk-80 image "
                           "(check the path / that it is a v2 image).");
        return false;
    }
    st80_run(4000);
    W = st80_display_width();
    H = st80_display_height();
    if (W <= 0 || H <= 0) {
        st80dos::showError("the image never set a display size "
                           "(primitiveBeDisplay was not reached).");
        st80_shutdown();
        return false;
    }
    return true;
}

int runProbe(const Args &args) {
    int W = 0, H = 0;
    if (!primeAndSize(args, W, H)) return 2;

    st80dos::showProbeHeader();
    std::printf("VM display: %dx%d (1-bit)\n", W, H);

    st80dos::VbeDisplay vbe;
    st80dos::VbeModeInfo m;
    if (!vbe.probe(W, H, m)) {
        st80dos::showError(vbe.errorText());
        st80_shutdown();
        return 4;
    }
    std::printf(
        "chosen VBE mode: 0x%04X  %dx%dx%d  pitch=%u\n"
        "  PhysBasePtr=0x%08lX  %s\n",
        m.modeNumber, m.width, m.height, m.bpp, m.bytesPerLine,
        m.physBase, vbe.clipped() ? "(clipped viewport)"
                                  : "(fully encloses display)");

    st80dos::MouseInt33 mouse;
    const bool haveMouse = mouse.begin(W, H);
    std::printf("INT 33h mouse: %s",
                haveMouse ? "present" : "NOT FOUND");
    if (haveMouse) std::printf(" (%d buttons)", mouse.buttonCount());
    std::printf("\nprobe ok\n");
    std::fflush(stdout);

    st80_shutdown();
    return 0;
}

int runHeadless(const Args &args) {
    if (!st80_init(args.imagePath)) {
        std::fprintf(stderr, "st80: st80_init FAILED\n");
        return 2;
    }
    const int total = args.cyclesPerFrame * 10;
    const int ran = st80_run(total);
    std::fprintf(stderr, "st80: ran %d cycles, quit=%d\n",
                 ran, st80_quit_requested());
    st80_shutdown();
    return 0;
}

// Map a physical mouse button (bit0 left, bit1 right, bit2 middle)
// to a Smalltalk button. docs/dos-plan.md: plain = red, right =
// yellow, Shift+left = blue, middle = blue (3-button mice).
St80MouseButton mapButton(int physicalBit, bool shift) {
    if (physicalBit == 1) return ST80_BTN_YELLOW;   // right
    if (physicalBit == 2) return ST80_BTN_BLUE;     // middle
    return shift ? ST80_BTN_BLUE : ST80_BTN_RED;    // left
}

int runWindowed(const Args &args) {
    int W = 0, H = 0;
    if (!primeAndSize(args, W, H)) return 2;

    st80dos::MouseInt33 mouse;
    if (!mouse.begin(W, H)) {
        st80dos::showError("no mouse driver (INT 33h). Smalltalk-80 "
                           "is unusable without a mouse — load one "
                           "(e.g. CTMOUSE) and retry.");
        st80_shutdown();
        return 3;
    }
    mouse.setScale(args.scaleNum, args.scaleDen);

    st80dos::VbeDisplay vbe;
    if (!vbe.begin(W, H)) {
        st80dos::showError(vbe.errorText());
        st80_shutdown();
        return 4;
    }

    st80dos::KbdInt16 kbd;

    // Initial full paint: flush the VM's display then blit it all.
    st80_display_sync();
    const std::uint32_t *px = st80_display_pixels();
    if (px) vbe.blit(px, W, 0, 0, W, H);

    int curX = W / 2, curY = H / 2;
    St80MouseButton heldMap[3] = {ST80_BTN_RED, ST80_BTN_RED,
                                  ST80_BTN_RED};

    while (!st80_quit_requested()) {
        // --- keyboard ---------------------------------------------------
        kbd.pump();
        int code; std::uint32_t mods;
        while (kbd.next(code, mods)) st80_post_key_down(code, mods);

        // --- mouse ------------------------------------------------------
        st80dos::MousePoll mp = mouse.poll();
        if (mp.moved) {
            curX = mp.x; curY = mp.y;
            st80_post_mouse_move(curX, curY);
        }
        const bool shift = kbd.shiftDown();
        for (int bit = 0; bit < 3; ++bit) {
            const int mask = 1 << bit;
            if (mp.pressed & mask) {
                heldMap[bit] = mapButton(bit, shift);
                st80_post_mouse_down(curX, curY, heldMap[bit]);
            }
            if (mp.released & mask) {
                // Release the button we pressed, even if Shift state
                // changed in between.
                st80_post_mouse_up(curX, curY, heldMap[bit]);
            }
        }

        // --- run the VM -------------------------------------------------
        st80_run(args.cyclesPerFrame);

        // --- VM-driven resize (rare for the v2 image) -------------------
        const int nw = st80_display_width();
        const int nh = st80_display_height();
        if (nw > 0 && nh > 0 && (nw != W || nh != H)) {
            vbe.end();
            W = nw; H = nh;
            if (!vbe.begin(W, H)) {
                st80dos::showError(vbe.errorText());
                st80_shutdown();
                return 4;
            }
            st80_display_sync();
            px = st80_display_pixels();
            if (px) vbe.blit(px, W, 0, 0, W, H);
        }

        // --- present ----------------------------------------------------
        const St80Rect d = st80_display_sync();
        px = st80_display_pixels();
        if (px && d.w > 0 && d.h > 0)
            vbe.blit(px, W, d.x, d.y, d.w, d.h);

        // Cursor form may have changed (scroll-bar / text regions);
        // composite it every frame so a region repainted under it
        // by the dirty-rect blit gets the pointer drawn back.
        if (px) {
            std::uint16_t bits[16];
            st80_cursor_image(bits);
            vbe.drawCursor(px, W, bits, curX, curY);
        }
    }

    vbe.end();
    st80_shutdown();
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    Args args;
    if (int rc = parseArgs(argc, argv, args); rc != 0)
        return rc == 1 ? 0 : rc;     // --help is a clean exit

    st80dos::showBanner(args.imagePath, args.cyclesPerFrame);

    if (args.probe)     return runProbe(args);
    if (args.noDisplay) return runHeadless(args);
    return runWindowed(args);
}
