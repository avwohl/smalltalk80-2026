#!/usr/bin/env bash
# Build St80Catalyst.app — a Mac Catalyst (UIKit-on-macOS) app that
# links the same st80_apple + st80core static libs as the native
# macOS target in ../apple/.
#
# We drive `swiftc` directly — no .xcodeproj needed. The trick is
# the `arm64-apple-ios*-macabi` target plus a `-F` path at the
# macOS SDK's `System/iOSSupport/System/Library/Frameworks` so
# UIKit resolves.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$ROOT/build"
APP_DIR="$BUILD_DIR/St80Catalyst.app"
MACOS_DIR="$APP_DIR/Contents/MacOS"
RES_DIR="$APP_DIR/Contents/Resources"

# Catalyst builds need the full Xcode toolchain (iOSSupport headers
# and frameworks aren't in the Command Line Tools bundle).
if [ -d /Applications/Xcode.app ] && [ -z "${DEVELOPER_DIR-}" ]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
    echo "(using DEVELOPER_DIR=$DEVELOPER_DIR)"
fi

SDK="$(xcrun --sdk macosx --show-sdk-path)"
IOS_SUPPORT="$SDK/System/iOSSupport"

echo "=== Building core + apple static libs for Mac Catalyst slice ==="
# A separate CMake build dir with -target macabi in CXXFLAGS so
# the .o files carry the macCatalyst platform marker. Linking a
# plain-macOS .o into a macabi binary produces
#    ld: building for 'macCatalyst', but linking in object file
#       built for 'macOS'
CATALYST_BUILD="$BUILD_DIR-catalyst"
if [ ! -f "$CATALYST_BUILD/CMakeCache.txt" ]; then
    cmake -S "$ROOT" -B "$CATALYST_BUILD" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
        -DCMAKE_CXX_FLAGS="-target arm64-apple-ios17.0-macabi"
fi
cmake --build "$CATALYST_BUILD" --target st80_apple

echo "=== Staging bundle layout ==="
mkdir -p "$MACOS_DIR" "$RES_DIR"
cp "$SCRIPT_DIR/Shaders.metal" "$RES_DIR/Shaders.metal"
cp "$SCRIPT_DIR/Info.plist" "$APP_DIR/Contents/Info.plist"

echo "=== Compiling Swift (Catalyst / macabi) ==="
swiftc \
    -parse-as-library \
    -O \
    -target "$(uname -m)-apple-ios17.0-macabi" \
    -sdk "$SDK" \
    -F "$IOS_SUPPORT/System/Library/Frameworks" \
    -import-objc-header "$ROOT/src/include/Bridge.h" \
    -I "$ROOT/src/include" \
    -L "$CATALYST_BUILD/src/platform/apple" -lst80_apple \
    -L "$CATALYST_BUILD" -lst80core \
    -lc++ \
    -framework UIKit -framework Metal -framework MetalKit \
    -framework SwiftUI -framework Foundation \
    -o "$MACOS_DIR/St80Catalyst" \
    "$SCRIPT_DIR/AppDelegate.swift" \
    "$SCRIPT_DIR/ContentView.swift" \
    "$SCRIPT_DIR/MetalView.swift" \
    "$SCRIPT_DIR/MetalRenderer.swift" \
    "$SCRIPT_DIR/St80MTKView.swift" \
    "$SCRIPT_DIR/St80Image.swift" \
    "$SCRIPT_DIR/ImageManager.swift" \
    "$SCRIPT_DIR/ImageLibraryView.swift" \
    "$SCRIPT_DIR/AboutView.swift" \
    "$SCRIPT_DIR/DocumentExporter.swift"

touch "$APP_DIR"

echo ""
echo "Built: $APP_DIR"
echo "Run:   open $APP_DIR --args /path/to/VirtualImage"
