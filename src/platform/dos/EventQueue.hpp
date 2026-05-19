// st80-2026 — EventQueue.hpp (DOS / DJGPP)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Single-threaded EventQueue for the DOS slice. Same class, same
// API surface as src/platform/common/EventQueue.hpp — but with no
// std::mutex. This is docs/dos-plan.md Risk #1's mitigation: DJGPP's
// libstdc++ is built --disable-threads, and in that configuration
// <mutex> does NOT define std::mutex at all (it is gated behind
// _GLIBCXX_HAS_GTHREADS — not merely a no-op). The common header
// therefore cannot compile under DJGPP.
//
// The lock was only ever there to keep the future worker-thread
// refactor honest (see docs/architecture.md "Threading contract").
// On DOS there is exactly one thread — the cooperative frame loop —
// so Bridge.h's same-thread contract is satisfied unconditionally
// and the FIFO needs no synchronisation. Dropping the lock here is
// the correct design for this target, not a workaround.
//
// No `#ifdef` in any portable header: the DOS CMake slice lists
// src/platform/dos ahead of src/platform/common on the include
// path, so `#include "EventQueue.hpp"` resolves to this file for
// the DOS build only — the same shadowing mechanism the per-platform
// HostFileSystem.hpp alias already uses. Every other port keeps the
// mutex-guarded common header unchanged.

#pragma once

#include <cstdint>
#include <deque>

namespace st80 {

class EventQueue {
 public:
    EventQueue() = default;

    void push(std::uint16_t word) {
        words_.push_back(word);
    }

    bool pop(std::uint16_t *outWord) {
        if (words_.empty()) return false;
        *outWord = words_.front();
        words_.pop_front();
        return true;
    }

    std::size_t size() {
        return words_.size();
    }

    void clear() {
        words_.clear();
    }

 private:
    std::deque<std::uint16_t> words_;
};

}  // namespace st80
