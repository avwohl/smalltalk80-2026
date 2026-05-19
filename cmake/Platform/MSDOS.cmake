# st80-2026 — cmake/Platform/MSDOS.cmake
#
# Minimal CMake platform definition for the DJGPP / MS-DOS target.
#
# Modern CMake (>= ~3.x, confirmed on 4.2.3) does NOT ship a
# Platform/MSDOS module and does NOT set the `DJGPP` variable on its
# own — it just prints "System is unknown to cmake" and leaves
# `DJGPP` undefined. The whole st80-2026 DOS build gate keys off
# `if(DJGPP)` (root CMakeLists.txt, tools/CMakeLists.txt), so without
# this file none of src/platform/dos or app/dos is ever added and
# the tools silently build against the wrong (host POSIX) slice.
#
# cmake/toolchain-djgpp.cmake puts this directory on CMAKE_MODULE_PATH
# so CMake's early `include(Platform/${CMAKE_SYSTEM_NAME} OPTIONAL)`
# finds this file. It only loads when CMAKE_SYSTEM_NAME == MSDOS,
# i.e. only under that toolchain file — the four shipping host ports
# never see it.

set(DJGPP 1)
set(MSDOS 1)
set(UNIX 0)
set(WIN32 0)
set(APPLE 0)

# DJGPP produces COFF static libs (libfoo.a) and go32-v2-stubbed
# COFF executables (foo.exe). No shared objects.
set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_SHARED_LIBRARY_SUFFIX "")
set(CMAKE_IMPORT_LIBRARY_PREFIX "")
set(CMAKE_IMPORT_LIBRARY_SUFFIX "")
set(CMAKE_EXECUTABLE_SUFFIX ".exe")
set(CMAKE_LINK_LIBRARY_SUFFIX "")
set(CMAKE_DL_LIBS "")

set(CMAKE_FIND_LIBRARY_PREFIXES "lib")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

# DJGPP has no concept of shared builds; force everything static so
# add_library() without an explicit type is a static archive.
set(CMAKE_SHARED_LIBRARY_CXX_FLAGS "")
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")
set(BUILD_SHARED_LIBS OFF)
