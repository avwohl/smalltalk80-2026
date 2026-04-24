# Changes

User-visible changes. Most-recent build on top.

## build 35 — 2026-04-24

**Apple-side input, clipboard, and icon fixes.** A batch of
bug-fixes and small features across Mac Catalyst and iOS.

Keyboard (Mac Catalyst / iPad hardware keyboard):

  - Shift-key mapping. `pressesBegan` now prefers `key.characters`
    (fully resolved with Shift / Ctrl already applied) over
    `charactersIgnoringModifiers`, which stripped Shift. Symptom:
    typing `2 + 2` produced `2 = 2`, `"` became `'`, `(` became
    `9`, etc. Fix also lets hardware Ctrl+letter arrive at the
    image as the matching 0x01..0x1F control byte — so the Blue
    Book editor's Ctrl+D / Ctrl+P / Ctrl+S bindings now work from
    the physical keyboard.

Clipboard (Mac + iOS, both directions):

  - New `st80_clipboard_read()` HAL entry point. Walks the
    SystemDictionary (oop 25286) to find the `ParagraphEditor`
    class, reads `classPool[#CurrentSelection]`, unwraps the
    resulting `Text` to its backing `String`, and returns UTF-8
    bytes. Defensive against image-layout variants (falls back
    to scanning the class's pointer slots for any Dictionary that
    contains `#CurrentSelection` if the canonical index-7 slot
    doesn't match).
  - Cmd+C / Cmd+X post Ctrl+C / Ctrl+X to the VM, then 150ms
    later mirror `CurrentSelection` to `UIPasteboard.general`.
    150ms is a best-effort delay since the interpreter runs on
    its own worker thread and the input queue has no synchronous
    barrier — in practice a hammered Cmd+C gets one stale read on
    the first press and catches up on the next.
  - Cmd+V streams the iOS / Mac pasteboard into the VM as a key
    event stream (the existing path — now available from soft-key
    input on iOS too, not just macCatalyst).
  - Strip Cut / Copy / Paste buttons wired to the same
    system-clipboard paths.

iPhone on-screen strip:

  - Action buttons ported from iospharo: Do it, Print it,
    Inspect, Accept, Cancel, Cut, Copy, Paste, Backspace. Each
    emits the matching ASCII control byte (Ctrl+D = 0x04, …)
    directly, since the Xerox image's decoded keyboard has no
    modifier side-channel.
  - Strip now flips to the side opposite the camera notch /
    Dynamic Island on each orientation change (`UIDevice.current
    .orientation` drives a `stripOnRight` flag in ContentView,
    with `interfaceOrientation` as fallback for unknown / flat).
  - `delaysTouchesBegan = true` on the yellow-menu long-press
    gesture. Fixes "do it on selection" — previously the
    spurious RED click that fired before long-press recognized
    was clearing the current text selection.
  - `touchesBegan` reclaims first responder when `keyboardVisible`
    is set. SwiftUI strip button taps were silently stealing
    first responder; symptom was "typing stopped going anywhere"
    after tapping any strip button.
  - Action stack wrapped in `ScrollView` so every button stays
    reachable on DI iPhones in landscape, where the bottom zone
    is narrow.

Icon:

  - New top-down app icon: 8-gore colored balloon dome viewed
    from above, wicker basket peeking out below with suspension
    ropes visibly attached to the rim. SVG source committed
    alongside the 1024×1024 PNG so future edits are text-only.
    Previous icon had the ropes floating detached from the
    balloon and an `St` text-tag in the stripe.

Build:

  - Apple `CFBundleVersion` bumped 1 → 35 (both plists) so the
    marketing version chip in the Image Library header shows the
    new build number.

## build 34 — 2026-04-21

**Docs refresh for all four frontends.** Root `README.md` now lists
macOS, Mac Catalyst, Linux, and Windows with links to each frontend's
own build guide. Per-platform READMEs under `app/apple/`,
`app/apple-catalyst/`, `app/linux/`, and `app/windows/` each cover
prerequisites from a clean OS install through to a running image.
`docs/architecture.md` drops the "Win32/Linux future" markers,
describes `src/platform/{linux,windows,win,common}/`, and points to
the per-frontend guides. `docs/release.md` now signposts that
Windows (NSIS / WiX / MSIX) and Linux (CPack + GitHub Actions) have
their own release paths documented next to each frontend.

## build 33 — 2026-04-21

**Windows .msi installer.** `cpack -G WIX` in `build-<arch>/` now
produces `st80-<ver>-Windows-AMD64.msi` (or `-ARM64`) for sideloading
to testers while the `.msix` goes through Microsoft Store review.
Required config fixes:

  - Target WiX v4/v5 (`CPACK_WIX_VERSION=4`). Legacy v3
    (candle/light) was discontinued; v7 requires the OSMF EULA.
  - Stage a `.txt` copy of the extensionless `LICENSE` for CPack
    — WiX rejects license files without a recognised extension.
  - Drop the empty `CPACK_WIX_LICENSE_RTF` override; the default
    auto-converts the plaintext license to RTF.

Unsigned `.msi` launches via UAC with an "Unknown publisher"
warning — clickable through, unlike `.msix` which is a hard stop
without a trusted signature.

Tooling: `dotnet tool install --global wix --version 5.0.2` +
`wix extension add --global WixToolset.UI.wixext/5.0.2`. No admin
required.

## build 32 — 2026-04-20

**GitHub Actions release workflow (Linux).** New
`.github/workflows/release-linux.yml` builds `.deb` and `.rpm`
packages for x86_64 and aarch64 on every `v*` tag push and attaches
the four artifacts to the matching GitHub release. Uses GitHub's
native ARM runners (`ubuntu-24.04-arm`) — no QEMU. Also runnable
via `workflow_dispatch` for build-only smoke tests.

## build 31 — 2026-04-18

**Linux launcher (GTK4).** `st80-linux` no longer requires an image
path — when invoked with no positional argument it now opens a
GTK4 image picker that mirrors the Catalyst and Win32 launchers.
Built only when `libgtk-4-dev` and `libcurl-dev` are present at
configure time (the .deb keeps building either way; without them
the binary still works but only via a CLI image path).

Features (port of the Win32 launcher's behaviour, no Pharo /
iospharo Export-as-App):

  - Sortable Name / Source / Size / Last Modified columns.
  - Search box at the top (GtkSearchEntry, with built-in clear-X).
  - Per-row right-click menu: Launch / Rename / Duplicate /
    Show in Files / Set/Clear Auto-Launch / Delete.
  - Detail panel under the list with the action strip.
  - Header-bar buttons: Download…, Add image from file…, Settings.
  - Download dialog with built-in templates from the manifest
    *and* a custom-URL field.
  - Cancel-during-download (curl progress callback observes a
    flag set from the UI thread).
  - SHA256-verified manifest downloads, with on-disk fallback
    cache and a built-in Xerox-v2 fallback if the manifest host
    is unreachable.
  - Auto-launch star: `--launcher` overrides; otherwise a starred
    image triggers a 3-second countdown splash with a "Show
    Library" escape hatch.
  - GtkAboutDialog as the Settings sheet (version, project link,
    license, credits including dbanay, Wolczko, iriyak, Blue Book).
  - Library state lives at `$XDG_DATA_HOME/st80-2026/library.json`
    (default `~/.local/share/st80-2026/library.json`); image
    directories under `Images/<slug>/`. Same JSON schema as the
    Win32 / Catalyst catalogs, so syncing across platforms is
    "scp the directory."

Code reuse: `tools/Json.{cpp,hpp}` (relocated from
`app/windows/`) and `tools/Sha256.{cpp,hpp}` are now shared by
both the Win32 launcher and the GTK4 launcher.

**Windows launcher catches up to Catalyst / iospharo parity.**

  - **Custom URL download** — new "From URL…" button between
    "Download…" and "Add from file…". Prompts for a direct download
    URL, infers a slug + filename from the URL's last path segment,
    and runs the same single-asset job pipeline as a manifest
    download (no SHA256 since the URL didn't come from the manifest).
  - **Cancel a download mid-transfer** — the bottom Cancel button
    becomes "Cancel Download" while a transfer is running. Pressing
    it (or ESC, or closing the launcher window) sets a cancel flag
    that the WinHTTP read loop checks at every 16 KB chunk; the
    half-downloaded file is removed and the status line shows
    "Download cancelled."
  - **Clear-X next to the filter** — small × button beside the
    filter edit, hidden when the field is empty.
  - **Settings dialog** — new "Settings…" button (bottom-left)
    opens a TaskDialog with VM project, bug-report, changes, and
    acknowledgements links. Mirrors SettingsView on Catalyst.
  - **Auto-launch splash** — when a starred image triggers the
    skip-the-picker path, the app now shows a 3-second countdown
    with a "Show Library" button before booting the VM. Identical
    semantics to AutoLaunchSplashView on Catalyst, so a damaged
    starred image no longer locks the user out of the picker.

New public API in `Launcher.hpp`:

  - `bool ShowAutoLaunchSplash(HINSTANCE, const std::string &path,
    const std::string &displayName);`
  - `std::string LoadAutoLaunchInfo(std::string &outDisplayName);`
  - `void ShowSettingsDialog(HWND owner);`

**Catalyst launcher: Pharo-Launcher-style picker.** The iOS / Mac
Catalyst image picker is rewritten to match the launcher in the
companion `iospharo` project (https://github.com/avwohl/iospharo).
Same behaviour, ported to st80 conventions:

  - Sortable column headers — Name, Source, Size, Last Modified
    (click to toggle direction).
  - Filter / search box at the top with a clear-X button.
  - Detail panel under the table — image filename, location, source
    label, total size on disk, modification date, added date, last
    launched date, plus an action strip (Launch / Rename / Share /
    Show in Files / Auto-Launch / Delete).
  - Per-row context menu (long-press) with the same actions plus
    Duplicate.
  - Swipe-to-delete on each row, with a confirmation alert that
    spells out exactly what will be removed.
  - Star / Auto-Launch — pick an image to skip the picker on next
    launch. Persisted in `UserDefaults` under
    `st80.autoLaunchImageID`.
  - Auto-launch splash — when an image is starred, the app shows a
    3-second countdown with a "Show Library" button before opening
    the VM, so a damaged image can't lock you out.
  - Download dialog (`NewImageView`) — picks a built-in template
    *or* downloads from a custom URL. Downloads now have a cancel
    button while in flight.
  - Settings sheet — version, links to GitHub / Issues / Changes,
    and a navigable Acknowledgements view that mirrors
    `THIRD_PARTY_LICENSES`.
  - Project-info bar across the top of the library showing
    version + GitHub / Changes / Bug links.
  - Error banner overlay at the bottom — tap to dismiss.

Library catalog backwards compatibility: existing
`Documents/image-library.json` files keep loading. New optional
fields (`lastLaunchedAt`, `imageSizeBytes`, `imageLabel`) are
filled in on first save. The catalog is also rescanned on load so
images dropped into `Documents/Images/<slug>/` by hand show up
automatically.

The `Export-as-App` feature from iospharo is intentionally left out
— it's a Pharo-specific build-an-Xcode-project pipeline that
doesn't translate to st80's Smalltalk-80 image format.

## build 30 — 2026-04-18

**Windows packaging: cross-arch `.msixbundle` for Store release.**

`packaging/windows/build-release.ps1` now accepts an `-Archs` list
(e.g. `-Archs x64,ARM64`) and drives per-architecture builds into
`build-<arch>/`, emitting one `.msix` per architecture and then
combining them into a single `build/st80-<ver>.msixbundle` via a
new `packaging/windows/pack-msixbundle.ps1` helper. This is the
artifact Partner Center wants for Store submission — the Store
inspects the bundle manifest and serves each device the package
that matches its CPU (x64 for Intel/AMD, arm64 for Snapdragon X /
Copilot+ PCs).

Fixes along the way:

  - `cmake/WindowsPackaging.cmake` now reads
    `CMAKE_GENERATOR_PLATFORM` (the Visual Studio `-A` value)
    rather than `CMAKE_SYSTEM_PROCESSOR` when tagging
    `ProcessorArchitecture` in `AppxManifest.xml`. On a cross
    build from x64 → ARM64, the host-side `CMAKE_SYSTEM_PROCESSOR`
    stays `AMD64`, which previously mis-tagged the ARM64
    `.msix` as x64 and made `MakeAppx bundle` reject the bundle
    for arch collision.
  - `AppxManifest.xml.in` collapsed the multi-line
    `<Description>` into a single line. The AppX schema rejects
    control characters (`[^\x01-\x1f]+`), so the pretty-printed
    version failed `MakeAppx pack` schema validation.
  - WiX is now optional: if the WiX Toolset isn't on PATH, the
    `cpack -G WIX` step warns instead of aborting — the
    `.msixbundle` + NSIS `.exe` cover every distribution channel
    we care about.

New files:

  - `packaging/windows/pack-msixbundle.ps1` — wraps
    `MakeAppx.exe bundle` over a set of per-arch `.msix` paths,
    optionally signing the bundle for side-load. Not needed for
    Store uploads (the Store re-signs).
  - `build_arm64.bat` (previous commit) — parallel to `build.bat`,
    builds under `build-arm64/` with `-A ARM64`. Requires the
    `Microsoft.VisualStudio.Component.VC.Tools.ARM64` VS
    component (x64-hosted ARM64 cross-compiler).

## build 29 — 2026-04-18

**Windows launcher: iospharo parity + SHA256-verified downloads.**

The first-launch picker has been rewritten to mirror the
iospharo launcher (`app/iospharo/iospharo/Views/ImageLibraryView.swift`)
and moved off the registry-backed last-image model:

  - **Sortable ListView** with Star / Name / Size / Last Modified
    columns. Clicking a header toggles ascending/descending;
    sort arrows render via `Header_SetItem` + `HDF_SORTUP/DOWN`.
  - **Filter box** above the table, matching both the custom
    display name and slug (case-insensitive via `StrStrIW`).
  - **Detail panel** on the right with image name, file name,
    slug, size, last-modified timestamp, and the on-disk path.
  - **Action strip** (plus right-click context menu): Launch,
    Rename..., Duplicate, Show in Explorer, Auto-Launch toggle,
    Delete.
  - **Auto-Launch star** replaces `HKCU\...\LastImagePath`. The
    user explicitly stars an image; on next start the launcher
    skips the window and boots straight into that image. The
    legacy registry value is migrated to `library.json` once on
    first run.

New persistent state lives under
`%USERPROFILE%\Documents\Smalltalk-80\`:

  - `Images\<slug>\` — one subdirectory per image, holding the
    `VirtualImage` (or `*.image`) plus optional companion
    `Smalltalk-80.sources` / `Smalltalk-80.changes`.
  - `library.json` — catalog of images (ids, custom names,
    timestamps, auto-launch pointer).
  - `manifest-cache.json` — last successful download manifest,
    used if the network is unavailable.

**Downloadable images are described by a manifest JSON** fetched
from `https://raw.githubusercontent.com/avwohl/st80-images/main/manifest.json`
(WinHTTP, redirect-follow enabled). Each asset in the manifest
carries a `sha256` digest and a `size`. The launcher streams the
download into a `Sha256` hasher and verifies the hex digest on
completion; a mismatch deletes the file rather than leaving a
tampered image on disk. A bundled fallback (Xerox v2) lets the
download picker still open when the manifest fetch fails.

"**Add from file...**" lets the user import an existing image
plus its companion files into the library directly; a sibling
`*.sources` / `*.changes` next to the selected `*.image` is
picked up automatically.

New files:

  - `app/windows/Json.hpp` / `Json.cpp` — minimal recursive-descent
    JSON reader/writer. No exceptions, no third-party headers;
    error → null root.
  - `packaging/st80-images-manifest.json` — sample manifest to
    host at the URL above, with placeholder SHA256 values to be
    filled in from `sha256sum` on the published release assets.

## build 28 — 2026-04-18

**Windows: menu bar with About, Edit → Paste, and focus-loss fix.**

The pure-Win32 app now carries a standard menu bar:

  - **File → Exit** — closes the window (Alt+F4 still works).
  - **Edit → Copy Selection...** — shows an info dialog explaining
    that OS-clipboard copy-out isn't wired up yet. Matches the
    same limitation the Mac Catalyst frontend documents at
    `app/apple-catalyst/St80MTKView.swift:461`: copy-out needs a
    HAL primitive that touches the VM core.
  - **Edit → Paste from Clipboard** (Ctrl+Shift+V) — reads
    `CF_UNICODETEXT` from the clipboard and injects it as a
    stream of `st80_post_key_down` events, clipping to printable
    7-bit ASCII and translating LF→CR. Mirrors Mac Catalyst's
    `pasteFromSystemClipboard()`. Ctrl+Shift+V is used instead of
    plain Ctrl+V so the image's own Ctrl+V binding keeps working.
  - **Help → About Smalltalk-80...** — TaskDialog with the same
    content as `app/apple-catalyst/AboutView.swift` (project
    link, references list, Blue Book footnote). Hyperlinks open
    in the default browser via `ShellExecuteW`.

Also: `WM_KILLFOCUS` now calls `ReleaseCapture` if the window
held mouse capture when focus was lost. Previous behavior could
leave the app mid-drag when the user alt-tabbed during a
text-selection drag, and the next click on return wouldn't line
up cleanly.

New files:

  - `app/windows/AboutDialog.hpp` / `AboutDialog.cpp` —
    `TaskDialogIndirect`-backed About dialog. Ports the content
    of `AboutView.swift` to a single Win32 modal with hyperlink
    support. No dialog template resource needed.

Modified:

  - `app/windows/st80_windows_main.cpp` — menu bar built in code
    via `CreateMenu`/`AppendMenuW`; `WM_COMMAND`, `WM_KILLFOCUS`,
    and the Ctrl+Shift+V accelerator handled in `WndProc`.
    `AdjustWindowRectEx` now passes `TRUE` for the menu flag so
    the advertised client area still fits the full VM display
    with the new menu bar attached.
  - `app/windows/CMakeLists.txt` — adds `AboutDialog.cpp`.
    `comctl32` was already linked for the launcher's progress
    bar, which is where `TaskDialogIndirect` lives.

## build 27 — 2026-04-18

**Windows launcher: pick / download / import images on first run.**

Mirrors the Mac/Catalyst `ImageLibraryView` flow on Windows. Running
`st80-win.exe` with no image path now opens a launcher dialog
instead of silently exiting (the old behaviour was a stderr-only
usage message that nobody could see under /SUBSYSTEM:WINDOWS).

Launcher capabilities:

  - Lists every image found under
    `%USERPROFILE%\Documents\Smalltalk-80\Images\<slug>\` (matches
    the Mac `Documents/Images/<slug>/` layout).
  - "Download Xerox v2" pulls VirtualImage + Smalltalk-80.sources
    from the same GitHub release the Mac client uses
    (`https://github.com/avwohl/st80-images/releases/download/xerox-v2/`),
    via WinHTTP, with a progress bar.
  - "Add from file…" opens GetOpenFileName, copies the chosen
    file plus any sibling `.sources` / `.changes` files into the
    library.
  - "Delete" removes a library entry (with confirmation).

Last-launched image path is persisted to
`HKCU\Software\Aaron Wohl\st80-2026\LastImagePath`, so the second
run skips the launcher and goes straight into the VM. Hold Shift
at startup, or pass `--launcher`, to force the launcher back.

CLI behaviour unchanged for scripts / CI: passing an image path on
the command line bypasses the launcher entirely. `--no-window`
still requires an explicit path (no UI to pick one in headless mode).

Bug fixes folded in:

  - `app/windows/CMakeLists.txt` now passes `/MANIFEST:NO` to the
    linker. Our `.rc` already embeds a side-by-side manifest as
    resource `1 24`; the MSVC linker was generating a second one
    and the resource compiler failed with CVT1100 "duplicate
    resource". Without this, the GUI exe never linked in Debug.

New files:

  - `app/windows/Launcher.hpp` — public API
    (`ShowLauncher`, `RememberLastImage`, `LoadLastImage`).
  - `app/windows/Launcher.cpp` — pure Win32 implementation. No
    dialog template resource — controls are created with
    `CreateWindowExW`. WinHTTP download runs on a worker thread
    and posts progress back via custom `WM_APP_*` messages.

Modified:

  - `app/windows/st80_windows_main.cpp` — `WinMain` now picks
    image path from CLI > remembered path > launcher, in that
    order. Saves the path on clean exit. Usage MessageBox shown
    when arguments are malformed (was silently dropped).
  - `app/windows/CMakeLists.txt` — adds `Launcher.cpp` and
    links `comctl32`, `shlwapi`, `advapi32`, `ole32`, `winhttp`.

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

Build + package commands (x64, VS 2026):

    cmake -S . -B build -G "Visual Studio 18 2026" -A x64
    cmake --build build --config Release --parallel
    cd build && ctest -C Release --output-on-failure
    cd build && cpack -G NSIS -C Release      # st80-0.1.0-Windows-AMD64.exe
    cd build && cpack -G WIX  -C Release      # st80-0.1.0-Windows-AMD64.msi
    cmake --build build --target st80_appx_layout --config Release
    powershell -ExecutionPolicy Bypass -File packaging/windows/pack-msix.ps1 `
        -Layout build/st80-0.1.0-appx -Output build/st80-0.1.0.msix

Or the one-shot scripts at the repo root:

    build.bat                 REM Debug configure + build
    build_release.bat         REM Release + NSIS + WIX + MSIX
    build_release.bat -Arch ARM64

`build_release.bat` delegates to `packaging\windows\build-release.ps1`,
which auto-detects whether Visual Studio 2026 (install root
`C:\Program Files\Microsoft Visual Studio\18`) is present and falls
back to `"Visual Studio 17 2022"` if not. The VS 2026 toolset is
`v145`; CMake picks it implicitly from the generator.

ARM64 is reachable by passing `-Arch ARM64` to `build_release.bat`
(or `-A ARM64` to the raw `cmake -G` invocation) — the Windows
slice is architecture-agnostic and doesn't touch the JIT (Phase 6).
A separate pass will wire the ARM64 CI job once the Phase-6 JIT
backend needs it.

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
