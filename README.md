# st80-2026

A Smalltalk-80 implementation of the 1983 Blue Book Xerox virtual
image for modern systems. Target order: macOS / Mac Catalyst, iOS,
Windows, Linux. Interpreter first, JIT afterwards on every target
except iOS.

**Status**: **Phase 4** complete — running natively on macOS, Mac
Catalyst, x86_64 Linux, and Windows (x64). The Xerox v2 image boots
to its desktop on all four; clicks open context menus, cursors
render, keyboard events deliver. Linux `.deb` / `.rpm` and Windows
NSIS / WiX / MSIX packages build via CPack. Phase 1 gate (Xerox
`trace2` byte-for-byte) stays green everywhere.

## Documentation

  * Roadmap: [`docs/plan.md`](docs/plan.md)
  * Architecture: [`docs/architecture.md`](docs/architecture.md)
  * Build-by-build log: [`docs/changes.md`](docs/changes.md)

## Build and run

Per-platform build and usage docs live next to each frontend. Pick
yours:

  * macOS (SwiftUI + Metal, AppKit)
    → [`app/apple/README.md`](app/apple/README.md)

  * Mac Catalyst + Xcode project for App Store submission
    → [`app/apple-catalyst/README.md`](app/apple-catalyst/README.md)

  * Linux (SDL2 + `.deb` / `.rpm`)
    → [`app/linux/README.md`](app/linux/README.md)

  * Windows (pure Win32 + GDI, NSIS / WiX / MSIX)
    → [`app/windows/README.md`](app/windows/README.md)

Each file covers prerequisites from a clean OS install, cloning,
the build command, running the CTest suite, and how to get an
image loaded.

## Getting a Smalltalk-80 image

All four frontends will happily load Xerox's original 1983 virtual
image, released by Xerox and hosted by Mario Wolczko at
<http://www.wolczko.com/st80/>. A mirror split into individual
release assets (no tarball extraction) lives at
<https://github.com/avwohl/st80-images/releases/tag/xerox-v2> —
this is what the Mac Catalyst, iOS, and Windows launchers download
when you click "Download Xerox v2".

The image loader auto-detects big-endian (raw Xerox `VirtualImage`)
vs. little-endian (dbanay's pre-swapped `VirtualImageLSB`), so
either variant works. See [`docs/image-preprocessing.md`](docs/image-preprocessing.md).

## What's in the box

    src/core/                C++17 VM core — Oops, RealWordMemory,
                             ObjectMemory, BitBlt, Interpreter,
                             Primitives. Pure portable code.
    src/core/hal/            IHal, IFileSystem — platform-abstraction
                             interfaces the core speaks through.
    src/include/Bridge.h     Pure-C API the frontend consumes.
    src/platform/apple/      AppleHal + Bridge.h implementation.
    src/platform/linux/      LinuxHal + Bridge.h implementation.
    src/platform/windows/    WindowsHal + Bridge.h implementation.
    src/platform/posix/      POSIX IFileSystem (macOS + Linux).
    src/platform/win/        Win32 IFileSystem.
    src/platform/common/     Host-neutral EventQueue.
    src/platform/headless/   No-op IHal (tests + tools).
    app/apple/               SwiftUI + Metal AppKit frontend.
    app/apple-catalyst/      SwiftUI + Metal UIKit frontend.
    app/linux/               SDL2 frontend.
    app/windows/             Pure Win32 + GDI frontend with launcher.
    cmake/LinuxPackaging.cmake
                             CPack config for .deb and .rpm.
    packaging/linux/         .desktop file.
    packaging/windows/       NSIS / WiX / MSIX release driver.
    tools/                   st80_probe (loader sanity), st80_run
                             (trace tool), st80_validate.
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

## Related

Other repos in this collection:

- **[iospharo](https://github.com/avwohl/iospharo)** — Pharo Smalltalk VM for iOS and Mac Catalyst (interpreter-only, low-bit oop encoding for ASLR compatibility).
- **[validate_smalltalk_image](https://github.com/avwohl/validate_smalltalk_image)** — Standalone validator and export tool for Spur-format Smalltalk image files (heap integrity, SHA-256 manifests, reference graphs).
- **[pharo-headless-test](https://github.com/avwohl/pharo-headless-test)** — Headless Pharo test runner with a fake GUI; clicks menus, takes screenshots, runs SUnit without a display.
- **[soogle](https://github.com/avwohl/soogle)** — Smalltalk code search engine that indexes packages across Pharo, Squeak, GemStone and more.

## License

MIT. See [`LICENSE`](LICENSE).
