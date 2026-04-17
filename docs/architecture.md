# Architecture

How st80-2026 is structured, and why. Kept in sync with the code;
the repo layout is the source of truth and this file narrates it.

## Layering — 30-second tour

    +---------------------------------------------------+
    |  app/apple        Swift + Metal frontend          |
    |  app/win32        (future)                        |
    |  app/linux        (future)                        |
    +----------------- C boundary ---------------------+
    |  src/include/Bridge.h   pure-C API               |
    +---------------------------------------------------+
    |  src/platform/apple/    AppleHal, AppleBridge     |
    |  src/platform/posix/    PosixFileSystem           |
    |  src/platform/headless/ HeadlessHal (tests)       |
    +----------------- IHal / IFileSystem --------------+
    |  src/core/              Interpreter, ObjectMemory,|
    |                         BitBlt, Oops, …           |
    +---------------------------------------------------+

Rule: the core is pure C++17 with no platform deps. Everything that
would bind us to an OS lives behind `IHal` and `IFileSystem`
(the "HAL"). The frontend only speaks to the core through the pure-C
`Bridge.h`; no C++ names leak out.

## `src/core/` — the VM

Ported from dbanay/Smalltalk @ `ab6ab55` under MIT. Bug-for-bug
faithful except for two changes:

  * `class IHardwareAbstractionLayer` → `class IHal` (shorter name;
    every callsite was already in our namespace).
  * Endian-aware loading (see below).

`Oops.hpp` — the well-known Object Table indices from the Xerox
`specialObjects` array (nil, true, false, class pointers for
SmallInteger/String/Array/etc., `doesNotUnderstand:` selector, …).
BB p. 576.

`RealWordMemory.hpp` — 16 segments × 65 536 16-bit words = 2 MiB
logical address space. Byte- and bit-range accessors. G&R p. 656.

`ObjectMemory.{hpp,cpp}` — Blue Book chapter 27. Object Table in
segment 15, heap in segments 0 – 14. GC: pure mark/sweep
(`GC_REF_COUNT` disabled per `docs/plan.md`; `GC_MARK_SWEEP` on).
**Endian-aware loader**: `loadObjectTable` auto-detects whether the
image on disk was written big-endian (raw Xerox v2) or little-endian
(dbanay's pre-swapped `VirtualImageLSB`) by sanity-checking the
header's `objectTableLength` against the file size, and sets a
`swapOnLoad` flag that `loadObjects` honours. Per-class body-word
handling in `swapObjectBodyBytes` mirrors
`dbanay/misc/imageswapper.c`: CompiledMethod swaps header + literals
only; Float does a 4-byte reversal; pointer / DisplayBitmap /
WordArray swap every body word; byte-type objects leave their bodies
raw. See `docs/image-preprocessing.md`.

`Interpreter.{hpp,cpp}` — Blue Book chapter 28. Switch-dispatched
bytecode loop. Primitives (chapter 29) are inlined in this same file.
The class inherits `IGCNotification`; a virtual destructor was added
(dbanay's original omitted it, which is UB for base-pointer delete
through the inheritance).

`BitBlt.{hpp,cpp}` — BB chapter 18. 1-bit raster engine.
Single most bug-prone piece in any Smalltalk port; ported with no
logic changes and no deviations from dbanay's tested implementation.

## `src/core/hal/`

`IHal.hpp` (renamed from `IHardwareAbstractionLayer`) — monolithic
HAL contract per BB §29. Clock, cursor, display, input-word queue,
semaphore scheduling, lifecycle. We kept it monolithic during
porting; `docs/plan.md` calls for splitting into IDisplay / IInput /
IClock / … later, but the current interpreter consumes all of these
through one pointer and the cost of that split isn't worth paying
until we have more than one platform implementation in production.

`IFileSystem.hpp` — the other, separate interface. Snapshot load/save
and sources/changes file I/O route through it.

## `src/include/Bridge.h` — the C API

This is the contract between the VM core and any frontend. Pure C —
Swift, Objective-C, and plain C all consume it directly. No class
types escape.

Lifecycle (`st80_init` / `run` / `stop` / `shutdown`), display
(`st80_display_width` / `height` / `pixels` / `sync`), cursor
(`st80_cursor_image`), events (`st80_post_mouse_*`,
`st80_post_key_*`). Rectangles are the one struct that crosses the
boundary.

### Threading contract (Phase 2a)

All Bridge.h calls are expected from the **same thread**. The VM's
`asynchronousSignal` walks a shared array without a lock, so calling
`st80_post_*` concurrently with `st80_run` would race. The Swift
frontend honours this by running both from the main thread (display
link drives `st80_run`; `NSEvent` handlers post between frames).
A proper worker-thread layout with a lock-free signal path is a
future Phase 2b task.

## `src/platform/`

`apple/AppleHal.{hpp,cpp}` — `IHal` implementation. Owns the RGBA8
staging buffer (populated by `st80_display_sync`), a thread-safe
`EventQueue` of Blue Book input words, a shadow cursor position
(read back by `get_cursor_location`), and the 16-word cursor bitmap
the image writes via `set_cursor_image`. Scheduled semaphores
(`signal_at`) are polled each cycle from the bridge's run loop.

`apple/AppleBridge.cpp` — implements `Bridge.h` on top of
`AppleHal` + `Interpreter` + `PosixFileSystem`. A single global
`Runtime` struct owns all three. Key subtlety here: the input
semaphore is signalled **per word**, not per event. The image wakes
on each signal, reads one word, blocks on the semaphore again.
Signalling once and enqueueing four words strands three of them.
Follows `dbanay/main.cpp:532`.

Display expansion (1-bit → RGBA8) is in `AppleBridge.cpp`'s
`st80_display_sync` because that's where the Interpreter is in scope;
the HAL owns the output buffer but doesn't know how to read the VM's
display bitmap.

`apple/EventQueue.hpp` — header-only mutex-guarded deque of
`uint16_t`. Push from UI thread, pop from VM (same thread under the
Phase-2a contract, but the lock keeps the future multi-threaded
refactor honest).

`posix/PosixFileSystem.hpp` — header-only POSIX implementation of
`IFileSystem`. Works on macOS / Catalyst / Linux / BSD. The
`_WIN32` branches are kept intact against Phase-4 Win32 work.

`headless/HeadlessHal.hpp` — a no-op `IHal` used by the CLI tools
and tests. Any graphics-touching call is a no-op; `error` and
`exit_to_debugger` abort loudly so stray GUI-path calls get noticed.

## `app/apple/` — SwiftUI + Metal frontend

Built from the command line by `build-macos-app.sh` — no Xcode
project required. `swiftc` compiles the Swift sources against
`libst80_apple.a`, using `-import-objc-header
src/include/Bridge.h` so Swift sees the C API directly.

`main.swift` — classic `NSApplication` entry point. We started with
`@main struct SwiftUIApp` but under `swiftc -parse-as-library` the
Scene body doesn't instantiate; the process starts but no window
appears. Routing the SwiftUI `ContentView` through `NSHostingView`
inside an explicit `NSWindow` created in `AppDelegate` is reliable.

`ContentView.swift` — a one-liner that embeds the Metal view.

`MetalView.swift` — `NSViewRepresentable` around an MTKView
subclass (`St80MTKView`).

`MetalRenderer.swift` — `MTKViewDelegate`. Per frame: run ~4000 VM
cycles, sync the dirty rect, upload pixel bytes into a Metal texture,
refresh the custom NSCursor, draw the fullscreen quad.

`St80MTKView.swift` — MTKView subclass that hosts mouse / keyboard /
cursor logic. Maps `NSEvent` to the Bridge C API, flipping NSView's
Y-up to the VM's Y-down. Modifier → button mapping for single-button
trackpads: plain = red, ⌥ = yellow, ⌘ = blue. Rebuilds an `NSCursor`
lazily when the image's `set_cursor_image` bits change.

`Shaders.metal` — vertex + fragment for a fullscreen quad. Compiled
at **runtime** via `MTLDevice.makeLibrary(source:)` so the build
doesn't depend on the offline Metal toolchain (a separately-
downloaded Xcode component).

## Tests

`tests/core_smoke_test.cpp` — static_asserts on the OOP constants,
round-trips on `RealWordMemory`. Runs without the Xerox image.

`tests/bridge_api_test.cpp` — boots the image through `Bridge.h`,
runs 3 000 cycles, posts a full event sequence (mouse + key), runs
another batch, posts a yellow-click, and checks that the display
changed by at least 100 RGBA pixels. Skips (CTest SKIP_RETURN_CODE =
77) if `reference/xerox-image/VirtualImage` isn't on disk.

## CLI tools

`tools/st80_probe <image>` — minimal loader sanity check. Useful
when the loader itself needs debugging before you drag the full VM
in.

`tools/st80_run [-n cycles] <image>` — run N VM cycles and emit one
decimal bytecode per line on stdout. The trace2 gate check is a diff
between the first 499 lines of its output and the Xerox `trace2`
file (see `docs/trace-verification.md`).

## Build paths

    CMake                 CMakeLists.txt            libst80core.a + tests
                          src/platform/apple/
                            CMakeLists.txt          libst80_apple.a
                          tools/CMakeLists.txt      st80_probe, st80_run

    App bundle            app/apple/build-macos-app.sh  → build/St80.app

## Things that intentionally *aren't* here (yet)

  * Mac Catalyst target. Needs an .xcodeproj and UIKit rewrites of
    the frontend (NSView → UIView, NSCursor → UIPointerInteraction,
    NSEvent → UIEvent). Planned.
  * iOS target. Phase 3. Touch → 3-button mapping; soft keyboard.
  * Win32 frontend. Phase 4.
  * Linux frontend. Phase 4.
  * JIT. Phase 6. `docs/jit-plan.md` has the copy-and-patch design.
  * Saved-image endian portability. `saveObjects` currently writes
    host-native; the auto-detect on re-load handles it, but if we
    want canonical BE images, that's a separate change.
