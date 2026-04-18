// st80-2026 — EventQueue.hpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Thread-safe FIFO of Blue Book input words. UI thread pushes 16-bit
// encoded events in; VM thread pops them out via
// IHal::next_input_word. Platform-neutral — shared by every host HAL
// under src/platform/.

#pragma once

#include <cstdint>
#include <deque>
#include <mutex>

namespace st80 {

class EventQueue {
 public:
    EventQueue() = default;

    void push(std::uint16_t word) {
        std::lock_guard<std::mutex> lock(mutex_);
        words_.push_back(word);
    }

    bool pop(std::uint16_t *outWord) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (words_.empty()) return false;
        *outWord = words_.front();
        words_.pop_front();
        return true;
    }

    std::size_t size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return words_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        words_.clear();
    }

 private:
    std::mutex mutex_;
    std::deque<std::uint16_t> words_;
};

}  // namespace st80
