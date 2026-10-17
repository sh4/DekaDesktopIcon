# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([Parameter(Mandatory)][string]$OutputPath)

$ErrorActionPreference = 'Stop'
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Path (Split-Path $OutputPath -Parent) -Force | Out-Null
Add-Type -AssemblyName System.Drawing

# A deterministic, transparent 1024px fixture; no binary assets are tracked.
$bitmap = [Drawing.Bitmap]::new(1024, 1024, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$folder = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 57, 203, 157))
$outline = [Drawing.Pen]::new([Drawing.Color]::FromArgb(255, 12, 104, 91), 14)
$white = [Drawing.SolidBrush]::new([Drawing.Color]::White)
$font = [Drawing.Font]::new('Segoe UI', 100, [Drawing.FontStyle]::Bold)

try {
    $graphics.Clear([Drawing.Color]::Transparent)
    $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $points = [Drawing.Point[]]@(
        [Drawing.Point]::new(100, 260), [Drawing.Point]::new(100, 150),
        [Drawing.Point]::new(420, 150), [Drawing.Point]::new(500, 260),
        [Drawing.Point]::new(924, 260), [Drawing.Point]::new(924, 850),
        [Drawing.Point]::new(100, 850)
    )
    $graphics.FillPolygon($folder, $points)
    $graphics.DrawPolygon($outline, $points)
    $graphics.DrawString('1024', $font, $white, [single]285, [single]445)
    $bitmap.Save($OutputPath, [Drawing.Imaging.ImageFormat]::Png)
} finally {
    $font.Dispose()
    $white.Dispose()
    $outline.Dispose()
    $folder.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}
Write-Output $OutputPath
