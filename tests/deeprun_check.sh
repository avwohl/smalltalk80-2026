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
# Exit: 0 ran cleanly · 1 crashed/short · 77 prerequisites absent.
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
echo "deeprun_check: OK ($CYCLES cycles ran clean)"
exit 0
