# st80-2026 — build-release.ps1
#
# One-shot Windows release build. Configures CMake with the Visual
# Studio 2026 generator (VS 18, toolset v145), builds in Release,
# runs CTest, then emits all the package artifacts. When -Archs
# contains more than one value, the per-arch .msix files are also
# combined into a single .msixbundle ready for Microsoft Store
# submission.
#
# Usage (x64 only — dev loop):
#
#   powershell -ExecutionPolicy Bypass `
#       -File packaging\windows\build-release.ps1 `
#       -Archs x64
#
# Usage (Store release — bundle x64 + ARM64):
#
#   powershell -ExecutionPolicy Bypass `
#       -File packaging\windows\build-release.ps1 `
#       -Archs x64,ARM64
#
# Supported -Archs values: x64, ARM64, Win32.
#
# Per-arch outputs land in build-<arch>\; the cross-arch bundle
# lands at the top level as build\st80-<ver>.msixbundle.

param(
    [ValidateSet('x64','ARM64','Win32')]
    [string[]]$Archs = @('x64'),

    [string]$BuildRoot = 'build',

    [ValidateSet('Release','Debug','RelWithDebInfo')]
    [string]$Config = 'Release',

    [switch]$SkipTests,
    [switch]$SkipPackage,
    [switch]$SkipMsix,
    [switch]$SkipBundle
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot | Split-Path -Parent
Set-Location $root

Write-Host "=== st80-2026 Windows release build ==="
Write-Host "    root    : $root"
Write-Host "    archs   : $($Archs -join ', ')"
Write-Host "    config  : $Config"
Write-Host ""

# Pick generator once — shared across arches.
$generator = 'Visual Studio 18 2026'
$vs2026Root = "${env:ProgramFiles}\Microsoft Visual Studio\18"
if (-not (Test-Path $vs2026Root)) {
    Write-Host "VS 2026 not found at $vs2026Root; falling back to VS 2022."
    $generator = 'Visual Studio 17 2022'
}
Write-Host "    gen     : $generator"
Write-Host ""

# Version pulled from the top-level CMakeLists.txt so per-arch
# output paths agree with the .msix files st80_appx_layout emits.
# Match the 3-part project() VERSION (e.g. "VERSION 0.1.0"), not the
# 2-part cmake_minimum_required(VERSION 3.20) line at the top.
$ver = (Select-String -Path CMakeLists.txt -Pattern 'VERSION (\d+\.\d+\.\d+)' |
    Select-Object -First 1).Matches[0].Groups[1].Value
$bundleVer = "$ver.0"  # 4-part Major.Minor.Build.Revision for MSIX

# Collected per-arch .msix outputs, in $Archs order.
$msixOutputs = @()

foreach ($Arch in $Archs) {
    Write-Host ""
    Write-Host "--- arch: $Arch ---"

    $BuildDir = "$BuildRoot-$($Arch.ToLower())"
    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir | Out-Null
    }

    cmake -S . -B $BuildDir -G $generator -A $Arch
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($Arch)" }

    cmake --build $BuildDir --config $Config --parallel
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($Arch)" }

    # Tests only on a host that can natively execute the target
    # binary — cross-compiled ARM64 exes don't run on x64 hosts.
    if (-not $SkipTests) {
        $hostArch = $env:PROCESSOR_ARCHITECTURE
        $canRun = ($Arch -eq 'x64'   -and $hostArch -in 'AMD64','ARM64') -or `
                  ($Arch -eq 'ARM64' -and $hostArch -eq 'ARM64') -or `
                  ($Arch -eq 'Win32' -and $hostArch -in 'AMD64','ARM64','x86')
        if ($canRun) {
            Push-Location $BuildDir
            try {
                ctest -C $Config --output-on-failure
                if ($LASTEXITCODE -ne 0) { throw "ctest failed ($Arch)" }
            } finally { Pop-Location }
        } else {
            Write-Host "Skipping tests for $Arch on $hostArch host (cross-compile)."
        }
    }

    # NSIS + WiX installers. These are per-arch and go into the
    # per-arch build dir; their file names already encode the CPU
    # via CPACK_PACKAGE_FILE_NAME.
    if (-not $SkipPackage) {
        Push-Location $BuildDir
        try {
            cpack -G NSIS -C $Config
            if ($LASTEXITCODE -ne 0) { throw "cpack NSIS failed ($Arch)" }
            # WiX is optional: requires the WiX Toolset to be on PATH
            # (candle.exe + light.exe). If it isn't installed we warn
            # and skip — the NSIS .exe + .msixbundle still cover every
            # distribution channel we care about.
            cpack -G WIX -C $Config
            if ($LASTEXITCODE -ne 0) {
                Write-Warning "cpack WIX skipped for $Arch (WiX Toolset not found). Install from wixtoolset.org to produce an .msi."
                $global:LASTEXITCODE = 0
            }
        } finally { Pop-Location }
    }

    # MSIX AppX layout + single-arch .msix.
    if (-not $SkipMsix) {
        cmake --build $BuildDir --target st80_appx_layout --config $Config
        if ($LASTEXITCODE -ne 0) { throw "st80_appx_layout build failed ($Arch)" }

        $layoutDir = Join-Path $BuildDir "st80-$ver-appx"
        $msixOut   = Join-Path $BuildDir "st80-$ver-$Arch.msix"

        & "$PSScriptRoot\pack-msix.ps1" -Layout $layoutDir -Output $msixOut
        if ($LASTEXITCODE -ne 0) { throw "pack-msix.ps1 failed ($Arch)" }

        $msixOutputs += (Resolve-Path $msixOut).Path
    }
}

# Cross-arch bundle. Only meaningful when we actually built .msix
# files; single-arch builds can still produce a bundle (Store is
# happy with a 1-package .msixbundle) but most devs only need the
# bundle when shipping multiple architectures.
if (-not $SkipMsix -and -not $SkipBundle -and $msixOutputs.Count -gt 1) {
    if (-not (Test-Path $BuildRoot)) {
        New-Item -ItemType Directory -Path $BuildRoot | Out-Null
    }
    $bundleOut = Join-Path $BuildRoot "st80-$ver.msixbundle"

    Write-Host ""
    Write-Host "--- bundle ($($msixOutputs.Count) packages) ---"
    & "$PSScriptRoot\pack-msixbundle.ps1" `
        -Msix $msixOutputs `
        -Output $bundleOut `
        -Version $bundleVer
    if ($LASTEXITCODE -ne 0) { throw "pack-msixbundle.ps1 failed" }
}

Write-Host ""
Write-Host "=== done ==="
foreach ($Arch in $Archs) {
    $BuildDir = "$BuildRoot-$($Arch.ToLower())"
    if (Test-Path $BuildDir) {
        Get-ChildItem $BuildDir -Filter 'st80-*' -File -ErrorAction SilentlyContinue |
            ForEach-Object { Write-Host "  $($_.FullName)" }
    }
}
if (Test-Path $BuildRoot) {
    Get-ChildItem $BuildRoot -Filter 'st80-*.msixbundle' -File -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "  $($_.FullName)" }
}
