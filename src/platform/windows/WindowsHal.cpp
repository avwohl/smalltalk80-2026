// st80-2026 — WindowsHal.cpp
// Copyright (c) 2026 Aaron Wohl. MIT License.

#include "WindowsHal.hpp"

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

// Smalltalk-80 epoch: seconds since 1901-01-01 00:00 GMT. See
// LinuxHal.cpp for the offset derivation. The same number works on
// Windows because `std::chrono::system_clock` is the Unix epoch on
// every known Windows toolchain (MSVC, MinGW, Clang).
std::uint32_t smalltalkEpochSeconds() {
    constexpr std::uint64_t kOffsetSecs = 2177452800ull;
    const auto now = std::chrono::system_clock::now();
    const auto secsSinceUnix = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    const std::uint64_t smalltalk = static_cast<std::uint64_t>(secsSinceUnix)
                                    + kOffsetSecs;
    return static_cast<std::uint32_t>(smalltalk);
}
}  // namespace

WindowsHal::WindowsHal() {
    startupMs_ = msSinceEpoch();
}

std::uint32_t WindowsHal::get_smalltalk_epoch_time() {
    return smalltalkEpochSeconds();
}

std::uint32_t WindowsHal::get_msclock() {
    return msSinceEpoch();
}

void WindowsHal::signal_at(int semaphore, std::uint32_t msClockTime) {
    scheduledSemaphore_ = semaphore;
    scheduledTime_ = msClockTime;
}

bool WindowsHal::scheduledSemaphoreDue(int *outSem) {
    if (!scheduledSemaphore_) return false;
    const std::uint32_t now = msSinceEpoch();
    if (static_cast<std::int32_t>(now - scheduledTime_) < 0) return false;
    if (outSem) *outSem = scheduledSemaphore_;
    scheduledSemaphore_ = 0;
    scheduledTime_ = 0;
    return true;
}

void WindowsHal::set_cursor_image(std::uint16_t *image) {
    if (!image) return;
    for (int i = 0; i < 16; ++i) cursorBits_[i] = image[i];
}

void WindowsHal::copyCursorImage(std::uint16_t out[16]) const {
    for (int i = 0; i < 16; ++i) out[i] = cursorBits_[i];
}

void WindowsHal::set_cursor_location(int x, int y) {
    cursorX_ = x;
    cursorY_ = y;
}

void WindowsHal::get_cursor_location(int *x, int *y) {
    if (x) *x = cursorX_;
    if (y) *y = cursorY_;
}

void WindowsHal::set_link_cursor(bool link) {
    cursorLinked_ = link;
}

bool WindowsHal::set_display_size(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (width == displayWidth_ && height == displayHeight_) return true;

    std::lock_guard<std::mutex> lock(dirtyMutex_);
    displayWidth_ = width;
    displayHeight_ = height;
    pixels_.assign(static_cast<std::size_t>(width) * height, 0xFFFFFFFFu);
    dirty_ = {0, 0, width, height};
    return true;
}

void WindowsHal::display_changed(int x, int y, int width, int height) {
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

WindowsHal::DirtyRect WindowsHal::takeDirtyRect() {
    std::lock_guard<std::mutex> lock(dirtyMutex_);
    DirtyRect r = dirty_;
    dirty_ = {0, 0, 0, 0};
    return r;
}

bool WindowsHal::next_input_word(std::uint16_t *word) {
    return events_.pop(word);
}

void WindowsHal::set_input_semaphore(int semaphore) {
    inputSemaphore_ = semaphore;
}

void WindowsHal::error(const char *message) {
    std::fprintf(stderr, "st80 fatal: %s\n", message ? message : "(null)");
    std::abort();
}

void WindowsHal::signal_quit() {
    quit_.store(true);
}

void WindowsHal::exit_to_debugger() {
    std::fprintf(stderr, "st80 debugger break\n");
    std::abort();
}

const char *WindowsHal::get_image_name() {
    return imageName_.c_str();
}

void WindowsHal::set_image_name(const char *name) {
    imageName_ = name ? name : "";
}

}  // namespace st80
