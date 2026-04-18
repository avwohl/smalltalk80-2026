# st80-2026 — gen-store-assets.ps1
#
# Generates the seven PNGs that AppxManifest.xml references, from a
# single 1024x1024 source icon. The Microsoft Store rejects an
# upload if any of these are missing, with the error:
#
#     Package acceptance validation error: The following image(s)
#     specified in the appxManifest.xml were not found: ...
#
# Sizes match the Store-recommended dimensions for a Desktop Bridge
# / Win32 MSIX package (full-trust). Corners are left square — the
# Windows shell applies its own rounding and tile styling.
#
# Usage:
#
#   powershell -ExecutionPolicy Bypass -File packaging\windows\gen-store-assets.ps1
#
# Outputs overwrite packaging\windows\Assets\*.png.

param(
    # Source square icon, >= 1024x1024 strongly recommended so the
    # 310x310 tile scales down rather than up.
    [string]$Source = (Join-Path $PSScriptRoot '..\..\app\apple-catalyst\Assets.xcassets\AppIcon.appiconset\icon_1024.png'),

    [string]$OutDir = (Join-Path $PSScriptRoot 'Assets'),

    # Background behind the square icon on the wide tile and splash
    # screen. Keep in sync with AppxManifest.xml's BackgroundColor.
    [string]$BackgroundHex = '#1E1E1E'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$Source = (Resolve-Path $Source).Path
if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}
$OutDir = (Resolve-Path $OutDir).Path

Write-Host "Source : $Source"
Write-Host "OutDir : $OutDir"
Write-Host "BG     : $BackgroundHex"
Write-Host ""

$bgColor = [System.Drawing.ColorTranslator]::FromHtml($BackgroundHex)
$src = [System.Drawing.Image]::FromFile($Source)

try {
    function Save-Scaled([int]$w, [int]$h, [string]$path, [bool]$letterbox = $false) {
        $bmp = New-Object System.Drawing.Bitmap $w, $h
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        try {
            $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

            if ($letterbox) {
                # Non-square target: fill background, center square icon.
                $g.Clear($bgColor)
                $iconSize = [Math]::Min($w, $h)
                $x = [int](($w - $iconSize) / 2)
                $y = [int](($h - $iconSize) / 2)
                $g.DrawImage($src, $x, $y, $iconSize, $iconSize)
            } else {
                $g.DrawImage($src, 0, 0, $w, $h)
            }
        } finally { $g.Dispose() }
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        Write-Host ("  {0,-25} {1}x{2}" -f ([System.IO.Path]::GetFileName($path)), $w, $h)
    }

    Save-Scaled  50  50 (Join-Path $OutDir 'StoreLogo.png')        $false
    Save-Scaled  44  44 (Join-Path $OutDir 'Square44x44Logo.png')  $false
    Save-Scaled  71  71 (Join-Path $OutDir 'Square71x71Logo.png')  $false
    Save-Scaled 150 150 (Join-Path $OutDir 'Square150x150Logo.png') $false
    Save-Scaled 310 310 (Join-Path $OutDir 'Square310x310Logo.png') $false
    Save-Scaled 310 150 (Join-Path $OutDir 'Wide310x150Logo.png')   $true
    Save-Scaled 620 300 (Join-Path $OutDir 'SplashScreen.png')      $true
} finally {
    $src.Dispose()
}

Write-Host ""
Write-Host "OK."
