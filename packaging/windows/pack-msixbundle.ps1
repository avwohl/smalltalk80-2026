# st80-2026 — pack-msixbundle.ps1
#
# Bundles a set of per-architecture .msix files into a single
# .msixbundle suitable for Microsoft Store submission. The Store
# inspects the bundle manifest and serves each device the .msix
# package matching its CPU (x64 to Intel/AMD, arm64 to Snapdragon X
# / Surface Pro X / Copilot+ PCs), so one upload covers both.
#
# Pairs with pack-msix.ps1 — that script emits one .msix per arch;
# this one wraps them together.
#
# Usage (Store publish — unsigned; Store re-signs):
#
#   powershell -ExecutionPolicy Bypass -File packaging\windows\pack-msixbundle.ps1 `
#       -Msix @('build\st80-0.1.0-x64.msix','build\st80-0.1.0-ARM64.msix') `
#       -Output build\st80-0.1.0.msixbundle `
#       -Version 0.1.0.0
#
# Usage (side-load testing — signed with your own cert):
#
#   ... -CertFile C:\certs\st80-test.pfx -CertPassword (Read-Host -AsSecureString)

param(
    [Parameter(Mandatory=$true)][string[]]$Msix,
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][string]$Version,

    # Optional SignTool inputs for side-loading. Store uploads are
    # unsigned — Store re-signs with the publisher's Store cert.
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
    throw "Could not locate $name in Windows 10/11 SDK."
}

# Resolve inputs + validate each exists.
$resolved = @()
foreach ($m in $Msix) {
    if (-not (Test-Path $m)) { throw "Missing .msix: $m" }
    $resolved += (Resolve-Path $m).Path
}

$OutputDir = Split-Path -Parent $Output
if ($OutputDir -and -not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

# MakeAppx bundle wants a directory containing ONLY the per-arch
# .msix files to bundle. Stage a clean dir beside the output so we
# don't drag along unrelated .msix files that happen to be siblings.
$stageDir = Join-Path ([System.IO.Path]::GetTempPath()) `
    "st80-msixbundle-$([System.Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

try {
    foreach ($m in $resolved) {
        Copy-Item -Path $m -Destination $stageDir
    }

    $MakeAppx = Find-SdkTool 'MakeAppx.exe'
    Write-Host "MakeAppx: $MakeAppx"
    Write-Host "Stage   : $stageDir"
    Write-Host "Output  : $Output"
    Write-Host "Version : $Version"
    Write-Host "Packages:"
    foreach ($m in $resolved) { Write-Host "  $m" }

    # /bv must be 4-part Major.Minor.Build.Revision. Store requires
    # Revision == 0 on upload; Partner Center assigns its own.
    & $MakeAppx bundle /d $stageDir /p $Output /bv $Version /o
    if ($LASTEXITCODE -ne 0) { throw "MakeAppx bundle failed ($LASTEXITCODE)" }

    if ($CertFile) {
        $SignTool = Find-SdkTool 'SignTool.exe'
        $CertFile = (Resolve-Path $CertFile).Path
        Write-Host "Signing bundle with $CertFile"
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
} finally {
    if (Test-Path $stageDir) {
        Remove-Item -Recurse -Force $stageDir
    }
}
