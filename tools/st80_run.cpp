// st80-2026 — st80_run
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Minimal interpreter cycle runner. Loads a Xerox Smalltalk-80 v2
// image via the endian-aware loader, constructs the Interpreter, and
// calls `cycle()` N times, printing the executed bytecode per step.
//
// Not a trace2/trace3 gold-file comparison — the Xerox trace format
// is a human-readable English description of each instruction that
// would take a dedicated decoder to emit. This tool is the smaller
// step: prove the interpreter can actually advance without crashing.
//
// Usage:
//   st80_run [-n <cycles>] <path-to-image>
//
// Exit codes:
//   0  ran the requested number of cycles
//   2  load failure
//  64  usage error

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Interpreter.hpp"
#include "ObjectMemory.hpp"
#include "HostFileSystem.hpp"
#include "HeadlessHal.hpp"

static void usage(const char *argv0) {
    std::fprintf(stderr, "usage: %s [-n <cycles>] <path-to-image>\n", argv0);
}

int main(int argc, char **argv) {
    int cycles = 100;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (std::strcmp(argv[argi], "-n") == 0 && argi + 1 < argc) {
            cycles = std::atoi(argv[++argi]);
            argi++;
        } else {
            usage(argv[0]);
            return 64;
        }
    }
    if (argi >= argc) {
        usage(argv[0]);
        return 64;
    }

    const std::string fullPath = argv[argi];
    std::string dir = ".";
    std::string name = fullPath;
    const auto slash = fullPath.find_last_of("\\/");
    if (slash != std::string::npos) {
        dir = fullPath.substr(0, slash);
        name = fullPath.substr(slash + 1);
    }

    st80::HostFileSystem fs(dir);
    st80::HeadlessHal hal;
    hal.set_image_name(name.c_str());
    st80::Interpreter vm(&hal, &fs);

    std::fprintf(stderr, "st80_run: initializing (image=%s, dir=%s, cycles=%d)\n",
                 name.c_str(), dir.c_str(), cycles);

    if (!vm.init()) {
        std::fprintf(stderr, "st80_run: Interpreter::init() FAILED\n");
        return 2;
    }

    std::fprintf(stderr, "st80_run: init OK; cycling...\n");

    for (int i = 1; i <= cycles; i++) {
        vm.cycle();
        const int bc = vm.lastBytecode();
        std::printf("%d\n", bc);
    }

    std::fprintf(stderr, "st80_run: completed %d cycles\n", cycles);
    return 0;
}
