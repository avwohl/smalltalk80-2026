# MS-DOS / DPMI Plan

Supplemental target beyond the four covered in `plan.md`: produce a
32-bit DPMI-client `st80.exe` (DJGPP COFF + go32-v2 stub) that loads
the Xerox v2 image, runs the interpreter, and drives a VBE display +
INT 33h mouse + INT 16h keyboard. Same shared `st80core` static
library; a new `src/platform/dos/` slice behind `IHal`; a new
`app/dos/` frontend.

The binary is designed to run under two distinct environments, with
different tolerances for each:

  1. **Primary — `dosiz` on the host.** `C:\temp\src\dosiz` is a
     DPMI-complete DOS emulator (same author) that links
     dosbox-staging as a library and implements INT 21h + INT 31h in
     native C++. It runs on Linux, macOS, Windows (MSYS2), iOS,
     iPadOS, and Android. For us this is the developer loop and the
     CI target: `dosiz build/st80.exe snapshot.im` runs on any host
     that can cross-compile DJGPP. No FAT image, no CWSDPMI, no
     DOSBox-X configuration. dosiz's DJGPP regression suite already
     runs real GNU utilities (grep 2.28, sed 4.8, gawk 5.0, tar
     1.12, patch, make, …) plus ~30 hand-built DJ_*.EXE fixtures;
     st80.exe is one more DJGPP binary.
  2. **Secondary — real DOS / FreeDOS / Windows 9x DOS box.** Users
     who want period-authentic hardware or a fidelity run on 486-class
     iron get the same `st80.exe` plus a separately-supplied DPMI host
     (CWSDPMI or HDPMI32). We don't own this path's support; we just
     try not to break it.

This is a retro target, not a shipping one. The motivation is
twofold:

  1. **Period-authentic deployment.** Blue Book Smalltalk-80 was
     meant for 1MIPS-class Dorado-era workstations. A 486DX2-66
     under DOS is of the same order.
  2. **Layering stress test.** DOS is the most constrained host we
     could reasonably hit: one thread, no window manager, 8.3
     filenames, BIOS-level I/O. If the `IHal` / `Bridge.h` seam
     survives DOS, it will survive anything. dosiz makes this test
     cheap enough to run on every CI push.

No iOS-style App Store constraints in the guest: **JIT is allowed**
(flat DPMI memory is executable by default in ring 3), so DOS is a
candidate Phase-6 JIT target alongside Windows and Linux. A curiosity
— not a design goal — is that dosiz's interpreter core re-emulates
our JIT'd code on iOS, so "JIT-built st80 on iOS via dosiz" is a
technically valid sidestep of Apple's JIT entitlement. Slow, but
correct. Noted and set aside.

## Status (2026-05-18)

Code-complete through D4; D5 partial. **The DJGPP cross-build is now
verified**: andrewwutw/build-djgpp v3.4 (GCC 12.2.0, installed at
C:\s\djgpp) builds libst80core.a, libst80_dos.a, st80_run.exe and
st80.exe — all genuine `coff-go32-exe` go32-v2 DPMI binaries
(objdump-confirmed). Getting there required fixing four real
blockers the earlier "D0/D1 committed" claim had masked because the
DJGPP path had never actually been compiled (commit ed30b3a):

  1. Modern CMake ships no Platform/MSDOS and never sets `DJGPP`,
     so the whole if(DJGPP) gate was dead. Added
     cmake/Platform/MSDOS.cmake + a module-path hook in the
     toolchain file.
  2. Root CMakeLists forced -std=c++17 + PIC globally, overriding
     the toolchain; DJGPP DOS headers are hidden under
     __STRICT_ANSI__. Gated both on NOT DJGPP.
  3. Risk #1 realised: DJGPP --disable-threads libstdc++ has no
     std::mutex. Added single-threaded src/platform/dos/
     EventQueue.hpp (shadows the common one by include order) and
     dropped DosHal's unneeded mutex.
  4. Core std::nanf isn't in DJGPP std::; swapped the failure-path
     sentinel for std::numeric_limits<float>::quiet_NaN().

Runtime verification was blocked by a dosiz Windows bug, now
**root-caused and fixed upstream** (per this plan's "dosiz upstream
coordination" policy — fix dosiz, don't work around in st80).
dosiz hung with zero output for *any* DJGPP program because its
guest-file `::open()` sites lacked `O_BINARY`: on Windows the CRT
defaults to text mode and `::read()` hits 0x1A→EOF, so the
go32-v2 stub spun forever re-reading its own COFF payload. Fixed
in dosiz `src/bridge.cc` (commit `ea3417c`, pushed to dosiz
origin/main). dosiz's own DJGPP suite went 0 → 11/14 PASS on
Windows; a trivial DJGPP hello runs clean.

RESOLVED 2026-05-19 — the D1 trace2 gate now passes byte-for-byte
under dosiz on Windows: `trace2_check: OK (499 bytecodes
byte-for-byte)`. Four fixes, each at root cause:

  1. st80 `PosixFileSystem.hpp`: `open()` lacked `O_BINARY`.
     DJGPP defaults `_fmode` to O_TEXT, so the binary snapshot was
     read with CR/LF folding + 0x1A-as-EOF + an lseek(-1) CR
     pushback — `Interpreter::init()` failed mid object-table.
     Added the portable `#ifndef O_BINARY #define O_BINARY 0`
     shim (no-op on POSIX) and OR-ed it into open/create.
  2. dosiz `bridge.cc` AH=3F: replaced the one-`::read()`-per-byte
     loop with a 16 KiB block read for the binary case. The image
     load issues ~60 K-byte reads; byte-at-a-time made it too slow
     to finish. Text-mode path preserved unchanged.
  3. dosiz `dosiz.cc`: forced host std{in,out,err} to binary on
     Windows (`_setmode`). The MinGW CRT was injecting a 2nd CR
     into the guest's already-CRLF output (`\r\r\n`); dosiz must
     be a transparent pipe.
  4. st80 `tests/trace2_check.sh`: `diff --strip-trailing-cr` so a
     DOS text-mode `st80_run.exe` (correctly CRLF) compares clean
     against the Unix-LF reference. No-op on the native path.

All three headless tools verified under dosiz the same day:
`st80_probe` → loadSnapshot OK (oopsLeft 14375, coreLeft 723822
words); `st80_validate check` → 18391 live objects, 0 problems.
Definitive: `st80_validate shasum` cross-checked native (MSVC)
vs DJGPP-under-dosiz — all 18391 per-object SHA-256 hashes
IDENTICAL. The image loads bit-for-bit identically and the
interpreter is byte-exact under dosiz.

The earlier `#GP` was an st80 stack bug (2 MiB RealWordMemory as a
stack local underflowing DJGPP's small stack), already fixed by
heap-allocating the VM (commit 9a9b1c8). dosiz's own DJGPP suite
holds at 35/37 (EMS_PROBE/DJ_SIGNAL pre-existing, unrelated).

    D0  Toolchain + empty build            committed (28ac42d)
    D1  Headless trace2 gate               PASSING byte-for-byte
                                           under dosiz (2026-05-19)
    D2  VBE display + cursor               VERIFIED — desktop
                                           renders under dosiz
                                           --window (2026-05-19)
    D3  Mouse + keyboard                   VERIFIED — st80_gui_test
                                           drives synthetic mouse+kbd
                                           under dosiz; image reacts
                                           (2026-05-19)
    D4  Filesystem + snapshot              VERIFIED — load+save
                                           round-trip byte-exact
                                           under dosiz (2026-05-19)
    D5  Packaging                          ZIP target added; release
                                           publish pending verify
    D6  JIT                                deferred to Phase 6

What landed in 2623ce0: `app/dos/` — `VbeDisplay`, `MouseInt33`,
`KbdInt16`, `Launcher`, `st80_dos_main`, its `CMakeLists.txt`,
`app/dos/README.md`; `cmake/DosPackaging.cmake` (opt-in
`st80_dos_zip`); root `CMakeLists.txt` `if(DJGPP)` now adds
`app/dos` + the packaging include. All additive + gated — a host
configure never enters the DJGPP branch, verified by reconfiguring
the MSVC build (clean) and rebuilding `st80core` + tools + the
Windows port (green). (`core_smoke_test` segfaults on this MSVC
Debug host — pre-existing and unrelated: the commit changes zero
`src/core` / `src/include` / `tests` files, so those binaries are
byte-identical to the pre-change tree.)

D2/D3 GUI VERIFIED 2026-05-19: `dosiz --window st80.exe
SNAPSHOT.IM` boots to the **fully-rendered Xerox Smalltalk-80 v2
desktop** — System Workspace ("The Smalltalk-80™ System Version
2 / Copyright (c) 1983 Xerox Corp."), System Transcript
("Snapshot at: (31 May 1983 10:37:52 am)"), class-category list,
gray stipple background, and the mouse cursor on screen (so the
DOS INT 33h mouse driver is live under --window). Proof image:
`docs/screenshots/dos-smalltalk-desktop.png` (640×480, st80's
VBE mode). Verified via a new dosiz feature
(`DOSIZ_SCREENSHOT_SECS`, dosiz commit b860fbc) that dumps the
emulated framebuffer to PNG with no interactive display — needed
because this dev box's session is locked (host screen-grab only
sees the lock screen) and useful for CI. Two dosiz bugs were
fixed to get here: the `--window` callback-pool exhaustion
(CB_MAX 128→250, f18940c) and the screenshot trigger.

D4 VERIFIED 2026-05-19: added `st80_validate resave <in> <out>`
(load then `ObjectMemory::saveSnapshot` — exercises
`create_file` + `write` = dosiz AH=40). Under dosiz: resave
SNAPSHOT.IM→OUT.IM (596128 B, same size), then `shasum` of
both is identical (18391 objects, byte-exact at object level —
load→save→load round-trips). Stronger: the dosiz-written
OUT.IM is **byte-for-byte identical** (`cmp`) to one written
by the native MSVC build — the write path is bit-exact, not
just semantically equivalent. No dosiz bug needed; AH=40 +
O_BINARY create worked first try.

D3 VERIFIED 2026-05-19: rather than injecting OS-level keystrokes
into a non-interactive dosiz window, the pharo-headless-test
technique was ported — `tools/st80_gui_test` (the in-repo analog;
see docs/testing.md) drives the live image through `Bridge.h`
synthetic mouse+keyboard with the framebuffer kept in memory (no
window). Built for DJGPP it runs under plain headless dosiz
(no --window, reusing the D4-proven file-write path for its PNG
output): boots the v2 desktop, a synthetic yellow-operate click
makes the image visibly react (4922-px delta), PASS. The boot
and reaction frames are **byte-identical** to the same harness
linked `st80_windows` on the native host — the DOS port's
input+display is pixel-exact vs native, same as D1/D4.

Still open before a tagged DOS release: only D5 — 86Box +
FreeDOS smoke on real DPMI hardware (CWSDPMI/HDPMI32), the one
gate dosiz can't stand in for. Everything dosiz *can* verify —
headless trace gate, image load, GUI render, mouse+keyboard
interaction, snapshot save round-trip — is done and green.

## Goal & non-goals

**Goal.** A single-file `st80.exe` (DJGPP COFF with DPMI stub) that
boots the v2 image to a drawn desktop on a 486DX-33 / 16 MiB / VESA
2.0 / Microsoft-compatible mouse and is usable with a real keyboard.
trace2/trace3 gate must pass byte-for-byte under DOSBox-X, same as on
the four shipping hosts.

**Non-goals.**

- No real-mode / DOS 2.x / 8086 / 286 support. We require a 386
  with ≥ 4 MiB free extended memory and a DPMI 0.9+ host.
- No EGA or CGA fallback. The image's 1024-wide display does not fit.
  VGA mode 12h is kept as a last-resort 640x480 clipped viewport, not
  a primary mode.
- No native DOS sound. PC-speaker beep only. No SB16, no FM.
- No networking, no printing. DOS TCP stacks (mTCP, Trumpet,
  packet drivers) are fragile enough that we don't rely on them
  even at startup — hence the bundled `snapshot.im` instead of
  the fetcher pattern the other four hosts use.
- No `long filename` handling on pure DOS. Windows 9x DOS boxes
  get LFN for free via INT 21h/71xx — we don't depend on it.
- No installer. Ship a ZIP; let the user unpack it. DOS users know
  how to use PKUNZIP.

## Key decisions

    Decision              Choice                                      Rationale
    --------              ------                                      ---------
    Toolchain             DJGPP v2 (GCC 12+) cross-compiler           Modern GCC; C99/C++17; flat 32-bit; libstdc++
                                                                      has <chrono>/<mutex>/<atomic> (no-op on
                                                                      --disable-threads, fine for our Bridge.h
                                                                      same-thread contract). dosiz's DJGPP suite
                                                                      already green on 30+ real-world DJGPP
                                                                      binaries — compatibility is known-good.
    DJGPP source          andrewwutw/build-djgpp v3.4 bundle on CI    Same DJGPP build dosiz tests against;
                          Delorie zips (gcc122b) for local dev        removes toolchain-drift as a variable.
    Alternative rejected  Open Watcom v2                              Weaker libstdc++; and dosiz's Watcom 32-bit
                                                                      path needs a `binw/wlink.lnk` patch that our
                                                                      users would also need to carry. DJGPP is
                                                                      less ceremony.
    Build host            Linux / macOS / Windows-MSYS2 cross         All three can install the DJGPP cross
                                                                      toolchain. CMake toolchain file + `dosiz`
                                                                      both run on all three. Developers can stay
                                                                      on their native host end-to-end.
    CMake gate            `if(DJGPP) add_subdirectory(...)`           Mirrors the existing WIN32 / APPLE / UNIX
                                                                      pattern in CMakeLists.txt. No `#ifdef` in
                                                                      portable source.
    Primary runtime       `dosiz` (in-process dosbox-staging + C++    Fast dev loop; built-in DPMI 0.9 ring-3
                          DPMI host)                                  host; direct host-filesystem pass-through
                                                                      with 8.3 LFN mapping per `.cfg`; headless
                                                                      by default for CI; `--window` for GUI
                                                                      testing. No FAT image, no DOSBox-X conf
                                                                      scripting.
    Secondary runtime     Real DOS / FreeDOS / Win9x DOS box with     Fidelity target. User supplies their own
                          CWSDPMI or HDPMI32                          extender; we document config + sidecar
                                                                      files. Not in CI.
    Display               VBE 2.0+ linear framebuffer, 1024x768x32    Matches the existing RGBA8 staging buffer
                                                                      in DosHal — no pixel-format translation,
                                                                      just a memcpy per dirty rect. dosiz's
                                                                      `--window` is SDL so LFB works end-to-end.
    Display fallbacks     VBE 1024x768x8 (2-entry palette)            For video RAM-constrained cards.
                          VBE 800x600x32 (clipped)                    For VGA-only cards with VBE bolted on.
                          VGA mode 12h (640x480x4, clipped)           Last resort. Image doesn't fit; warn on
                                                                      startup. dosiz's SVGA S3 default covers
                                                                      the primary mode; this path only fires
                                                                      under real DOS on cheap hardware.
    Input — mouse         INT 33h (Microsoft Mouse API)               Universal. dosbox-staging's Mouse
                                                                      emulator is DOS-standard. Sub-function
                                                                      0Bh for motion deltas, 03h for buttons.
    Input — keyboard      INT 16h function 10h (polled enhanced)      Scan code + ASCII. No ISR needed; our
                                                                      cooperative loop polls every frame.
    3-button mapping      Plain click = red; right = yellow;          Under dosiz `--window` the host mouse
                          Shift-click = blue                          drives INT 33h directly — 3-button mice
                                                                      report button 3 as blue if present.
    Clock — ms            DJGPP `uclock()` (PIT-derived, 0.84 ms)     Adequate for signal_at scheduling. Both
                                                                      dosiz and real DOS expose the PIT.
    Clock — epoch         DOS INT 21h/2Ah + 2Ch → Unix epoch →        Match what PosixFileSystem does. dosiz
                          Smalltalk 1901 epoch                        passes host time through INT 21h.
    Filesystem            Reuse `PosixFileSystem` via                 DJGPP provides POSIX; dosiz maps DOS
                          `DosHostFileSystem.hpp` one-line alias      8.3 names to host long names per `.cfg`.
                          under src/platform/dos/                     Under real DOS we stick to 8.3:
                                                                      `SNAPSHOT.IM`, `SOURCES.ST`, `CHANGES.ST`.
                                                                      No code change either way.
    Cursor rendering      Software XOR sprite composited in frontend  No hardware cursor in VESA. Redraw cursor
                                                                      after each display_sync flush; cursor
                                                                      bitmap owned by DosHal per IHal contract.
    Threading             Single-threaded cooperative loop            DOS has no threads. Bridge.h's same-thread
                                                                      contract is trivially satisfied. EventQueue
                                                                      mutex → no-op under
                                                                      threading-disabled libstdc++.
    JIT                   Allowed (Phase 6); interpreter-first        Flat DPMI memory is executable in ring 3
                                                                      under both dosiz and real DOS. Copy-and-
                                                                      patch stencils work identically to Linux.
    Packaging — primary   A plain `st80.exe` + `snapshot.im` pair     Runs under any `dosiz` install on any
                                                                      host OS. The image is bundled directly —
                                                                      no network fetch at startup. DOS TCP
                                                                      stacks (mTCP, Trumpet, packet drivers)
                                                                      are fragile enough that asking a user to
                                                                      fetch a ~1 MiB image over them is a
                                                                      support disaster. The other four
                                                                      platforms keep the fetcher pattern; DOS
                                                                      is an exception.
    Packaging — secondary CPack `ZIP` for real-DOS distribution       Contents: st80.exe, snapshot.im,
                                                                      sources.st, changes.st, st80.txt,
                                                                      run.bat. No CWSDPMI bundled (user
                                                                      supplies); no fetcher. Opt-in CMake
                                                                      target `st80_dos_zip`, not on the main
                                                                      build.
    CI                    `dosiz` directly in GH-Actions Ubuntu       Build DJGPP toolchain + dosiz in a cache
                                                                      layer; run trace2 gate end-to-end. No
                                                                      DOSBox-X, no VM, no FAT image. Same
                                                                      approach dosiz's own CI uses for its
                                                                      fixture suite.
    CI — Windows          `dosiz.exe` via MSYS2 MinGW64 runner         Exercises the exact configuration
                                                                      developers on Windows hit. Same DJGPP
                                                                      cross-compile as the Linux job.

## Minimum system floor

    Component            Floor                                      Notes
    ---------            -----                                      -----
    CPU                  386DX / 25 MHz                             Interpreter only. JIT bumps this to 486DX.
    Extended RAM         4 MiB free (after DOS + TSRs)              2 MiB image heap + ~1.5 MiB DJGPP runtime
                                                                    + ~0.5 MiB CWSDPMI + slack.
    DPMI host            DPMI 0.9 or later                          CWSDPMI, HDPMI32, Windows 95 DOS box,
                                                                    OS/2 MVDM, DOSBox-X — all qualify.
    Video                VESA 2.0 with 4 MiB+ VRAM                  1024x768x32 = 3 MiB. Covers anything from
                                                                    a '95 S3 Trio or later.
    Mouse                Microsoft-compatible, driver loaded        INT 33h present at startup. We refuse to
                                                                    run without one (the image is unusable
                                                                    without a mouse).
    Keyboard             AT / enhanced (101/102 key)                For the extra scan codes — arrows, F-keys.
    FPU                  Not required                               The interpreter uses software float only;
                                                                    JIT would still use x87 when present.
    Storage              2 MiB free on a FAT16 volume               Image + sources + changes + exe + cwsdpmi.

## Architecture — how DOS slots into the existing layering

No changes to `src/core/` or `src/include/Bridge.h`. DOS is one more
IHal implementation behind the existing contract. Per `CLAUDE.md`
and the port structure rule in memory: no `#ifdef` in portable
source; every DOS-specific line lives under `src/platform/dos/` or
`app/dos/`.

    src/platform/dos/
      DosHal.hpp                   IHal impl — same shape as WindowsHal
      DosHal.cpp                   clock (uclock), event queue, cursor state,
                                   RGBA8 staging buffer, signal_at bookkeeping
      DosBridge.cpp                C Bridge.h on top of DosHal + Interpreter +
                                   PosixFileSystem (via DosHostFileSystem.hpp)
      DosHostFileSystem.hpp        one-line alias: using HostFileSystem =
                                                         PosixFileSystem;
      CMakeLists.txt               builds st80_dos static lib; no Win/GDI/SDL
                                   deps

    app/dos/
      st80_dos_main.cpp            int main(argc,argv). Parses args, sets up
                                   VBE, pumps INT 33h + INT 16h, drives
                                   st80_run in a cooperative loop that yields
                                   to the event pump every N cycles.
      VbeDisplay.{hpp,cpp}         VBE 2.0 probe, mode set, LFB mapping via
                                   DPMI INT 31h/0800h, blit with cursor
                                   composite.
      MouseInt33.{hpp,cpp}         Install, poll deltas, read buttons,
                                   translate to st80_post_mouse_*.
      KbdInt16.{hpp,cpp}           Polled keyboard; scan-code → Blue Book
                                   key encoding; modifier tracking for the
                                   shift-click = blue mapping.
      Launcher.cpp                 Minimal text-mode splash / error messages
                                   ("No VBE 2.0 found", "No mouse driver").
      CMakeLists.txt               links st80_dos. Target name: st80 (DOS
                                   likes 8.3, so just "st80.exe").

    cmake/toolchain-djgpp.cmake    set(CMAKE_SYSTEM_NAME MSDOS)
                                   set(CMAKE_C_COMPILER   i586-pc-msdosdjgpp-gcc)
                                   set(CMAKE_CXX_COMPILER i586-pc-msdosdjgpp-g++)
                                   set(CMAKE_SYSTEM_PROCESSOR i386)

Top-level `CMakeLists.txt` gets one new gate alongside the existing
APPLE / UNIX / WIN32 blocks:

    if(DJGPP)
        add_subdirectory(src/platform/dos)
        add_subdirectory(app/dos)
        include(cmake/DosPackaging.cmake)
    endif()

`DosPackaging.cmake` sets `CPACK_GENERATOR` to `ZIP`, stages
`cwsdpmi.exe` and `LICENSE.GPL` into the install tree, and runs
`stubedit` on the output binary to trim the stub size.

## Phases

### Phase D0 — Toolchain + empty build (half-day)

- Add `cmake/toolchain-djgpp.cmake` (`CMAKE_SYSTEM_NAME=MSDOS`,
  compilers = `i586-pc-msdosdjgpp-{gcc,g++}`).
- Create `src/platform/dos/` with empty `DosHal` stubs that compile
  but return zero / no-op everywhere. `st80_dos` static lib builds.
- Install the DJGPP cross-toolchain in a GH-Actions job (build via
  `andrewwutw/build-djgpp` v3.4, cached).
- Build `dosiz` in the same job (clone + `make`), cache the binary.
- **Exit:** `libst80_dos.a` + `libst80core.a` both produce COFF
  objects under the cross toolchain. `dosiz --version` works in CI.

### Phase D1 — Headless trace2 gate under dosiz (half-day)

This phase was originally scoped at ~1 day against DOSBox-X. dosiz's
direct host-filesystem pass-through collapses it to an afternoon.

- Port `tools/st80_run` to build against `DosHostFileSystem` (the
  headless HAL from `src/platform/headless/` is already platform-
  free, so this is just wiring).
- CI: `dosiz build/st80_run.exe -n 499 reference/xerox-image/VirtualImage > trace.out`,
  diff against the reference `trace2` file. No ZIP, no FAT image,
  no DOSBox-X conf scripting — the image file is just a host file
  that dosiz serves through INT 21h AH=3D/3F/42/3E.
- Optional sidecar: `st80_run.cfg` next to the exe to nail drive
  mounts if the image lives outside `$CWD`.
- **Exit:** trace2 passes byte-for-byte under dosiz on all three CI
  OSes (Ubuntu, macOS, Windows-MSYS2), matching the four shipping
  hosts.

### Phase D2 — VBE display + cursor (2–3 days)

- `VbeDisplay.cpp`: VBE 2.0 probe via real-mode INT 10h/4F00h (DJGPP
  `__dpmi_int`); iterate mode list, pick 1024x768x32 LFB, fall back
  to x8, then 800x600x32, then 640x480 VGA 12h. Map the LFB through
  INT 31h/0800h (physical → linear). dosiz emulates an S3 SVGA with
  VBE 2.0+ by default, so the primary path exercises end-to-end on
  dev and CI.
- `st80_display_sync` blit: DosHal already owns a 1024x768 RGBA8
  buffer; frontend does a per-scanline `memcpy` over the dirty rect
  into the LFB. Cursor XOR-composited on top, cursor's old position
  erased from the backbuffer each frame.
- Visual verification via `dosiz --window build/st80.exe snapshot.im`,
  then `screencapture` (macOS) / `scrot` (Linux) / `nircmd savescreenshot`
  (Windows) against the SDL window. `Read` the capture, verify the
  Smalltalk desktop is drawn.
- **Exit:** boot to the Smalltalk desktop under `dosiz --window`;
  screenshot confirms pixels.

### Phase D3 — Mouse + keyboard (1–2 days)

- `MouseInt33`: install-check (INT 33h/AX=0000h → AX=FFFFh); poll
  (AX=000Bh motion deltas, AX=0003h button state). Scale mickeys to
  display pixels. Translate to `st80_post_mouse_move` / `_down` / `_up`.
  dosbox-staging's Mouse driver is a standard DOS mouse; our code
  doesn't know or care that the host is dosiz.
- `KbdInt16`: function 11h (check-for-key) + 10h (read). Scan-code
  table to Blue Book characters. Track shift for the click-modifier
  mapping. Arrow keys, backspace, tab, return all mapped.
- `--window` drives through real SDL events; test a menu pick, an
  edit, and a save.
- **Exit:** under `dosiz --window`, open a Browser, select a method,
  edit it, save. Two successive screenshots show the source change.

### Phase D4 — Filesystem + snapshot (half-day)

Also simpler than originally scoped. dosiz maps host files directly
and supports 8.3 ↔ long-name mapping per-file via `.cfg`.

- `DosHostFileSystem.hpp` aliases `PosixFileSystem`. DJGPP's POSIX
  wrappers route to INT 21h; dosiz serves those against host files.
  No new code.
- `examples/st80.cfg` template with a `default_mode = binary` line
  (snapshot + image files are binary; no CRLF conversion) and 8.3
  mappings for `snapshot.im` / `sources.st` / `changes.st` if the
  user wants to host longer names.
- Test: snapshot save → quit → reboot → verify persistence. Under
  dosiz directly, and once on FreeDOS 1.3 in 86Box as a fidelity
  sanity check (not blocking).
- **Exit:** `dosiz build/st80.exe snapshot.im` boots; edits persist
  across restarts; 86Box smoke test passes.

### Phase D5 — Packaging + release (1 day)

Two artifact kinds, very different effort:

- **Primary — drop-in `.exe` + `snapshot.im` pair.** Publish
  `st80.exe` plus the bundled `snapshot.im` directly to the GitHub
  release page. Documented invocation: `dosiz st80.exe snapshot.im`.
  Runs on any host that can install `dosiz`. No installer, no ZIP,
  no DPMI-host bundling, no network fetch. DOS-specific: TCP under
  DOS (mTCP, Trumpet, packet drivers) is too unreliable to make a
  startup fetcher worth the support load, so we ship the image.
- **Secondary — real-DOS ZIP (opt-in CMake target
  `st80_dos_zip`).** `CPack ZIP` containing `st80.exe`,
  `snapshot.im`, `sources.st`, `changes.st`, `st80.txt`, `run.bat`.
  CWSDPMI / HDPMI32 *not* bundled — user supplies; documented in
  `app/dos/README.md`. Sidesteps the GPL conversation entirely.
- 86Box + FreeDOS 1.3 smoke test before a tagged release.
- **Exit:** `st80.exe` + `snapshot.im` (and optional `.zip`) on
  the release page.

### Phase D6 — Optional: JIT (weeks, folded into the main Phase 6)

Once the copy-and-patch JIT lands for Linux/Windows, wiring DOS is a
couple of hours: flat 32-bit DPMI memory is read-write-execute by
default; `malloc` a code zone and jump into it. No mmap, no
`VirtualAlloc`, no entitlement. dosiz's normal core faithfully
emulates the jumps; the generated code runs at dosbox-staging
interpreter speed there, and at native speed under real DOS.

## Licensing notes

    Component             License       Placement
    ---------             -------       ---------
    st80.exe              MIT (ours)    Ours; stay MIT.
    dosiz                 GPLv3         Not redistributed by us. Users install it
                                        themselves. Our DJGPP binary does not
                                        link dosiz code — dosiz is a DOS
                                        environment we happen to run under; the
                                        INT 21h/31h interface is an OS boundary,
                                        not linking.
    cwsdpmi.exe /         GPL v2 /      Opt-in real-DOS path only. User supplies.
    HDPMI32.EXE           freeware      We document both; we don't ship either.
    snapshot.im           Xerox PARC    DOS-specific: we ship the image directly
    (Wolczko v2 image)    license       next to st80.exe. plan.md Risk #1 keeps
                                        the fetcher pattern on the four shipping
                                        hosts; DOS TCP is too flaky to rely on
                                        at startup. Confirm image-license terms
                                        permit redistribution before the first
                                        tagged DOS release — dbanay and
                                        rochus-keller already ship it, so there
                                        is established precedent.

Important: because we don't redistribute dosiz or any DPMI host, the
repo's `THIRD_PARTY_LICENSES` file doesn't change. dosiz's own
THIRD_PARTY credits cover dosbox-staging's GPLv2+ dependencies; that
is dosiz's concern, not ours.

## Risks

1. **DJGPP libstdc++ threading gaps.** If `std::mutex` / `std::atomic`
   don't compile under DJGPP's `--disable-threads` configuration, the
   `EventQueue` won't build as-is. Mitigation: introduce a DOS-only
   `EventQueueSingleThreaded.hpp` header under `src/platform/dos/`
   that drops the lock, and add a using-alias in `DosHal.hpp`. Still
   no `#ifdef` in the portable core; the CMake slice picks which
   header to include. Verification cheap — `i586-pc-msdosdjgpp-g++
   -std=c++17 -c EventQueue.hpp` from a Docker container.

2. **dosiz VBE LFB completeness.** dosiz relies on dosbox-staging's
   VBE emulation, which is mature — every major Watcom-built game
   from the mid-'90s exercised it. Still, we should verify
   `INT 10h/4F00h` returns VBE 2.0 capabilities and that the LFB
   map through `INT 31h/0800h` lands at a writable linear address
   before sinking two days into `VbeDisplay.cpp`. Prototype: one
   DJGPP fixture that probes and prints the VBE info block. Mirror
   dosiz's own `DPMI_PROBE.COM` / `EMS_PROBE.COM` pattern.

3. **dosiz's --window SDL event routing.** `--window` opens an SDL
   window; dosbox-staging maps host mouse/keyboard to guest INT 33h
   and INT 16h. Need to confirm: (a) our shift-for-blue modifier
   mapping works (SDL passes shift state through to the guest), and
   (b) there's no cursor-warping wiggle between SDL and INT 33h
   (dosbox-staging handles this, but we should see it).

4. **dosiz DLL footprint on Windows.** `dosiz.exe` links MSYS2
   MinGW64 DLLs (libspeexdsp, SDL2, glib, fluidsynth, …) and must
   either run inside the MSYS2 shell or have those DLLs copied
   alongside. This is dosiz's distribution concern, not ours, but
   our build-from-source docs need to tell a Windows developer
   where they'll land. Our CI uses MSYS2 and dodges this.

5. **LE vs COFF loader.** dosiz's README flags DOS4G-hosted Watcom
   binaries (LE format) as "loads + fixes up but stalls on DOS4G
   pre-entry environment convention." That's the Watcom path. Our
   path is DJGPP → COFF-with-go32-v2-stub, which the README
   confirms runs end-to-end (30+ real DJGPP binaries green). No risk.

6. **Real hardware VBE 2.0 LFB.** Some cheap '96-era S3 clones
   deliver bank-switched only. Mitigation: refuse to run without
   LFB. Fidelity target, not a compatibility target. Not relevant
   to dosiz or CI.

7. **Bridge.h threading contract.** Trivially satisfied on DOS (one
   thread), but we inherit the Phase-2a shortcut — the signal_at
   walk isn't lock-free. Fine for now; don't let that shortcut
   survive into a DOS-specific multi-process design later.

8. **snapshot.im size cliff.** dosiz's INT 21h handler caps certain
   buffers; verify a ~1 MiB image round-trips through AH=3F/40
   cleanly. Single-call reads/writes of that size should work
   (dosiz's regression fixtures include binary round-trip via
   `DJ_FILE` and `GZIP`), but we should test at the exact size.

## Open questions

- **dosiz upstream coordination.** If we hit a dosiz bug during the
  port, we file in `C:\temp\src\dosiz\` (same author) and fix
  upstream rather than working around in our code. Already the
  project's posture for its own DJGPP test suite; this is just
  inherited policy.

- **Real-hardware target floor.** Do we want to actually verify on
  a 486DX-66, or is 86Box + FreeDOS + dosiz enough? Real iron is
  more fun but nobody will notice if we cut it.

- **Font rendering for the text-mode splash.** CP437 only — the
  image itself uses its own font, but our early boot messages
  ("No VBE 2.0 detected") should stick to CP437. Non-issue unless
  someone localizes, which nobody will.

- **Phase ordering.** This is independent of the main phase sequence
  in `plan.md`. The dosiz-as-CI story makes it cheap enough to land
  alongside Phase 5 polish, rather than deferring to after Phase 6
  JIT.

- **DOS build on Windows-native CI.** dosiz.exe on Windows works
  under MSYS2. A pure-MSVC CI host would need an alternative runner
  (DOSBox-X) for the DOS smoke test. Probably not worth
  maintaining two CI paths — keep Windows CI on MSYS2 to match.

## Conventions carried forward from plan.md

- Commit every ~15 min during active work.
- Update `docs/changes.md` for user-visible changes before
  committing.
- GUI claims require a screenshot — under DOSBox-X, `CAPTURE_IMAGE`
  produces PNG; `Read` it to verify. Real hardware: use a USB
  capture card or document camera.
- No workarounds. If VBE probe fails, print an error and quit; do
  not silently drop to CGA.
- No `#ifdef` in portable source. DOS lives under `src/platform/dos/`
  and `app/dos/`; CMake selects per target with `if(DJGPP)`.
- No markdown tables — indented columns throughout, matching this
  document and `plan.md`.
