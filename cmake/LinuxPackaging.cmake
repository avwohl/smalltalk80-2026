# st80-2026 Linux packaging — DEB and RPM via CPack.
#
# Produces st80-<version>-<arch>.deb and st80-<version>-<arch>.rpm
# under `build/`. The package ships:
#
#   /usr/bin/st80-linux                          main CLI
#   /usr/share/applications/st80.desktop         menu entry
#   /usr/share/doc/st80/{README.md,LICENSE,
#                       THIRD_PARTY_LICENSES}    docs + license
#
# We don't ship the Xerox virtual image — it has its own licence
# terms. st80-linux tells the user where to fetch it on first
# launch (and the README explains).
#
# Build targets after `cmake --build build`:
#   cd build && cpack -G DEB    # st80-0.1.0-<arch>.deb
#   cd build && cpack -G RPM    # st80-0.1.0-<arch>.rpm

set(CPACK_PACKAGE_NAME "st80")
set(CPACK_PACKAGE_VENDOR "Aaron Wohl")
set(CPACK_PACKAGE_CONTACT "Aaron Wohl <aawohl@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Smalltalk-80 Blue Book (1983) implementation")
set(CPACK_PACKAGE_DESCRIPTION
    "A faithful implementation of the 1983 Xerox Smalltalk-80 Blue Book virtual machine. Loads Wolczko's v2 image and runs the original Xerox desktop (Browser, Workspace, Transcript, etc.). Port source: dbanay/Smalltalk (MIT).")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README  "${CMAKE_SOURCE_DIR}/README.md")

# Detect the host architecture once and reuse for both generators.
execute_process(
    COMMAND dpkg --print-architecture
    OUTPUT_VARIABLE ST80_DEB_ARCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT ST80_DEB_ARCH)
    set(ST80_DEB_ARCH "amd64")
endif()

execute_process(
    COMMAND uname -m
    OUTPUT_VARIABLE ST80_RPM_ARCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT ST80_RPM_ARCH)
    set(ST80_RPM_ARCH "x86_64")
endif()

set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}-${CMAKE_SYSTEM_NAME}")

# --- Install rules shared by both generators -----------------------

install(FILES
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES"
        "${CMAKE_SOURCE_DIR}/README.md"
    DESTINATION share/doc/st80
    COMPONENT Runtime)

install(FILES
        "${CMAKE_SOURCE_DIR}/packaging/linux/st80.desktop"
    DESTINATION share/applications
    COMPONENT Runtime)

# --- DEB generator -------------------------------------------------

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${ST80_DEB_ARCH}")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CMAKE_PROJECT_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# --- RPM generator -------------------------------------------------

set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
set(CPACK_RPM_PACKAGE_URL "${CMAKE_PROJECT_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_REQUIRES "SDL2")
set(CPACK_RPM_PACKAGE_ARCHITECTURE "${ST80_RPM_ARCH}")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

# Don't try to own standard system dirs — the host OS already does.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    "/usr/share/applications"
    "/usr/share/doc")

# --- Generators ----------------------------------------------------

set(CPACK_GENERATOR "DEB;RPM")

include(CPack)
