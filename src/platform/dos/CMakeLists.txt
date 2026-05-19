# st80_dos: MS-DOS / DPMI platform glue.
#
# Implements Bridge.h on top of DosHal + Interpreter +
# PosixFileSystem (via DosHostFileSystem.hpp). The DOS app frontend
# (app/dos — VBE + INT 33h mouse + INT 16h keyboard) links against
# this the same way the Win32 frontend links against st80_windows,
# the SDL Linux frontend links against st80_linux, and the Swift
# frontend links against st80_apple. Header-only EventQueue is
# shared with the other slices via src/platform/common.
#
# DJGPP's libstdc++ is configured --disable-threads, so std::mutex
# degrades to a no-op and std::atomic uses compiler builtins. Both
# match our Bridge.h single-thread contract on DOS. No -pthread.

add_library(st80_dos STATIC
    DosHal.cpp
    DosBridge.cpp
)

# The DOS slice dir comes FIRST so `#include "EventQueue.hpp"` and
# `#include "HostFileSystem.hpp"` resolve to the DOS copies that
# shadow src/platform/common + src/platform/posix (single-threaded
# EventQueue — docs/dos-plan.md Risk #1 — and the PosixFileSystem
# alias). This shadow-by-include-order is the project's no-#ifdef
# per-platform mechanism; ordering it wrong silently picks the
# mutex-guarded common header and fails to build on DJGPP.
target_include_directories(st80_dos PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/platform/posix
    ${CMAKE_SOURCE_DIR}/src/platform/common
)

target_link_libraries(st80_dos PUBLIC st80core)

# DJGPP's libc is POSIX-enough that PosixFileSystem works unchanged
# (see DosHostFileSystem.hpp). No extra link libs — DJGPP's libc
# and libstdc++ are linked implicitly by the driver.
