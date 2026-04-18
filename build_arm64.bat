@echo off
REM st80-2026 — Debug ARM64 build wrapper.
REM
REM Parallel to build.bat, but targets Windows on ARM (Copilot+ PCs,
REM Surface Pro X, Snapdragon X-series). Uses MSVC's x64-hosted
REM ARM64 cross-compiler, which is the `Microsoft.VisualStudio.Component.VC.Tools.ARM64`
REM component in Visual Studio 2026.
REM
REM The resulting st80-win.exe runs natively on ARM64 (no x64
REM emulation layer), which is much faster for an interpreter-heavy
REM workload like a Smalltalk VM.

setlocal

set "VS2026_ROOT=C:\Program Files\Microsoft Visual Studio\18"
if not exist "%VS2026_ROOT%" (
    echo [build_arm64.bat] Visual Studio 2026 not found at %VS2026_ROOT%.
    echo                  Falling back to Visual Studio 17 2022 generator.
    set "GENERATOR=Visual Studio 17 2022"
) else (
    set "GENERATOR=Visual Studio 18 2026"
)

if not exist "%~dp0build-arm64" mkdir "%~dp0build-arm64"

cmake -S "%~dp0." -B "%~dp0build-arm64" -G "%GENERATOR%" -A ARM64 || exit /b %ERRORLEVEL%
cmake --build "%~dp0build-arm64" --config Debug --parallel || exit /b %ERRORLEVEL%

endlocal
