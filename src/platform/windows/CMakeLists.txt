# st80_windows: Windows-platform glue.
#
# Implements Bridge.h on top of WindowsHal + Interpreter +
# WindowsFileSystem. The Windows app frontend (pure Win32 + GDI,
# no SDL) links against this the same way the SDL Linux frontend
# links against st80_linux and the Swift frontend links against
# st80_apple. Header-only EventQueue is shared with the other
# slices via src/platform/common.

add_library(st80_windows STATIC
    WindowsHal.cpp
    WindowsBridge.cpp
)

target_include_directories(st80_windows PUBLIC
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/platform/win
    ${CMAKE_SOURCE_DIR}/src/platform/common
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(st80_windows PUBLIC st80core)

# Win32 file system uses CreateFile / MoveFileEx / DeleteFile /
# FindFirstFile — all in kernel32. Link it explicitly even though
# MSVC auto-links kernel32 by default, so MinGW / clang-cl builds
# pick it up too.
target_link_libraries(st80_windows PUBLIC kernel32)

if(MSVC)
    # Match the stricter warning level the GNU/Clang slices use.
    target_compile_options(st80_windows PRIVATE /W4 /permissive-)
    # Narrow-char Win32 entry points only matter inside the .cpp /
    # .hpp files here — WindowsFileSystem calls FindFirstFileA /
    # MoveFileExA / DeleteFileA explicitly (no macro resolution),
    # so don't export UNICODE=0 to consumers. Bridge.h is pure C
    # and doesn't include <windows.h>.
    target_compile_definitions(st80_windows PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
