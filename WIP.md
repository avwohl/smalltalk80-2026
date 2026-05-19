# WIP — MS-DOS / FreeDOS / DPMI port

Work-in-progress snapshot. Session dated 2026-05-18.
Delete this file once Phase D5 ships (release published + verified).

## Where we are

D0–D1 landed earlier (scaffolding + headless wiring). This session
added the actual DOS-runnable frontend (D2 VBE / D3 mouse+kbd / D4
fs inherited / D5 packaging) **and got it genuinely cross-building
under DJGPP**. The earlier "D0/D1 committed" claim was never
compiled — the first real DJGPP build exposed four blockers, all
now fixed (commit ed30b3a). `st80.exe` + `st80_run.exe` are real
`coff-go32-exe` DPMI binaries.

Runtime verification is **blocked by dosiz on this Windows box**
(not the port): dosiz hangs with zero output for *any* program,
including its own DJGPP fixtures. See "dosiz blocker" below.

Toolchain is installed: `C:\s\djgpp` (andrewwutw v3.4, GCC 12.2.0).
Xerox image fetched to `reference/xerox-image/`.

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

## "Don't break existing ports" — verified (twice)

After the additive frontend commit AND after the ed30b3a build
fixes (which touch `src/core/Interpreter.cpp` + root CMakeLists),
reconfigured the MSVC `build/` (clean) and rebuilt `st80core`,
`st80_run/probe/validate`, `st80_windows`, `st80-win` — all green.
The CMake change is `if(DJGPP)/else()`; the else() branch is the
exact pre-existing flags, so non-DOS ports are unchanged. The
Interpreter.cpp change (`std::nanf("")` →
`std::numeric_limits<float>::quiet_NaN()`) is a never-consumed
failure-path sentinel — behaviour-identical on every toolchain.

Caveat: `ctest core_smoke_test` SEGFAULTs (~4.3 s) on this MSVC
Debug host, **before and after** these changes, identically.
Pre-existing and unrelated (it also can't even be launched from
this Git Bash — exit 127 — an environment limitation of this box,
not the code). Worth a separate look on its own.

## How to reproduce the build (verified working)

    export PATH="/c/s/djgpp/bin:$PATH"      # andrewwutw v3.4
    CM=".../VS/.../CMake/bin/cmake.exe"      # cmake 3.20+
    NINJA=".../VS/.../Ninja/ninja.exe"
    cd /c/temp/src/smalltalk80-2026
    "$CM" --fresh -S . -B build-dos -G Ninja \
          -DCMAKE_MAKE_PROGRAM="$NINJA" \
          -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-djgpp.cmake
    "$CM" --build build-dos --target st80_run st80
    # => build-dos/tools/st80_run.exe + build-dos/app/dos/st80.exe
    #    both: objdump -f => "file format coff-go32-exe"

## dosiz blocker (file upstream in C:\temp\src\dosiz)

`dosiz --version` / `--help` work. Running ANY program hangs: zero
stdout, `--verbose` prints nothing, process uses ~0.04 s CPU over a
full timeout (blocked on I/O, not spinning), never exits. Verified
not our code — dosiz's OWN shipped fixtures hang too:

    export PATH="/c/s/msys64/mingw64/bin:$PATH"   # dosiz's DLLs
    cd /c/temp/src/dosiz
    timeout 20 ./build/dosiz.exe tests/DJ_PRINTF.EXE   # rc=124, 0 lines
    timeout 20 ./build/dosiz.exe tests/DJ_WRITE.EXE    # rc=124, 0 lines

Minimal repro independent of st80: a trivial DJGPP `printf("hi")`
(`/c/s/st80t/HI.EXE`) also hangs. Likely a dosiz Windows-headless
console/boot block (cf. c-toolchain-guide.md:130 "FreeCOM hangs
silently after any external spawn"). Fix dosiz, then run the gate:

    cd /c/s/st80t   # 8.3-clean staging: ST80RUN.EXE ST80.EXE SNAPSHOT.IM
    export PATH="/c/s/msys64/mingw64/bin:$PATH"
    /c/temp/src/dosiz/build/dosiz.exe ST80RUN.EXE -n 499 SNAPSHOT.IM
    # diff its stdout vs the awk-extracted reference/xerox-image/trace2
    /c/temp/src/dosiz/build/dosiz.exe ST80.EXE --probe SNAPSHOT.IM
    /c/temp/src/dosiz/build/dosiz.exe --window ST80.EXE SNAPSHOT.IM

NOTE dosiz maps host CWD → C:\ and is 8.3-only without a .cfg, so
paths like `build-dos/` / `reference/xerox-image/VirtualImage` are
NOT reachable from the guest — stage 8.3 names (done in C:\s\st80t).
Or run the gate on Linux/macOS CI where dosiz's own suite is green.

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
