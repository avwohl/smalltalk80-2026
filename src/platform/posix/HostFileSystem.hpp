// st80-2026 — HostFileSystem.hpp (POSIX)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// One-line alias so platform-neutral tools (`st80_probe`,
// `st80_run`, `st80_validate`) can `#include "HostFileSystem.hpp"`
// and get the right IFileSystem for whichever target they are
// being built against. Each host dir under `src/platform/` ships
// its own copy of this header; CMake selects the directory per
// target. No `#ifdef` in source code.

#pragma once

#include "PosixFileSystem.hpp"

namespace st80 {
using HostFileSystem = PosixFileSystem;
}  // namespace st80
