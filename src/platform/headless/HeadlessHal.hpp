// st80-2026 — HeadlessHal.hpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Minimal IHal implementation for unit tests, loader probes, and
// trace replays. No display, no real clock, no input. Any call that
// would matter in a normal run aborts loudly so we notice if a test
// accidentally exercises GUI-path code. Not shared with Phase 2
// Catalyst work — the Apple HAL lives under src/platform/apple/.

#pragma once

#include "hal/IHal.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>

namespace st80 {

class HeadlessHal : public IHal {
 public:
    void set_input_semaphore(int) override {}

    std::uint32_t get_smalltalk_epoch_time() override { return 0; }

    std::uint32_t get_msclock() override { return ticks_++; }

    void signal_at(int, std::uint32_t) override {}

    void set_cursor_image(std::uint16_t *) override {}
    void set_cursor_location(int, int) override {}
    void get_cursor_location(int *x, int *y) override {
        if (x) *x = 0;
        if (y) *y = 0;
    }
    void set_link_cursor(bool) override {}

    bool set_display_size(int, int) override { return true; }
    void display_changed(int, int, int, int) override {}

    bool next_input_word(std::uint16_t *) override { return false; }

    void error(const char *message) override {
        std::fprintf(stderr, "st80 fatal: %s\n", message ? message : "(null)");
        std::abort();
    }

    void signal_quit() override { quit_ = true; }

    void exit_to_debugger() override {
        std::fprintf(stderr, "st80 debugger break\n");
        std::abort();
    }

    const char *get_image_name() override { return image_name_.c_str(); }
    void set_image_name(const char *name) override {
        image_name_ = name ? name : "";
    }

    bool quit_requested() const { return quit_; }

 private:
    std::uint32_t ticks_ = 0;
    bool quit_ = false;
    std::string image_name_;
};

}  // namespace st80
