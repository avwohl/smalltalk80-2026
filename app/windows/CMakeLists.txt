# st80-win: pure-Win32 desktop frontend for the Windows port.
#
# Uses the pure-C Bridge.h API exposed by `st80_windows`; the only
# Windows-specific dependencies in the app layer are GDI32 (for
# StretchDIBits), USER32 (windowing), and SHELL32 (for
# CommandLineToArgvW). A headless mode (`--no-window`) drives the
# same CI smoke-test loop that the Linux `.deb` post-install
# script uses.
#
# WIN32_EXECUTABLE selects /SUBSYSTEM:WINDOWS under MSVC so no
# console is attached. The entry point is WinMain.

add_executable(st80-win WIN32
    st80_windows_main.cpp
    st80-win.rc
)

# Icon resource is separate so a fresh clone without the .ico file
# (a binary asset kept out of source) still builds cleanly.
if(EXISTS "${CMAKE_SOURCE_DIR}/resources/windows/st80.ico")
    target_sources(st80-win PRIVATE st80-win-icon.rc)
endif()

# Thread version into the .rc via rc-scoped defines so Explorer
# shows the real version for every build. rc.exe has quirky
# comma-in-define handling, so scope to $<COMPILE_LANGUAGE:RC>.
target_compile_definitions(st80-win PRIVATE
    $<$<COMPILE_LANGUAGE:RC>:ST80_VERSION_STRING="${PROJECT_VERSION}.0">
    $<$<COMPILE_LANGUAGE:RC>:ST80_FILE_VERSION=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0>
    $<$<COMPILE_LANGUAGE:RC>:ST80_PRODUCT_VERSION=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0>
)

target_link_libraries(st80-win PRIVATE
    st80_windows
    gdi32
    user32
    shell32
    comdlg32
)

if(MSVC)
    target_compile_options(st80-win PRIVATE /W4 /permissive-)
    target_compile_definitions(st80-win PRIVATE
        UNICODE=1          # CreateWindowExW + RegisterClassExW path
        _UNICODE=1
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

install(TARGETS st80-win
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)
