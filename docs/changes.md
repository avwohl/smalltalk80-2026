# Changes

User-visible changes. Most-recent build on top.

## build 26 — 2026-04-17

**Windows port (x64) — Phase 4 second target.**

Second non-Apple target, done the same way the Linux slice was:
a dedicated `src/platform/windows/` directory behind `IHal` and
`Bridge.h`, a dedicated `src/platform/win/` with the IFileSystem
implementation, and a pure-Win32 frontend under `app/windows/`.
No `#ifdef` mixed into the portable layers.

The frontend deliberately does not use SDL. On Apple we render
through SwiftUI+Metal; on Linux we use SDL2 (lingua-franca of X11
+ Wayland + KMS); on Windows we use native Win32 + GDI
`StretchDIBits` directly. That avoids shipping SDL2.dll in the
installer + MSIX, which in turn keeps the Microsoft Store
submission self-contained (no third-party redistributables to
declare).

New files:

  - `src/platform/windows/WindowsHal.{hpp,cpp}` — IHal impl.
    RGBA8 staging buffer + dirty rect + `signal_at` scheduling +
    shared-header EventQueue. Wall-clock Smalltalk epoch
    (1901-01-01) from `chrono::system_clock`. Structurally
    identical to LinuxHal and AppleHal; no Win32 calls at all —
    the portable `<chrono>` / `<mutex>` / `<atomic>` subset is
    enough for the HAL contract.
  - `src/platform/windows/WindowsBridge.cpp` — implements
    `Bridge.h` on top of WindowsHal + Interpreter +
    WindowsFileSystem. Splits image paths on both `\` and `/`
    so drag-and-drop from Explorer and CLI / shell usage both
    work.
  - `src/platform/windows/CMakeLists.txt` — builds the
    `st80_windows` static lib; wired in via `if(WIN32)` from
    the top-level `CMakeLists.txt`.
  - `src/platform/win/WindowsFileSystem.hpp` — header-only
    Win32 implementation of IFileSystem. CRT-level `_open` /
    `_read` / `_write` / `_lseek` / `_chsize` / `_fstat64` /
    `_commit` for file I/O (matches IFileSystem's `int`
    file-handle contract), Win32 `FindFirstFileA` /
    `MoveFileExA` / `DeleteFileA` / `GetFileAttributesA` for
    directory / rename / delete / stat.
  - `src/platform/win/HostFileSystem.hpp` +
    `src/platform/posix/HostFileSystem.hpp` — one-line alias
    headers so tools (`st80_probe`, `st80_run`,
    `st80_validate`) include `HostFileSystem.hpp` and get the
    right IFileSystem impl per platform. CMake picks the
    include dir; no `#ifdef` in source.
  - `app/windows/st80_windows_main.cpp` — pure-Win32 desktop
    frontend. `WinMain` entry (/SUBSYSTEM:WINDOWS, no console
    window), `CommandLineToArgvW` + UTF-8 conversion for
    arguments, `RegisterClassExW` + `CreateWindowExW` window,
    `PeekMessage` idle loop interleaved with `st80_run`,
    `StretchDIBits` with a top-down 32-bit BI_RGB DIB for the
    1-bit VM display buffer. Mouse buttons map to red / yellow
    / blue; Ctrl+Left = blue, Alt+Left = yellow for the common
    single-button-mouse case. `WM_CHAR` feeds 7-bit ASCII into
    `st80_post_key_down`; `VK_DELETE` is routed manually since
    it has no WM_CHAR counterpart. `--no-window` flag drives a
    headless smoke-test loop.
  - `app/windows/st80-win.rc` + `st80-win.manifest` +
    `st80-win-icon.rc` — side-by-side manifest enabling
    per-monitor DPI v2, Common Controls v6, long-path,
    UTF-8 active code page; VS_VERSION_INFO populated from
    PROJECT_VERSION at configure time; icon resource guarded
    by `EXISTS` so a fresh clone builds without requiring the
    binary `.ico` asset.
  - `app/windows/CMakeLists.txt` — `WIN32_EXECUTABLE` target;
    links `gdi32` / `user32` / `shell32` / `comdlg32`; embeds
    the .rc under `$<COMPILE_LANGUAGE:RC>` so rc-only defines
    don't pollute the C++ compilation.
  - `cmake/WindowsPackaging.cmake` — CPack with NSIS + WIX
    generators (self-extracting `.exe` installer + traditional
    `.msi`), plus a custom `st80_appx_layout` target that
    stages an MSIX / AppX directory (`AppxManifest.xml` at
    root, `bin/st80-win.exe`, `Assets/`) ready for
    `MakeAppx.exe pack`. Upgrade GUID is stable across
    versions so the MSI upgrades cleanly rather than
    side-installing.
  - `packaging/windows/AppxManifest.xml.in` — CMake template
    for the AppxManifest, expanded at configure time with
    version + architecture + publisher. Targets Windows 10
    1809+ (first MSIX-capable build). Declares
    `runFullTrust` — the capability a Win32 `.exe` needs to
    live inside a Store package.
  - `packaging/windows/pack-msix.ps1` — PowerShell wrapper
    around `MakeAppx.exe pack` + optional `SignTool.exe sign`
    for side-loading. Auto-discovers the SDK tools under
    `%ProgramFiles(x86)%\Windows Kits\10\bin\*`.
  - `packaging/windows/build-release.ps1` — one-shot release
    build: configure → build → test → NSIS + WIX + MSIX.
  - `packaging/windows/Assets/` + `resources/windows/` — asset
    staging dirs with README-documented filename expectations
    (StoreLogo.png, Square150x150Logo.png, Wide310x150Logo.png,
    etc. for MSIX; st80.ico for the installer + EXE).

Tools + tests changes:

  - `tools/{st80_probe,st80_run,st80_validate}.cpp` — switched
    from `#include "PosixFileSystem.hpp"` to
    `#include "HostFileSystem.hpp"`. Path splits now accept
    both `\` and `/`.
  - `tools/CMakeLists.txt` — per-platform `ST80_TOOLS_FS_DIR`
    selects the correct alias header (no `#ifdef`).
  - `tests/CMakeLists.txt` — `trace2_check.sh` (bash) gated to
    `if(UNIX)` so Windows CTest runs don't fail on the missing
    shell; the Mac and Linux toolchains still see it.
  - Top-level `CMakeLists.txt` — MSVC-only block enables
    `/utf-8 /Zc:preprocessor /EHsc`, pre-defines
    `WIN32_LEAN_AND_MEAN`, `NOMINMAX`, and
    `_CRT_SECURE_NO_WARNINGS` (the CRT's POSIX-emulation
    wrappers trigger deprecation noise otherwise).
    `enable_language(RC)` inside the `if(WIN32)` block brings
    resource compilation online.

Build + package commands (x64, VS 2022):

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release --parallel
    cd build && ctest -C Release --output-on-failure
    cd build && cpack -G NSIS -C Release      # st80-0.1.0-Windows-AMD64.exe
    cd build && cpack -G WIX  -C Release      # st80-0.1.0-Windows-AMD64.msi
    cmake --build build --target st80_appx_layout --config Release
    powershell -ExecutionPolicy Bypass -File packaging/windows/pack-msix.ps1 `
        -Layout build/st80-0.1.0-appx -Output build/st80-0.1.0.msix

Or the one-shot:

    powershell -ExecutionPolicy Bypass `
        -File packaging/windows/build-release.ps1 -Arch x64

ARM64 is reachable by passing `-A ARM64` to the configure step —
the Windows slice is architecture-agnostic and doesn't touch the
JIT (Phase 6). A separate pass will wire the ARM64 CI job once
the Phase-6 JIT backend needs it.

## build 25 — 2026-04-17

**Linux port (x86_64) — Phase 4 kickoff.**

First non-Apple target. The Xerox 1983 desktop renders in an SDL2
window on Ubuntu 25.10 / Wayland; `.deb` and `.rpm` packages build
via CPack. No `#ifdef` branching in the platform layer — each host
gets its own files behind `IHal` and `Bridge.h`.

New files:

  - `src/platform/common/EventQueue.hpp` — host-neutral FIFO of
    16-bit Blue Book input words (moved up from
    `src/platform/apple/` so Linux can include it unchanged).
  - `src/platform/linux/LinuxHal.{hpp,cpp}` — IHal impl. RGBA8
    staging buffer + dirty rect + `signal_at` scheduling +
    EventQueue, mirroring `AppleHal`. No GUI deps inside the HAL.
    Wall-clock Smalltalk epoch (1901-01-01) from
    `chrono::system_clock`.
  - `src/platform/linux/LinuxBridge.cpp` — implements `Bridge.h`
    on top of `LinuxHal`, `Interpreter`, `PosixFileSystem`.
    Structurally identical to `AppleBridge.cpp`.
  - `src/platform/linux/CMakeLists.txt` — builds
    `libst80_linux.a`; wired in via `if(UNIX AND NOT APPLE)` from
    the top-level `CMakeLists.txt`.
  - `app/linux/st80_linux_main.cpp` + `CMakeLists.txt` — SDL2
    frontend. `SDL_Init(VIDEO|EVENTS)`, window at the VM display
    size, `SDL_PIXELFORMAT_ARGB8888` streaming texture, dirty-rect
    updates via `SDL_UpdateTexture`. Mouse buttons map to
    red/yellow/blue; Ctrl+click = blue, Alt+click = yellow for
    single-button hosts. `SDL_TEXTINPUT` feeds 7-bit ASCII
    keystrokes into `st80_post_key_down`. `--no-window` flag
    drives a headless smoke-test loop.
  - `cmake/LinuxPackaging.cmake` + `packaging/linux/st80.desktop`
    — CPack config producing `.deb` and `.rpm`. SHLIBDEPS picks
    up the correct runtime deps (`libsdl2-2.0-0`, libc, libgcc_s,
    libstdc++); RPM additionally requires `SDL2`.
  - `src/platform/posix/PosixFileSystem.hpp` — Windows `_WIN32`
    branches removed. The file is now pure POSIX and is shared
    between the Apple and Linux slices. A future Windows port
    will add `src/platform/win32/WindowsFileSystem.hpp` next to
    it rather than maintaining mixed branches in one file.

Verification on a stock Ubuntu 25.10 host (Wayland + Xwayland):

  - `cmake --build build-linux` — 19/19 targets compile clean,
    zero warnings.
  - `ctest` — `core_smoke_test` green; `trace2_check` green
    byte-for-byte on 499-bytecode Xerox reference.
  - `st80_probe` vs raw big-endian `VirtualImage` →
    `oopsLeft = 14375, coreLeft = 723822 words` (identical to
    the Mac numbers, so the endian-aware loader ports clean).
  - `st80-linux` windowed: screenshot shows System Transcript
    "Snapshot at: (31 May 1983 10:37:52 am)", System Workspace
    Xerox banner, class-category browser — same output as
    Catalyst.
  - `.deb` 85 K, `.rpm` 91 K under `build-linux/`. Both contain
    `/usr/bin/st80-linux`, `/usr/share/applications/st80.desktop`,
    and `/usr/share/doc/st80/{LICENSE,README.md,THIRD_PARTY_LICENSES}`.
  - Install / uninstall of the `.deb` via `dpkg -i` then
    `dpkg -r` round-trips clean; installed binary runs the
    reference image in headless mode.

Build commands:

    cmake -S . -B build-linux -G Ninja
    cmake --build build-linux
    cd build-linux && cpack -G DEB && cpack -G RPM

Not yet wired on Linux (intentional, Phase 5 polish):

  - Cursor image rendering (SDL has `SDL_CreateCursor` — one
    afternoon of glue).
  - Sandboxed image library / first-launch download
    (the Catalyst build has this via `ImageManager.swift`).
  - Sources/changes file paths (image is read-only today).

ARM Linux is the next slice; the tree is ready for it — only the
`dpkg --print-architecture` / `uname -m` detections in
`cmake/LinuxPackaging.cmake` need to run on the aarch64 host.

## build 24 — 2026-04-17

**Mac polish sweep** (todo-list items 1–10).

- **Cursor sync on Catalyst.** Hide the system pointer via
  `UIPointerInteraction.hidden()` and paint Smalltalk's 16×16 cursor
  bitmap into a UIImageView overlay tracked by hover.
- **Window resize / letterbox.** Both Mac targets now preserve the VM
  display's aspect ratio under resize instead of stretching.
  Hit-testing computes from the same content rect so clicks land
  where the user sees the cursor.
- **Clipboard bridge.** ⌘-V reads `UIPasteboard` and injects the text
  as keystrokes. One-way bridge; copy-out left for future work.
- **Mac menu bar.** "About Smalltalk80" now opens our About sheet via
  `UIMenuBuilder` + a NotificationCenter post, and File → Export
  Current Image… (⇧⌘E) opens a `UIDocumentPickerViewController` to
  save the image outside the sandbox.
- **`tools/st80_validate`.** New tool with `check` (walks OT, flags
  dangling class references / bogus word lengths) and `shasum`
  (per-OOP SHA-256 manifest, reproducible, useful for diffing two
  snapshots). Sha256 ported from
  `../validate_smalltalk_image` (MIT, same author).
- **Docs.** `docs/testing.md` (existing tests + gaps),
  `docs/performance.md` (benchmark candidates),
  `docs/release.md` (signing / notarization runbook),
  `docs/interpreter-audit.md` (baseline ~1.25 Mbc/s, ranked
  Phase-5 proposals).

## build 23 — 2026-04-17

**Mac app rename, About box, yellow-button menu, quit.**

- Renamed the user-visible Mac app to **Smalltalk80** (was
  "Smalltalk-80 (Catalyst)" / "Smalltalk-80"). Bundle identifiers
  and internal product names unchanged.
- New About sheet in `app/apple-catalyst/AboutView.swift`, reachable
  from an info button in the image-library top bar. Lists the GitHub
  project (`avwohl/smalltalk80-2026`) and the three Smalltalk-80
  reference repos (`dbanay`, `rochus-keller`, `iriyak`) as clickable
  links.
- **Right and middle clicks on Mac Catalyst.**
  - Right-click (yellow menu): UIKit intercepts Catalyst right-click
    as a system context-menu request before `touchesBegan` sees it,
    so `St80MTKView` installs a `UIContextMenuInteraction` whose
    delegate swallows the system menu and posts a yellow-button down
    at the click location.
  - Middle-click (blue / window-management menu): UIKit does not
    deliver middle-button clicks to `touchesBegan` at all on Catalyst.
    We bridge to AppKit at runtime via `NSClassFromString("NSEvent")`
    + `class_getMethodImplementation` and install an
    `addLocalMonitorForEventsMatchingMask:handler:` monitor for
    `.otherMouseDown/Up`. The click location is read from
    `UIHoverGestureRecognizer`'s most recent position (already in
    view coords) rather than converting NSEvent coordinates.
  - Ctrl+click / ⌥+click → yellow fallback for single-button
    pointers. `⌘+click = blue` was removed.
- **Sticky yellow/blue menus.** A real 3-button mouse drives a
  Smalltalk menu with press-hold-drag-release; releasing at the click
  location selects the item under the cursor — which on Catalyst
  would always be item 1 because a mouse click has no drag phase.
  The fix: we no longer send the button-up immediately. A new
  `UIHoverGestureRecognizer` tracks cursor movement without any
  button held, so the menu highlight follows the pointer, and the
  user's next click (any button) fires the pending up at that
  location, selecting the item or dismissing the menu. Same state
  machine handles both yellow and blue menus.
- **Smalltalk `quit` now terminates the app.** Previously
  `primitiveQuit` (113) set the HAL's quit flag but nothing acted on
  it, so picking "quit" from the yellow menu just stopped the VM
  loop and left the app sitting there. New `st80_quit_requested()` in
  Bridge.h is polled from the MetalRenderer each frame on both Mac
  targets; Catalyst calls `exit(0)`, AppKit calls `NSApp.terminate`.

## build 22 — 2026-04-17

**Image library + runtime download.** First-launch flow on the
sandboxed Catalyst / iOS build now offers to fetch the Xerox 1983
image from a public HTTPS mirror; no need to pre-populate any path
outside the sandbox. Modelled on iospharo's
`iospharo/Image/ImageManager.swift` but trimmed to what Phase 2
actually needs.

Asset hosting: `github.com/avwohl/st80-images` (new public companion
repo). Release `xerox-v2` serves four individual files — no archive
extraction required on the client:

    VirtualImage          596 128 bytes
    Smalltalk-80.sources 1 411 072 bytes
    trace2                 20 644 bytes
    trace3                 23 679 bytes

All unmodified copies of Wolczko's distribution. The repo's README
documents the provenance and licensing.

New Swift (all in `app/apple-catalyst/`):

- `St80Image.swift` — one-image model. Files live under
  `Documents/Images/<slug>/`; `imagePath` and `exists` conveniences.
- `ImageManager.swift` — `ObservableObject` singleton. Loads /
  saves `Documents/image-library.json` (ISO8601 dates on both sides
  — an early bug where save used ISO8601 and load used the default
  reference-interval decoder silently erased the catalog; fixed).
  `downloadTemplate(_:)` fetches each asset file of a template via
  `URLSession.shared.download`, moves into place, publishes a
  0…1 `downloadProgress` the UI binds to. No archive parsing
  because the release serves files individually.
- `ImageLibraryView.swift` — first-launch UI. Empty state: stack
  icon, "No image downloaded yet", a "Download Xerox Smalltalk-80
  v2 (1983)" button. Non-empty state: list with Launch button and
  destructive-swipe Delete. Progress row while downloading.
- `ContentView.swift` — switches between `ImageLibraryView` and
  `MetalView` based on which image the user launched.
- `MetalView.swift` / `MetalRenderer.swift` — now take an explicit
  `imagePath: String` init argument rather than resolving at
  runtime via CLI args / env var. The env fallback was useful for
  the unsigned swiftc build; now the library controls the path.

Xcode project updates: added the three new Swift sources to the
Sources build phase plus the `apple-catalyst` group. SourceKit
complains about missing types (it analyses each file in isolation)
but swiftc + xcodebuild both compile the whole module together and
succeed.

Verified on the sandboxed Catalyst build:
- Cold launch with empty Documents → library empty state renders.
- Pre-populate `Documents/image-library.json` pointing at
  `Images/xerox-v2/VirtualImage` → library lists the image with a
  Launch button. (Full end-to-end download wasn't automated in this
  commit because CGEvent clicks from outside the signed sandboxed
  app are blocked by Accessibility; the wiring is tested via the
  pre-populated path.)

GitHub companion repo layout:

    avwohl/st80-images  (public)
      README.md
      releases/xerox-v2
        ├── VirtualImage
        ├── Smalltalk-80.sources
        ├── trace2
        └── trace3

## build 21 — 2026-04-17

App icon + team config → code-signed Catalyst archive.

- `scripts/generate-appicon.swift` — a Swift / CoreGraphics script
  that draws a Smalltalk hot-air balloon at 1024×1024 and writes
  the PNG straight into
  `app/apple-catalyst/Assets.xcassets/AppIcon.appiconset/icon_1024.png`.
  Red + cream striped envelope, brown wicker basket with visible
  weave, four suspension ropes, "St" monogram in Georgia Bold, and
  a blue sky-gradient with three clouds. Re-run any time to
  regenerate.
- `Assets.xcassets/AppIcon.appiconset/Contents.json` — references
  `icon_1024.png` as the single universal iOS icon.
- `Local.xcconfig` (gitignored) — sets `DEVELOPMENT_TEAM = 644N5FNKG2`
  to match `../iospharo`. A real Apple Dev team ID lets automatic
  code signing finish without `CODE_SIGNING_ALLOWED=NO`.

Verified:

    xcodebuild ... build
    → BUILD SUCCEEDED
    codesign --display --verbose=2 build-xcodeproj/.../St80
    → Authority=Apple Development: Aaron Wohl (K769USDV9G)
    codesign --display --entitlements - ...
    → sandbox + network client + user-selected files + get-task-allow

Caveat: the signed Catalyst build is sandboxed, so running it
directly against `reference/xerox-image/VirtualImage` fails —
that path is outside the sandbox container. Expected App-Store
behaviour. A file-picker UI (and eventually an "open image…"
menu plus download-to-Documents option) is a separate Phase-2
polish commit. For dev iteration, use the unsigned/ad-hoc-signed
`build-macos-app.sh` or `build-catalyst-app.sh` paths which don't
carry the sandbox entitlement.

## build 20 — 2026-04-17

**Proper Xcode project.** `st80.xcodeproj` builds the Catalyst app
the App-Store way — through `xcodebuild` (or the Xcode GUI), via a
fully-signed archive. Modeled directly on iospharo's layout.

Pieces:
- `scripts/build-xcframework.sh` — produces
  `Frameworks/St80Core.xcframework` with three slices:
  `ios-arm64` (device), `ios-arm64_x86_64-maccatalyst`,
  `ios-arm64_x86_64-simulator`. Each slice builds `libst80core` +
  `libst80_apple` in a private CMake dir, merges them with
  `libtool -static` into `libSt80Core.a`, lipos the universal
  variants, and calls `xcodebuild -create-xcframework`.
- `Config.xcconfig` — sdk-conditional `ST80_XCFW_SLICE` picks the
  right slice; `-force_load` on the merged static library so no
  Bridge.h symbol gets stripped.
- `Local.xcconfig.example` — `DEVELOPMENT_TEAM` placeholder. User
  copies to `Local.xcconfig` (gitignored) and fills in.
- `app/apple-catalyst/st80.entitlements` — basic sandbox + network
  client + user-selected file read/write. Enough for App Store.
- `app/apple-catalyst/Assets.xcassets/` — placeholder `AppIcon`
  and `AccentColor` sets. Art is a Phase-2 polish item.
- `st80.xcodeproj/project.pbxproj` — one target (`St80`), product
  type `com.apple.product-type.application`,
  `SUPPORTS_MACCATALYST = YES`, `TARGETED_DEVICE_FAMILY = "1,2,6"`
  (iPhone + iPad + Mac). Uses `GENERATE_INFOPLIST_FILE = YES` with
  `INFOPLIST_KEY_*` so we don't hand-maintain an Info.plist. Swift
  sees Bridge.h via `SWIFT_OBJC_BRIDGING_HEADER`. The
  `Check XCFramework Freshness` pre-Sources shell script rebuilds
  the xcframework if a VM source file is newer.
- `st80.xcodeproj/xcshareddata/xcschemes/St80.xcscheme` — one
  scheme, Debug/Release configs, build + run + archive.

Shader note: `Shaders.metal` is in the Resources phase (not
Sources). Same rationale as the swiftc build — avoids requiring
the separately-downloaded Xcode Metal offline toolchain. Runtime
compile via `MTLDevice.makeLibrary(source:)` still works.

Verification:

    xcodebuild -list -project st80.xcodeproj
    # → Targets: St80; Schemes: St80

    xcodebuild -project st80.xcodeproj -scheme St80 \
        -configuration Debug \
        -destination 'platform=macOS,variant=Mac Catalyst' \
        -derivedDataPath build-xcodeproj \
        CODE_SIGNING_ALLOWED=NO build
    # → BUILD SUCCEEDED

    otool -l build-xcodeproj/Build/Products/Debug-maccatalyst/St80.app/Contents/MacOS/St80
    # → platform 6 (macCatalyst), minos 15.0

Launching the resulting `St80.app` against the Xerox image renders
the same 1983 desktop as the swiftc-built variant. Both frontends
remain in the tree — the swiftc one stays useful for rapid iteration
(no Xcode GUI round-trip), the xcodeproj one is what you archive
and upload to App Store Connect.

TODO for actual App Store submission (owner's responsibility):
- Copy `Local.xcconfig.example` → `Local.xcconfig`, set
  `DEVELOPMENT_TEAM`.
- Real 1024×1024 app icon artwork into `AppIcon.appiconset`.
- Xcode `Product → Archive` → `Distribute App` → `App Store Connect`.

## build 19 — 2026-04-17

**Mac Catalyst target lands. The Xerox desktop renders in a UIKit
window.**

Same `libst80_apple.a` + `libst80core.a` static libs, compiled for
the macCatalyst slice (`-target arm64-apple-ios17.0-macabi`), with
a new UIKit-based Swift frontend in `app/apple-catalyst/`. The
native-macOS AppKit frontend in `app/apple/` is untouched and still
builds and runs independently.

Key finding: `swiftc` can build a Catalyst executable directly —
no .xcodeproj required. Needs the macOS SDK plus `-F` pointing at
`MacOSX.sdk/System/iOSSupport/System/Library/Frameworks` so
UIKit resolves.

Files under `app/apple-catalyst/`:
- `AppDelegate.swift` — `UIApplicationMain` + `UIWindowSceneDelegate`
  hosting a `UIHostingController` around SwiftUI `ContentView`.
- `ContentView.swift` — same shape as the AppKit variant, just
  importing the UIKit-aware MetalView.
- `MetalView.swift` — `UIViewRepresentable` (vs the AppKit path's
  `NSViewRepresentable`).
- `MetalRenderer.swift` — largely identical to the AppKit
  renderer; kept in its own file so platform imports stay local.
- `St80MTKView.swift` — MTKView with UIKit event handling:
  `touchesBegan/Moved/Ended` route to `st80_post_mouse_*`, using
  `UIEvent.buttonMask` to pick red / yellow / blue on Catalyst;
  `pressesBegan` maps `UIKey` + modifier flags into the keyboard
  entrypoint.
- `Shaders.metal` — copied from the AppKit path.
- `Info.plist` — Catalyst metadata (LSRequiresIPhoneOS=false,
  UIDeviceFamily=2, UIApplicationSceneManifest, UILaunchScreen).
- `build-catalyst-app.sh` — driver: configures a separate CMake
  build dir (`build-catalyst/`) with macabi `-target` in CXXFLAGS
  so the .o files carry platform-6 markers, then links via
  `swiftc` against UIKit/SwiftUI/Metal/MetalKit.

Verified: `otool -l` reports `platform 6` (macCatalyst) for the
Catalyst binary vs `platform 1` (macOS) for the native one. Both
launch; both render the same Xerox desktop. Native AppKit build
unchanged.

Next: iOS (Phase 3) — same Swift sources target
`arm64-apple-ios17.0` and run inside the iOS simulator or on a
device; mostly a different `-target` and different entitlements.

## build 18 — 2026-04-17

App UX: friendly error when the image file is missing.

Previously `st80_init` failed silently deep inside
`MetalRenderer.setup` and the user got a blank grey window with no
feedback. `AppDelegate.applicationDidFinishLaunching` now checks
the CLI arg / `ST80_IMAGE` env var / default path up front and, if
the file isn't there, shows an `NSAlert` with the exact `curl + tar`
commands to fetch the Xerox image, plus a "Quit" button.

The happy path (image present) is unchanged: window titled
"Smalltalk-80 — VirtualImage", desktop renders normally.

## build 17 — 2026-04-17

CTest now guards the Phase 1 exit gate automatically.

- `tests/trace2_check.sh` — shell script that awks the bytecode
  numbers out of `reference/xerox-image/trace2`, runs `st80_run` for
  the same count, and diffs. Exit 77 (CTest `SKIP_RETURN_CODE`)
  when the image or trace file isn't on disk.
- `tests/CMakeLists.txt` — wires the script as `trace2_check`.

CTest runs 3 / 3 green (core_smoke_test, bridge_api_test,
trace2_check) in 0.27 s total. Any future drift in the interpreter
or loader that moves a single bytecode will fail this check on the
first CI pass.

## build 15 — 2026-04-17

**GC policy aligned with plan.md: `#define GC_REF_COUNT` disabled.**
The hybrid ref-count + mark/sweep combo we inherited from dbanay is
now mark/sweep only. `ObjectMemory::increaseReferencesTo` and
`decreaseReferencesTo` compile to no-ops; the rectifyCounts pass
during GC rebuilds the count field from scratch anyway (it's what
dbanay already does under GC_MARK_SWEEP). Silenced the two fresh
`-Wunused-parameter` warnings that show up when those inlines go
empty.

Regression: `bridge_api_test` green; `st80_run` trace2 byte-for-byte.

Perf note: `st80_run -n 100000` now runs in **0.09 s user** (from
0.12 s — about 25 % faster). The per-store RC overhead was visible.

## build 14 — 2026-04-17

Polish. Zero compile warnings now; a small keyboard correctness
fix; a couple of UX tweaks.

- **Keyboard**: `St80MTKView.keyDown` was using
  `event.charactersIgnoringModifiers` which drops shift — typing an
  uppercase letter arrived at Smalltalk as lowercase. Switched to
  `event.characters` so the already-shifted ASCII char is sent, per
  dbanay's "decoded keyboard" pattern (`main.cpp:593`). Also gates
  strictly to 7-bit ASCII (rejects NSEvent's ≥0xF700 function/arrow
  codes rather than silently sending 12-bit nonsense).
- **Compile warnings**: reordered the member-initializer lists in
  `Interpreter::Interpreter` and the `BitBltLoop` constructor to
  match field declaration order (`-Wreorder-ctor`); fixed the
  `-Wsign-compare` in `Interpreter.cpp:3145` (use `std::size_t` in
  the sizeof loop); silenced the three `-Wunused-parameter` noise
  lines in `getDisplayBits` / `updateDisplay` with `(void)` casts,
  keeping the dbanay-faithful signatures.
- **Window**: default size bumped 960×720 → 1024×768 (matches an
  Alto/Star aspect better). Title now includes the image filename
  when one is passed.

Regression: `bridge_api_test` still green; `st80_run` trace2 match
still byte-for-byte.

## build 13 — 2026-04-17

Cursor polish: position seeding + cursor-image rendering via NSCursor.

**Cursor position**: the World Menu had been opening at (0, 0) despite
clicks landing elsewhere. Root cause: `AppleHal::get_cursor_location`
returned a shadow state that was never updated from the pointer
events. Fix: `st80_post_mouse_*` now calls
`AppleHal::setShadowCursor(x, y)` before pushing the event words.
The image's `Sensor cursorPoint` / menu-placement logic now sees the
current pointer and menus pop where clicked. Verified via demo-click
screencap — the yellow text-operate menu (again/undo/copy/.../paste/
do it/print it/accept/cancel) opens adjacent to the click.

**Cursor image**: `AppleHal::set_cursor_image` was a stub. Now it
stores the 16-word Form the image hands us. The new
`st80_cursor_image` C API copies those bits out. `St80MTKView`
polls each frame (FNV-1a hash), rebuilds an `NSCursor` from the
bitmap (set bit = opaque black, clear = transparent — classic Xerox
single-plane convention), and registers it via
`addCursorRect(bounds, cursor:)` in `resetCursorRects`. Verified
from logs that the Xerox image sets its cursor twice during boot.

New in `src/include/Bridge.h`:
    void st80_cursor_image(uint16_t out[16]);

## build 12 — 2026-04-17

**Events work end-to-end. Yellow-click opens the Smalltalk-80 World
Menu.**

Fix (`AppleBridge.cpp`): signal the input semaphore **after every
queued word**, not per logical event. The earlier code pushed all 4
words for a mouse-down (time, X, Y, BI_DOWN) and signaled once; the
image only consumed 2 words before blocking on the semaphore again,
stranding the rest in the queue. Matches dbanay `main.cpp:532`.

- `src/platform/apple/AppleBridge.cpp` — new `pushWord(rt, word)`
  helper that enqueues AND signals; all `st80_post_*` functions
  go through it.
- `src/platform/apple/AppleHal.hpp` — added `queueDepth()` getter
  (used during diagnosis; harmless to keep).

Event wiring (Swift):

- `app/apple/St80MTKView.swift` — MTKView subclass. Mouse
  move/down/up/drag (left/right/other), keyDown forwarded into
  Bridge.h. 1-button trackpad modifier mapping: plain=red,
  ⌥=yellow, ⌘=blue. Coordinate conversion flips NSView Y-up to
  VM Y-down. `updateTrackingAreas` + `viewDidMoveToWindow` wire up
  motion and first-responder handling.
- `app/apple/MetalView.swift` — now creates `St80MTKView`.
- `app/apple/main.swift` — new `--demo-click` command-line flag.
  At t=4 s post a yellow press at the VM display centre; at t=12 s
  release. Events originate inside our process, so Accessibility
  permission isn't required — this is how automated visual tests
  exercise the event path without driver privileges.

Regression coverage:

- `tests/bridge_api_test.cpp` — post-click pixel-diff check (must
  be ≥100 diffs after a yellow-click sequence). Currently measures
  59035 against the Xerox v2 image.

Verified visually: screencap during the held yellow-click shows the
World Menu open — "restore display / exit project / project /
file list / browser / workspace / system transcript / system
workspace / save / quit". The full loop works.

Known caveat: the menu appears at (0, 0) rather than the cursor
position — the image's own cursor-location state hasn't been seeded
to match where we think the cursor is. Cosmetic; interaction still
works.

## build 11 — 2026-04-17

**The Xerox 1983 Smalltalk-80 desktop renders in a native macOS
SwiftUI+Metal window.** No Xcode project — everything builds from
the command line via `swiftc` + a small bundling script.

- `app/apple/main.swift` — classic `NSApplication` entry point. We
  started with SwiftUI `@main` App but under `swiftc -parse-as-library`
  the Scene body never instantiated. Plain `NSHostingView` inside
  an `NSWindow` created by `AppDelegate` works reliably.
- `app/apple/ContentView.swift` — tiny; just embeds the MetalView.
- `app/apple/MetalView.swift` — `NSViewRepresentable` wrapping an
  `MTKView`.
- `app/apple/MetalRenderer.swift` — `MTKViewDelegate`. Resolves the
  image path (CLI arg → `ST80_IMAGE` env → fallback), calls
  `st80_init`, then each frame runs 4000 cycles, calls
  `st80_display_sync`, uploads the RGBA staging buffer into a
  texture, blits via a fullscreen quad.
- `app/apple/Shaders.metal` — vertex + fragment for the quad blit.
  **Compiled at runtime** via `MTLDevice.makeLibrary(source:)`,
  which avoids depending on the offline Metal toolchain (a
  separately-downloaded Xcode component).
- `app/apple/Info.plist` — app bundle metadata.
- `app/apple/build-macos-app.sh` — ensures the CMake static libs are
  built, stages the shader text, invokes `swiftc` with
  `-import-objc-header Bridge.h` and links against `libst80_apple`
  and `libst80core`. Sets `DEVELOPER_DIR` to Xcode.app so Swift
  gets the matched SDK frameworks.

Verified visually: screencap of the app window shows System
Transcript, System Workspace (with the Xerox 1983 copyright), and
a class-category browser list — all legible. Snapshot date in the
image: 31 May 1983 10:37:52 am.

What's NOT wired yet (next commit):
- Mouse events (move, down, up — BitBlt already plumbed through
  `st80_post_mouse_*`).
- Keyboard events.
- Cursor rendering.
- `signal_at` for `Delay` — already in Phase-2-task-3 but not
  stressed until events arrive.

What's intentionally deferred:
- Mac Catalyst target (needs `xcodebuild`; the Swift sources will
  be mostly shared).
- iOS target.

## build 10 — 2026-04-17

Phase 2 tasks 1–3: display-bit expansion, Blue Book input encoding,
signal_at scheduling. Runtime now fully functional behind the
Bridge.h C API — the remaining Phase 2 work is the Swift/Metal
frontend.

**1. 1-bit DisplayBitmap → RGBA8 expansion** (`AppleBridge.cpp`).
`st80_display_sync` pulls the active display form OOP from
`Interpreter::getDisplayBits`, reads words via
`fetchWord_ofDislayBits`, and writes black/white RGBA8 pixels into
`AppleHal`'s staging buffer for the dirty rect. MSB-first bit order
per G&R p. 657; 1 bit = black ink.

**2. Blue Book input encoding** (`AppleBridge.cpp`, §29.12).
`st80_post_*` functions encode platform events as 16-bit input
words: 4-bit type in high nibble, 12-bit parameter in low.

      type 0: delta time (ms, 0–4095)
      type 1: X coordinate
      type 2: Y coordinate
      type 3: bistate DOWN  (key or mouse button)
      type 4: bistate UP
      type 5: absolute time (followed by two words, high+low)

Button codes match dbanay main.cpp:693-696: red=130, yellow=129,
blue=128. Key events emit a type-3/type-4 pair ("decoded keyboard"
per dbanay's note at main.cpp:593). Each posted event starts with a
type-0 or type-5 timestamp and ends with a call to
`Interpreter::asynchronousSignal(inputSemaphore)` so the image's
input-waiting process wakes up.

**3. `signal_at` scheduling** (`AppleHal.{hpp,cpp}`).
`AppleHal::signal_at` records the semaphore + msclock-time;
`scheduledSemaphoreDue` returns true once the time has passed (with
32-bit wraparound handling). The bridge's `st80_run` polls it each
cycle and invokes `asynchronousSignal`. This unblocks `Delay` and
any other primitive that relies on timed semaphores.

**E2E test** (`tests/bridge_api_test.cpp`). Boots the Xerox image
through Bridge.h, runs 3000 cycles, posts mouse + key events,
shuts down. Passes; reports `display=640x480` — the image's
`primitiveBeDisplay` fires and our HAL sees the resize. Test
auto-skips (exit 77) if the image isn't on disk.

**Regression check**: trace2 byte-for-byte match still green; the
inserted `signal_at` polling and virtual Interpreter dtor didn't
perturb the bytecode sequence.

## build 9 — 2026-04-17

Phase 1 declared done. Phase 2 scaffolding begins: C API boundary +
Apple HAL skeleton. No Swift or Xcode project yet — that's the next
commit. Everything here builds as a host-native static library so
the plumbing stays compile-checked before the iOS/Catalyst
xcframework dance.

- `src/include/Bridge.h` — pure-C API the frontend will call.
  Lifecycle (`st80_init`/`run`/`stop`/`shutdown`), display access
  (`st80_display_width/height/pixels/sync`), input
  (`st80_post_mouse_*`, `st80_post_key_*`). Modifier flags and
  mouse-button enum included.
- `src/platform/apple/EventQueue.hpp` — thread-safe FIFO of 16-bit
  Blue Book input words. UI thread pushes; VM thread pops via
  `IHal::next_input_word`.
- `src/platform/apple/AppleHal.{hpp,cpp}` — IHal implementation.
  Owns an RGBA8 staging buffer and the event queue. `display_changed`
  merges dirty rects with union. Clock uses `std::chrono::steady_clock`.
  `signal_at` / `set_cursor_image` left as `TODO(phase2)` stubs.
- `src/platform/apple/AppleBridge.cpp` — Bridge.h implementation
  on top of AppleHal + Interpreter + PosixFileSystem. Singleton
  runtime owned by `st80_init`/`shutdown`.
- `src/platform/apple/CMakeLists.txt` + root `CMakeLists.txt` —
  Apple subdir wired up (`if(APPLE)`) building `libst80_apple.a`.
- `src/core/Interpreter.hpp` — added `virtual ~Interpreter()` to
  fix a latent delete-through-base-pointer issue (Interpreter
  inherits `IGCNotification`). dbanay's original missed it.

Still green:
- `core_smoke_test` passes.
- `st80_run` trace2 check still byte-for-byte (build 8 result
  unchanged).

TODO before a real Catalyst launch:
- Blue Book input-word encoding for the `st80_post_*` calls.
- Actual 1-bit → RGBA8 expansion in `st80_display_sync` (needs
  `Interpreter::fetchWord_ofDislayBits` wiring).
- `signal_at` semaphore scheduling (for `Delay`).
- Xcode project + SwiftUI + Metal frontend.

## build 8 — 2026-04-17

**Phase 1 trace gate passes (trace2).**

- `tools/st80_run.cpp` — minimal cycle runner. Usage:
  `st80_run [-n <cycles>] <path-to-image>`. Init the Interpreter,
  loop `cycle()` N times, emit one bytecode-number per line to
  stdout; status messages on stderr.
- `tools/CMakeLists.txt` — builds `st80_run` alongside `st80_probe`.

Verification (see `docs/trace-verification.md`):

    awk '/^Bytecode <[0-9]+>/ { match($0, /<[0-9]+>/);
        print substr($0, RSTART+1, RLENGTH-2) }' \
        reference/xerox-image/trace2 > /tmp/trace2_bc.txt
    ./build/tools/st80_run -n 499 \
        reference/xerox-image/VirtualImage > /tmp/st80_bc.txt 2>/dev/null
    diff /tmp/st80_bc.txt /tmp/trace2_bc.txt      # empty

**All 499 bytecodes of trace2 match byte-for-byte.** This covers the
initial startup sequence from image boot through to stable
execution — message sends, primitive calls, method returns,
conditional jumps. Proof that ObjectMemory + Interpreter port is
functionally correct end-to-end.

Stress: 100 000 cycles in ~0.12 s CPU on Apple Silicon
(~830 K cycles/sec). Clean exit, no crashes, no memory issues.

trace3 (message-send/return annotated view) still TODO — needs a
decorated tracer in the interpreter. Not a correctness gate; trace2
already establishes that.

## build 7 — 2026-04-17

Endian-aware loader landed (chose Path 2 from build 6's doc). Raw
Xerox `VirtualImage` now loads directly — no preprocessing step.

- `src/core/Oops.hpp` — added `ClassDisplayBitmapPointer = 30` (Blue
  Book well-known OOP, missing from earlier port).
- `src/core/ObjectMemory.hpp` — new private `swapObjectBodyBytes`
  method; new `bool swapOnLoad` member.
- `src/core/ObjectMemory.cpp` — `bswap16`/`bswap32` helpers (use
  `__builtin_bswap*` on GCC/Clang, fallback elsewhere);
  `ClassWordArrayPointer = 0xA72` image-specific constant.
  `loadObjectTable` does plausibility-based auto-detection of the
  image's byte order and sets `swapOnLoad`. `loadObjects` swaps
  `objectSize`/`classBits` when reading, then post-processes each
  object body via `swapObjectBodyBytes`. The swap dispatch mirrors
  dbanay's `imageswapper.c`:

      CompiledMethod:       swap header + literals, bytecodes raw
      Float:                4-byte reversal across two body words
      pointer / DisplayBitmap / WordArray: swap every body word
      byte-type objects:    no body swap

- `saveObjects` unchanged — still writes host-native, which
  round-trips via the auto-detect on re-load.

Verified: `st80_probe` against both the raw Xerox `VirtualImage`
(big-endian) and dbanay's pre-produced `VirtualImageLSB`
(little-endian) produces identical stats:

      oopsLeft = 14375
      coreLeft = 723822 words

docs/image-preprocessing.md updated to reflect the resolution.

## build 6 — 2026-04-17

First real end-to-end exercise of the ObjectMemory port. Docs only;
no code changes.

- Fetched Xerox v2 image `image.tar.gz` from wolczko.com into
  `reference/xerox-image/` (gitignored). Contains `VirtualImage`
  (596 KB, big-endian), `Smalltalk-80.sources`, `trace2`, `trace3`.
- Ran `st80_probe` against raw `VirtualImage` → `loadSnapshot
  FAILED`, exactly as predicted. The header's int32 lengths read
  as little-endian nonsense, seek goes negative.
- Discovered dbanay's workflow uses a one-time BE→LE preprocessor:
  `reference/dbanay-smalltalk/misc/imageswapper.c` produces
  `reference/dbanay-smalltalk/org/VirtualImageLSB`.
- Ran `st80_probe` against `VirtualImageLSB` →

      st80_probe: loadSnapshot OK
        oopsLeft = 14375
        coreLeft = 723822 words

  OT populated, heap consistent. First concrete signal that the
  ObjectMemory port is functionally correct end-to-end.

See `docs/image-preprocessing.md` for the BE→LE policy and next
steps (port `imageswapper.c` into `tools/`, or make the loader
endian-aware in-core).

## build 5 — 2026-04-17

Phase 1: HAL/FS implementations + runnable loader probe.

- `src/platform/posix/PosixFileSystem.hpp` — ported from dbanay
  `posixfilesystem.h` @ ab6ab55. Class renamed from
  `PosixST80FileSystem` to `PosixFileSystem`; added missing
  `#pragma once` and virtual overrides. Windows branches retained.
- `src/platform/headless/HeadlessHal.hpp` — fresh no-op `IHal`
  implementation for loader probes and unit tests. Aborts loudly on
  `error()` / `exit_to_debugger` so we notice if a test wanders onto
  a GUI path.
- `tools/st80_probe.cpp` + `tools/CMakeLists.txt` — tiny CLI that
  instantiates ObjectMemory + PosixFileSystem + HeadlessHal and
  calls `loadSnapshot`. First end-to-end exercise of the loader.

Build green. Probe with a nonexistent path prints
`loadSnapshot FAILED` and exits non-zero, as expected.
Still no exercise of a real image — that's next.

## build 4 — 2026-04-17

Phase 1: Interpreter + BitBlt ported.

- `src/core/BitBlt.hpp` / `BitBlt.cpp` — ported from dbanay
  `bitblt.{h,cpp}` @ ab6ab55 (~960 LOC). Namespace wrap, include
  retarget. BitBlt's fidelity is load-bearing for display primitives,
  so the code is left byte-for-byte against dbanay where possible.
- `src/core/Interpreter.hpp` / `Interpreter.cpp` — ported from dbanay
  `interpreter.{h,cpp}` @ ab6ab55 (~6950 LOC). Namespace wrap, include
  retarget, `IHardwareAbstractionLayer` → `IHal`. Feature macros
  (IMPLEMENT_PRIMITIVE_NEXT, IMPLEMENT_PRIMITIVE_AT_END,
  IMPLEMENT_PRIMITIVE_NEXT_PUT, IMPLEMENT_PRIMITIVE_SCANCHARS) kept
  enabled per dbanay's defaults.
- `CMakeLists.txt` — BitBlt.cpp and Interpreter.cpp added.

Build green; smoke test (1/1) still passes. `libst80core.a` now
218 KB. No runtime exercise of the interpreter yet — that needs an
`IHal` implementation and a test image.

Known compile warnings carried over (not blocking):
- `-Wreorder-ctor` in BitBlt and Interpreter constructors —
  cosmetic, matches dbanay's initializer order.
- `-Wunused-parameter` on three display-bound methods.
- `-Wsign-compare` on one `sizeof` comparison in Interpreter.cpp.

Cumulative LOC under `src/core/` (excluding tests): ~10 K.

## build 3 — 2026-04-17

Phase 1: ObjectMemory + HAL interfaces ported.

- `src/core/hal/IHal.hpp` — ported from dbanay `hal.h` @ ab6ab55.
  Class renamed from `IHardwareAbstractionLayer` to `IHal` for
  brevity; interface otherwise identical. The monolithic form
  stays until Phase 2 (where plan.md calls for a split into
  IDisplay/IInput/IClock).
- `src/core/hal/IFileSystem.hpp` — ported from dbanay
  `filesystem.h` @ ab6ab55. Virtual dtor added.
- `src/core/ObjectMemory.hpp` — ported from dbanay `objmemory.h`
  @ ab6ab55. ~860 lines of class declaration including inline OT
  accessors. Namespace wrap, include paths retargeted to our
  layout, `IHardwareAbstractionLayer` → `IHal`. GC_MARK_SWEEP and
  GC_REF_COUNT both enabled (dbanay's hybrid default); revisit
  per docs/plan.md before Phase 2 polish.
- `src/core/ObjectMemory.cpp` — ported from dbanay `objmemory.cpp`
  @ ab6ab55. ~1730 lines. Image loader `loadObjects` still does
  raw memcpy reads — the latent little-endian issue is a known
  Phase 1 item to be fixed when we exercise the loader against
  the Xerox v2 image and verify against trace2/trace3.
- `CMakeLists.txt` — ObjectMemory.cpp added to `st80core` target.

No runtime verification added yet — ObjectMemory requires an
IHal implementation and a test image, both landing in later
commits. Build green; smoke test (1/1) still passes.

## build 2 — 2026-04-17

Phase 1 foundation (first two ported files):

- `src/core/Oops.hpp` — ported from dbanay `oops.h`. Well-known
  OOPs (nil/true/false, class and selector roots). Namespaced as
  `st80::`, constants switched to `inline constexpr`.
- `src/core/RealWordMemory.hpp` — ported from dbanay
  `realwordmemory.h`. 16 × 65536 word segmented memory, byte and
  bit range accessors. The endianness footnote is carried forward
  verbatim; a concrete answer waits on porting the image loader.
- `src/core/core_init.cpp` — links the headers into a compilable
  TU and gives `st80core` a non-empty static library.
- `tests/core_smoke_test.cpp` — round-trips word, byte, and bit
  accessors; static-asserts OOP constants. Wired through CTest.
- `CMakeLists.txt` — real now (replaces the Phase-0 stub).

Build: `cmake -S . -B build && cmake --build build && (cd build && ctest)`
→ green on macOS arm64, AppleClang 21.

## build 1 — 2026-04-17

- Repo scaffolded (Phase 0): LICENSE (MIT), THIRD_PARTY_LICENSES,
  CLAUDE.md, plan in `docs/plan.md`, supporting docs, placeholder
  CMakeLists.txt, directory skeleton.
- No runnable code yet.
