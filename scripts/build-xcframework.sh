#!/usr/bin/env bash
# Build Frameworks/St80Core.xcframework — the single static library
# that Xcode's St80 target links against. Three slices:
#
#     ios-arm64                           iPhone / iPad device
#     ios-arm64_x86_64-maccatalyst        Mac Catalyst (zippered)
#     ios-arm64_x86_64-simulator          iOS Simulator
#
# Each slice builds libst80core + libst80_apple and merges them into
# a single libSt80Core.a via `libtool -static` so the xcframework has
# one library per slice. Xcode's "Check XCFramework Freshness" build
# phase invokes this script when a VM source file is newer than the
# xcframework's Info.plist, per iospharo's pattern.
set -euo pipefail

# Homebrew tools (cmake, ninja) — needed when Xcode runs this from a
# sandboxed build phase where PATH is minimal.
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
cd "$ROOT"

# Full Xcode required for iOSSupport SDK bits and multi-slice SDK paths.
if [ -d /Applications/Xcode.app ] && [ -z "${DEVELOPER_DIR-}" ]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi

BUILD_BASE="$ROOT/build-xcframework"
OUT="$ROOT/Frameworks/St80Core.xcframework"
TMP="$ROOT/Frameworks/St80Core-tmp.xcframework"

echo "=== Building St80Core.xcframework (iOS / Catalyst / Simulator) ==="

rm -rf "$BUILD_BASE" "$TMP"
mkdir -p "$BUILD_BASE" "$ROOT/Frameworks"

# Build one slice. Produces $slice/libSt80Core.a containing the
# merged st80core + st80_apple symbols.
build_slice() {
    local slice="$1"    # friendly name + directory
    local sdk="$2"      # SDK identifier for xcrun
    local arch="$3"     # cpu arch
    local extra="$4"    # extra -target / -min-version flags

    echo ""
    echo "--- slice: $slice ($arch, $sdk) ---"

    local sdkpath
    sdkpath="$(xcrun --sdk "$sdk" --show-sdk-path)"
    local builddir="$BUILD_BASE/$slice"
    mkdir -p "$builddir"

    local cflags="-arch $arch -isysroot $sdkpath $extra -O2"

    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Darwin \
        -DCMAKE_OSX_ARCHITECTURES="$arch" \
        -DCMAKE_OSX_SYSROOT="$sdkpath" \
        -DCMAKE_C_FLAGS="$cflags" \
        -DCMAKE_CXX_FLAGS="$cflags" \
        -DBUILD_TESTING=OFF \
        -B "$builddir" -S .

    cmake --build "$builddir" --target st80_apple -- -j"$(sysctl -n hw.ncpu)"

    # Merge core + apple into one .a for the xcframework.
    local merged="$builddir/libSt80Core.a"
    libtool -static -o "$merged" \
        "$builddir/libst80core.a" \
        "$builddir/src/platform/apple/libst80_apple.a"

    echo "  $(lipo -info "$merged" 2>&1)"
}

# Combine two single-arch .a files into a fat universal.
make_universal() {
    local out="$1"; shift
    mkdir -p "$(dirname "$out")"
    lipo -create "$@" -output "$out"
    echo "universal: $(lipo -info "$out" 2>&1)"
}

# ─── iOS device (arm64 only) ─────────────────────────────────────
build_slice "ios-arm64"               iphoneos        arm64   "-mios-version-min=15.0"

# ─── Mac Catalyst (arm64 + x86_64) ───────────────────────────────
build_slice "catalyst-arm64"          macosx          arm64   "-target arm64-apple-ios15.0-macabi"
build_slice "catalyst-x86_64"         macosx          x86_64  "-target x86_64-apple-ios15.0-macabi"
make_universal "$BUILD_BASE/catalyst-universal/libSt80Core.a" \
    "$BUILD_BASE/catalyst-arm64/libSt80Core.a" \
    "$BUILD_BASE/catalyst-x86_64/libSt80Core.a"

# ─── iOS Simulator (arm64 + x86_64) ──────────────────────────────
build_slice "sim-arm64"               iphonesimulator arm64   "-mios-simulator-version-min=15.0"
build_slice "sim-x86_64"              iphonesimulator x86_64  "-mios-simulator-version-min=15.0"
make_universal "$BUILD_BASE/sim-universal/libSt80Core.a" \
    "$BUILD_BASE/sim-arm64/libSt80Core.a" \
    "$BUILD_BASE/sim-x86_64/libSt80Core.a"

# ─── Assemble the xcframework ────────────────────────────────────
echo ""
echo "=== xcodebuild -create-xcframework ==="
xcodebuild -create-xcframework \
    -library "$BUILD_BASE/ios-arm64/libSt80Core.a" \
    -library "$BUILD_BASE/catalyst-universal/libSt80Core.a" \
    -library "$BUILD_BASE/sim-universal/libSt80Core.a" \
    -output "$TMP" > /dev/null

# Atomic swap — don't leave Xcode with a partial xcframework on mid-
# build failure.
rm -rf "$OUT"
mv "$TMP" "$OUT"
touch "$OUT/Info.plist"

echo ""
echo "=== Done: $OUT ==="
for dir in "$OUT"/*/; do
    name="$(basename "$dir")"
    [ -f "$dir/libSt80Core.a" ] && echo "  $name: $(lipo -info "$dir/libSt80Core.a" 2>&1)"
done
