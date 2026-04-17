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

## What's available but not yet wired

1. **`trace3`** — Xerox ships a second reference trace alongside
   `trace2`, annotated with message sends / returns. Lives at
   `reference/xerox-image/trace3`. Wiring it would be a second
   invocation of `st80_run` with a flag to emit the richer trace
   format; the diff script pattern from `trace2_check.sh` would
   follow. Catches send-receive or stack-frame bugs that `trace2`
   doesn't exercise.

2. **`st80_validate check`** — New in `tools/`. Walks the OT after
   load and reports dangling class references / bogus word lengths.
   Not yet a CTest step; should be added as a fast gate alongside
   the smoke test.

3. **`st80_validate shasum` diffs** — Useful for snapshot
   regressions: run A, snapshot, run B, snapshot, diff the two
   manifests to flag any object whose contents changed. Not a test
   by itself but a debugging lever. CTest could pin the post-boot
   manifest once we've frozen a baseline.

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
