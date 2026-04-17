#!/usr/bin/env bash
# Build St80.app — a native macOS SwiftUI+Metal app that links
# against the pre-built st80core + st80_apple static libraries.
#
# Mac Catalyst build lands in a follow-up; it reuses the same Swift
# source set but goes through an Xcode-driven xcframework slice.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$ROOT/build"
APP_DIR="$BUILD_DIR/St80.app"
MACOS_DIR="$APP_DIR/Contents/MacOS"
RES_DIR="$APP_DIR/Contents/Resources"

# Point at full Xcode for Swift's matched frameworks (SwiftUI,
# MetalKit). `xcode-select` often still points at the Command Line
# Tools path, which lacks those SDK overlays.
if [ -d /Applications/Xcode.app ] && [ -z "${DEVELOPER_DIR-}" ]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
    echo "(using DEVELOPER_DIR=$DEVELOPER_DIR)"
fi

echo "=== Ensuring st80core + st80_apple are built ==="
cmake --build "$BUILD_DIR" --target st80_apple

echo "=== Staging Metal shader source ==="
# Runtime compilation avoids depending on the (separately-downloaded)
# Metal offline toolchain. The .metal file is just text; we compile
# it at startup via MTLDevice.makeLibrary(source:).
mkdir -p "$RES_DIR"
cp "$SCRIPT_DIR/Shaders.metal" "$RES_DIR/Shaders.metal"

echo "=== Compiling Swift ==="
mkdir -p "$MACOS_DIR"
# -import-objc-header brings the Bridge C API into Swift's view
# directly, no bridging-header module map needed.
swiftc \
    -O \
    -target "$(uname -m)-apple-macos13.0" \
    -import-objc-header "$ROOT/src/include/Bridge.h" \
    -I "$ROOT/src/include" \
    -L "$BUILD_DIR/src/platform/apple" -lst80_apple \
    -L "$BUILD_DIR" -lst80core \
    -lc++ \
    -framework Metal -framework MetalKit -framework SwiftUI -framework AppKit \
    -o "$MACOS_DIR/St80" \
    "$SCRIPT_DIR/main.swift" \
    "$SCRIPT_DIR/ContentView.swift" \
    "$SCRIPT_DIR/MetalView.swift" \
    "$SCRIPT_DIR/MetalRenderer.swift" \
    "$SCRIPT_DIR/St80MTKView.swift"

echo "=== Installing Info.plist ==="
cp "$SCRIPT_DIR/Info.plist" "$APP_DIR/Contents/Info.plist"

# Touch the bundle so Launch Services re-reads its Info.plist.
touch "$APP_DIR"

echo ""
echo "Built: $APP_DIR"
echo "Run:   open $APP_DIR"
echo "       (or with a specific image: open $APP_DIR --args /path/to/VirtualImage)"
