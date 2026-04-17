# Performance — where to measure, what to measure

No established benchmark suite yet. This doc lists candidates, what
each measures, and what it'd take to wire them. Primary driver is
Phase-6 JIT tier-up, which needs interpreter-vs-JIT numbers that
aren't hand-waving.

## Candidate benchmarks

### 1. Blue Book `tinyBenchmarks`

Already in the Xerox v2 image. Returns two integers: bytecodes per
second and message sends per second. Runs in a few tens of
milliseconds. Cannot be easier to drive — the same VM harness that
boots the image can poke
`Transcript show: BenchmarksReport run printString` and pattern-match
the output.

Wiring: needs a `st80_run_until(condition)` or a flag on `st80_run`
that exits when the transcript matches a regex. Add to `tools/` as
`st80_bench.cpp`.

Gives us: the single number headline — "the interpreter does N
bytecodes/sec, the JIT does M". Also the ratio, which is the Phase-6
exit criterion (≥2×).

### 2. `macroBenchmarks` (ported from Squeak)

Richer suite — runs compiler, BitBlt, text layout, SystemDictionary
walks. Not in the Xerox image as-shipped; would need to fileIn a
snippet of Smalltalk from the Squeak source. Order of minutes per
run. Good for catching regressions that `tinyBenchmarks` misses
because its innermost loop is too uniform.

Wiring: a `.st` snippet shipped in `resources/benchmarks.st`, loaded
on demand via a primitive or via the file-in path.

### 3. BitBlt throughput

Standalone C++ benchmark over our `src/core/BitBlt.cpp`. No image
needed. Useful because BitBlt is the single hottest non-interpreter
path on Xerox's UI — fill rectangles, scroll, render text. An
improvement here is visible to the user as faster scrolling /
smoother window dragging.

Wiring: a CTest performance target that times a fixed workload
(e.g. 10 000 fills of 640×480 with each combination rule). Run on
release build only, use `clock_gettime(CLOCK_MONOTONIC_RAW)`.

### 4. Bytecode dispatch microbenchmark

Time how long `st80_run` takes to execute 1 000 000 of the hottest
bytecodes — typically `push temp`, `push literal`, `send arg0`,
`return top`. This isolates the dispatch loop from primitive costs
and is the number we'd watch for computed-goto or direct-threaded
dispatch wins.

Wiring: a reduced image that loops on a single tight method, or
counter-instrumented `st80_run`.

### 5. Image boot time

Wall-clock from `st80_init` to first display ready. Regression gate
on image-loader changes. We already have the plumbing — just needs
a `time` around `bridge_api_test`.

## What exists in the world

- Squeak's `Benchmarks` class category has a dozen or so standard
  Smalltalk benchmarks ready to fileIn. MIT-compatible.
- iospharo's `jit` branch likely has numbers from Pharo's
  `Slots benchmarkReport` — similar shape, different workload,
  worth looking at for methodology.

## Method — how to actually report numbers

- Median of 5 runs (not mean — outliers dominate on a desktop).
- Fix the CPU governor to performance mode on Linux; note what macOS
  does (it's usually fine but thermally-throttled Macs will skew).
- Pin to a single core with `taskset` / `sudo dispatch_sync`. CPU
  migration alone is ~3% noise.
- Always run both interpreter and JIT in the same invocation, same
  process — avoids the cold-cache penalty biasing whichever ran
  first.
- Report hardware: `sysctl machdep.cpu.brand_string` + macOS version.

## Gaps right now

- No `st80_bench` binary. Closest is `st80_run` which emits bytecode
  traces, not timings.
- No perf CI; any regression gets caught by feel, not signal.
- No JIT yet — the whole reason this file exists is to have numbers
  ready before that lands so we don't waste Phase-6 time inventing
  the benchmark infrastructure.
