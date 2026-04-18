# st80_linux: Linux-platform glue.
#
# Implements Bridge.h on top of LinuxHal + Interpreter +
# PosixFileSystem. The Linux app frontend (SDL2 today; X11 / Wayland
# later) links against this the same way the Swift frontend links
# against st80_apple on Apple.
#
# Header-only EventQueue is shared with the Apple slice via
# src/platform/common.

add_library(st80_linux STATIC
    LinuxHal.cpp
    LinuxBridge.cpp
)

target_include_directories(st80_linux PUBLIC
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/platform/posix
    ${CMAKE_SOURCE_DIR}/src/platform/common
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(st80_linux PUBLIC st80core)
