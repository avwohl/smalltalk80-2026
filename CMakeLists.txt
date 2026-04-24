# st80-2026 — Smalltalk-80 Blue Book implementation.

cmake_minimum_required(VERSION 3.20)
project(st80-2026
    VERSION 0.1.0
    DESCRIPTION "Smalltalk-80 Blue Book (1983) implementation"
    HOMEPAGE_URL "https://github.com/avwohl/st80-2026"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# MSVC: project-wide warning level + sensible defaults so all
# TUs see the same Win32 macros regardless of which slice pulls
# in <windows.h>.
if(MSVC)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS   # _open / _fstat64 / strerror
        _WINSOCK_DEPRECATED_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    add_compile_options(/utf-8 /Zc:preprocessor /EHsc)
endif()

# --- core library ---------------------------------------------------
# Shared VM core: no platform deps, no UI. Phase 1 adds ObjectMemory,
# Interpreter, Primitives, BitBlt, ImageLoader. Everything included
# here is portable C++17.
add_library(st80core STATIC
    src/core/core_init.cpp
    src/core/ObjectMemory.cpp
    src/core/BitBlt.cpp
    src/core/Interpreter.cpp
)
target_include_directories(st80core PUBLIC
    src/include
    src/core
)

# --- tests ----------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# --- tools ----------------------------------------------------------
add_subdirectory(tools)

# --- platform slices ------------------------------------------------
# Apple: scaffolding (Bridge C API + AppleHal + EventQueue) builds
# under the host toolchain so the plumbing stays compile-checked.
# Phase-2b adds a proper xcframework with iOS/Catalyst slices.
if(APPLE)
    add_subdirectory(src/platform/apple)
endif()

# Linux: SDL2-backed frontend + LinuxHal/Bridge built on every
# non-Apple Unix host (Ubuntu/Debian/Fedora/etc.).
if(UNIX AND NOT APPLE)
    add_subdirectory(src/platform/linux)
    add_subdirectory(app/linux)
endif()

# Windows: pure-Win32 + GDI frontend, WindowsHal/Bridge,
# WindowsFileSystem. No SDL — see app/windows for rationale.
# RC language must be enabled explicitly so the version-info +
# manifest + icon resources compile as part of the app target.
if(WIN32)
    enable_language(RC)
    add_subdirectory(src/platform/windows)
    add_subdirectory(app/windows)
endif()

# MS-DOS / DPMI: DJGPP cross-compiled slice. Activates only when
# configured with -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-djgpp.cmake
# (which sets CMAKE_SYSTEM_NAME=MSDOS; CMake maps that to DJGPP=1).
# Primary runtime is `dosiz` (https://github.com/avwohl/dosiz) — an
# in-process dosbox-staging host with a full ring-3 DPMI implementation
# that serves INT 21h directly against the host filesystem. Secondary
# runtime is real DOS / FreeDOS with CWSDPMI or HDPMI32.
# See docs/dos-plan.md.
if(DJGPP)
    add_subdirectory(src/platform/dos)
    # app/dos/ lands in Phase D2 with the VBE frontend. Until then,
    # st80_dos alone compiles and is enough to link st80_run against
    # for the headless trace2 gate (Phase D1).
endif()

# --- packaging ------------------------------------------------------
# Linux: CPack → DEB + RPM.
# Windows: CPack → NSIS (self-extracting installer) + WIX (.msi),
#          plus MSIX AppX layout for Microsoft Store submission.
# Mac: xcodebuild → App Store (no CPack).
if(UNIX AND NOT APPLE)
    include(cmake/LinuxPackaging.cmake)
endif()
if(WIN32)
    include(cmake/WindowsPackaging.cmake)
endif()
