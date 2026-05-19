#!/usr/bin/env bash
# deeprun_check.sh — sustained-execution stability gate.
#
# trace2_check.sh pins the first 499 bytecodes (the deterministic boot
# prefix Xerox shipped a gold file for). Nothing guards the interpreter
# *past* that point — where late-emerging bugs live: a primitive that
# only fires deep in the scheduler/idle loop, GC pressure, a dosiz DPMI
# or memory edge that a long run trips. This runs st80_run for a large
# cycle count and asserts it completes cleanly: exit 0 AND exactly N
# bytecodes emitted (one per cycle) — i.e. it neither crashed,
# asserted, nor hung/short-circuited somewhere in the middle.
#
# Not a determinism check: past the snapshot point the VM does real
# scheduler work whose timing legitimately differs native vs dosiz, so
# the *content* may diverge — only completion is asserted here.
#
# Optional 4th arg RUNNER is prepended to the st80_run invocation
# (the DOS slice passes a dosiz wrapper), same contract as
# trace2_check.sh.
#
# Usage:
#   deeprun_check.sh <image> <st80_run> <cycles> [RUNNER]
#
# Env:
#   ST80_DEEP_SHA256   if set, additionally assert the (CR-stripped)
#                      bytecode stream hashes to exactly this value —
#                      upgrades the liveness check to a byte-for-byte
#                      content gate. Deterministic so the pin is
#                      identical native and under dosiz.
#
# Exit: 0 ran cleanly (and matched the pin if set) · 1 crashed /
# short / content drifted · 77 prerequisites absent.
#
# Copyright (c) 2026 Aaron Wohl. MIT License.
set -uo pipefail

IMAGE="${1:-}"
ST80_RUN="${2:-}"
CYCLES="${3:-250000}"
RUNNER="${4:-${RUNNER:-}}"

[ -n "$IMAGE" ] && [ -n "$ST80_RUN" ] || {
    echo "deeprun_check: SKIP (usage: deeprun_check.sh <image> <st80_run> <cycles> [RUNNER])"
    exit 77
}
[ -f "$IMAGE" ] || { echo "deeprun_check: SKIP (missing $IMAGE)"; exit 77; }
if [ -n "$RUNNER" ]; then
    [ -f "$ST80_RUN" ] || { echo "deeprun_check: SKIP (missing $ST80_RUN)"; exit 77; }
else
    [ -x "$ST80_RUN" ] || { echo "deeprun_check: SKIP (missing or non-exec $ST80_RUN)"; exit 77; }
fi

ACTUAL="$(mktemp -t st80deep.XXXX)"
trap 'rm -f "$ACTUAL"' EXIT

if [ -n "$RUNNER" ]; then
    $RUNNER "$ST80_RUN" -n "$CYCLES" "$IMAGE" > "$ACTUAL" 2>/dev/null
else
    "$ST80_RUN" -n "$CYCLES" "$IMAGE" > "$ACTUAL" 2>/dev/null
fi
rc=$?

# Strip the DOS text-mode CR so the line count is exact on both paths.
LINES=$(tr -d '\r' < "$ACTUAL" | grep -c '')

if [ "$rc" -ne 0 ]; then
    echo "deeprun_check: FAIL — st80_run exited $rc before $CYCLES cycles (got $LINES)"
    exit 1
fi
if [ "$LINES" -ne "$CYCLES" ]; then
    echo "deeprun_check: FAIL — expected $CYCLES bytecodes, got $LINES (truncated/hung)"
    exit 1
fi

# Content gate. HeadlessHal is fully deterministic — get_msclock() is
# a plain ticks_++ counter (not wall time), epoch time fixed, no input
# or rand — so st80_run on a fixed image emits a bit-exact stream
# regardless of host or emulator. With ST80_DEEP_SHA256 pinned (the
# Xerox v2 image at this cycle count) this becomes a 250k-cycle
# byte-for-byte gate — ~500x deeper than trace2's 499 — via one
# constant, no megabyte gold file. The same pin holds on the native
# host and inside dosiz (proven: c2f447e7…), so it guards the
# interpreter, the image loader, AND dosiz's CPU/DPMI emulation over
# sustained execution.  CR is stripped first so LF (native) and CRLF
# (DOS text-mode) hash identically.
EXPECT_SHA="${ST80_DEEP_SHA256:-}"
if [ -n "$EXPECT_SHA" ]; then
    if   command -v sha256sum >/dev/null 2>&1; then SHA_CMD="sha256sum"
    elif command -v shasum    >/dev/null 2>&1; then SHA_CMD="shasum -a 256"
    else echo "deeprun_check: OK ($CYCLES cycles ran clean; no sha tool — content unverified)"; exit 0
    fi
    GOT_SHA=$(tr -d '\r' < "$ACTUAL" | $SHA_CMD | cut -d' ' -f1)
    if [ "$GOT_SHA" != "$EXPECT_SHA" ]; then
        echo "deeprun_check: FAIL — $CYCLES-cycle stream drifted"
        echo "  expected sha256 $EXPECT_SHA"
        echo "  got      sha256 $GOT_SHA"
        exit 1
    fi
    echo "deeprun_check: OK ($CYCLES cycles, byte-for-byte vs pinned reference)"
    exit 0
fi
echo "deeprun_check: OK ($CYCLES cycles ran clean; content unpinned)"
exit 0
