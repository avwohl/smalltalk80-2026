// st80-2026 — HostFileSystem.hpp (DOS / DJGPP)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// DJGPP provides a POSIX-ish C library on top of INT 21h — <fcntl.h>,
// <unistd.h>, <dirent.h>, <sys/stat.h> are all present and route to
// DOS file-handle calls (open → AH=3D, read → AH=3F, ...). That
// means we reuse `PosixFileSystem` verbatim; this header is a one-
// line alias matching the pattern used by src/platform/posix/
// HostFileSystem.hpp on macOS / Linux.
//
// Under dosiz the same POSIX calls land in dosiz's INT 21h handler,
// which serves them against host files directly (see dosiz's
// docs/c-toolchain-guide.md for the DJGPP file-I/O test results).
// Under real DOS they route through whatever DOS kernel + redirector
// is active.
//
// No `#ifdef` in portable source — CMake picks this header up only
// when `DJGPP` is defined, same way the other per-platform
// HostFileSystem.hpp files get selected.

#pragma once

#include "PosixFileSystem.hpp"

namespace st80 {
using HostFileSystem = PosixFileSystem;
}  // namespace st80
