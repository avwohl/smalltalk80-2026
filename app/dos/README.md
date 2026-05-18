# st80-2026 — MS-DOS / FreeDOS frontend

A single-file `st80.exe` (DJGPP COFF + go32-v2 stub, 32-bit DPMI
client) that boots the Xerox Blue Book v2 image to a VESA desktop
with an INT 33h mouse and INT 16h keyboard. This is the fifth
frontend; it speaks the same pure-C `Bridge.h` API as the
Apple / Linux / Windows ones and shares the whole VM core.

There are two ways to run it, with very different support levels:

  1. Primary — `dosiz` on a modern host. `dosiz`
     (`C:\temp\src\dosiz`) is an in-process dosbox-staging build with
     a full ring-3 DPMI host. This is the developer and CI loop:
     `dosiz st80.exe snapshot.im`. No FAT image, no CWSDPMI, no
     DOSBox-X config. See `docs/dos-plan.md`.
  2. Secondary — real DOS / FreeDOS / a Win9x DOS box. Period-
     authentic hardware. You supply the DPMI host; we don't ship
     one. This README's "Run on real DOS" section covers it.

The authoritative design + phase plan is `docs/dos-plan.md`. This
file is the build/run guide only.

## What you need to build

A DJGPP v2 cross-compiler on `PATH`, named `i586-pc-msdosdjgpp-gcc`
/ `-g++` (the `cmake/toolchain-djgpp.cmake` file looks for exactly
those). Use the same build dosiz's own DJGPP regression suite
tracks so toolchain drift isn't a variable:

  * Linux / macOS: `andrewwutw/build-djgpp` v3.4 into `~/djgpp`,
    then `export PATH=$PATH:~/djgpp/bin`.
  * Windows: the Delorie prebuilt (`gcc122b.zip` + binutils from
    <https://www.delorie.com/pub/djgpp/current/>) unpacked to
    `C:\DJGPP`, with `C:\DJGPP\BIN` on `PATH` and
    `DJGPP=C:\DJGPP\DJGPP.ENV` set; or the andrewwutw cross build
    under MSYS2. (DJGPP libc quirks for our use are documented in
    `C:\temp\src\dosiz\docs\c-toolchain-guide.md`.)

CMake ≥ 3.20 on the build host (the same one the other ports use).

## Build

    cmake -S . -B build-dos \
          -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-djgpp.cmake
    cmake --build build-dos --target st80

Output: `build-dos/app/dos/st80.exe` — a COFF executable with the
go32-v2 stub, runnable under any DPMI 0.9+ host. The toolchain file
sets `CMAKE_SYSTEM_NAME=MSDOS`, which trips CMake's `DJGPP=1`; only
then does the root `CMakeLists.txt` add `src/platform/dos` +
`app/dos`. A normal host configure never enters that branch, so the
other four ports build exactly as before.

The headless trace2 gate (Phase D1) is a separate target:

    cmake --build build-dos --target st80_run

## Run under dosiz (primary)

    dosiz build-dos/app/dos/st80.exe snapshot.im

Useful flags:

    st80.exe --probe          print the VBE + mouse probe and exit
                              (stays in text mode — Risk #2 tool)
    st80.exe --no-display     headless: run cycles + exit (CI parity
                              with the Win32 --no-window path)
    st80.exe --cycles-per-frame N    VM cycles between frames (4000)
    st80.exe --scale NUM/DEN  mouse pixels per mickey (default 1/1;
                              lower it if the pointer is too fast)
    st80.exe --help

On Windows, `dosiz.exe` needs its MSYS2 MinGW64 DLLs — run it from
an MSYS2 MINGW x64 shell or copy the DLLs next to it.

## Run on real DOS / FreeDOS (secondary)

You must provide a DPMI host. None is bundled (CWSDPMI is GPLv2,
HDPMI32 is freeware; neither belongs in an MIT repo's release).
Any one of these works:

  * `CWSDPMI.EXE` in the current directory or on `PATH` — DJGPP's
    go32-v2 stub auto-loads it.
  * `HDPMI32.EXE` run once before `st80.exe`.
  * A Windows 95/98 "MS-DOS Prompt" — DPMI is already present.

Plus, from the minimum system floor in `docs/dos-plan.md`:

  * 386DX or better, ≥ 4 MiB free extended memory.
  * A VESA 2.0 BIOS exposing a **linear** framebuffer with ≥ 4 MiB
    VRAM (the 1024-wide Blue Book display does not fit EGA/CGA; the
    frontend refuses to run without a usable LFB mode rather than
    silently dropping to a broken display).
  * A Microsoft-compatible mouse driver loaded (e.g. CTMOUSE). The
    image is unusable without a mouse, so `st80.exe` exits with an
    explanatory message if INT 33h reports none.

Then:

    st80.exe snapshot.im

or just `run.bat` from the distribution ZIP (see below).

If startup fails, `st80.exe --probe` prints exactly what VESA mode
and mouse it found (or why it gave up) without changing video mode.

## The distribution ZIP (opt-in)

The primary release artifact is just `st80.exe` + `snapshot.im`.
For the real-DOS fidelity path there is an opt-in ZIP target — it
is **not** part of the default build or `cmake --install`:

    cmake --build build-dos --target st80_dos_zip

Produces `build-dos/st80-dos.zip` containing `st80.exe`, `run.bat`,
`ST80.TXT`, this README, and — if you dropped them into
`reference/xerox-image/` before configuring — `SNAPSHOT.IM`,
`SOURCES.ST`, `CHANGES.ST` (8.3 names). No DPMI host inside.

## Three-button mouse mapping

DOS mice are usually two-button, and Smalltalk needs three:

    plain left click      red    (select)
    Shift + left click    blue   (window / frame menu)
    right click           yellow (operate menu)
    middle click          blue   (3-button mice)

Keyboard follows the same decoded-keyboard contract as the other
four frontends: 7-bit ASCII (incl. BS/TAB/CR/ESC) passes through;
forward Delete is 127. Arrow / function keys are intentionally not
mapped so all five ports behave identically.

## Layering note

No `#ifdef` anywhere in this directory or the core. Every DOS line
lives under `app/dos/` (BIOS/DPMI frontend) or `src/platform/dos/`
(`DosHal` / `DosBridge`, portable C++17). CMake's `if(DJGPP)` gate
is the only switch. This is the project's port-structure rule, and
DOS — one thread, no window manager, BIOS I/O — is its hardest
test: if the seam survives here it survives anywhere.
