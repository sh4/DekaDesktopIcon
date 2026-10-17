# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\TestSupport.ps1"
$validation = Join-Path $PSScriptRoot 'build\validation'
New-Item -ItemType Directory -Path $validation -Force | Out-Null
$profile = Join-Path $validation 'explorer-test.ini'
$sample = Get-DekaTestImage
"[Images]`r`nCount=1`r`n[Image0]`r`nTarget=::{645FF040-5081-101B-9F08-00AA002F954E}`r`nPath=$sample`r`n" | Set-Content -LiteralPath $profile -Encoding unicode
function Invoke-ExplorerPatch([string[]]$Options, [string]$Name) {
    $report = Join-Path $validation $Name
    return Invoke-DekaCommand -BinaryDirectory $BinaryDirectory -Options $Options `
    -Profile $profile -Report $report
}
$before = Invoke-ExplorerPatch @('--status') 'explorer-before.json'
if ($before.active) {
    throw 'An existing desktop patch is active; refusing to replace it during validation'
}
try {
    $apply = Invoke-ExplorerPatch @('--apply','--size','1024','--lease','20') 'explorer-applied.json'
    if (-not $apply.active -or $apply.imageWidth -ne 1024 -or $apply.imageHeight -ne 1024) {
        throw 'Explorer did not accept the 1024px list'
    }
    $capture = Join-Path $validation 'explorer-native-1024.png'
    $render = Invoke-ExplorerPatch @('--capture',$capture) 'explorer-rendered.json'
    if ($render.customPaintCount -lt 1 -or $render.lastDrawWidth -ne 1024 -or $render.nativeIconWidth -lt 1024 -or $render.nativeHitAtCenter -lt 0) {
        throw 'Explorer native drawing or hit test failed'
    }
    $restore = Invoke-ExplorerPatch @('--restore') 'explorer-restored.json'
    if ($restore.active -or $restore.subclassInstalled -or $restore.imageWidth -ne $before.imageWidth -or $restore.restorePositionFailures) {
        throw 'Explorer state did not restore'
    }
    [pscustomobject]@{
        passed = $true
        targetPid = $apply.pid
        process = 'explorer.exe'
        initialWidth = $before.imageWidth
        patchedWidth = $apply.imageWidth
        drawWidth = $render.lastDrawWidth
        nativeIconRectWidth = $render.nativeIconWidth
        nativeHitAtCenter = $render.nativeHitAtCenter
        sourceWidth = $render.sourceWidth
        restoredWidth = $restore.imageWidth
        positionsRestored = $restore.restoreMatched
        restorePositionFailures = $restore.restorePositionFailures
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $validation 'explorer-summary.json') -Encoding utf8
    Get-Content -LiteralPath (Join-Path $validation 'explorer-summary.json')
} finally {
    Invoke-ExplorerPatch @('--restore') 'explorer-final-state.json' | Out-Null
}
