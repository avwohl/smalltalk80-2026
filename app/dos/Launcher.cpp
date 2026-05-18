// st80-2026 — app/dos/Launcher.cpp
// Copyright (c) 2026 Aaron Wohl. MIT License.

#include "Launcher.hpp"

#include <cstdio>

#include <bios.h>

namespace st80dos {

void showBanner(const char *imagePath, int cyclesPerFrame) {
    std::printf(
        "Smalltalk-80 (st80-2026) - Xerox Blue Book v2 image\n"
        "  image  : %s\n"
        "  cycles : %d per frame\n"
        "Probing VESA / mouse ...\n",
        imagePath ? imagePath : "(none)", cyclesPerFrame);
    std::fflush(stdout);
}

void showError(const char *message) {
    std::fprintf(stderr, "\nst80: %s\n", message ? message : "(error)");
    std::fprintf(stderr, "Press any key to exit.\n");
    std::fflush(stderr);
    // Wait up to a few seconds for a key, then give up so an
    // unattended run (CI / dosiz headless) still terminates.
    for (long i = 0; i < 30000000L; ++i) {
        if (_bios_keybrd(_NKEYBRD_READY)) {
            _bios_keybrd(_NKEYBRD_READ);
            break;
        }
    }
}

void showProbeHeader() {
    std::printf("st80 VBE/DPMI probe (no mode change)\n");
    std::fflush(stdout);
}

}  // namespace st80dos
