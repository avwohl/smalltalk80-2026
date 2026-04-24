@echo off
REM st80-2026 — Debug build wrapper.
REM
REM Mirrors the z80cpmw convention: a one-shot .bat at the project
REM root that invokes Visual Studio 2026's MSBuild. We use CMake
REM upstream (no checked-in .sln), so this configures the build
REM tree first and then drives cmake --build, which ends up calling
REM the same MSBuild.exe from the VS 2026 install.
REM
REM VS 2026 install path: C:\Program Files\Microsoft Visual Studio\18\Community

setlocal

set "VS2026_ROOT=C:\Program Files\Microsoft Visual Studio\18"
if not exist "%VS2026_ROOT%" (
    echo [build.bat] Visual Studio 2026 not found at %VS2026_ROOT%.
    echo            Falling back to Visual Studio 17 2022 generator.
    set "GENERATOR=Visual Studio 17 2022"
) else (
    set "GENERATOR=Visual Studio 18 2026"
)

if not exist "%~dp0build" mkdir "%~dp0build"

cmake -S "%~dp0." -B "%~dp0build" -G "%GENERATOR%" -A x64 || exit /b %ERRORLEVEL%
cmake --build "%~dp0build" --config Debug --parallel || exit /b %ERRORLEVEL%

endlocal
