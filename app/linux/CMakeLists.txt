# st80-linux: SDL2 desktop frontend for the Linux port.
#
# Uses the pure-C Bridge.h API exposed by `st80_linux`; the SDL2
# layer drives the VM window once an image has been chosen. Image
# selection is delegated to a GTK4 launcher that mirrors the
# Catalyst / Win32 launchers — see app/linux/Launcher.{hpp,cpp}.
# A headless mode (`--no-window`) lets package smoke tests exercise
# the VM without a display.

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

# Optional GTK4 launcher. If the dev headers aren't installed, we
# fall back to a launcher-less build that still works as long as the
# user supplies an image path on the command line. This keeps CI on
# minimal containers happy without forcing a heavy desktop dep.
pkg_check_modules(GTK4 IMPORTED_TARGET gtk4)
pkg_check_modules(LIBCURL IMPORTED_TARGET libcurl)

set(ST80_LINUX_SOURCES
    st80_linux_main.cpp
)

if(GTK4_FOUND AND LIBCURL_FOUND)
    list(APPEND ST80_LINUX_SOURCES
        Launcher.cpp
        ${CMAKE_SOURCE_DIR}/tools/Json.cpp
        ${CMAKE_SOURCE_DIR}/tools/Sha256.cpp
    )
endif()

add_executable(st80-linux ${ST80_LINUX_SOURCES})

target_link_libraries(st80-linux PRIVATE
    st80_linux
    PkgConfig::SDL2
)

if(GTK4_FOUND AND LIBCURL_FOUND)
    target_link_libraries(st80-linux PRIVATE
        PkgConfig::GTK4
        PkgConfig::LIBCURL
    )
    target_include_directories(st80-linux PRIVATE
        ${CMAKE_SOURCE_DIR}/tools
    )
    target_compile_definitions(st80-linux PRIVATE
        ST80_LINUX_HAS_LAUNCHER=1
    )
else()
    message(STATUS
        "Linux launcher disabled (need libgtk-4-dev + libcurl-dev). "
        "Image path must be supplied on the command line.")
endif()

install(TARGETS st80-linux
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)
