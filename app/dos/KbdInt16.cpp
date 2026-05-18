// st80-2026 — app/dos/KbdInt16.cpp
// Copyright (c) 2026 Aaron Wohl. MIT License.

#include "KbdInt16.hpp"

#include <bios.h>

#include "Bridge.h"   // ST80_MOD_* — app layer may see the C API

namespace st80dos {

std::uint32_t KbdInt16::modifiersNow() {
    const int s = _bios_keybrd(_NKEYBRD_SHIFTSTATUS);
    std::uint32_t m = 0;
    if (s & 0x03) m |= ST80_MOD_SHIFT;   // bit0 right shift, bit1 left
    if (s & 0x04) m |= ST80_MOD_CTRL;    // bit2 ctrl
    if (s & 0x08) m |= ST80_MOD_OPTION;  // bit3 alt → ST-80 "option"
    return m;
}

bool KbdInt16::shiftDown() {
    return (_bios_keybrd(_NKEYBRD_SHIFTSTATUS) & 0x03) != 0;
}

void KbdInt16::pump() {
    // Bound the drain so a stuck/repeating key can't starve the VM
    // for a frame (the BIOS buffer is only 15 keys deep anyway).
    for (int guard = 0; guard < 32; ++guard) {
        if (_bios_keybrd(_NKEYBRD_READY) == 0) break;
        const int k  = _bios_keybrd(_NKEYBRD_READ);
        const int al = k & 0xFF;
        const int ah = (k >> 8) & 0xFF;

        int code = -1;
        if (al != 0 && al != 0xE0 && al < 0x80) {
            // Printables plus BS(8), TAB(9), CR(13), ESC(27) and
            // Ctrl+letter (1..26) — same set the Win32 WM_CHAR path
            // forwards.
            code = al;
        } else if (al == 0 || al == 0xE0) {
            // Extended key: only forward Delete is mapped, matching
            // the other frontends (VK_DELETE → 127). Arrows / F-keys
            // are intentionally not mapped here so all five ports
            // behave identically.
            if (ah == 0x53) code = 127;
        }
        if (code < 0) continue;

        const int nt = (tail_ + 1) % kCap;
        if (nt == head_) break;          // ring full — drop the rest
        codes_[tail_] = code;
        mods_[tail_]  = modifiersNow();
        tail_         = nt;
    }
}

bool KbdInt16::next(int &charCode, std::uint32_t &mods) {
    if (head_ == tail_) return false;
    charCode = codes_[head_];
    mods     = mods_[head_];
    head_    = (head_ + 1) % kCap;
    return true;
}

}  // namespace st80dos
