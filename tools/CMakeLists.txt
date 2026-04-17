# Development tools built against st80core.
#
# Phase-1 tools use the header-only POSIX filesystem and headless HAL
# implementations. Real platform slices (Apple, Win32, Linux) land in
# Phases 2–4 and will supply their own HAL/filesystem.

add_executable(st80_probe st80_probe.cpp)
target_link_libraries(st80_probe PRIVATE st80core)
target_include_directories(st80_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/src/platform/posix
    ${CMAKE_SOURCE_DIR}/src/platform/headless
)

add_executable(st80_run st80_run.cpp)
target_link_libraries(st80_run PRIVATE st80core)
target_include_directories(st80_run PRIVATE
    ${CMAKE_SOURCE_DIR}/src/platform/posix
    ${CMAKE_SOURCE_DIR}/src/platform/headless
)
