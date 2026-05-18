// st80-2026 — app/dos/VbeDisplay.cpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// VBE 2.0+ probe / mode-set / linear-framebuffer blit via DJGPP DPMI.
//
// Real-mode BIOS calls follow the dosiz DJGPP fixture convention
// (C:\temp\src\dosiz\tests\djgpp\dj_ems.c): zero a __dpmi_regs, point
// ES:DI (or DS:DX) at the shared `__tb` transfer buffer, __dpmi_int,
// then dosmemget the result back. The LFB is reached through a single
// LDT descriptor whose base is the linear address returned by
// INT 31h/0800h (__dpmi_physical_address_mapping); writes go through
// `movedata` so we never depend on the near-pointer hack (which some
// DPMI hosts disable). VESA reference: VBE Core Functions Std 3.0,
// functions 4F00h/4F01h/4F02h/4F09h.

#include "VbeDisplay.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/farptr.h>
#include <sys/movedata.h>

namespace st80dos {

namespace {

constexpr std::uint32_t PIXEL_BLACK = 0xFF000000u;

inline std::uint16_t rd16(const unsigned char *p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
inline std::uint32_t rd32(const unsigned char *p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

// Real-mode INT via DPMI with the registers zeroed first (dj_ems.c
// pattern). Returns the BIOS AX so the caller can check 0x004F.
std::uint16_t biosInt10(__dpmi_regs &r) {
    __dpmi_int(0x10, &r);
    return r.x.ax;
}

int bppRank(int bpp) {
    switch (bpp) {
        case 32: return 0;
        case 24: return 1;
        case 16: return 2;
        case 15: return 3;
        case 8:  return 4;
        default: return 9;
    }
}

// Program a 256-entry 6-bit grayscale DAC ramp so the 8-bpp fallback
// can map any RGBA pixel to its luma index. Standard VGA DAC ports;
// dosbox-staging (hence dosiz) and every real VGA expose these.
void setGrayscalePalette() {
    outportb(0x3C8, 0);
    for (int i = 0; i < 256; ++i) {
        const unsigned char c = static_cast<unsigned char>(i >> 2);  // 6-bit
        outportb(0x3C9, c);
        outportb(0x3C9, c);
        outportb(0x3C9, c);
    }
}

}  // namespace

VbeDisplay::~VbeDisplay() { end(); }

bool VbeDisplay::selectMode(int vmW, int vmH) {
    vmW_ = vmW;
    vmH_ = vmH;

    // The 512-byte VBE info block and 256-byte mode block both go
    // through the DJGPP DOS transfer buffer; make sure it is big
    // enough (default is >= 4 KiB, but don't assume).
    if (_go32_info_block.size_of_transfer_buffer < 512) {
        std::snprintf(error_, sizeof(error_),
                      "DJGPP transfer buffer is %lu bytes; need >=512 "
                      "for the VBE info block.",
                      _go32_info_block.size_of_transfer_buffer);
        return false;
    }

    // ---- 4F00h: VBE controller info ------------------------------------
    unsigned char vib[512];
    std::memset(vib, 0, sizeof(vib));
    std::memcpy(vib, "VBE2", 4);             // request VBE 2.0+ extensions
    dosmemput(vib, 512, __tb);

    __dpmi_regs r;
    std::memset(&r, 0, sizeof(r));
    r.x.ax = 0x4F00;
    r.x.es = static_cast<unsigned short>(__tb >> 4);
    r.x.di = static_cast<unsigned short>(__tb & 0x0F);
    if (biosInt10(r) != 0x004F) {
        std::snprintf(error_, sizeof(error_),
                      "VBE not present (INT 10h/4F00h returned %04X). "
                      "A VESA 2.0 BIOS or driver is required.", r.x.ax);
        return false;
    }
    dosmemget(__tb, 512, vib);
    if (std::memcmp(vib, "VESA", 4) != 0) {
        std::snprintf(error_, sizeof(error_),
                      "VBE info block signature missing (not 'VESA').");
        return false;
    }
    const int vbeVer = rd16(vib + 4);
    if (vbeVer < 0x0200) {
        std::snprintf(error_, sizeof(error_),
                      "VBE %d.%d found; 2.0+ required for a linear "
                      "framebuffer.", vbeVer >> 8, (vbeVer & 0xFF) >> 4);
        return false;
    }
    const std::uint32_t modePtr = rd32(vib + 0x0E);
    const unsigned long modeListLin =
        (static_cast<unsigned long>((modePtr >> 16) & 0xFFFF) << 4) +
        (modePtr & 0xFFFF);

    // ---- 4F01h: walk the mode list, score LFB modes --------------------
    VbeModeInfo best;
    bool found = false;
    bool bestEncloses = false;
    long bestScore = 0;

    for (int i = 0; i < 512; ++i) {
        const std::uint16_t m =
            _farpeekw(_dos_ds, modeListLin + 2u * i);
        if (m == 0xFFFF) break;

        std::memset(&r, 0, sizeof(r));
        r.x.ax = 0x4F01;
        r.x.cx = m;
        r.x.es = static_cast<unsigned short>(__tb >> 4);
        r.x.di = static_cast<unsigned short>(__tb & 0x0F);
        if (biosInt10(r) != 0x004F) continue;

        unsigned char mib[256];
        dosmemget(__tb, 256, mib);
        const std::uint16_t attr = rd16(mib + 0x00);
        // bit0 supported, bit4 graphics, bit7 linear framebuffer.
        if ((attr & 0x01) == 0) continue;
        if ((attr & 0x10) == 0) continue;
        if ((attr & 0x80) == 0) continue;       // refuse banked-only

        const int xres = rd16(mib + 0x12);
        const int yres = rd16(mib + 0x14);
        const int bpp  = mib[0x19];
        if (bppRank(bpp) == 9) continue;
        if (xres <= 0 || yres <= 0) continue;

        unsigned bpl = rd16(mib + 0x10);
        if (vbeVer >= 0x0300) {
            const unsigned linBpl = rd16(mib + 0x32);
            if (linBpl) bpl = linBpl;
        }
        const std::uint32_t phys = rd32(mib + 0x28);
        if (phys == 0) continue;

        const bool encloses = (xres >= vmW && yres >= vmH);
        // Prefer modes that fully hold the Smalltalk display; within
        // a class prefer higher colour depth then the tightest fit
        // (smallest extra area). Score is minimised.
        const long area = static_cast<long>(xres) * yres;
        const long score = static_cast<long>(bppRank(bpp)) * 100000000L
                           + area;

        bool take = false;
        if (!found) {
            take = true;
        } else if (encloses && !bestEncloses) {
            take = true;                         // enclosing always wins
        } else if (encloses == bestEncloses) {
            take = (score < bestScore);
        }
        if (!take) continue;

        best.modeNumber   = m;
        best.width        = xres;
        best.height       = yres;
        best.bpp          = bpp;
        best.bytesPerLine = bpl;
        best.physBase     = phys;
        best.redSize = mib[0x1F]; best.redPos = mib[0x20];
        best.grnSize = mib[0x21]; best.grnPos = mib[0x22];
        best.bluSize = mib[0x23]; best.bluPos = mib[0x24];
        found        = true;
        bestEncloses = encloses;
        bestScore    = score;
    }

    if (!found) {
        std::snprintf(error_, sizeof(error_),
                      "No usable VBE linear-framebuffer mode found "
                      "(need >=%dx%d, 8/15/16/24/32bpp, LFB).",
                      vmW, vmH);
        return false;
    }
    mode_    = best;
    clipped_ = !bestEncloses;
    return true;
}

bool VbeDisplay::probe(int vmW, int vmH, VbeModeInfo &out) {
    if (!selectMode(vmW, vmH)) return false;
    out = mode_;
    return true;
}

bool VbeDisplay::begin(int vmW, int vmH) {
    if (!selectMode(vmW, vmH)) return false;

    __dpmi_regs r;

    // ---- 4F02h: set the mode with the LFB bit (bit 14) -----------------
    std::memset(&r, 0, sizeof(r));
    r.x.ax = 0x4F02;
    r.x.bx = static_cast<unsigned short>(mode_.modeNumber | 0x4000);
    if (biosInt10(r) != 0x004F) {
        std::snprintf(error_, sizeof(error_),
                      "VBE mode set failed for %dx%dx%d (4F02h=%04X).",
                      mode_.width, mode_.height, mode_.bpp, r.x.ax);
        return false;
    }

    // ---- INT 31h/0800h: map the LFB physical address -------------------
    __dpmi_meminfo mi;
    std::memset(&mi, 0, sizeof(mi));
    mi.address = mode_.physBase;
    mi.size    = static_cast<unsigned long>(mode_.bytesPerLine) *
                 mode_.height;
    if (__dpmi_physical_address_mapping(&mi) == 0) {
        mode_.linearBase = mi.address;          // host returned a linear
    } else {
        // Hosts that already identity-map physical RAM below 16 MiB
        // (raw DPMI / some DOS boxes) legitimately fail the call; the
        // physical address is then directly usable as linear. Not a
        // workaround — DPMI 0.9 §0800h explicitly allows this.
        mode_.linearBase = mode_.physBase;
    }

    const int sel = __dpmi_allocate_ldt_descriptors(1);
    if (sel < 0) {
        std::snprintf(error_, sizeof(error_),
                      "DPMI: could not allocate an LDT descriptor for "
                      "the framebuffer.");
        return false;
    }
    __dpmi_set_segment_base_address(sel, mode_.linearBase);
    // Limits above 1 MiB must be page-granular (DPMI 0.9 §0008h).
    const unsigned long lim =
        ((mi.size + 0xFFFUL) & ~0xFFFUL) - 1UL;
    __dpmi_set_segment_limit(sel, lim);
    mode_.lfbSelector = sel;

    if (mode_.bpp == 8) setGrayscalePalette();

    // Centre the VM display inside the (possibly larger) VESA mode.
    offX_ = (mode_.width  - vmW_) / 2; if (offX_ < 0) offX_ = 0;
    offY_ = (mode_.height - vmH_) / 2; if (offY_ < 0) offY_ = 0;

    active_     = true;
    haveCursor_ = false;
    prevCurX_   = prevCurY_ = -1;
    return true;
}

void VbeDisplay::end() {
    if (mode_.lfbSelector > 0) {
        __dpmi_free_ldt_descriptor(mode_.lfbSelector);
        mode_.lfbSelector = 0;
    }
    if (active_) {
        __dpmi_regs r;
        std::memset(&r, 0, sizeof(r));
        r.x.ax = savedTextMode_ & 0xFF;          // INT 10h AH=00 set mode
        __dpmi_int(0x10, &r);
        active_ = false;
    }
}

// Pack `n` RGBA8 pixels (0xAARRGGBB) into the framebuffer's pixel
// format. The Smalltalk display is monochrome so this is exact for
// black/white; the generic path keeps it correct for any colour.
void VbeDisplay::encodeSpan(const std::uint32_t *srcRow, int n,
                            unsigned char *out) const {
    const int bpp = mode_.bpp;
    if (bpp == 32) {
        for (int i = 0; i < n; ++i) {
            const std::uint32_t v = srcRow[i];
            out[0] = static_cast<unsigned char>(v & 0xFF);          // B
            out[1] = static_cast<unsigned char>((v >> 8) & 0xFF);   // G
            out[2] = static_cast<unsigned char>((v >> 16) & 0xFF);  // R
            out[3] = 0;                                             // X
            out += 4;
        }
    } else if (bpp == 24) {
        for (int i = 0; i < n; ++i) {
            const std::uint32_t v = srcRow[i];
            out[0] = static_cast<unsigned char>(v & 0xFF);
            out[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
            out[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
            out += 3;
        }
    } else if (bpp == 16 || bpp == 15) {
        for (int i = 0; i < n; ++i) {
            const std::uint32_t v = srcRow[i];
            const unsigned R = (v >> 16) & 0xFF;
            const unsigned G = (v >> 8) & 0xFF;
            const unsigned B = v & 0xFF;
            const unsigned pix =
                ((R >> (8 - mode_.redSize)) << mode_.redPos) |
                ((G >> (8 - mode_.grnSize)) << mode_.grnPos) |
                ((B >> (8 - mode_.bluSize)) << mode_.bluPos);
            out[0] = static_cast<unsigned char>(pix & 0xFF);
            out[1] = static_cast<unsigned char>((pix >> 8) & 0xFF);
            out += 2;
        }
    } else {  // 8bpp — grayscale DAC ramp, index = luma
        for (int i = 0; i < n; ++i) {
            const std::uint32_t v = srcRow[i];
            const unsigned R = (v >> 16) & 0xFF;
            const unsigned G = (v >> 8) & 0xFF;
            const unsigned B = v & 0xFF;
            *out++ = static_cast<unsigned char>(
                (R * 77 + G * 150 + B * 29) >> 8);   // BT.601-ish luma
        }
    }
}

void VbeDisplay::putFbRow(int fbX, int fbY,
                          const unsigned char *bytes, int n) {
    if (n <= 0) return;
    const int bytesPerPx = (mode_.bpp + 7) / 8;
    const unsigned long off =
        static_cast<unsigned long>(fbY) * mode_.bytesPerLine +
        static_cast<unsigned long>(fbX) * bytesPerPx;
    // DJGPP flat model: a near pointer's value is its DS offset.
    movedata(_my_ds(),
             static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(bytes)),
             static_cast<unsigned>(mode_.lfbSelector),
             static_cast<unsigned>(off),
             static_cast<std::size_t>(n) * bytesPerPx);
}

void VbeDisplay::blitNoCursor(const std::uint32_t *src, int srcStride,
                              int x, int y, int w, int h) {
    if (!active_ || !src) return;

    // Clip the requested rect to the VM display, then to the mode.
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(vmW_, x + w);
    int y1 = std::min(vmH_, y + h);
    if (clipped_) {
        x1 = std::min(x1, mode_.width  - offX_);
        y1 = std::min(y1, mode_.height - offY_);
    }
    if (x1 <= x0 || y1 <= y0) return;

    const int span = x1 - x0;
    // Single-threaded DOS (DJGPP --disable-threads, no TLS): a plain
    // function-static scratch line is correct and avoids depending
    // on thread_local, which the target libc may not support.
    static unsigned char line[2048 * 4];
    if (span > 2048) return;                       // mode wider than we map

    for (int yy = y0; yy < y1; ++yy) {
        const std::uint32_t *srcRow = src + static_cast<std::size_t>(yy) *
                                              srcStride + x0;
        encodeSpan(srcRow, span, line);
        putFbRow(offX_ + x0, offY_ + yy, line, span);
    }
}

void VbeDisplay::blit(const std::uint32_t *src, int srcStride,
                      int x, int y, int w, int h) {
    blitNoCursor(src, srcStride, x, y, w, h);
}

void VbeDisplay::drawCursor(const std::uint32_t *src, int srcStride,
                            const std::uint16_t bits[16],
                            int cx, int cy) {
    if (!active_ || !src) return;

    // Erase the previous cursor by repainting the pixels it covered.
    if (haveCursor_)
        blitNoCursor(src, srcStride, prevCurX_, prevCurY_, 16, 16);

    // Composite the new cursor: a set bit is opaque black, a clear
    // bit shows the underlying RGBA pixel (Blue Book §29 hotspot is
    // the form's top-left, which Bridge.h already accounts for).
    static unsigned char line[16 * 4];
    std::uint32_t row[16];
    for (int yy = 0; yy < 16; ++yy) {
        const int vy = cy + yy;
        if (vy < 0 || vy >= vmH_) continue;
        if (clipped_ && vy >= mode_.height - offY_) continue;

        int x0 = -1, x1 = -1;
        for (int xx = 0; xx < 16; ++xx) {
            const int vx = cx + xx;
            if (vx < 0 || vx >= vmW_) continue;
            if (clipped_ && vx >= mode_.width - offX_) continue;
            const bool on = (bits[yy] >> (15 - xx)) & 1;
            row[xx] = on ? PIXEL_BLACK
                         : src[static_cast<std::size_t>(vy) * srcStride + vx];
            if (x0 < 0) x0 = xx;
            x1 = xx;
        }
        if (x0 < 0) continue;
        const int span = x1 - x0 + 1;
        encodeSpan(row + x0, span, line);
        putFbRow(offX_ + cx + x0, offY_ + vy, line, span);
    }
    prevCurX_ = cx; prevCurY_ = cy;
    haveCursor_ = true;
}

}  // namespace st80dos
