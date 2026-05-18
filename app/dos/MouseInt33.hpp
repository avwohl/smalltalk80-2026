// st80-2026 — app/dos/MouseInt33.hpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Microsoft-compatible mouse via INT 33h. We track an absolute VM
// cursor by accumulating relative motion counters (sub-function 0Bh,
// mickeys) rather than the driver's notion of screen coordinates —
// the driver has no idea what VESA resolution we set, so its
// absolute coordinates (fn 03h CX/DX) would be wrong. Buttons come
// from fn 03h. This is exactly the split docs/dos-plan.md specifies.
//
// dosbox-staging's mouse emulator (hence dosiz `--window`) is a
// standard DOS mouse; this code doesn't know or care that the host
// is dosiz rather than a real driver.

#pragma once

namespace st80dos {

struct MousePoll {
    int x = 0, y = 0;          // absolute VM-space cursor
    bool moved = false;        // x or y changed since the last poll
    unsigned char buttons = 0; // bit0 left, bit1 right, bit2 middle
    unsigned char pressed = 0; // edges: bits newly 0→1 this poll
    unsigned char released = 0;// edges: bits newly 1→0 this poll
};

class MouseInt33 {
 public:
    // Reset/detect (INT 33h AX=0000h). Returns false if no driver is
    // installed — the image is unusable without a mouse, so the
    // caller errors out (docs/dos-plan.md minimum system floor).
    bool begin(int vmW, int vmH);

    // Read motion deltas (AX=000Bh) + buttons (AX=0003h) and fold
    // them into an absolute VM cursor clamped to the display.
    MousePoll poll();

    int buttonCount() const { return buttonCount_; }

    // Mouse travel scaling: `num`/`den` pixels per mickey. Default
    // 1/1 tracks dosiz `--window` SDL relative motion well; real
    // hardware may want it slower (set from a CLI flag).
    void setScale(int num, int den) {
        scaleNum_ = num > 0 ? num : 1;
        scaleDen_ = den > 0 ? den : 1;
    }

 private:
    int vmW_ = 0, vmH_ = 0;
    int x_ = 0, y_ = 0;
    int remX_ = 0, remY_ = 0;          // sub-pixel mickey remainder
    int scaleNum_ = 1, scaleDen_ = 1;
    unsigned char prevButtons_ = 0;
    int buttonCount_ = 0;
    bool present_ = false;
};

}  // namespace st80dos
