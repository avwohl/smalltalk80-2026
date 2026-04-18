# st80-linux: SDL2 desktop frontend for the Linux port.
#
# Uses the pure-C Bridge.h API exposed by `st80_linux`; the only
# Linux-specific dependency in the app layer is SDL2 for windowing
# and input. A headless mode (`--no-window`) lets package smoke
# tests exercise the VM without a display.

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_executable(st80-linux
    st80_linux_main.cpp
)

target_link_libraries(st80-linux PRIVATE
    st80_linux
    PkgConfig::SDL2
)

install(TARGETS st80-linux
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)
