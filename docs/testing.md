# Testing the VM + Image

Inventory of correctness tests, what we run, and what's still on the
floor.

## What runs today (CTest)

All under `tests/CMakeLists.txt`:

1. **`core_smoke_test`** — Static asserts on Blue Book well-known OOPs
   (NilPointer=2, FalsePointer=4, etc.) plus `RealWordMemory`
   round-trip and byte-endian sanity. Compiles-and-links test. No
   image needed. Phase-1 scaffolding that catches typos in
   `Oops.hpp` / `RealWordMemory.hpp`.

2. **`bridge_api_test`** (Apple only) — Boots the Xerox v2 image via
   the pure-C `Bridge.h` API, runs 2000 cycles past image startup,
   verifies display dirty-rect metadata, round-trips input-word
   queueing. The Catalyst/AppKit frontends depend on this API; this
   is the smoke test that the C boundary still works. Skips
   (exit 77) if `reference/xerox-image/VirtualImage` is absent.

3. **`trace2_check.sh`** — Phase-1 regression gate. Runs
   `st80_run -n <N> <image>` and `diff`s the emitted bytecode stream
   against the Xerox-provided `trace2` reference. Any drift in the
   interpreter or image loader blows up here first. Byte-for-byte
   match is required. Skips if the image or trace file is absent.

3a. **`st80_run_deep`** (`deeprun_check.sh`) — deep byte-for-byte
   gate. `trace2` only pins the first 499 (boot) cycles; this drives
   `st80_run` to 250 000 cycles, ~500x deeper, well past the
   snapshot/scheduler entry. `HeadlessHal` is fully deterministic
   (`get_msclock()` is a `ticks_++` counter, not wall time; epoch
   time fixed; no input/rand), so the entire stream is bit-exact
   regardless of host or emulator — it does **not** drift native vs
   dosiz. The check asserts exit 0, exactly N bytecodes, **and**
   that the CR-stripped stream hashes to a pinned SHA-256
   (`ST80_DEEP_SHA256`, single 64-char constant — no megabyte gold
   file). That one pin is identical on the native host and inside
   dosiz (`c2f447e7…`, verified 2026-05-19), so it simultaneously
   guards the interpreter, the image loader, and dosiz's CPU/DPMI
   emulation over a long sustained run — where a late primitive or
   a dosiz memory edge would surface. Regenerate the pin only on an
   intentional interpreter change:
   `st80_run -n 250000 <image> | tr -d '\r' | sha256sum`. CTest on
   UNIX/CI (bash); the DJGPP build runs the same script under dosiz
   via the gate below, against the same pin.

   The default 250 000 is the fast tier (~2 s host, a few min in
   dosiz). A much deeper stress run is opt-in via the existing
   overrides — `ST80_DEEP_CYCLES` + the matching `ST80_DEEP_SHA256`
   — kept out of the routine gate so CI stays quick. The 2 000 000
   -cycle tier (well into steady-state scheduler + multiple GC
   passes, ~8x the default) has been verified byte-for-byte
   native == dosiz (2026-05-19): exit 0, 2 000 000 lines, sha256
   `33d73dcc435b2bba0341108d474837964d551415a7e5442a0e3642e553e52212`
   identical on the host and inside dosiz — no crash, assert, or
   drift over millions of cycles of emulated execution. Run it with
   `ST80_DEEP_CYCLES=2000000
   ST80_DEEP_SHA256=33d73dcc…e52212 bash tests/dos_dosiz_gate.sh …`

4. **`st80_gui_test`** — headless "fake GUI" runner, the in-repo
   analog of `avwohl/pharo-headless-test` (README "Related").
   pharo-headless-test installs an in-memory Display Form, injects
   synthetic events at the Morphic layer, screenshots the Form, and
   runs SUnit with no display. We have the same seam in the pure-C
   `Bridge.h` API: `st80_post_mouse_*`/`st80_post_key_*` feed the
   Sensor, `st80_display_*` return the rendered RGBA framebuffer,
   and every platform HAL keeps that buffer purely in memory (no
   window). The test boots the Xerox v2 image through the real
   platform bridge, drives a synthetic interaction against the live
   Smalltalk-80 environment, writes PNG screenshots (self-contained
   encoder, no libpng/zlib), and asserts the image visibly reacts
   (≥100-pixel framebuffer delta on a yellow-button operate-menu).
   One source, four link targets: host CI links `st80_windows` /
   `st80_linux` / `st80_apple`; the DJGPP build links `st80_dos`
   and runs under dosiz for the DOS-port gate. Host and dosiz
   produce byte-identical boot + interaction frames (same 4922-px
   delta) — the DOS port matches native pixel-for-pixel. CTest on
   every non-DJGPP host; skips (77) if the image is absent. This
   is the harness the "In-image tests" gap below asked for.

5. **`st80_validate_check`** — fast structural gate: load the v2
   image, walk the whole Object Table, verify every class ref
   resolves and word lengths are sane. CTest on non-DJGPP hosts;
   added only when the image is present (no skip plumbing). The
   DJGPP build runs the same `st80_validate check` under dosiz.

6. **`st80_validate_roundtrip`** — D4 regression gate.
   `load → saveSnapshot → reload` must reproduce a bit-identical
   object graph (one SHA-256 over every live object: oop + class
   + length + body, big-endian). Guards the `create_file`/`write`
   path — including the dosiz AH=40 + `O_BINARY` path on the DOS
   port — plus byte-order and OT/page-layout stability. The
   digest is identical on the native host and under dosiz
   (`9db7adac…`), so the same gate pins both. Cross-platform, no
   shell.

7. **`dos_dosiz_gate`** — the consolidated "st80 works fully
   under dosiz" gate (`tests/dos_dosiz_gate.sh`). Stages the
   DJGPP-cross binaries into a temp 8.3 dir and runs all four
   DOS checks inside dosiz in one shot: trace2 byte-for-byte,
   deep-run (250 k cycles, byte-for-byte vs the pinned native
   reference), `st80_validate roundtrip`, and `st80_gui_test`.
   This is the automated form of the
   verification previously done by hand each iteration — the
   regression guard for the DOS port and for dosiz itself. CTest
   on UNIX/CI (bash); behind the `ST80_DOS_BUILD_DIR` /
   `ST80_DOSIZ_BIN` cache vars and self-SKIPs (77) when the
   DJGPP tree or dosiz is absent, so a host without the DOS
   toolchain stays green. On the Windows dev box it's run
   directly via git-bash (same as `trace2_check`). Current
   result: 4/4 PASS (trace2 OK, 250 k cycles byte-for-byte vs
   pin, roundtrip digest `9db7adac…`, fake-GUI Δ4922) — every
   figure identical to the native host.

## What's available but not yet wired

1. **`trace3`** — Xerox ships a second reference trace alongside
   `trace2`, annotated with message sends / returns. Lives at
   `reference/xerox-image/trace3`. Wiring it would be a second
   invocation of `st80_run` with a flag to emit the richer trace
   format; the diff script pattern from `trace2_check.sh` would
   follow. Catches send-receive or stack-frame bugs that `trace2`
   doesn't exercise.

2. **`st80_validate shasum` diffs** — still a useful debugging
   lever for post-boot drift: run A, snapshot, run B, snapshot,
   diff the two per-OOP manifests to flag any object whose
   contents changed. CTest could pin the post-boot manifest once
   we've frozen a baseline. (The static load→save→load case is
   now covered automatically by `st80_validate_roundtrip` above.)

## What exists in the world but we haven't imported

1. **Blue Book `tinyBenchmarks`** — Smalltalk-side benchmark suite
   that returns a pair of numbers (bytecode/sec, sends/sec). It's
   already in the Xerox image; we just need a driver that pokes
   `BenchmarksReport run` through the VM and captures the transcript
   output. Would double as a perf gate (see `docs/performance.md`).

2. **dbanay's BitBlt test vectors** — dbanay ships a standalone
   BitBlt unit-test harness with reference output bits for tricky
   cases (overlap, word alignment, mask edges). We port his bitblt
   but **not** his tests; porting them would be a direct MIT copy
   (with attribution per `THIRD_PARTY_LICENSES`).

3. **iriyak / rochus-keller reference outputs** — both target the
   same Xerox v2 image and could be diffed against for
   second-opinion regression signal. No glue yet.

4. **In-image tests** — the Xerox image includes `TestCase` hierarchy
   precursors. Running the full SUnit-esque suite via our VM and
   diffing the transcript against a known-good baseline would be a
   strong correctness gate. Requires a harness that drives the VM
   past boot and watches for a transcript pattern.

## Gaps

- No gtest / Catch2 yet; Phase-1 plan says to add it when test
  volume justifies. `st80_validate` argues we're close.
- CI isn't configured. The existing suite is fast (<5s) so a plain
  GitHub Actions macOS runner would cover it.
- No coverage measurement; once we care about the interpreter hot
  path (Phase-5 / JIT tier-up), `llvm-cov` over a trace-replay run
  would tell us which bytecodes are exercised.
