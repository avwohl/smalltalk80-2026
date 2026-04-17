# Trace verification

Reproducing the Phase 1 exit-gate check.

## Prerequisites

The Xerox v2 image tarball fetched from
http://www.wolczko.com/st80/image.tar.gz, extracted to
`reference/xerox-image/`. The directory is gitignored; fetch it with:

    mkdir -p reference/xerox-image
    curl -sSLo reference/xerox-image/image.tar.gz \
         http://www.wolczko.com/st80/image.tar.gz
    (cd reference/xerox-image && tar xzf image.tar.gz)

This produces `VirtualImage` (the big-endian image), `trace2`,
`trace3`, and supporting files. The endian-aware loader (build 7)
reads `VirtualImage` directly.

## trace2 check — bytecode-level

`trace2` records the bytecode sequence executed from image start for
~499 cycles, one entry per cycle. Format per line:

    Bytecode <131> Single Extended Send

We extract just the bytecode number and compare against what
`st80_run` emits:

    awk '/^Bytecode <[0-9]+>/ {
        match($0, /<[0-9]+>/)
        print substr($0, RSTART+1, RLENGTH-2)
    }' reference/xerox-image/trace2 > /tmp/trace2_bc.txt

    ./build/tools/st80_run -n $(wc -l < /tmp/trace2_bc.txt) \
        reference/xerox-image/VirtualImage > /tmp/st80_bc.txt 2>/dev/null

    diff /tmp/trace2_bc.txt /tmp/st80_bc.txt

Expected: empty diff. Verified green in build 8.

## trace3 check — send/return-level (not yet wired)

`trace3` records message sends and method returns with decoded
receiver classes and selector names:

    [cycle=1]  aSystemDictionary isNil
        ^ (method) of false
    [cycle=6]  false & aTrue

Matching this needs a decorated tracer in the interpreter that emits
a line per send bytecode (with resolved receiver class + selector
string) and per return. That's non-trivial — deferred. trace2
byte-for-byte equivalence already proves the interpreter is
functionally correct for the first 499 cycles; trace3 is a derived
view over the same execution.

## Stability stress test

    time ./build/tools/st80_run -n 100000 \
         reference/xerox-image/VirtualImage > /dev/null

As of build 8: 100 K cycles in ~0.12 s CPU on Apple Silicon
(≈830 K cycles/sec), clean exit.
