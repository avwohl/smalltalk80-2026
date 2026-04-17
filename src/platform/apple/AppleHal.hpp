// st80-2026 — AppleHal.hpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// IHal implementation shared by Mac Catalyst and iOS targets.
// Owns the display's RGBA8 staging buffer (kept in sync with the
// VM's 1-bit DisplayBitmap on demand) and a thread-safe EventQueue
// of Blue Book input words. The Swift layer reads the buffer and
// pushes platform events through the C API in Bridge.h; this class
// is what sits behind those calls on the C++ side.

#pragma once

#include "hal/IHal.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "EventQueue.hpp"

namespace st80 {

class AppleHal : public IHal {
 public:
    AppleHal();
    ~AppleHal() override = default;

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

    // Pushes a pre-encoded Blue Book input word into the queue. The
    // Swift layer posts native events through Bridge.h; the bridge
    // translates them into these 16-bit words before enqueueing.
    void postInputWord(std::uint16_t word) { events_.push(word); }

    // Update the shadow cursor position that `get_cursor_location`
    // returns. Called by AppleBridge every time we observe a new
    // pointer position so the image's `Sensor cursorPoint` / menu
    // pop-up logic sees a current location.
    void setShadowCursor(int x, int y) { cursorX_ = x; cursorY_ = y; }

    // Copy the most recent 16-word cursor form out. Safe to call
    // every frame; the frontend hashes and rebuilds its NSCursor
    // only on change.
    void copyCursorImage(std::uint16_t out[16]) const;

    // Pops the accumulated dirty-rect since the last call. Returns
    // {0,0,0,0} if nothing is dirty.
    struct DirtyRect { int x, y, w, h; };
    DirtyRect takeDirtyRect();

    // If a `signal_at` request is due (scheduled time has passed),
    // writes the semaphore OOP into *outSem, clears the schedule,
    // and returns true. Caller invokes Interpreter::asynchronousSignal.
    bool scheduledSemaphoreDue(int *outSem);

    bool quitRequested() const { return quit_.load(); }

 private:
    // Display
    int displayWidth_ = 0;
    int displayHeight_ = 0;
    std::vector<std::uint32_t> pixels_;  // width*height RGBA8
    std::mutex dirtyMutex_;
    DirtyRect dirty_{0, 0, 0, 0};

    // Input
    EventQueue events_;
    int inputSemaphore_ = 0;

    // Clock
    std::uint32_t startupMs_ = 0;

    // Delay / signal_at scheduling. One outstanding request at a time
    // per dbanay / Blue Book §29 hal contract.
    int scheduledSemaphore_ = 0;
    std::uint32_t scheduledTime_ = 0;

    // Cursor (shadow state only — actual cursor is rendered by
    // the Swift layer via NSCursor).
    int cursorX_ = 0;
    int cursorY_ = 0;
    bool cursorLinked_ = true;
    std::uint16_t cursorBits_[16] = {0};

    // Lifecycle
    std::atomic<bool> quit_{false};
    std::string imageName_;
};

}  // namespace st80
