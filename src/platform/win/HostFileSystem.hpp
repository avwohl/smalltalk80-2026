// st80-2026 — HostFileSystem.hpp (Windows)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Windows twin of src/platform/posix/HostFileSystem.hpp. See that
// header for the rationale; this one just picks the Win32
// implementation of IFileSystem.

#pragma once

#include "WindowsFileSystem.hpp"

namespace st80 {
using HostFileSystem = WindowsFileSystem;
}  // namespace st80
