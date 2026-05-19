#!/usr/bin/env bash
# dos_dosiz_gate.sh — the consolidated "st80 works fully under dosiz"
# regression gate.
#
# The DOS port is verified by running the DJGPP-cross binaries inside
# dosiz (https://github.com/avwohl/dosiz — an in-process dosbox-staging
# host with a ring-3 DPMI implementation). This script packages the
# three checks that were otherwise run by hand each iteration into one
# repeatable, CI-able command:
#
#   1. trace2          — st80_run's bytecode stream is byte-for-byte
#                        identical to the Xerox `trace2` reference
#                        (reuses trace2_check.sh with its RUNNER hook).
#   2. snapshot rt     — st80_validate `roundtrip`: load -> saveSnapshot
#                        -> reload yields a bit-identical object graph
#                        (the dosiz AH=40 + O_BINARY write path).
#   3. fake-GUI        — st80_gui_test: boot the image through Bridge.h,
#                        inject synthetic input, assert the live
#                        Smalltalk-80 desktop renders + reacts.
#
# All three already pass with results byte-identical to the native
# host build; this script is the automated guard against regressions
# in either st80 or dosiz.
#
# Usage:
#   dos_dosiz_gate.sh <dos-build-dir> <image> <trace2> [dosiz-exe]
#
#   dos-build-dir   CMake DJGPP build tree (has tools/*.exe, the DOS
#                   st80.exe is optional and not required by the gate)
#   image           Xerox v2 image (host path; copied in as SNAPSHOT.IM)
#   trace2          reference/xerox-image/trace2
#   dosiz-exe       dosiz binary; default $DOSIZ_BIN or
#                   /c/temp/src/dosiz/build/dosiz.exe
#
# Env:
#   DOSIZ_DLL_PATH  prepended to PATH so dosiz finds its MinGW DLLs.
#                   Must be a bash-PATH-safe form (no embedded ':' —
#                   use the MSYS '/c/...' style, not 'C:/...').
#                   Default /c/s/msys64/mingw64/bin
#   DOSIZ_TIMEOUT   per-run hard timeout seconds (default 300)
#
# Exit: 0 all gates pass · 1 a gate failed · 77 prerequisites absent
# (CTest SKIP). Self-contained: stages into a temp 8.3 dir that dosiz
# maps to C:\, cleans up on exit.
#
# Copyright (c) 2026 Aaron Wohl. MIT License.
set -uo pipefail

DOS_BUILD="${1:-}"
IMAGE="${2:-}"
TRACE2="${3:-}"
DOSIZ_BIN="${4:-${DOSIZ_BIN:-/c/temp/src/dosiz/build/dosiz.exe}}"
DOSIZ_DLL_PATH="${DOSIZ_DLL_PATH:-/c/s/msys64/mingw64/bin}"
DOSIZ_TIMEOUT="${DOSIZ_TIMEOUT:-300}"
ST80_DEEP_CYCLES="${ST80_DEEP_CYCLES:-250000}"
# Deterministic pin (HeadlessHal counter clock): the 250000-cycle
# stream is byte-identical native and under dosiz. Keep in sync with
# ST80_DEEP_SHA256 in tests/CMakeLists.txt.
ST80_DEEP_SHA256="${ST80_DEEP_SHA256:-c2f447e7b109a00177448bd1315e01a01f98a096fea760fe9276d9ba86f15f4c}"
export ST80_DEEP_SHA256
HERE="$(cd "$(dirname "$0")" && pwd)"

skip() { echo "dos_dosiz_gate: SKIP ($1)"; exit 77; }

# Resolve to absolute up front: the gates run from a staging dir
# (cd "$STAGE"), so any caller-relative path would dangle there.
# Handles POSIX (/...) and Windows-drive (C:...) absolutes; MSYS
# git-bash has no reliable realpath.
abspath() { case "$1" in /*|[A-Za-z]:*) printf '%s\n' "$1";;
                          *) printf '%s\n' "$(pwd)/$1";; esac; }

[ -n "$DOS_BUILD" ] && [ -n "$IMAGE" ] && [ -n "$TRACE2" ] \
    || skip "usage: dos_dosiz_gate.sh <dos-build-dir> <image> <trace2> [dosiz]"
DOS_BUILD="$(abspath "$DOS_BUILD")"
IMAGE="$(abspath "$IMAGE")"
TRACE2="$(abspath "$TRACE2")"
DOSIZ_BIN="$(abspath "$DOSIZ_BIN")"
[ -f "$IMAGE" ]  || skip "image $IMAGE absent"
[ -f "$TRACE2" ] || skip "trace2 $TRACE2 absent"
[ -f "$DOSIZ_BIN" ] || skip "dosiz $DOSIZ_BIN absent"

RUN_EXE="$DOS_BUILD/tools/st80_run.exe"
VAL_EXE="$DOS_BUILD/tools/st80_validate.exe"
GUI_EXE="$DOS_BUILD/tools/st80_gui_test.exe"
for e in "$RUN_EXE" "$VAL_EXE" "$GUI_EXE"; do
    [ -f "$e" ] || skip "DOS binary $e absent (configure with the DJGPP toolchain)"
done

STAGE="$(mktemp -d -t st80dosgate.XXXX)"
trap 'rm -rf "$STAGE"' EXIT
cp "$RUN_EXE" "$STAGE/ST80RUN.EXE"
cp "$VAL_EXE" "$STAGE/ST80VAL.EXE"
cp "$GUI_EXE" "$STAGE/ST80GT.EXE"
cp "$IMAGE"   "$STAGE/SNAPSHOT.IM"

# dosiz maps the host CWD to C:\ and wants short 8.3 names. This shim
# is the RUNNER prefix: it adds the DLL dir to PATH, the ring-3 DPMI
# flag, and a hard timeout, then exec's dosiz with whatever args the
# caller appends.
RUNNER="$STAGE/dosiz-run.sh"
cat > "$RUNNER" <<EOF
#!/usr/bin/env bash
export PATH="$DOSIZ_DLL_PATH:\$PATH"
export DOSIZ_DPMI_RING3=1
exec timeout $DOSIZ_TIMEOUT "$DOSIZ_BIN" "\$@"
EOF
chmod +x "$RUNNER"

fail=0
pass() { echo "  [PASS] $1"; }
bad()  { echo "  [FAIL] $1"; fail=1; }

echo "dos_dosiz_gate: staging $STAGE  (dosiz=$DOSIZ_BIN)"

# --- 1. trace2 byte-for-byte under dosiz ---------------------------
# trace2_check.sh checks $ST80_RUN exists, builds the expected stream,
# then runs `$RUNNER $ST80_RUN -n N SNAPSHOT.IM`. Run from $STAGE so
# the bare 8.3 names resolve inside dosiz's C:\.
echo "dos_dosiz_gate: [1/4] trace2 ..."
if ( cd "$STAGE" && bash "$HERE/trace2_check.sh" \
        SNAPSHOT.IM "$TRACE2" ST80RUN.EXE "$RUNNER" ); then
    pass "trace2 byte-for-byte"
else
    rc=$?
    [ "$rc" = 77 ] && skip "trace2_check skipped (rc=77)"
    bad "trace2 (rc=$rc)"
fi

# --- 2. deep-run byte-for-byte under dosiz -------------------------
# trace2 only pins 499 cycles; this drives st80_run far past the
# snapshot point. HeadlessHal is deterministic, so ST80_DEEP_SHA256
# (exported above) makes this a byte-for-byte gate vs the native
# reference — a long sustained run is where a dosiz DPMI/memory edge
# or a late primitive would bite, and any drift fails the pin.
echo "dos_dosiz_gate: [2/4] deep-run ..."
if ( cd "$STAGE" && bash "$HERE/deeprun_check.sh" \
        SNAPSHOT.IM ST80RUN.EXE "$ST80_DEEP_CYCLES" "$RUNNER" ) \
        > "$STAGE/deep.log" 2>&1; then
    pass "deep-run ($(grep -oE '[0-9]+ cycles[^)]*' "$STAGE/deep.log" | head -1))"
else
    bad "deep-run (rc=$?)"; sed 's/^/    /' "$STAGE/deep.log"
fi

# --- 3. snapshot save round-trip under dosiz -----------------------
echo "dos_dosiz_gate: [3/4] snapshot roundtrip ..."
if ( cd "$STAGE" && "$RUNNER" ST80VAL.EXE roundtrip SNAPSHOT.IM ) \
        > "$STAGE/rt.log" 2>&1; then
    pass "snapshot roundtrip ($(grep -o 'digest [0-9a-f]*' "$STAGE/rt.log" | head -1))"
else
    bad "snapshot roundtrip (rc=$?)"; sed 's/^/    /' "$STAGE/rt.log"
fi

# --- 4. headless fake-GUI under dosiz ------------------------------
echo "dos_dosiz_gate: [4/4] fake-GUI ..."
if ( cd "$STAGE" && "$RUNNER" ST80GT.EXE SNAPSHOT.IM . ) \
        > "$STAGE/gui.log" 2>&1; then
    pass "fake-GUI ($(grep -o 'pixel delta=[0-9]*' "$STAGE/gui.log" | head -1))"
else
    bad "fake-GUI (rc=$?)"; sed 's/^/    /' "$STAGE/gui.log"
fi

if [ "$fail" = 0 ]; then
    echo "dos_dosiz_gate: OK — st80 works fully under dosiz (4/4)"
    exit 0
fi
echo "dos_dosiz_gate: FAIL — see above"
exit 1
