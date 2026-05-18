# st80-2026 — DosPackaging.cmake
#
# Opt-in real-DOS / FreeDOS distribution ZIP. Included only from the
# root if(DJGPP) gate; it adds a *manual* target `st80_dos_zip` and
# nothing else — the default build and `cmake --install` are
# untouched (docs/dos-plan.md Phase D5: primary distribution is the
# bare st80.exe + snapshot.im pair; this ZIP is the secondary
# fidelity path).
#
# Deliberately does NOT bundle a DPMI host. CWSDPMI (GPLv2) and
# HDPMI32 (freeware) are user-supplied; bundling either would drag a
# licensing conversation into an MIT repo for no benefit. The ZIP
# carries st80.exe + a run.bat + a readme; the image / sources /
# changes files are added only if they are present at configure time
# (they are large binaries kept out of git, fetched per
# docs/trace-verification.md).

set(_dos_stage "${CMAKE_BINARY_DIR}/dos-dist")

# run.bat — what a DOS user double-clicks. CWSDPMI auto-loads if it
# is on PATH or in the current dir; otherwise DJGPP prints a clear
# "no DPMI" message, which the readme explains.
file(WRITE "${CMAKE_BINARY_DIR}/run.bat"
"@echo off\r\n\
REM st80-2026 - Smalltalk-80 Blue Book, real-DOS launcher.\r\n\
REM Needs a DPMI host: CWSDPMI.EXE in this folder or on PATH,\r\n\
REM or HDPMI32, or a Windows 9x DOS box. See ST80.TXT.\r\n\
st80.exe %1 %2 %3\r\n")

file(WRITE "${CMAKE_BINARY_DIR}/ST80.TXT"
"st80-2026 - Smalltalk-80 (Xerox Blue Book v2 image) for MS-DOS\r\n\
================================================================\r\n\
\r\n\
Requirements\r\n\
  - 386DX or better, 4 MiB+ free extended memory\r\n\
  - A DPMI 0.9+ host. This ZIP does NOT include one. Options:\r\n\
      CWSDPMI.EXE  (GPLv2, free)  - put it next to st80.exe\r\n\
      HDPMI32.EXE  (freeware)     - run it first, then st80\r\n\
      A Windows 95/98 DOS box     - DPMI is built in\r\n\
  - VESA 2.0 BIOS with a linear framebuffer and 4 MiB+ VRAM\r\n\
  - A Microsoft-compatible mouse driver (e.g. CTMOUSE)\r\n\
\r\n\
Run\r\n\
  run.bat                 (uses snapshot.im in this folder)\r\n\
  st80.exe other.im       (a different image)\r\n\
  st80.exe --probe        (print the VESA/mouse probe, stay in text)\r\n\
  st80.exe --help\r\n\
\r\n\
The primary, supported way to run st80-2026 is under dosiz on a\r\n\
modern host; this real-DOS ZIP is the period-authentic fidelity\r\n\
path. See docs/dos-plan.md in the source repo.\r\n")

# Assemble the staging commands as a list so optional image files
# get copied AFTER make_directory and BEFORE the tar step (PRE_BUILD
# would run before the target wipes/creates the stage dir).
set(_zip_cmds
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_dos_stage}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dos_stage}"
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:st80>" "${_dos_stage}/st80.exe"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/run.bat" "${_dos_stage}/run.bat"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/ST80.TXT" "${_dos_stage}/ST80.TXT"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/app/dos/README.md"
            "${_dos_stage}/README.TXT")

set(_img "${CMAKE_SOURCE_DIR}/reference/xerox-image")
foreach(pair "VirtualImage:SNAPSHOT.IM" "sources:SOURCES.ST"
             "changes:CHANGES.ST")
    string(REPLACE ":" ";" _pp "${pair}")
    list(GET _pp 0 _src)
    list(GET _pp 1 _dst)
    if(EXISTS "${_img}/${_src}")
        list(APPEND _zip_cmds
            COMMAND ${CMAKE_COMMAND} -E copy
                    "${_img}/${_src}" "${_dos_stage}/${_dst}")
    endif()
endforeach()

list(APPEND _zip_cmds
    # chdir into the stage so the zip has no leading path components
    # (DOS unzip tools expect st80.exe at the archive root). The
    # stage dir is created by the make_directory command above, so we
    # cannot use the target's WORKING_DIRECTORY for this.
    COMMAND ${CMAKE_COMMAND} -E chdir "${_dos_stage}"
            ${CMAKE_COMMAND} -E tar cf
            "${CMAKE_BINARY_DIR}/st80-dos.zip" --format=zip "."
    COMMAND ${CMAKE_COMMAND} -E echo
            "Wrote ${CMAKE_BINARY_DIR}/st80-dos.zip")

add_custom_target(st80_dos_zip
    ${_zip_cmds}
    DEPENDS st80
    COMMENT "Staging + zipping the real-DOS distribution -> st80-dos.zip"
    VERBATIM
)
set_target_properties(st80_dos_zip PROPERTIES EXCLUDE_FROM_ALL TRUE)
