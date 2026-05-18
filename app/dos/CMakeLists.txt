# st80.exe: MS-DOS / FreeDOS frontend (DJGPP COFF + go32-v2 stub).
#
# The VBE display, INT 33h mouse and INT 16h keyboard glue lives
# here; the portable VM + DosHal/DosBridge come in through st80_dos
# the same way st80-win links st80_windows and the SDL frontend
# links st80_linux. Bridge.h is visible transitively (st80_dos
# exports src/include PUBLIC). No SDL, no GDI, no platform #ifdefs —
# this whole directory is selected only by the root if(DJGPP) gate.
#
# Target name is bare "st80" so the artifact is 8.3-clean st80.exe;
# DOS users type `st80` (or `dosiz st80.exe snapshot.im`).

add_executable(st80
    st80_dos_main.cpp
    VbeDisplay.cpp
    MouseInt33.cpp
    KbdInt16.cpp
    Launcher.cpp
)

target_link_libraries(st80 PRIVATE st80_dos)

# DJGPP's gcc driver links the go32-v2 stub automatically when it
# produces a COFF executable; libc / libstdc++ are implicit. We only
# pin the artifact name so it is always st80.exe regardless of how
# CMake's MSDOS platform module sets the suffix.
set_target_properties(st80 PROPERTIES
    OUTPUT_NAME "st80"
    SUFFIX ".exe"
)

# Installed by the opt-in real-DOS ZIP (cmake/DosPackaging.cmake);
# not part of the default build's install set.
install(TARGETS st80
    RUNTIME DESTINATION .
    COMPONENT dos
    EXCLUDE_FROM_ALL
)
