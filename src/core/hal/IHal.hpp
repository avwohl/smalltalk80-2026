// st80-2026 — IHal.hpp
//
// Ported from dbanay/Smalltalk @ ab6ab55:src/hal.h
//   Copyright (c) 2020 Dan Banay. MIT License.
//   See THIRD_PARTY_LICENSES for full text.
// Modifications for st80-2026 Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Host abstraction for cursor, display, input, clock, semaphores,
// and lifecycle. Renamed from dbanay's `IHardwareAbstractionLayer`
// for brevity; the interface itself is unchanged.
//
// Platform implementations live under src/platform/. Phase-2 work
// will likely split this into narrower interfaces (IDisplay,
// IInput, IClock) per docs/plan.md, but the monolithic form keeps
// the initial ObjectMemory + Interpreter port against the same
// contract dbanay used.

#pragma once

#include <cstdint>

namespace st80 {

class IHal {
 public:
    virtual ~IHal() = default;

    // Specify the semaphore to signal on input
    virtual void set_input_semaphore(int semaphore) = 0;

    // The number of seconds since 00:00 in the morning of January 1, 1901
    virtual std::uint32_t get_smalltalk_epoch_time() = 0;

    // the number of milliseconds since the millisecond clock was
    // last reset or rolled over (a 32-bit unsigned number)
    virtual std::uint32_t get_msclock() = 0;

    // Schedule a semaphore to be signaled at a time. Only one outstanding
    // request may be scheduled at anytime. When called any outstanding
    // request will be replaced (or canceled if semaphore is 0).
    // Will signal immediate if scheduled time has passed.
    virtual void signal_at(int semaphore, std::uint32_t msClockTime) = 0;

    // Set the cursor image (a 16 word form)
    virtual void set_cursor_image(std::uint16_t *image) = 0;

    // Set the mouse cursor location
    virtual void set_cursor_location(int x, int y) = 0;
    virtual void get_cursor_location(int *x, int *y) = 0;
    virtual void set_link_cursor(bool link) = 0;

    // Set the display size
    virtual bool set_display_size(int width, int height) = 0;

    // Notify that screen contents changed
    virtual void display_changed(int x, int y, int width, int height) = 0;

    // Input queue
    virtual bool next_input_word(std::uint16_t *word) = 0;

    // Report catastrophic failure
    virtual void error(const char *message) = 0;

    // Lifetime
    virtual void signal_quit() = 0;
    virtual void exit_to_debugger() = 0;

    // Snapshot name
    virtual const char *get_image_name() = 0;
    virtual void set_image_name(const char *new_name) = 0;
};

}  // namespace st80
