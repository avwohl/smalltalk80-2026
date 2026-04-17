# Phase-5 Interpreter Audit

Baseline measurements and ranked optimization candidates. Written
ahead of the JIT (Phase 6) because the tier-up seams depend on
knowing which inner loops dominate in the interpreter today.

## Baseline (Apple M-series, release build, post-boot)

    ./build/tools/st80_run -n 100000   /Users/…/VirtualImage → 0.10s user (~1.0 Mbc/s)
    ./build/tools/st80_run -n 1000000  /Users/…/VirtualImage → 0.79s user (~1.26 Mbc/s)

Call it ~1.25 million bytecodes/second per core on an M-series Mac.
Roughly 10× slower than a modern Spur+Sista VM, which is the headroom
we'd like Phase-6 to close.

Memory pressure is ~5 MB RSS; GC is not the bottleneck.

## How to measure

    time ./build/tools/st80_run -n <N> <image> > /dev/null

Use `-n 1000000` for timing (the `-n 100000` run is measurement noise
+ startup). Fix CPU governor / disable turbo variability by taking
the median of five runs.

Finer-grained profile:

    # macOS sample:
    sample $(pgrep st80_run) 5 -f /tmp/st80.sample
    # or samply / Instruments' Time Profiler against a release binary

## Hot paths (by inspection)

The innermost loop is `Interpreter::cycle`, which calls three things
per bytecode:

1. `checkProcessSwitch` — cheap, one flag test.
2. `fetchByte` — calls `memory.fetchByte_ofObject(ip, method)`. Every
   call walks the Object Table to resolve `method`'s heap address,
   reads the word, masks the byte. This is **the single hottest
   line in the interpreter** because it runs once per bytecode.
3. `dispatchOnThisBytecode` — if-else chain by bytecode range, then
   an inner switch inside each handler.

A secondary hot path is `sendBytecode` → `lookupMethodInClass`. The
existing 1024-entry method cache already catches the common case;
cache hit rate on the Xerox image is probably in the 95–99% band
(hasn't been measured — item below).

## Ranked proposals

### 1. Cache the current method's byte pointer locally — high impact, low risk

`method` only changes at send / return. Between those, every
`fetchByte` re-resolves the same OT entry. Add a pair of fields
`currentMethodBytes: const std::uint8_t *` + `currentMethodEnd` set
once per method activation and refreshed whenever `method` changes.
Replace the per-byte `memory.fetchByte_ofObject(ip, method)` with a
direct `currentMethodBytes[ip - HeaderSize * 2]` (minus whatever
header fixup applies).

Estimated gain: ~1.5–2× on the interpreter hot loop (removes one
OT indirection per bytecode).

Risks: `ObjectMemory.garbageCollect()` would invalidate the pointer.
Either refresh it from a GC hook, or avoid caching across any
operation that can trigger GC — safest is to refresh on every return
to the interpreter after a primitive.

Safety net: `trace2_check` — if we break dispatch, trace diverges.

### 2. Measure and publish method-cache hit rate — diagnostic, not code change

Add a pair of counters around the `methodCache[hash] ==
messageSelector && methodCache[hash+1] == cls` check in
`Interpreter.cpp:4250`, expose via `st80_stats()` or similar, print
at `-n` run completion. We'll know whether to invest in a larger
cache, a better hash, or move to PIC-per-call-site.

Estimated work: 20 lines. Blocks proposals 3 & 4.

### 3. Polymorphic inline cache (PIC) at call sites — medium impact, high disruption

Instead of one global method cache, attach a mini-cache to each
`send` bytecode location. Each cache holds 2–4 (class → method)
entries and is probed inline before falling back to the global
cache. Well-known 2–5× speedup on OO-heavy workloads.

Risks: the bytecode stream is immutable in the Blue Book image.
PICs require a side-table keyed by `(method-oop, bytecode-offset)`.
The JIT plan already needs this kind of side table, so this work
doubles as scaffolding.

Estimated gain: 1.3–2× on send-heavy code *on top of* proposal 1.

### 4. Replace dispatch if-else with explicit jump table — low impact

`dispatchOnThisBytecode` is already compiled to something close to a
jump table by modern Clang, but making it explicit (a 256-entry
`void(Interpreter::*)() table[]`) removes the branch predictor's
guess and dodges cascade ordering biases. Likely 5–10% gain.

Estimated gain: 1.05–1.10×.

Risks: member-function pointers have their own overhead — actual
win is implementation-specific; measure before committing.

### 5. Specialize integer bytecode handlers — medium impact

The Blue Book's "special select bytecodes" 176–207 are
`+`, `-`, `<`, `>`, `=`, etc. Most of the time both operands are
SmallIntegers and the primitive path succeeds. A specialized fast
path ("if both top-of-stack values have the SmallInteger tag, do the
op in C") would skip the full send + primitive machinery for these.

Estimated gain: highly workload-dependent; perhaps 1.2–1.5× on
arithmetic-heavy code.

## What NOT to change yet

- **OOP → direct-address caching for receiver.** The JIT will want
  this anyway, but doing it in the interpreter requires the same GC
  hook as proposal 1 and buys less because receiver access isn't as
  hot as bytecode fetching.
- **Threaded / computed-goto dispatch.** Compiler-dependent, harder
  to port to Windows (MSVC doesn't have computed goto without hacks).
  Defer to Phase 6 where the JIT bypasses dispatch entirely.

## Suggested order

1. Proposal 2 (measure cache hit rate) — 1 hour. Informs everything.
2. Proposal 1 (fetchByte cache) — half a day. Biggest single win.
3. Re-benchmark, run trace2. If still below target, proposal 5.
4. Proposal 3 (PIC) alongside Phase-6 JIT — they share infrastructure.

Any change must keep `trace2_check` green byte-for-byte. Any change
that touches send/return must also keep `bridge_api_test` green.
