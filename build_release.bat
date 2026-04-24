@echo off
REM st80-2026 — Release build + package wrapper.
REM
REM Delegates to packaging\windows\build-release.ps1 which configures
REM with the Visual Studio 2026 generator, builds in Release, runs
REM CTest, and emits NSIS + WIX installers plus an MSIX Store package.
REM
REM Pass -Arch ARM64 / -Arch Win32 to the underlying script to target
REM a non-x64 architecture; default is x64.

powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0packaging\windows\build-release.ps1" %*
exit /b %ERRORLEVEL%
