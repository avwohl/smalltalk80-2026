// st80-2026 — st80_probe
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Tiny loader probe: feed it a Xerox Smalltalk-80 v2 virtual image
// file path, and it instantiates ObjectMemory + a POSIX filesystem +
// a headless HAL, calls loadSnapshot, and reports whether the load
// succeeded plus basic heap stats.
//
// Not a VM runner — the interpreter isn't started. This is the
// smallest possible exercise of the ObjectMemory code path. It is
// where the endianness bug in loadObjects (little-endian hosts) will
// show up.
//
// Usage:
//   st80_probe <path-to-image>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "ObjectMemory.hpp"
#include "HostFileSystem.hpp"
#include "HeadlessHal.hpp"

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path-to-image>\n", argv[0]);
        return 64;  // EX_USAGE
    }

    const std::string fullPath = argv[1];
    std::string dir = ".";
    std::string name = fullPath;
    const auto slash = fullPath.find_last_of("\\/");
    if (slash != std::string::npos) {
        dir = fullPath.substr(0, slash);
        name = fullPath.substr(slash + 1);
    }

    st80::HostFileSystem fs(dir);
    st80::HeadlessHal hal;
    // Heap-allocate: ObjectMemory embeds RealWordMemory (2 MiB by
    // value); a stack local underflows DJGPP's small default stack
    // on the DOS port (see tools/st80_run.cpp for the full story).
    auto memoryPtr = std::make_unique<st80::ObjectMemory>(&hal);
    auto &memory = *memoryPtr;

    std::printf("st80_probe: loading %s (dir=%s name=%s)\n",
                fullPath.c_str(), dir.c_str(), name.c_str());

    const bool ok = memory.loadSnapshot(&fs, name.c_str());

    if (!ok) {
        std::fprintf(stderr, "st80_probe: loadSnapshot FAILED\n");
        return 2;
    }

    std::printf("st80_probe: loadSnapshot OK\n");
    std::printf("  oopsLeft = %d\n", memory.oopsLeft());
    std::printf("  coreLeft = %u words\n", memory.coreLeft());
    return 0;
}
