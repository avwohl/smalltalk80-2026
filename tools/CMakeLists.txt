# Development tools built against st80core.
#
# Tools use the header-only HeadlessHal and a per-platform
# IFileSystem implementation selected through the thin
# `HostFileSystem.hpp` alias header — POSIX on Apple/Linux,
# Win32 on Windows, POSIX (via DJGPP's libc wrapping INT 21h)
# on DOS. No `#ifdef` in the source files.
#
# DOS paths include src/platform/posix too, so the POSIX impl
# is visible when the DOS alias header `#include`s it.

if(DJGPP)
    set(ST80_TOOLS_FS_DIR "${CMAKE_SOURCE_DIR}/src/platform/dos")
    set(ST80_TOOLS_FS_EXTRA "${CMAKE_SOURCE_DIR}/src/platform/posix")
elseif(WIN32)
    set(ST80_TOOLS_FS_DIR "${CMAKE_SOURCE_DIR}/src/platform/win")
    set(ST80_TOOLS_FS_EXTRA "")
else()
    set(ST80_TOOLS_FS_DIR "${CMAKE_SOURCE_DIR}/src/platform/posix")
    set(ST80_TOOLS_FS_EXTRA "")
endif()

add_executable(st80_probe st80_probe.cpp)
target_link_libraries(st80_probe PRIVATE st80core)
target_include_directories(st80_probe PRIVATE
    ${ST80_TOOLS_FS_DIR}
    ${ST80_TOOLS_FS_EXTRA}
    ${CMAKE_SOURCE_DIR}/src/platform/headless
)

add_executable(st80_run st80_run.cpp)
target_link_libraries(st80_run PRIVATE st80core)
target_include_directories(st80_run PRIVATE
    ${ST80_TOOLS_FS_DIR}
    ${ST80_TOOLS_FS_EXTRA}
    ${CMAKE_SOURCE_DIR}/src/platform/headless
)

add_executable(st80_validate st80_validate.cpp Sha256.cpp)
target_link_libraries(st80_validate PRIVATE st80core)
target_include_directories(st80_validate PRIVATE
    ${ST80_TOOLS_FS_DIR}
    ${ST80_TOOLS_FS_EXTRA}
    ${CMAKE_SOURCE_DIR}/src/platform/headless
)

# st80_gui_test — headless "fake GUI" runner (pharo-headless-test
# analog). Unlike the tools above it drives the live image through the
# Bridge.h C API, so it links the platform bridge (which is the same
# in-memory-framebuffer HAL the real frontends use, no window needed)
# rather than st80core+HeadlessHal. One source, four link targets;
# the DJGPP build is what runs under dosiz for the DOS-port gate.
if(DJGPP)
    set(ST80_GUI_TEST_BRIDGE st80_dos)
elseif(WIN32)
    set(ST80_GUI_TEST_BRIDGE st80_windows)
elseif(APPLE)
    set(ST80_GUI_TEST_BRIDGE st80_apple)
else()
    set(ST80_GUI_TEST_BRIDGE st80_linux)
endif()
add_executable(st80_gui_test st80_gui_test.cpp)
target_link_libraries(st80_gui_test PRIVATE ${ST80_GUI_TEST_BRIDGE})
