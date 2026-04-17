# st80-2026

A Smalltalk-80 implementation of the 1983 Blue Book Xerox virtual
image for modern systems. Target order: macOS / Mac Catalyst, iOS,
Windows, Linux. Interpreter first, JIT afterwards on every target
except iOS.

**Status**: **Phase 2**, running on native macOS and Mac Catalyst.
The Xerox v2 image boots to its desktop, clicks open context
menus, cursors render, keyboard events deliver. Phase 1 gate
(Xerox `trace2` byte-for-byte) stays green.

See [`docs/plan.md`](docs/plan.md) for the roadmap,
[`docs/architecture.md`](docs/architecture.md) for how the code is
laid out, and [`docs/changes.md`](docs/changes.md) for the build-
by-build log.

## Quickstart (macOS)

Prerequisites:

  * Xcode.app installed (the Metal shader compiler and matched
    SwiftUI SDK live inside it; the Command Line Tools alone aren't
    enough)
  * `cmake` ≥ 3.20 — `brew install cmake`
  * `gh` CLI for the one-time GitHub clone — `brew install gh`

Clone and build the core libraries and CLI tools:

    git clone git@github.com:avwohl/st80-2026.git
    cd st80-2026
    cmake -S . -B build
    cmake --build build

Fetch the Xerox virtual image (released by Xerox 1983; hosted by
Mario Wolczko):

    mkdir -p reference/xerox-image
    curl -sSLo reference/xerox-image/image.tar.gz \
         http://www.wolczko.com/st80/image.tar.gz
    (cd reference/xerox-image && tar xzf image.tar.gz)

Run the CTest suite:

    (cd build && ctest --output-on-failure)

Sanity-check the Phase 1 gate — diffing our bytecode stream against
Xerox's canonical `trace2`:

    awk '/^Bytecode <[0-9]+>/ { match($0, /<[0-9]+>/);
         print substr($0, RSTART+1, RLENGTH-2) }' \
         reference/xerox-image/trace2 > /tmp/trace2_bc.txt
    ./build/tools/st80_run -n 499 \
         reference/xerox-image/VirtualImage > /tmp/st80_bc.txt 2>/dev/null
    diff /tmp/st80_bc.txt /tmp/trace2_bc.txt    # empty = green

Build and launch the macOS app (no Xcode project — the script
drives `swiftc` directly):

    app/apple/build-macos-app.sh
    open build/St80.app --args "$PWD/reference/xerox-image/VirtualImage"

You should see the 1983 Xerox Smalltalk-80 desktop. Yellow-click
opens the World Menu; click-and-hold on a text pane opens the
text-operate menu.

### Mac Catalyst build

A second app target lives in `app/apple-catalyst/`. Same C++ core,
same Swift patterns, but the frontend uses UIKit (which is what
iOS will use). It builds alongside the AppKit app:

    app/apple-catalyst/build-catalyst-app.sh
    open build/St80Catalyst.app --args "$PWD/reference/xerox-image/VirtualImage"

`otool -l` confirms the Catalyst binary carries `platform 6`
(macCatalyst) while the AppKit binary is `platform 1` (macOS).
Both render the identical Xerox desktop.

### Xcode project build (for App Store submission)

`st80.xcodeproj` is the proper Xcode project for producing a signed
archive uploadable to App Store Connect. Build from the command
line or open the project in Xcode.app:

    # one-time: copy and edit Local.xcconfig with your Apple team ID
    cp Local.xcconfig.example Local.xcconfig
    # edit Local.xcconfig → set DEVELOPMENT_TEAM = XXXXXXXXXX

    # build the xcframework, then the app
    xcodebuild -project st80.xcodeproj -scheme St80 \
        -configuration Release \
        -destination 'platform=macOS,variant=Mac Catalyst' \
        -derivedDataPath build-xcodeproj \
        build

`st80.xcodeproj` supports `SUPPORTS_MACCATALYST = YES` and
`TARGETED_DEVICE_FAMILY = "1,2,6"` — same target can build for
iPhone, iPad, and Mac Catalyst. For App Store submission: open in
Xcode, `Product → Archive`, then upload via the Organizer.

A `Check XCFramework Freshness` build phase regenerates
`Frameworks/St80Core.xcframework` (via `scripts/build-xcframework.sh`)
whenever a VM source file is newer than the xcframework's
`Info.plist`, so editing C++ core code and hitting build in Xcode
Just Works.

## What's in the box

    src/core/                C++17 VM core — Oops, RealWordMemory,
                             ObjectMemory, BitBlt, Interpreter,
                             Primitives. Pure portable code.
    src/core/hal/            IHal, IFileSystem — platform-abstraction
                             interfaces the core speaks through.
    src/include/Bridge.h     Pure-C API the frontend consumes.
    src/platform/apple/      AppleHal + Bridge.h implementation.
    src/platform/posix/      POSIX IFileSystem.
    src/platform/headless/   No-op IHal (tests + tools).
    app/apple/               SwiftUI + Metal AppKit frontend (macOS).
    app/apple-catalyst/      SwiftUI + Metal UIKit frontend (Catalyst).
    tools/                   st80_probe (loader sanity), st80_run
                             (trace tool).
    tests/                   CTest smoke + end-to-end bridge test.
    reference/               gitignored — clone of dbanay/Smalltalk
                             (MIT) and the Xerox image go here.

## Credit

  * Object memory, BitBlt, interpreter, and primitive table ported
    from [dbanay/Smalltalk](https://github.com/dbanay/Smalltalk)
    under MIT. Dan Banay did the hard work; this project wouldn't
    exist without his clean C++ port from the Blue Book spec.
  * Endian-aware loading mirrors
    [dbanay's `imageswapper.c`](https://github.com/dbanay/Smalltalk/blob/master/misc/imageswapper.c)
    but does the swap at load time rather than in a separate
    preprocessing pass.
  * The virtual image itself is Xerox's 1983 release, distributed
    by Mario Wolczko at <http://www.wolczko.com/st80/>.
  * Blue Book: Adele Goldberg and David Robson, *Smalltalk-80: The
    Language and its Implementation*, Addison-Wesley 1983. Chapters
    26 – 30 are the VM spec.

See [`THIRD_PARTY_LICENSES`](THIRD_PARTY_LICENSES) for the full
attribution notices.

## License

MIT. See [`LICENSE`](LICENSE).
