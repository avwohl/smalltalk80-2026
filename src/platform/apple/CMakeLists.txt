# st80_apple: Apple-platform glue.
#
# Implements Bridge.h (src/include/Bridge.h) on top of AppleHal +
# Interpreter + PosixFileSystem. Swift on macOS / Catalyst / iOS links
# against this.
#
# Phase 2a: builds under the host toolchain as a static library so the
# plumbing stays compile-checked. A later xcframework build (scripts/
# build-xcframework.sh) will drive this through CMake three times —
# once per slice: macOS, iOS device, iOS simulator.

add_library(st80_apple STATIC
    AppleHal.cpp
    AppleBridge.cpp
)

target_include_directories(st80_apple PUBLIC
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/platform/posix
    ${CMAKE_SOURCE_DIR}/src/platform/common
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(st80_apple PUBLIC st80core)
