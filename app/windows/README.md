# st80-2026 — Windows (x64)

Pure Win32 + GDI frontend. No SDL, no Direct2D, no third-party
runtime. Ships as an NSIS `.exe` installer, a WiX `.msi`, or an
MSIX package for the Microsoft Store. Tested on Windows 11 x64;
the same build also produces ARM64 binaries (pass `-Arch ARM64`
to the release script).

## Prerequisites — from a clean Windows install

Everything below is a one-time install per machine.

### 1. Visual Studio 2026 Community (or 2022)

Download from <https://visualstudio.microsoft.com/> and run the
installer.

In the installer's **Workloads** tab, check:

  * **Desktop development with C++**

Leave the default sub-components alone — the MSVC toolset, the
Windows 11 SDK, and a bundled CMake all come in through that
workload.

Visual Studio 2022 works as a fallback. The build scripts detect
VS 2026 first and fall back to VS 2022 automatically.

### 2. Git for Windows

Download from <https://git-scm.com/download/win>. During install,
accept the defaults — you want `git.exe` on `PATH` and the
bundled `bash.exe` available (the release PowerShell script shells
out to it).

### 3. Optional: NSIS and WiX for release packaging

Only needed if you want to produce installers (not needed for
debug builds):

  * **NSIS** — <https://nsis.sourceforge.io/Download>. Install the
    stable release; the default location puts `makensis.exe` where
    CPack finds it.
  * **WiX Toolset v5** — install as a .NET global tool, no admin:

        dotnet tool install --global wix --version 5.0.2
        wix extension add --global WixToolset.UI.wixext/5.0.2

    Pin to 5.0.2. WiX 7 requires accepting the Open Source
    Maintenance Fee EULA; v5 is the latest MS-RL-licensed line.
    Legacy v3 (candle/light) is discontinued and no longer supported
    by our CMake config.

MSIX packaging uses `makeappx.exe` and `signtool.exe` from the
Windows SDK, which the VS workload already installed. No extra
download.

## Clone the repo

From any shell (PowerShell or the Git Bash that came with Git for
Windows):

    git clone https://github.com/avwohl/st80-2026.git
    cd st80-2026

## Debug build

Open the **Developer Command Prompt for VS 2026** from the Start
Menu (or VS 2022's equivalent). This is a `cmd` shell with
`cmake`, `MSBuild`, and the MSVC compilers already on `PATH`.

    cd C:\path\to\st80-2026
    build.bat

`build.bat` configures CMake with the Visual Studio generator and
builds every target in Debug. Artifacts:

    build\app\windows\Debug\st80-win.exe       — GUI app with launcher
    build\tools\Debug\st80_probe.exe           — image loader sanity
    build\tools\Debug\st80_run.exe             — headless trace tool
    build\tools\Debug\st80_validate.exe        — image validator
    build\tests\Debug\core_smoke_test.exe      — core smoke test

Running from a non-VS shell: source the VS environment first.

    cmd /k "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64

…then run `build.bat` in the resulting prompt.

## Run the CTest suite

    ctest --test-dir build -C Debug --output-on-failure

## First run — the launcher

Double-click `build\app\windows\Debug\st80-win.exe`, or invoke it
from the command line with no arguments. You'll see the launcher.

  * **Download Xerox v2** — pulls `VirtualImage` and
    `Smalltalk-80.sources` from
    <https://github.com/avwohl/st80-images/releases/tag/xerox-v2>
    via WinHTTP and registers the image in the library.
  * **Add from file…** — browse to an image you already have on
    disk. Any `Smalltalk-80.sources` / `Smalltalk-80.changes` files
    sitting next to the image are copied in alongside.
  * **Delete** — removes an entry from the library.
  * **Launch** — loads the selected image and drops into the
    Smalltalk-80 desktop.

The library lives at
`%USERPROFILE%\Documents\Smalltalk-80\Images\<slug>\`.

## Subsequent runs

The last-launched image path is saved to
`HKCU\Software\Aaron Wohl\st80-2026\LastImagePath` so double-
clicking the exe goes straight into the VM without the launcher.

To get the launcher back:

  * Hold **Shift** while launching, **or**
  * Run `st80-win.exe --launcher` from the command line.

To skip the launcher entirely (scripting / CI), pass an image path
directly:

    st80-win.exe C:\path\to\VirtualImage

`--no-window` runs a fixed batch of cycles headlessly and exits —
used by the MSIX Store smoke test and CI.

## Mouse and keyboard mapping

  * plain left click        — red (select)
  * right-click / Alt+Left  — yellow (text / operate menu)
  * middle-click / Ctrl+Left — blue (window / frame menu)

Keyboard input is ASCII via `WM_CHAR`. Forward-delete (VK_DELETE)
is remapped to ASCII 127 since most Windows layouts don't send
WM_CHAR for it.

## Release build + installer packaging

`build_release.bat` wraps `packaging\windows\build-release.ps1`.
It configures CMake in Release, runs CTest, then emits NSIS + WiX
installers plus an MSIX AppX package.

    build_release.bat            :: x64, Release (default)
    build_release.bat -Arch ARM64
    build_release.bat -SkipMsix  :: NSIS + WiX only, skip MSIX

Output lands next to the build tree:

    build\st80-<ver>-Windows-AMD64.exe   — NSIS
    build\st80-<ver>-Windows-AMD64.msi   — WiX
    build\st80-<ver>.msix                — MSIX (Store submission)
