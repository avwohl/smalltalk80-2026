# st80-2026 — DJGPP cross-toolchain file.
#
# Cross-compile the DOS slice (src/platform/dos/ + app/dos/) to a
# go32-v2-stubbed COFF DPMI client that runs under dosiz (primary)
# or a stock CWSDPMI / HDPMI32 host on real DOS (secondary).
#
# Usage:
#
#     cmake -S . -B build-dos \
#           -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-djgpp.cmake
#     cmake --build build-dos
#
# Pulls the DJGPP cross-compiler binaries off the caller's PATH —
# typical install paths:
#
#   macOS / Linux: `andrewwutw/build-djgpp` v3.4 into /usr/local
#                  or ~/djgpp, `export PATH=$PATH:~/djgpp/bin`
#   Windows MSYS2: the cross build from the same repo, or the
#                  Delorie pre-built toolchain in C:\DJGPP\BIN
#
# dosiz's own DJGPP test suite uses the andrewwutw bundle; we
# inherit that as the canonical target so our binaries track
# whatever version that suite is validating against.
#
# See docs/dos-plan.md for the full story.

set(CMAKE_SYSTEM_NAME      MSDOS)
set(CMAKE_SYSTEM_PROCESSOR i386)

# DJGPP's cross-compiler is `i586-pc-msdosdjgpp-*`. Let CMake find
# the compilers on PATH rather than hard-coding an install prefix;
# every distributor places them differently (brew, apt, manual).
set(CMAKE_C_COMPILER   i586-pc-msdosdjgpp-gcc)
set(CMAKE_CXX_COMPILER i586-pc-msdosdjgpp-g++)
set(CMAKE_AR           i586-pc-msdosdjgpp-ar)
set(CMAKE_RANLIB       i586-pc-msdosdjgpp-ranlib)

# Treat the cross toolchain's sysroot as the only search path for
# libraries and headers; don't let CMake find host-side /usr/include
# or /opt/homebrew stuff.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# DJGPP doesn't support PIC — the DPMI flat model doesn't need it
# and some older binutils reject the flag outright.
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# DJGPP's libstdc++ is configured --disable-threads; <mutex> and
# <atomic> still compile, but std::mutex is a no-op. That matches
# our single-threaded Bridge.h contract on DOS (see dos-plan.md).
# No -pthread anywhere.
