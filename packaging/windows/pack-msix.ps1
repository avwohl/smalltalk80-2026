# st80-2026 — pack-msix.ps1
#
# Takes the `build/st80-<ver>-appx/` layout directory produced by
# the CMake `st80_appx_layout` target and packs it into a signable
# `.msix` with Microsoft's MakeAppx.exe (part of the Windows 10/11
# SDK). Optionally signs it with SignTool.exe when -CertFile is
# supplied, which is required for side-loading outside the Store.
#
# Usage (side-load smoke test):
#
#   powershell -ExecutionPolicy Bypass -File packaging\windows\pack-msix.ps1 `
#       -Layout build\st80-0.1.0-appx `
#       -Output build\st80-0.1.0.msix
#
# Usage (publishing to the Store — unsigned; Store re-signs):
#
#   ... -NoValidate:$false -Skip Sign
#
# Usage (signing for side-load with your own test cert):
#
#   ... -CertFile C:\certs\st80-test.pfx -CertPassword (Read-Host -AsSecureString)

param(
    [Parameter(Mandatory=$true)][string]$Layout,
    [Parameter(Mandatory=$true)][string]$Output,

    # If set, skip MakeAppx's semantic validation. Needed for CI
    # smoke tests that use placeholder logo PNGs. Store uploads
    # MUST pass full validation — flip this off for publish.
    [bool]$NoValidate = $true,

    # Optional SignTool inputs for side-loading signing.
    [string]$CertFile = $null,
    [SecureString]$CertPassword = $null
)

$ErrorActionPreference = 'Stop'

function Find-SdkTool([string]$name) {
    $roots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "${env:ProgramFiles}\Windows Kits\10\bin"
    )
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $found = Get-ChildItem -Recurse -Path $root -Filter $name `
            -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    throw "Could not locate $name in Windows 10/11 SDK. Install it from visualstudio.microsoft.com/downloads."
}

$Layout = (Resolve-Path $Layout).Path
$OutputDir = Split-Path -Parent $Output
if ($OutputDir -and -not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

$MakeAppx = Find-SdkTool 'MakeAppx.exe'
$SignTool = Find-SdkTool 'SignTool.exe'

Write-Host "MakeAppx: $MakeAppx"
Write-Host "Layout  : $Layout"
Write-Host "Output  : $Output"

$packArgs = @('pack', '/d', $Layout, '/p', $Output, '/o')
if ($NoValidate) { $packArgs += '/nv' }

& $MakeAppx @packArgs
if ($LASTEXITCODE -ne 0) { throw "MakeAppx pack failed ($LASTEXITCODE)" }

if ($CertFile) {
    $CertFile = (Resolve-Path $CertFile).Path
    Write-Host "Signing with $CertFile"
    $signArgs = @('sign', '/fd', 'SHA256', '/a', '/f', $CertFile)
    if ($CertPassword) {
        $plain = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
            [Runtime.InteropServices.Marshal]::SecureStringToBSTR($CertPassword))
        $signArgs += @('/p', $plain)
    }
    $signArgs += $Output
    & $SignTool @signArgs
    if ($LASTEXITCODE -ne 0) { throw "SignTool sign failed ($LASTEXITCODE)" }
}

Write-Host "OK: $Output"
