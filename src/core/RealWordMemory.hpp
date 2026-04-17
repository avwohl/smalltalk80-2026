// st80-2026 — RealWordMemory.hpp
//
// Ported from dbanay/Smalltalk @ ab6ab55:src/realwordmemory.h
//   Copyright (c) 2020 Dan Banay. MIT License.
//   See THIRD_PARTY_LICENSES for full text.
// Modifications for st80-2026 Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Segmented word-addressable memory, 16 segments × 65536 16-bit
// words = 2 MiB logical address space. Matches the Blue Book spec
// (G&R p. 656).
//
// Byte-level access operates on the two halves of each 16-bit word;
// byteNumber 0 is the byte that appears FIRST in memory (the high
// byte on a big-endian host). The active path uses pointer casting,
// which means the host endianness and the image loader's byte-swap
// policy must stay in sync. Dbanay's loader byte-swaps the raw image
// into the host's native word order on load, so that on either host
// `word[0]` via pointer-cast reaches the byte the spec calls byte 0.
// If we ever need a host-endian-independent implementation we can
// swap to the shift form in the commented-out lines below.

#pragma once

#include <cstdint>
#include <cassert>

namespace st80 {

class RealWordMemory {
 public:
    static const int SegmentCount = 16;
    static const int SegmentSize = 65536;  // in words

    RealWordMemory() = default;

    inline int segment_word(int s, int w) {
        assert(s >= 0 && s < SegmentCount);
        assert(w >= 0 && w < SegmentSize);
        return memory[s][w];
    }

    inline int segment_word_put(int s, int w, int value) {
        assert(s >= 0 && s < SegmentCount);
        assert(w >= 0 && w < SegmentSize);
        assert(value >= 0 && value < 65536);
        memory[s][w] = static_cast<std::uint16_t>(value);
        return value;
    }

    // byteNumber 0 is the byte of the word that appears first in
    // memory; byteNumber 1 is the next.
    inline int segment_word_byte(int s, int w, int byteNumber) {
        assert(s >= 0 && s < SegmentCount);
        assert(w >= 0 && w < SegmentSize);
        assert(byteNumber == 0 || byteNumber == 1);
        // return (memory[s][w] >> (8*byteNumber)) & 0xff;     // endian-agnostic (shift form)
        return reinterpret_cast<std::uint8_t *>(&memory[s][w])[byteNumber];
        // return reinterpret_cast<std::uint8_t *>(&memory[s][w])[1 - byteNumber];  // little-endian host form
    }

    inline int segment_word_byte_put(int s, int w, int byteNumber, int value) {
        assert(s >= 0 && s < SegmentCount);
        assert(w >= 0 && w < SegmentSize);
        assert(value >= 0 && value < 65536);
        assert(byteNumber == 0 || byteNumber == 1);
        reinterpret_cast<std::uint8_t *>(&memory[s][w])[byteNumber] =
            static_cast<std::uint8_t>(value);
        return value;
    }

    // Bit 0 is the most significant bit of the word; bit 15 is the least
    // significant. (G&R p. 657.)
    inline int segment_word_bits_to(int s, int w, int firstBitIndex, int lastBitIndex) {
        assert(s >= 0 && s < SegmentCount);
        assert(w >= 0 && w < SegmentSize);
        std::uint16_t shift = memory[s][w] >> (15 - lastBitIndex);
        std::uint16_t mask = (1 << (lastBitIndex - firstBitIndex + 1)) - 1;
        return shift & mask;
    }

    inline int segment_word_bits_to_put(int s, int w, int firstBitIndex,
                                        int lastBitIndex, int value) {
        assert(s >= 0 && s < SegmentCount);
        assert(w >= 0 && w < SegmentSize);
        assert(value >= 0 && value < 65536);
        std::uint16_t mask = (1 << (lastBitIndex - firstBitIndex + 1)) - 1;
        assert((value & mask) == value);
        memory[s][w] = static_cast<std::uint16_t>(
            (memory[s][w] & ~(mask << (15 - lastBitIndex))) |
            (value << (15 - lastBitIndex)));
        return value;
    }

 private:
    std::uint16_t memory[SegmentCount][SegmentSize] = {};
};

}  // namespace st80
