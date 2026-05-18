// st80-2026 — app/dos/MouseInt33.cpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// INT 33h is a real-mode interrupt; we reach it from the DPMI client
// through __dpmi_int with a zeroed __dpmi_regs (same convention as
// VbeDisplay / dosiz's dj_ems.c). No buffer marshalling is needed —
// every sub-function we use passes its arguments in registers only.

#include "MouseInt33.hpp"

#include <cstring>

#include <dpmi.h>

namespace st80dos {

namespace {
int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
// CX/DX motion counters are signed 16-bit mickey deltas.
int s16(unsigned v) {
    return static_cast<int>(static_cast<short>(v & 0xFFFF));
}
}  // namespace

bool MouseInt33::begin(int vmW, int vmH) {
    vmW_ = vmW > 0 ? vmW : 1;
    vmH_ = vmH > 0 ? vmH : 1;

    __dpmi_regs r;
    std::memset(&r, 0, sizeof(r));
    r.x.ax = 0x0000;                     // reset + installation check
    __dpmi_int(0x33, &r);
    if (r.x.ax != 0xFFFF) {
        present_ = false;
        return false;
    }
    buttonCount_ = static_cast<int>(r.x.bx);
    present_     = true;

    x_ = vmW_ / 2;
    y_ = vmH_ / 2;
    remX_ = remY_ = 0;
    prevButtons_  = 0;

    // Prime fn 0Bh so the first real poll() doesn't see the startup
    // jump accumulated since the driver loaded.
    std::memset(&r, 0, sizeof(r));
    r.x.ax = 0x000B;
    __dpmi_int(0x33, &r);
    return true;
}

MousePoll MouseInt33::poll() {
    MousePoll out;
    if (!present_) return out;

    __dpmi_regs r;

    // --- AX=000Bh: relative motion (mickeys) ---------------------------
    std::memset(&r, 0, sizeof(r));
    r.x.ax = 0x000B;
    __dpmi_int(0x33, &r);
    const int dxm = s16(r.x.cx);
    const int dym = s16(r.x.dx);

    // Scale mickeys → pixels, carrying the remainder so slow drags
    // don't quantise away.
    const int accX = dxm * scaleNum_ + remX_;
    const int accY = dym * scaleNum_ + remY_;
    const int movX = accX / scaleDen_;
    const int movY = accY / scaleDen_;
    remX_ = accX - movX * scaleDen_;
    remY_ = accY - movY * scaleDen_;

    const int nx = clampi(x_ + movX, 0, vmW_ - 1);
    const int ny = clampi(y_ + movY, 0, vmH_ - 1);
    out.moved = (nx != x_) || (ny != y_);
    x_ = nx; y_ = ny;
    out.x = x_; out.y = y_;

    // --- AX=0003h: button state ----------------------------------------
    std::memset(&r, 0, sizeof(r));
    r.x.ax = 0x0003;
    __dpmi_int(0x33, &r);
    const unsigned char b = static_cast<unsigned char>(r.x.bx & 0x07);
    out.buttons  = b;
    out.pressed  = static_cast<unsigned char>(b & ~prevButtons_);
    out.released = static_cast<unsigned char>(prevButtons_ & ~b);
    prevButtons_ = b;
    return out;
}

}  // namespace st80dos
