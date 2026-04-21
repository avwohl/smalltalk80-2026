# st80-2026 — macOS (AppKit)

Native macOS frontend using SwiftUI + Metal with an AppKit host.
For the Mac Catalyst / iOS / App Store build, see
[`../apple-catalyst/README.md`](../apple-catalyst/README.md).

## Prerequisites

  * Xcode.app installed from the App Store. The Metal shader
    compiler and the matched SwiftUI SDK live inside `Xcode.app`;
    the Command Line Tools alone aren't enough.
  * `cmake` ≥ 3.20:

        brew install cmake

  * `gh` CLI (only needed for the initial clone):

        brew install gh

## Clone and build the VM core

    git clone git@github.com:avwohl/st80-2026.git
    cd st80-2026
    cmake -S . -B build
    cmake --build build

The C++ core, CLI tools (`st80_probe`, `st80_run`, `st80_validate`)
and the CTest suite all fall out of this step.

## Tests

    (cd build && ctest --output-on-failure)

Phase 1 gate — our bytecode stream diffed against Xerox's canonical
`trace2`:

    awk '/^Bytecode <[0-9]+>/ { match($0, /<[0-9]+>/);
         print substr($0, RSTART+1, RLENGTH-2) }' \
         reference/xerox-image/trace2 > /tmp/trace2_bc.txt
    ./build/tools/st80_run -n 499 \
         reference/xerox-image/VirtualImage > /tmp/st80_bc.txt 2>/dev/null
    diff /tmp/st80_bc.txt /tmp/trace2_bc.txt    # empty = green

## Fetch the Xerox image

    mkdir -p reference/xerox-image
    curl -sSLo reference/xerox-image/image.tar.gz \
         http://www.wolczko.com/st80/image.tar.gz
    (cd reference/xerox-image && tar xzf image.tar.gz)

## Build and launch the AppKit app

`app/apple/build-macos-app.sh` drives `swiftc` directly — no Xcode
project required for the AppKit variant.

    app/apple/build-macos-app.sh
    open build/St80.app --args "$PWD/reference/xerox-image/VirtualImage"

You should see the 1983 Xerox Smalltalk-80 desktop. Yellow-click
opens the World Menu; click-and-hold on a text pane opens the
text-operate menu.

## Mouse mapping

  * plain left click        — red (select)
  * right-click / Alt+Left  — yellow (text / operate menu)
  * middle-click / Ctrl+Left — blue (window / frame menu)
