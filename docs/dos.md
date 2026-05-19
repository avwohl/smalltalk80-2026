# Running st80-2026 on MS-DOS / FreeDOS

A quick-start for *running* the DOS port. For the toolchain and build
internals see [`../app/dos/README.md`](../app/dos/README.md); for the
design and phase plan see [`dos-plan.md`](dos-plan.md).

The DOS build is one self-contained program, `st80.exe` — a DJGPP
COFF binary with a go32-v2 stub (a 32-bit DPMI client). It boots the
1983 Xerox Blue Book v2 image to a VESA desktop driven by an INT 33h
mouse and INT 16h keyboard.


## 1. Get the binary

Two options:

  * Build it (DJGPP cross-compiler required — see
    `../app/dos/README.md` for toolchain setup):

        cmake -S . -B build-dos \
              -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-djgpp.cmake
        cmake --build build-dos --target st80

    Output: `build-dos/app/dos/st80.exe`.

  * Or build the opt-in distribution ZIP (not part of the default
    build):

        cmake --build build-dos --target st80_dos_zip

    Produces `build-dos/st80-dos.zip` — `st80.exe`, `run.bat`,
    `ST80.TXT`, the README, and the image files if you staged them
    before configuring. No DPMI host is bundled (license reasons).

`st80.exe` runs under any DPMI 0.9+ host; nothing else from the build
tree is needed at runtime.


## 2. Get a Smalltalk-80 image

The port runs Xerox's original 1983 virtual image (Mario Wolczko's
v2 image — the same one the macOS/Linux/Windows frontends use):

  * Original: <http://www.wolczko.com/st80/>
  * Pre-split mirror: the "xerox-v2" release at
    <https://github.com/avwohl/st80-images/releases/tag/xerox-v2>

The loader auto-detects byte order, so either the raw big-endian
`VirtualImage` or dbanay's pre-swapped `VirtualImageLSB` works
unchanged (details in [`image-preprocessing.md`](image-preprocessing.md)).

For DOS, copy the image to an 8.3 filename next to `st80.exe` — the
conventional name is `SNAPSHOT.IM`. (The sources/changes files, if
you want them, are `SOURCES.ST` / `CHANGES.ST`.)


## 3. Run it

### Under dosiz (recommended)

[`dosiz`](https://github.com/avwohl/dosiz) is an in-process
dosbox-staging host with a full ring-3 DPMI implementation — the
primary, best-supported runtime. For the interactive GUI use
`--window`:

    dosiz --window st80.exe SNAPSHOT.IM

Notes:

  * Set `DOSIZ_DPMI_RING3=1` for the ring-3 DPMI path.
  * dosiz maps the host current directory to `C:\`; run it from the
    directory holding `st80.exe` and `SNAPSHOT.IM`, using bare 8.3
    names.
  * On Windows, `dosiz.exe` needs its MSYS2 MinGW64 DLLs on `PATH`
    (or copied next to it) — run it from an MSYS2 MINGW x64 shell.
  * Headless tools (`st80_run`, `st80_validate`, `st80_gui_test`)
    run under plain `dosiz` with no `--window`; only the
    interactive desktop needs it (the headless path has no mouse —
    see below).

### On real DOS / FreeDOS / a Win9x DOS box

You supply a DPMI host (none is shipped). Any of:

  * `CWSDPMI.EXE` in the current dir or on `PATH` (the go32 stub
    auto-loads it),
  * `HDPMI32.EXE` run once beforehand,
  * a Windows 95/98 "MS-DOS Prompt" (DPMI already present).

Minimum machine:

  * 386DX or better, at least 4 MiB free extended memory.
  * A VESA 2.0 BIOS with a **linear** framebuffer and >= 4 MiB
    VRAM. The Blue Book display is large; `st80.exe` refuses to run
    rather than fall back to a broken EGA/CGA mode.
  * A Microsoft-compatible INT 33h mouse driver loaded (see below).

Then:

    st80.exe SNAPSHOT.IM

(or `run.bat` from the distribution ZIP).

### Useful flags

    st80.exe --probe              print the VESA + mouse probe and
                                  exit (stays in text mode; use this
                                  first if startup fails)
    st80.exe --no-display         headless: run cycles and exit
    st80.exe --cycles-per-frame N VM cycles between frames (def 4000)
    st80.exe --scale NUM/DEN      mouse pixels per mickey (def 1/1;
                                  lower it if the pointer is too fast)
    st80.exe --help

If startup fails, `st80.exe --probe` reports exactly which VESA mode
and mouse it found, or why it gave up, without changing video mode.


## 4. Mouse — required, and how the buttons map

Smalltalk-80 is unusable without a mouse, so `st80.exe` exits with an
explanatory message if INT 33h reports no driver:

  * Under `dosiz --window` the mouse is provided automatically — no
    driver to install.
  * On real DOS/FreeDOS, load a Microsoft-compatible driver first,
    e.g. **CTMOUSE** (`CTMOUSE.EXE`), or the mouse driver your
    mouse/system ships.
  * Plain `dosiz` (no `--window`) has no mouse — that mode is only
    for the headless command-line tools, not the desktop.

Smalltalk needs three buttons (red = select, yellow = operate menu,
blue = window/frame menu). Most DOS mice have two, so the mapping is:

    plain left click       red     (select / set insertion point)
    Shift + left click     blue    (window / frame menu)
    right click            yellow  (operate menu)
    middle click           blue    (on a true 3-button mouse)

Keyboard is the same decoded contract as the other four frontends:
7-bit ASCII (including Backspace, Tab, Return, Esc) passes through;
forward Delete is 127. Arrow and function keys are intentionally not
mapped, so all five ports behave identically.


## Verifying it works

The DOS port is gated, under dosiz, byte-for-byte against the native
build: the Xerox trace2 reference, a 250 000-cycle deterministic run,
a snapshot save round-trip, and a headless "fake-GUI" interaction.
Run the lot with:

    bash tests/dos_dosiz_gate.sh build-dos \
         reference/xerox-image/VirtualImage \
         reference/xerox-image/trace2

See [`testing.md`](testing.md) for what each check asserts.
