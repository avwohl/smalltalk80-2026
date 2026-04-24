# WIP — MS-DOS / DPMI port

Work-in-progress snapshot. Session dated 2026-04-24.
Delete this file once Phase D5 ships.

## Where we are

Phase D0 (scaffolding) and Phase D1 (tooling wire-up) landed in two
commits. Phase D2 (VBE display) has not started. Nothing
user-visible yet — no changes.md bump.

Recent commits on `main`:

    ddf9a1b  dos: wire st80_run for DJGPP, let trace2_check drive via dosiz
    28ac42d  dos: add DPMI plan doc, DJGPP toolchain file, and platform slice scaffold

## What this port is

A DJGPP-cross-compiled `st80.exe` (go32-v2 stub + COFF payload) that
boots the Xerox v2 image. Primary runtime is **dosiz**
(`C:\temp\src\dosiz`) — an in-process dosbox-staging + C++ ring-3
DPMI host by the same author that serves INT 21h against host files.
Secondary runtime is real DOS / FreeDOS with CWSDPMI or HDPMI32.

We do NOT ship a DPMI host. dosiz provides one for the dev/CI loop;
real-DOS users supply their own.

We DO ship the image bundled with the .exe on DOS (unlike the other
four platforms, which use a fetcher). Reason: DOS TCP stacks are
flaky enough that a startup download is a support hole.

Full plan is in `docs/dos-plan.md` — keep it authoritative.

## What's committed

- `cmake/toolchain-djgpp.cmake` — cross-toolchain file. Picks up
  `i586-pc-msdosdjgpp-{gcc,g++}` off PATH. Sets SYSTEM_NAME=MSDOS
  which triggers CMake's DJGPP=1.
- `src/platform/dos/` — HAL slice:
    DosHal.hpp / DosHal.cpp     IHal impl, mirrors WindowsHal
    DosBridge.cpp               Bridge.h C API on top of DosHal
    HostFileSystem.hpp          one-line alias to PosixFileSystem
    CMakeLists.txt              builds libst80_dos.a
- `CMakeLists.txt` (root) — new `if(DJGPP)` block alongside
  WIN32 / APPLE / UNIX. Only activates st80_dos; app/dos/ lands
  in D2.
- `tools/CMakeLists.txt` — DJGPP branch added. Selects
  `src/platform/dos/` for HostFileSystem.hpp and includes
  `src/platform/posix/` as extra header path so the alias works.
- `tests/trace2_check.sh` — optional RUNNER env var / 4th arg
  that prepends a command (eg `dosiz`) to the st80_run invocation.
  Lets the existing trace2 gate drive a DJGPP binary under dosiz.
- `docs/dos-plan.md` — full plan, updated for dosiz and for
  bundling the image.

Memory saved at `~/.claude/projects/.../memory/dosiz_reference.md`.

## What to run after reboot to verify D1

Need DJGPP cross toolchain on PATH (e.g. `andrewwutw/build-djgpp`
v3.4 — same one dosiz's own test suite uses). Need dosiz built at
`C:\temp\src\dosiz\build\dosiz.exe`. Need the Xerox v2 image at
`reference/xerox-image/VirtualImage` with the `trace2` sibling
(fetch per `docs/trace-verification.md`).

    # On Windows, use MSYS2 MINGW x64 shell since dosiz.exe needs
    # its MinGW64 DLL dir on PATH. Or copy the DLLs alongside.
    cd /c/temp/src/smalltalk80-2026

    # Cross-configure and build st80_run for DOS
    cmake -S . -B build-dos \
          -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-djgpp.cmake
    cmake --build build-dos --target st80_run

    # Expected: build-dos/tools/st80_run.exe exists (DJGPP COFF)
    file build-dos/tools/st80_run.exe     # => "MS-DOS executable"

    # Run the trace2 gate under dosiz
    RUNNER="/c/temp/src/dosiz/build/dosiz.exe" \
        tests/trace2_check.sh \
        reference/xerox-image/VirtualImage \
        reference/xerox-image/trace2 \
        build-dos/tools/st80_run.exe

Expected output: `trace2_check: OK (499 bytecodes byte-for-byte)`.

If that works, D1's exit criterion is met and we can proceed to D2
with confidence. If it fails, the interesting failure modes are:

  a. DJGPP cross-toolchain not on PATH → "compiler not found"
     during the first cmake call. Install it.
  b. libstdc++ build gaps (std::mutex on --disable-threads, etc.)
     → compile errors in DosHal.cpp or shared headers. If this
     happens we'll need the EventQueueSingleThreaded.hpp mitigation
     mentioned in `docs/dos-plan.md` Risk #1.
  c. dosiz INT 21h gaps on the exact calls DJGPP's newlib makes
     to open/read/lseek the image. dosiz's `DJ_FILE` fixture
     exercises this path so it should work — but our image read
     is ~1 MiB in one AH=3F call, larger than any fixture I've
     seen in dosiz's suite. See Risk #8.
  d. Trace diff mismatch → interpreter bug; not specific to DOS.
     Run the same gate natively first to confirm it passes.

## Next decision point

Three natural next moves, pick one after verifying D1:

1. **CI workflow** (`.github/workflows/dos-ci.yml`). Install DJGPP
   via `andrewwutw/build-djgpp`, clone+build dosiz, fetch Xerox
   image from Wolczko, run trace2 gate. Medium lift. Makes the D1
   gate regression-proof on every push.

2. **Phase D2 — VBE display** under `app/dos/`. DPMI INT 10h VBE
   probe via `__dpmi_int`, mode pick (1024x768x32 LFB preferred),
   LFB map through INT 31h/0800h, dirty-rect blit. Largest chunk
   remaining (2-3 days in the plan). No user-facing testing until
   D3 adds input.

3. **Local verify only** — don't advance until D1 is green on
   this machine.

Session recommendation was #3 first: D0+D1 make strong claims about
the DJGPP path that are cheap to validate before sinking time into
D2.

## Key files to open next session

- `docs/dos-plan.md` — authoritative plan, phases D0..D6
- `src/platform/dos/DosBridge.cpp` — mirrors WindowsBridge.cpp
- `src/platform/windows/WindowsHal.cpp` — reference pattern for D2
  when we write the VBE frontend
- `src/include/Bridge.h` — the C API the DOS frontend will drive
- `C:\temp\src\dosiz\docs\c-toolchain-guide.md` — DJGPP setup,
  libc quirks, env vars. Essential reading before debugging.
- `C:\temp\src\dosiz\tests\djgpp\` — working DJGPP fixtures that
  model what our st80_run.exe should look like under dosiz.

## Context — dosiz quirks to remember

- `dosiz.exe` on Windows needs MSYS2 MinGW64 DLLs on PATH or
  next to the .exe. Run from MSYS2 MINGW x64 shell.
- dosiz's DJGPP CI suite uses `andrewwutw/build-djgpp` v3.4 —
  use the same toolchain for version alignment.
- dosiz env vars: `DOSIZ_PATH` extends DOS PATH; `DOSIZ_TRACE`
  dumps INT 21h/31h per-call. `DOSIZ_EXC_TRACE` for PM fault
  debugging.
- dosiz host-side env passes `DJGPP` through to the DOS env
  block automatically (see dosiz's `build_env_block()`).
- CWSDPMI is NOT needed under dosiz — dosiz has its own DPMI
  0.9 ring-3 host. Only matters for the real-DOS secondary path.

## Context — project conventions (CLAUDE.md)

- No markdown tables. Indented plain-text columns.
- No #ifdef in portable source. Per-platform dirs only.
- Commit every 15 min silently. Update docs/changes.md before
  user-visible commits (this port isn't user-visible yet).
- No workarounds — fix root causes.
- GUI claims require a screenshot, read with Read tool.
- GUI tests always under `timeout N`; never `open -W`.
