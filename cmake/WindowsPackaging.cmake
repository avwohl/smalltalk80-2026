# st80-2026 Windows packaging — NSIS + WIX installers via CPack,
# plus an MSIX AppX layout for Microsoft Store submission.
#
# Produces the following under `build/`:
#
#   st80-<ver>-win64.exe     NSIS self-extracting installer
#   st80-<ver>-win64.msi     WiX MSI (traditional admin install)
#   st80-<ver>-appx/         MSIX staging dir — feed to MakeAppx.exe
#                            to produce the .msix for Store upload.
#
# Build targets:
#
#   cmake --build build --config Release
#   cd build
#   cpack -G NSIS -C Release          # st80-<ver>-win64.exe
#   cpack -G WIX  -C Release          # st80-<ver>-win64.msi
#   cmake --build . --target st80_appx_layout --config Release
#   powershell -File packaging/windows/pack-msix.ps1 `
#       -Layout build/st80-<ver>-appx `
#       -Output build/st80-<ver>.msix
#
# ARM64 packages are built with `-A ARM64` at configure time;
# everything below is architecture-agnostic (CPACK_SYSTEM_NAME
# follows CMAKE_SYSTEM_PROCESSOR automatically).

# -------------------------------------------------------------------
# Shared metadata
# -------------------------------------------------------------------
set(CPACK_PACKAGE_NAME          "Smalltalk80")
set(CPACK_PACKAGE_VENDOR        "Aaron Wohl")
set(CPACK_PACKAGE_CONTACT       "Aaron Wohl <aawohl@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Smalltalk-80 Blue Book (1983) implementation")
set(CPACK_PACKAGE_DESCRIPTION
    "A faithful implementation of the 1983 Xerox Smalltalk-80 Blue Book virtual machine. Loads Wolczko's v2 image and runs the original Xerox desktop (Browser, Workspace, Transcript, etc.). Pure-Win32 frontend, no SDL, no redistributables.")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Smalltalk-80")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README  "${CMAKE_SOURCE_DIR}/README.md")

# -------------------------------------------------------------------
# Install rules shared by NSIS, WIX, and the MSIX layout stage
# -------------------------------------------------------------------
install(FILES
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES"
        "${CMAKE_SOURCE_DIR}/README.md"
    DESTINATION doc
    COMPONENT Runtime)

if(EXISTS "${CMAKE_SOURCE_DIR}/resources/windows/st80.ico")
    install(FILES "${CMAKE_SOURCE_DIR}/resources/windows/st80.ico"
        DESTINATION bin
        COMPONENT Runtime)
endif()

# -------------------------------------------------------------------
# NSIS — self-extracting .exe installer
# -------------------------------------------------------------------
set(CPACK_NSIS_DISPLAY_NAME       "Smalltalk-80 ${PROJECT_VERSION}")
set(CPACK_NSIS_PACKAGE_NAME       "Smalltalk-80")
set(CPACK_NSIS_CONTACT            "${CPACK_PACKAGE_CONTACT}")
set(CPACK_NSIS_URL_INFO_ABOUT     "${CMAKE_PROJECT_HOMEPAGE_URL}")
set(CPACK_NSIS_HELP_LINK          "${CMAKE_PROJECT_HOMEPAGE_URL}")
set(CPACK_NSIS_MODIFY_PATH        OFF)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
set(CPACK_PACKAGE_EXECUTABLES     "st80-win" "Smalltalk-80")
set(CPACK_CREATE_DESKTOP_LINKS    "st80-win")

if(EXISTS "${CMAKE_SOURCE_DIR}/resources/windows/st80.ico")
    set(CPACK_NSIS_MUI_ICON         "${CMAKE_SOURCE_DIR}/resources/windows/st80.ico")
    set(CPACK_NSIS_MUI_UNIICON      "${CMAKE_SOURCE_DIR}/resources/windows/st80.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin/st80-win.exe")
endif()

# -------------------------------------------------------------------
# WIX — MSI installer (widely used for admin deployment + MDM)
# -------------------------------------------------------------------
# Upgrade GUID: stable across versions so the MSI upgrades cleanly
# instead of installing side-by-side. Generated once and committed.
set(CPACK_WIX_UPGRADE_GUID        "3DFB3F1E-5D1C-4E6F-8A2B-9B9A7A5C5C11")
set(CPACK_WIX_PRODUCT_GUID        "*")   # new GUID per build
set(CPACK_WIX_LICENSE_RTF         "")    # falls back to plaintext LICENSE
if(EXISTS "${CMAKE_SOURCE_DIR}/resources/windows/st80.ico")
    set(CPACK_WIX_PRODUCT_ICON    "${CMAKE_SOURCE_DIR}/resources/windows/st80.ico")
endif()
set(CPACK_WIX_PROPERTY_ARPCONTACT "${CPACK_PACKAGE_CONTACT}")
set(CPACK_WIX_PROPERTY_ARPHELPLINK    "${CMAKE_PROJECT_HOMEPAGE_URL}")
set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "${CMAKE_PROJECT_HOMEPAGE_URL}")
set(CPACK_WIX_ROOT_FEATURE_TITLE  "Smalltalk-80")
set(CPACK_WIX_ROOT_FEATURE_DESCRIPTION
    "The Smalltalk-80 VM and pure-Win32 desktop frontend.")

# -------------------------------------------------------------------
# Per-generator filenames
# -------------------------------------------------------------------
set(CPACK_PACKAGE_FILE_NAME
    "st80-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

set(CPACK_GENERATOR "NSIS;WIX")

include(CPack)

# -------------------------------------------------------------------
# MSIX layout — AppX staging dir that MakeAppx.exe turns into .msix
# -------------------------------------------------------------------
# Why a custom target (rather than a CPack generator): CPack does
# not ship an MSIX backend, and the Store pipeline expects a very
# specific directory shape (AppxManifest.xml at root + the binary
# under the directory referenced by Application/@Executable).
# The target below builds that shape deterministically so
# `MakeAppx.exe pack` can consume it.

set(ST80_APPX_DIR "${CMAKE_BINARY_DIR}/st80-${PROJECT_VERSION}-appx")

# Substitute version into AppxManifest.xml. The template lives in
# packaging/windows/; variables listed in @ST80_* are expanded here.
set(ST80_APPX_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
set(ST80_APPX_PUBLISHER "CN=Aaron Wohl")
set(ST80_APPX_PACKAGE_NAME "AaronWohl.Smalltalk80")
set(ST80_APPX_DISPLAY_NAME "Smalltalk-80")
set(ST80_APPX_PUBLISHER_DISPLAY_NAME "Aaron Wohl")
# Prefer CMAKE_GENERATOR_PLATFORM (the Visual Studio generator's -A
# arg). CMAKE_SYSTEM_PROCESSOR reflects the HOST, so on a cross
# build from x64 → ARM64 it stays "AMD64" and would mis-tag the
# .msix. Fall back to the host when no explicit platform was set
# (e.g. Ninja on a native host).
if(CMAKE_GENERATOR_PLATFORM)
    set(_st80_appx_platform "${CMAKE_GENERATOR_PLATFORM}")
else()
    set(_st80_appx_platform "${CMAKE_SYSTEM_PROCESSOR}")
endif()
if("${_st80_appx_platform}" MATCHES "ARM64|arm64")
    set(ST80_APPX_ARCH "arm64")
elseif("${_st80_appx_platform}" MATCHES "AMD64|amd64|x86_64|X86_64|x64")
    set(ST80_APPX_ARCH "x64")
else()
    set(ST80_APPX_ARCH "x86")
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/packaging/windows/AppxManifest.xml.in"
    "${ST80_APPX_DIR}/AppxManifest.xml"
    @ONLY)

add_custom_target(st80_appx_layout
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ST80_APPX_DIR}/bin"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ST80_APPX_DIR}/Assets"
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:st80-win>"
            "${ST80_APPX_DIR}/bin/st80-win.exe"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/LICENSE"
            "${ST80_APPX_DIR}/LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES"
            "${ST80_APPX_DIR}/THIRD_PARTY_LICENSES.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/packaging/windows/Assets"
            "${ST80_APPX_DIR}/Assets"
    DEPENDS st80-win
    COMMENT "Staging MSIX/AppX layout at ${ST80_APPX_DIR}"
    VERBATIM)
