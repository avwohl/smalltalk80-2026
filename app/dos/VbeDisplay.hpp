// st80-2026 — app/dos/VbeDisplay.hpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// VESA BIOS Extension (VBE) 2.0+ linear-framebuffer display for the
// MS-DOS / FreeDOS frontend. All real-mode BIOS access goes through
// DJGPP's DPMI bindings (`__dpmi_int`, `__dpmi_physical_address_mapping`,
// the `__tb` transfer buffer) — the exact pattern dosiz's own DJGPP
// regression fixtures use (see C:\temp\src\dosiz\tests\djgpp\dj_ems.c).
//
// This is the only place INT 10h / INT 31h code lives. The portable
// core (src/core, src/platform/dos) never sees it: VbeDisplay only
// consumes the Bridge.h RGBA8 buffer (`st80_display_pixels`) and the
// dirty rectangle (`st80_display_sync`) and pushes pixels into the
// VESA framebuffer. No `#ifdef` anywhere — CMake's `if(DJGPP)` gate
// is what selects app/dos.

#pragma once

#include <cstdint>

namespace st80dos {

// One probed + selected VESA mode plus its mapped framebuffer.
struct VbeModeInfo {
    int          modeNumber   = 0;
    int          width        = 0;     // pixels
    int          height       = 0;     // pixels
    int          bpp          = 0;     // 8/15/16/24/32
    unsigned     bytesPerLine = 0;     // framebuffer pitch
    unsigned long physBase    = 0;     // LFB physical address (VBE)
    unsigned long linearBase  = 0;     // after __dpmi_physical_address_mapping
    int          lfbSelector  = 0;     // LDT descriptor over the LFB
    // Direct-colour field layout (read from the mode info block; used
    // only for the 15/16-bpp packers — 24/32 are byte B,G,R[,X]).
    int redPos = 0, redSize = 0;
    int grnPos = 0, grnSize = 0;
    int bluPos = 0, bluSize = 0;
};

class VbeDisplay {
 public:
    VbeDisplay() = default;
    ~VbeDisplay();

    VbeDisplay(const VbeDisplay &) = delete;
    VbeDisplay &operator=(const VbeDisplay &) = delete;

    // Probe VBE, pick the best LFB mode that holds a `vmW`x`vmH`
    // Smalltalk display (preferring 32bpp, then 24/16/8; preferring
    // the smallest enclosing resolution, else the largest available
    // with a centred + clipped viewport), set the mode, and map the
    // linear framebuffer through DPMI. Returns false with `errorText()`
    // populated on any failure — the caller prints it in text mode and
    // quits (Blue Book display does not fit EGA/CGA; no silent
    // fallback per docs/dos-plan.md).
    bool begin(int vmW, int vmH);

    // Run only the VBE probe + mode selection (4F00h/4F01h) without
    // setting a graphics mode or mapping the LFB. Stays in text mode
    // so the caller can print the result — the Risk #2 verification
    // tool from docs/dos-plan.md, mirroring dosiz's DPMI_PROBE.COM.
    bool probe(int vmW, int vmH, VbeModeInfo &out);

    // Restore the BIOS text mode (INT 10h AX=0003h) and free the LFB
    // descriptor. Safe to call more than once.
    void end();

    // Blit the rectangle [x,y,w,h) of the Bridge RGBA8 buffer
    // (`src`, `srcStride` pixels per row) into the framebuffer,
    // centred/clipped to the chosen mode.
    void blit(const std::uint32_t *src, int srcStride,
              int x, int y, int w, int h);

    // Composite the 16x16 1-bit Smalltalk cursor `bits` at VM
    // coordinate (cx,cy). Erases the previous cursor box (re-blitting
    // the underlying RGBA pixels) so the pointer leaves no trail.
    void drawCursor(const std::uint32_t *src, int srcStride,
                    const std::uint16_t bits[16], int cx, int cy);

    bool active() const { return active_; }
    const VbeModeInfo &mode() const { return mode_; }
    bool clipped() const { return clipped_; }
    const char *errorText() const { return error_; }

 private:
    // 4F00h + mode-list scan; fills mode_/clipped_/error_. Shared by
    // begin() (which then sets the mode + maps the LFB) and probe().
    bool selectMode(int vmW, int vmH);

    void encodeSpan(const std::uint32_t *srcRow, int n,
                    unsigned char *out) const;       // → bpp bytes
    void putFbRow(int fbX, int fbY, const unsigned char *bytes, int n);
    void blitNoCursor(const std::uint32_t *src, int srcStride,
                      int x, int y, int w, int h);

    bool          active_  = false;
    bool          clipped_ = false;
    int           vmW_ = 0, vmH_ = 0;     // Smalltalk display size
    int           offX_ = 0, offY_ = 0;   // centring offset into the mode
    int           savedTextMode_ = 0x03;
    VbeModeInfo   mode_;
    char          error_[160] = {0};

    // Previous cursor footprint (VM coords), so we can erase it.
    int  prevCurX_ = -1, prevCurY_ = -1;
    bool haveCursor_ = false;
};

}  // namespace st80dos
