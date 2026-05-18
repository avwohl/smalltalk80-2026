# WIP — MS-DOS / FreeDOS / DPMI port

Work-in-progress snapshot. Session dated 2026-05-18.
Delete this file once Phase D5 ships (release published + verified).

## Where we are

D0–D1 landed earlier (scaffolding + headless wiring). This session
added the actual DOS-runnable frontend: **D2 (VBE display), D3
(mouse + keyboard), D4 (filesystem, inherited)** are code-complete,
and **D5** has its packaging target. Nothing binary-verified yet —
no DJGPP toolchain on this box.

Recent commits on `main`:

    2623ce0  dos: add app/dos VBE+mouse+keyboard frontend (D2-D4)
    ddf9a1b  dos: wire st80_run for DJGPP (D1)
    28ac42d  dos: DPMI plan + DJGPP toolchain + platform slice (D0)

`docs/dos-plan.md` is authoritative; its new "Status (2026-05-18)"
section has the phase table. This file is the run-it-next handoff.

## What this port is

A DJGPP-cross-compiled `st80.exe` (go32-v2 stub + COFF) that boots
the Xerox v2 image to a VESA desktop. Primary runtime is **dosiz**
(`C:\temp\src\dosiz`, same author) — in-process dosbox-staging +
ring-3 DPMI host. Secondary is real DOS / FreeDOS with a
user-supplied CWSDPMI / HDPMI32. We bundle the image (DOS TCP is
too flaky for a startup fetch); we do NOT ship a DPMI host.

## What landed this session (commit 2623ce0)

    app/dos/VbeDisplay.{hpp,cpp}   VBE 2.0+ probe (4F00/4F01),
                                   best-LFB-mode pick, 4F02h set,
                                   __dpmi_physical_address_mapping
                                   LFB, per-scanline blit + XOR-ish
                                   software cursor composite. Has a
                                   probe()-only path (no mode set).
    app/dos/MouseInt33.{hpp,cpp}   INT 33h: reset/detect (AX=0),
                                   motion mickeys (AX=0Bh) → absolute
                                   VM cursor, buttons (AX=03h) edges.
    app/dos/KbdInt16.{hpp,cpp}     polled _bios_keybrd; ASCII pass-
                                   through + fwd-Delete=127; shift
                                   status for the blue-button map.
    app/dos/Launcher.{hpp,cpp}     CP437 text banner / error / probe
    app/dos/st80_dos_main.cpp      arg parse, prime VM, cooperative
                                   loop; --probe / --no-display /
                                   --cycles-per-frame / --scale
    app/dos/CMakeLists.txt         target `st80` → st80.exe
    app/dos/README.md              build + run guide (dosiz + real)
    cmake/DosPackaging.cmake       opt-in `st80_dos_zip` (no DPMI
                                   host bundled)
    CMakeLists.txt                 if(DJGPP) now adds app/dos +
                                   include(DosPackaging) — gated,
                                   host builds untouched

All DPMI/BIOS access uses the dosiz `dj_ems.c` convention: zeroed
`__dpmi_regs`, ES:DI / DS:DX at `__tb`, `__dpmi_int`, `dosmemget`
back. Confirmed available under dosiz per its
`docs/c-toolchain-guide.md`.

## "Don't break existing ports" — verified

Reconfigured the MSVC `build/` (clean; the DJGPP block stays inert)
and rebuilt `st80core`, `st80_run/probe/validate`, `st80_windows`,
`st80-win` — all green. `git diff HEAD~1 HEAD` touches zero
`src/core` / `src/include` / `tests` files.

Caveat: `ctest` `core_smoke_test` SEGFAULTs (~4.3 s) on this MSVC
Debug host. **Pre-existing and unrelated** — the core/test compiler
inputs are byte-identical to the pre-change tree, so this commit
cannot have caused it. Flagged here so it isn't mistaken for DOS
fallout; worth a separate look on its own.

## What to run to verify (needs DJGPP + dosiz)

Prereqs: DJGPP cross toolchain on PATH (`andrewwutw/build-djgpp`
v3.4 — same one dosiz tests against). `dosiz.exe` already built at
`C:\temp\src\dosiz\build\dosiz.exe` (confirmed present). Xerox v2
image at `reference/xerox-image/VirtualImage` (+ `trace2` sibling).

    cd /c/temp/src/smalltalk80-2026
    cmake -S . -B build-dos \
          -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-djgpp.cmake
    cmake --build build-dos --target st80_run st80

    # D1 gate — byte-for-byte trace under dosiz
    RUNNER="/c/temp/src/dosiz/build/dosiz.exe" \
      tests/trace2_check.sh \
      reference/xerox-image/VirtualImage \
      reference/xerox-image/trace2 \
      build-dos/tools/st80_run.exe
    # expect: trace2_check: OK (499 bytecodes byte-for-byte)

    # D2 Risk-#2 probe (text mode, no graphics) — cheapest signal
    dosiz build-dos/app/dos/st80.exe --probe \
          reference/xerox-image/VirtualImage
    # expect: a chosen VBE mode line + "INT 33h mouse: present"

    # D2/D3 — boot to the desktop, screenshot, Read it
    dosiz --window build-dos/app/dos/st80.exe \
          reference/xerox-image/VirtualImage
    # then scrot/screencapture/nircmd; Read the PNG; confirm pixels

Likely failure modes, in order of interest:
  a. DJGPP not on PATH → cmake "compiler not found". Install it.
  b. libstdc++ `--disable-threads` gaps (std::mutex/atomic) →
     compile errors in shared headers. Mitigation = dos-plan
     Risk #1 (EventQueueSingleThreaded.hpp).
  c. VBE: dosiz returns no LFB mode → VbeDisplay::begin error
     text. `--probe` isolates this without a mode switch.
  d. `__dpmi_physical_address_mapping` semantics differ under
     dosiz — code already falls back to physBase as linear.
  e. trace mismatch → interpreter bug, not DOS-specific; run the
     same gate natively first.

## Next decision point

1. Get DJGPP installed, run the verify block above (cheapest:
   `--probe`, then the trace gate, then `--window` screenshot).
2. CI workflow `.github/workflows/dos-ci.yml`: build DJGPP +
   dosiz in a cache layer, run the D1 gate every push.
3. Wire arrow/function keys only if a real Smalltalk workflow
   needs them (the other four ports don't map them either).

Recommendation: #1 — the code makes strong claims about the DJGPP
DPMI/VBE path that are cheap to falsify with `--probe`.

## Key files next session

- `docs/dos-plan.md` — authoritative, see "Status (2026-05-18)"
- `app/dos/VbeDisplay.cpp` — the riskiest piece (VBE/DPMI)
- `app/dos/README.md` — build/run, both runtimes
- `C:\temp\src\dosiz\docs\c-toolchain-guide.md` — DJGPP setup +
  libc quirks. Read before debugging a cross-build failure.
- `C:\temp\src\dosiz\tests\djgpp\dj_ems.c` — the __dpmi_int /
  __tb / dosmemget pattern our BIOS calls mirror.

## Conventions (CLAUDE.md)

No markdown tables. No #ifdef in portable source — per-platform
dirs only. Commit ~15 min. No workarounds — fix root causes (e.g.
VbeDisplay errors out, never silently drops to CGA). GUI claims
need a screenshot Read with the Read tool, always under a timeout.
