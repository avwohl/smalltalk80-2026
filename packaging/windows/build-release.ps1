# st80-2026 — build-release.ps1
#
# One-shot Windows release build. Configures CMake with the Visual
# Studio 2022 generator, builds in Release, runs CTest, then emits
# all three package artifacts:
#
#   build\st80-<ver>-Windows-AMD64.exe    (NSIS)
#   build\st80-<ver>-Windows-AMD64.msi    (WiX)
#   build\st80-<ver>.msix                  (MSIX for Store)
#
# Usage:
#
#   powershell -ExecutionPolicy Bypass `
#       -File packaging\windows\build-release.ps1 `
#       -Arch x64
#
# Supported -Arch values: x64 (default), ARM64, Win32.

param(
    [ValidateSet('x64','ARM64','Win32')]
    [string]$Arch = 'x64',

    [string]$BuildDir = 'build',

    [ValidateSet('Release','Debug','RelWithDebInfo')]
    [string]$Config = 'Release',

    [switch]$SkipTests,
    [switch]$SkipPackage,
    [switch]$SkipMsix
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot | Split-Path -Parent
Set-Location $root

Write-Host "=== st80-2026 Windows release build ==="
Write-Host "    root    : $root"
Write-Host "    arch    : $Arch"
Write-Host "    config  : $Config"
Write-Host "    dir     : $BuildDir"
Write-Host ""

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# Configure + build.
cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A $Arch
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

# Tests.
if (-not $SkipTests) {
    Push-Location $BuildDir
    try {
        ctest -C $Config --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "ctest failed" }
    } finally { Pop-Location }
}

# NSIS + WiX installers.
if (-not $SkipPackage) {
    Push-Location $BuildDir
    try {
        cpack -G NSIS -C $Config
        if ($LASTEXITCODE -ne 0) { throw "cpack NSIS failed" }
        cpack -G WIX -C $Config
        if ($LASTEXITCODE -ne 0) { throw "cpack WIX failed" }
    } finally { Pop-Location }
}

# MSIX AppX layout + pack.
if (-not $SkipMsix) {
    cmake --build $BuildDir --target st80_appx_layout --config $Config
    if ($LASTEXITCODE -ne 0) { throw "st80_appx_layout build failed" }

    $ver = (Select-String -Path CMakeLists.txt -Pattern 'VERSION ([\d\.]+)' |
        Select-Object -First 1).Matches[0].Groups[1].Value
    $layoutDir = Join-Path $BuildDir "st80-$ver-appx"
    $msixOut   = Join-Path $BuildDir "st80-$ver.msix"

    powershell -NoProfile -ExecutionPolicy Bypass `
        -File "$PSScriptRoot\pack-msix.ps1" `
        -Layout $layoutDir `
        -Output $msixOut
    if ($LASTEXITCODE -ne 0) { throw "pack-msix.ps1 failed" }
}

Write-Host ""
Write-Host "=== done ==="
Get-ChildItem $BuildDir -Filter 'st80-*' | ForEach-Object {
    Write-Host "  $($_.Name)"
}
