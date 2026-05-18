// st80-2026 — app/dos/KbdInt16.hpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Polled enhanced keyboard via the BIOS (INT 16h, through DJGPP's
// _bios_keybrd). No ISR: the cooperative frame loop drains whatever
// is in the BIOS buffer each frame, exactly like the Win32 frontend
// drains WM_CHAR. We keep the same decoded-keyboard contract as the
// other four frontends — 7-bit ASCII passes through; forward Delete
// maps to 127 — so the Bridge.h input path stays in lockstep across
// every platform (see app/windows/st80_windows_main.cpp WM_CHAR).

#pragma once

#include <cstdint>

namespace st80dos {

class KbdInt16 {
 public:
    // Drain the BIOS buffer (bounded so a key-repeat storm can't
    // starve the VM). Call once per frame before next().
    void pump();

    // Pop one decoded keystroke. `charCode` is a Blue Book key code
    // (ASCII for printables/BS/TAB/CR/ESC, 127 for forward Delete);
    // `mods` is an ST80_MOD_* bitmask from the BIOS shift state.
    bool next(int &charCode, std::uint32_t &mods);

    // Live shift-key state (INT 16h AH=12h) for the
    // Shift-click = blue button mapping in the main loop.
    bool shiftDown();

 private:
    static constexpr int kCap = 64;
    int  codes_[kCap];
    std::uint32_t mods_[kCap];
    int  head_ = 0, tail_ = 0;

    std::uint32_t modifiersNow();
};

}  // namespace st80dos
