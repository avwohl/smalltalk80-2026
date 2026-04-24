/*
 * st80-2026 — Bridge.h
 *
 * Pure-C API between the Smalltalk-80 core (C++) and a platform
 * frontend (Swift/Objective-C on Apple, C on Win32/Linux). All
 * VM state lives inside the core library; the frontend drives it
 * through these entry points.
 *
 * Thread model: call `st80_init` and `st80_shutdown` on the main
 * thread. `st80_run` runs the interpreter loop until `st80_stop` is
 * called; on Apple we run it on a dedicated worker thread so UI
 * stays responsive. Display and event functions are safe to call
 * from any thread.
 *
 * Copyright (c) 2026 Aaron Wohl. MIT License.
 */

#ifndef ST80_BRIDGE_H
#define ST80_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Construct the core: open `imagePath` (Xerox v2 image, either endian
 * — auto-detected), load it, construct the Interpreter. Returns true
 * on success. Must be called before any other st80_* function.
 */
bool st80_init(const char *imagePath);

/* Run `maxCycles` interpreter cycles, returning the number of cycles
 * actually executed. Use 0 to mean "until st80_stop is called". This
 * function blocks; call it on the VM worker thread. */
int st80_run(int maxCycles);

/* Request the running st80_run to return at the next cycle boundary. */
void st80_stop(void);

/* Tear down the core. Invalidates all pointers previously returned. */
void st80_shutdown(void);

/* Non-zero if the image has invoked primitiveQuit (113). The frontend
 * should poll this after each `st80_run` call and terminate the host
 * app when it transitions to true. */
int st80_quit_requested(void);

/* =========================================================================
 * Display
 *
 * The Smalltalk-80 display is a 1-bit-per-pixel bitmap. On the frontend
 * we render it via Metal as a 32-bit RGBA texture. The core keeps a
 * persistent RGBA buffer that it refreshes from the VM's display bits
 * when `st80_display_sync` is called.
 * ========================================================================= */

int st80_display_width(void);
int st80_display_height(void);

/* Pointer to the RGBA8 buffer, `width*height*4` bytes.
 * Pointer is stable for the lifetime of st80 (between init and
 * shutdown) unless the VM requests a resize, in which case a new
 * pointer is returned here and the caller must reload. */
const uint32_t *st80_display_pixels(void);

/* Copy the current VM display bits into the RGBA buffer. Returns the
 * dirty rectangle since the last sync; if w == 0 there is nothing to
 * redraw. Safe to call every frame. */
typedef struct St80Rect {
    int x, y, w, h;
} St80Rect;

St80Rect st80_display_sync(void);

/* =========================================================================
 * Cursor
 *
 * The Smalltalk image supplies a 16×16 1-bit cursor form via
 * `IHal::set_cursor_image` (BB §29). The frontend reads the current
 * bits through the call below and renders them via its native cursor
 * API (NSCursor on Apple). `out` must point to at least 16 uint16_t
 * slots; each word is one scanline, bit 0 = MSB = leftmost pixel,
 * set bit = opaque/black.
 *
 * The same `out` value is returned every call until the image sets
 * a new cursor; consumers hash to detect change and rebuild their
 * native cursor lazily.
 * ========================================================================= */
void st80_cursor_image(uint16_t out[16]);

/* =========================================================================
 * Input
 *
 * The frontend translates native events (UIEvent, NSEvent, Win32 MSG,
 * etc.) into the calls below. Encoding them as 16-bit Blue Book input
 * words happens inside the core.
 * ========================================================================= */

typedef enum St80MouseButton {
    ST80_BTN_RED    = 0,  /* primary  (left click / plain tap) */
    ST80_BTN_YELLOW = 1,  /* middle   (⌥-click / long-press)  */
    ST80_BTN_BLUE   = 2   /* tertiary (⌘-click / two-finger)  */
} St80MouseButton;

void st80_post_mouse_move(int x, int y);
void st80_post_mouse_down(int x, int y, St80MouseButton button);
void st80_post_mouse_up(int x, int y, St80MouseButton button);

/* `charCode` is a Unicode code point (or ST-80 control code).
 * `modifiers` is a bitmask — see ST80_MOD_* below. */
#define ST80_MOD_SHIFT   (1u << 0)
#define ST80_MOD_CTRL    (1u << 1)
#define ST80_MOD_OPTION  (1u << 2)
#define ST80_MOD_COMMAND (1u << 3)

void st80_post_key_down(int charCode, uint32_t modifiers);
void st80_post_key_up(int charCode, uint32_t modifiers);

/* =========================================================================
 * Clipboard (copy-out direction)
 *
 * Reads the Blue Book image's global text clipboard —
 * `ParagraphEditor class>>CurrentSelection` — as a 7-bit-ASCII byte
 * stream. Returns a pointer to a caller-owned, NUL-terminated buffer
 * managed by the core; the buffer is valid until the next call or
 * `st80_shutdown`. Empty string means no selection, or the image
 * hasn't populated CurrentSelection yet. The frontend is expected to
 * call this after it has posted a Ctrl+C / Ctrl+X keystroke and the
 * interpreter has had a chance to process it.
 *
 * Paste direction is handled by streaming keystrokes through
 * `st80_post_key_down` — no separate entry point needed.
 * ========================================================================= */
const char *st80_clipboard_read(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* ST80_BRIDGE_H */
