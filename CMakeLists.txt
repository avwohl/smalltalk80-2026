# st80-2026 — Smalltalk-80 Blue Book implementation.

cmake_minimum_required(VERSION 3.20)
project(st80-2026 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
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
# Phase 4: add_subdirectory(src/platform/win32)
# Phase 4: add_subdirectory(src/platform/linux)
