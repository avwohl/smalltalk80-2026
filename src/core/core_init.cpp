// st80-2026 — core_init.cpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Minimal translation unit that forces the header-only core modules
// to compile so CMake can build `st80core` even before the full
// ObjectMemory / Interpreter land. Replaced with real entry points
// during Phase 1.

#include "Oops.hpp"
#include "RealWordMemory.hpp"

namespace st80 {

// Linker sentinel so `st80core` is a non-empty static library.
// Referenced by the Phase-1 unit tests once they exist.
int core_version() {
    return 0;
}

}  // namespace st80
