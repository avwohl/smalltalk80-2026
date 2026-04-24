// st80-2026 — DosHal.hpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// IHal implementation for MS-DOS / DPMI. Structurally identical to
// WindowsHal / LinuxHal — owns the RGBA8 staging buffer, dirty-rect
// tracking, a thread-safe EventQueue of 16-bit Blue Book input
// words, and the `signal_at` scheduling slot.
//
// DOS is single-threaded: under DJGPP's --disable-threads libstdc++,
// std::mutex degrades to a no-op, which matches our cooperative
// frontend loop. No Bridge.h contract changes required.
//
// No INT 21h / INT 10h / INT 33h code lives in the HAL itself — the
// frontend (app/dos — VBE + INT 33h mouse + INT 16h keyboard) reads
// `displayPixels()` and pushes pre-encoded input words through the C
// bridge. The HAL stays portable C++17 so the unit tests can cover
// it from a host build if we ever need to.

#pragma once

#include "hal/IHal.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "EventQueue.hpp"

namespace st80 {

class DosHal : public IHal {
 public:
    DosHal();
    ~DosHal() override = default;

    // --- IHal contract --------------------------------------------------

    void set_input_semaphore(int semaphore) override;
    std::uint32_t get_smalltalk_epoch_time() override;
    std::uint32_t get_msclock() override;
    void signal_at(int semaphore, std::uint32_t msClockTime) override;

    void set_cursor_image(std::uint16_t *image) override;
    void set_cursor_location(int x, int y) override;
    void get_cursor_location(int *x, int *y) override;
    void set_link_cursor(bool link) override;

    bool set_display_size(int width, int height) override;
    void display_changed(int x, int y, int width, int height) override;

    bool next_input_word(std::uint16_t *word) override;

    void error(const char *message) override;
    void signal_quit() override;
    void exit_to_debugger() override;

    const char *get_image_name() override;
    void set_image_name(const char *name) override;

    // --- Platform-facing accessors --------------------------------------

    int displayWidth() const { return displayWidth_; }
    int displayHeight() const { return displayHeight_; }
    const std::uint32_t *displayPixels() const { return pixels_.data(); }
    std::uint32_t *mutablePixels() { return pixels_.data(); }

    int inputSemaphore() const { return inputSemaphore_; }
    std::size_t queueDepth() { return events_.size(); }

    void postInputWord(std::uint16_t word) { events_.push(word); }
    void setShadowCursor(int x, int y) { cursorX_ = x; cursorY_ = y; }

    void copyCursorImage(std::uint16_t out[16]) const;

    struct DirtyRect { int x, y, w, h; };
    DirtyRect takeDirtyRect();

    bool scheduledSemaphoreDue(int *outSem);

    bool quitRequested() const { return quit_.load(); }

 private:
    int displayWidth_ = 0;
    int displayHeight_ = 0;
    std::vector<std::uint32_t> pixels_;
    std::mutex dirtyMutex_;
    DirtyRect dirty_{0, 0, 0, 0};

    EventQueue events_;
    int inputSemaphore_ = 0;

    std::uint32_t startupMs_ = 0;

    int scheduledSemaphore_ = 0;
    std::uint32_t scheduledTime_ = 0;

    int cursorX_ = 0;
    int cursorY_ = 0;
    bool cursorLinked_ = true;
    std::uint16_t cursorBits_[16] = {0};

    std::atomic<bool> quit_{false};
    std::string imageName_;
};

}  // namespace st80
