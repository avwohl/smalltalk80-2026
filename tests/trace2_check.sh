#!/usr/bin/env bash
# trace2_check.sh — Phase 1 exit-gate regression guard.
#
# Diffs the bytecode stream emitted by `st80_run` against the Xerox
# canonical `trace2` file's list of executed bytecodes. Any drift
# in the interpreter or image loader will blow up here first.
#
# Skips (exit 77) when the image or reference trace are absent.
# Copyright (c) 2026 Aaron Wohl. MIT License.
set -euo pipefail

IMAGE="$1"
TRACE2="$2"
ST80_RUN="$3"

if [ ! -f "$IMAGE" ] || [ ! -f "$TRACE2" ] || [ ! -x "$ST80_RUN" ]; then
    echo "trace2_check: SKIP (missing image, trace2, or st80_run)"
    exit 77
fi

EXPECTED="$(mktemp -t trace2.XXXX)"
ACTUAL="$(mktemp -t st80.XXXX)"
trap 'rm -f "$EXPECTED" "$ACTUAL"' EXIT

awk '/^Bytecode <[0-9]+>/ {
    match($0, /<[0-9]+>/)
    print substr($0, RSTART+1, RLENGTH-2)
}' "$TRACE2" > "$EXPECTED"

CYCLES=$(wc -l < "$EXPECTED" | tr -d ' ')
"$ST80_RUN" -n "$CYCLES" "$IMAGE" > "$ACTUAL" 2>/dev/null

if diff -q "$EXPECTED" "$ACTUAL" > /dev/null; then
    echo "trace2_check: OK ($CYCLES bytecodes byte-for-byte)"
    exit 0
fi

echo "trace2_check: MISMATCH — first 20 diff lines:" >&2
diff "$EXPECTED" "$ACTUAL" | head -20 >&2
exit 1
