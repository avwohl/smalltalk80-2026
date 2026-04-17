// Smoke test — confirms the core headers compile and the
// RealWordMemory primitives round-trip.
// Copyright (c) 2026 Aaron Wohl. MIT License.

#include <cassert>
#include <cstdio>

#include "Oops.hpp"
#include "RealWordMemory.hpp"

int main() {
    // Well-known OOPs (BB p. 576). Verify a handful to catch typos
    // in the Oops constants.
    static_assert(st80::NilPointer == 2, "nil OOP");
    static_assert(st80::FalsePointer == 4, "false OOP");
    static_assert(st80::TruePointer == 6, "true OOP");
    static_assert(st80::ClassSmallInteger == 12, "SmallInteger class OOP");
    static_assert(st80::DoesNotUnderstandSelector == 42, "DNU selector OOP");

    st80::RealWordMemory mem;

    // Word round-trip.
    mem.segment_word_put(0, 0, 0xBEEF);
    assert(mem.segment_word(0, 0) == 0xBEEF);
    mem.segment_word_put(15, 65535, 0x1234);
    assert(mem.segment_word(15, 65535) == 0x1234);

    // Byte access over a known word. segment_word_byte with a fresh
    // word tests that the pointer-cast path and the memory write
    // agree on which byte is "byte 0".
    mem.segment_word_put(1, 100, 0x0000);
    mem.segment_word_byte_put(1, 100, 0, 0xAB);
    mem.segment_word_byte_put(1, 100, 1, 0xCD);
    const int b0 = mem.segment_word_byte(1, 100, 0);
    const int b1 = mem.segment_word_byte(1, 100, 1);
    assert(b0 == 0xAB);
    assert(b1 == 0xCD);

    // Bit range extraction. Write 0xABCD, read bits 0..3 (high nibble).
    mem.segment_word_put(2, 0, 0xABCD);
    assert(mem.segment_word_bits_to(2, 0, 0, 3) == 0xA);
    assert(mem.segment_word_bits_to(2, 0, 12, 15) == 0xD);
    assert(mem.segment_word_bits_to(2, 0, 4, 11) == 0xBC);

    // Bit range put. Clear to zero, set bits 4..7 to 0x9.
    mem.segment_word_put(3, 0, 0x0000);
    mem.segment_word_bits_to_put(3, 0, 4, 7, 0x9);
    assert(mem.segment_word(3, 0) == 0x0900);

    std::printf("core_smoke_test: OK\n");
    return 0;
}
