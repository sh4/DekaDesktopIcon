# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\TestSupport.ps1"
$testDir = Join-Path $PSScriptRoot 'build\validation'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$profile = Join-Path $testDir 'test.ini'
$sample = Get-DekaTestImage
"[Images]`r`nCount=1`r`n[Image0]`r`nTarget=C:\Windows`r`nPath=$sample`r`n" | Set-Content -LiteralPath $profile -Encoding unicode
function Invoke-Patch([string[]]$Options, [string]$Name) {
    $report = Join-Path $testDir $Name
    return Invoke-DekaCommand -BinaryDirectory $BinaryDirectory -Options $Options `
    -Profile $profile -Report $report -TestHost
}
$hostProcess = Start-Process -FilePath (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe') -WindowStyle Hidden -PassThru
$hostProcess.WaitForInputIdle(5000) | Out-Null
$before = Invoke-Patch @('--status') 'host-before.json'
try {
    $apply = Invoke-Patch @('--apply','--size','1024','--lease','20') 'host-applied.json'
    if (-not $apply.active -or $apply.imageWidth -ne 1024 -or $apply.imageHeight -ne 1024) {
        throw 'Image list did not become 1024x1024'
    }
    $capture = Join-Path $testDir 'host-native-1024.png'
    $render = Invoke-Patch @('--capture',$capture) 'host-rendered.json'
    if ($render.customPaintCount -lt 1 -or $render.lastDrawWidth -ne 1024) {
        throw 'Native custom drawing did not run'
    }
    $restore = Invoke-Patch @('--restore') 'host-restored.json'
    if ($restore.active -or $restore.subclassInstalled -or $restore.imageWidth -ne $before.imageWidth -or $restore.restorePositionFailures) {
        throw 'Original state was not restored'
    }
    [pscustomobject]@{
        passed = $true
        initialWidth = $before.imageWidth
        patchedWidth = $apply.imageWidth
        drawWidth = $render.lastDrawWidth
        restoredWidth = $restore.imageWidth
        positionsRestored = $restore.restoreMatched
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $testDir 'host-summary.json') -Encoding utf8
    Get-Content -LiteralPath (Join-Path $testDir 'host-summary.json')
} finally {
    Invoke-Patch @('--restore') 'host-final-state.json' | Out-Null
    $ownHost = Get-Process -Id $hostProcess.Id -ErrorAction SilentlyContinue
    if ($ownHost -and $ownHost.Path -eq (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe')) {
        Stop-Process -Id $ownHost.Id
    }
}
