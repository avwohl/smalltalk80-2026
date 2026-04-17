// st80-2026 — AppleHal.cpp
// Copyright (c) 2026 Aaron Wohl. MIT License.

#include "AppleHal.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace st80 {

namespace {
std::uint32_t msSinceEpoch() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - start).count());
}
}  // namespace

AppleHal::AppleHal() {
    startupMs_ = msSinceEpoch();
}


std::uint32_t AppleHal::get_smalltalk_epoch_time() {
    // Smalltalk-80 clock: seconds since 1901-01-01 00:00. We don't wire
    // a real wall clock yet; return a stable value so the image boots.
    // Phase 2 polish: hook to CFAbsoluteTimeGetCurrent.
    return 0;
}

std::uint32_t AppleHal::get_msclock() {
    return msSinceEpoch();
}

void AppleHal::signal_at(int semaphore, std::uint32_t msClockTime) {
    // Record the request. Firing happens lazily via
    // scheduledSemaphoreDue(), which the bridge's run loop polls each
    // cycle. A semaphore value of 0 cancels any outstanding request
    // (BB §29 HAL contract; dbanay main.cpp:153).
    scheduledSemaphore_ = semaphore;
    scheduledTime_ = msClockTime;
}

bool AppleHal::scheduledSemaphoreDue(int *outSem) {
    if (!scheduledSemaphore_) return false;
    const std::uint32_t now = msSinceEpoch();
    // SDL_TICKS_PASSED-equivalent: handle 32-bit wrap with signed diff.
    if (static_cast<std::int32_t>(now - scheduledTime_) < 0) return false;
    if (outSem) *outSem = scheduledSemaphore_;
    scheduledSemaphore_ = 0;
    scheduledTime_ = 0;
    return true;
}

void AppleHal::set_cursor_image(std::uint16_t *image) {
    if (!image) return;
    for (int i = 0; i < 16; ++i) cursorBits_[i] = image[i];
}

void AppleHal::copyCursorImage(std::uint16_t out[16]) const {
    for (int i = 0; i < 16; ++i) out[i] = cursorBits_[i];
}

void AppleHal::set_cursor_location(int x, int y) {
    cursorX_ = x;
    cursorY_ = y;
}

void AppleHal::get_cursor_location(int *x, int *y) {
    if (x) *x = cursorX_;
    if (y) *y = cursorY_;
}

void AppleHal::set_link_cursor(bool link) {
    cursorLinked_ = link;
}

bool AppleHal::set_display_size(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (width == displayWidth_ && height == displayHeight_) return true;

    std::lock_guard<std::mutex> lock(dirtyMutex_);
    displayWidth_ = width;
    displayHeight_ = height;
    pixels_.assign(static_cast<std::size_t>(width) * height, 0xFFFFFFFFu);
    dirty_ = {0, 0, width, height};
    return true;
}

void AppleHal::display_changed(int x, int y, int width, int height) {
    std::lock_guard<std::mutex> lock(dirtyMutex_);
    if (dirty_.w == 0) {
        dirty_ = {x, y, width, height};
        return;
    }
    const int x0 = std::min(dirty_.x, x);
    const int y0 = std::min(dirty_.y, y);
    const int x1 = std::max(dirty_.x + dirty_.w, x + width);
    const int y1 = std::max(dirty_.y + dirty_.h, y + height);
    dirty_ = {x0, y0, x1 - x0, y1 - y0};
}

AppleHal::DirtyRect AppleHal::takeDirtyRect() {
    std::lock_guard<std::mutex> lock(dirtyMutex_);
    DirtyRect r = dirty_;
    dirty_ = {0, 0, 0, 0};
    return r;
}

bool AppleHal::next_input_word(std::uint16_t *word) {
    return events_.pop(word);
}

void AppleHal::set_input_semaphore(int semaphore) {
    inputSemaphore_ = semaphore;
}

void AppleHal::error(const char *message) {
    std::fprintf(stderr, "st80 fatal: %s\n", message ? message : "(null)");
    std::abort();
}

void AppleHal::signal_quit() {
    quit_.store(true);
}

void AppleHal::exit_to_debugger() {
    std::fprintf(stderr, "st80 debugger break\n");
    std::abort();
}

const char *AppleHal::get_image_name() {
    return imageName_.c_str();
}

void AppleHal::set_image_name(const char *name) {
    imageName_ = name ? name : "";
}

}  // namespace st80
