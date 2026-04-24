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
