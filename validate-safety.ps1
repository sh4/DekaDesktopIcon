# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\TestSupport.ps1"
$validation = Join-Path $PSScriptRoot 'build\validation'
New-Item -ItemType Directory -Path $validation -Force | Out-Null
function Invoke-SafetyCommand([string[]]$Options, [string]$Name, [int]$ExpectedExit = 0) {
    $report = Join-Path $validation $Name
    return Invoke-DekaCommand -BinaryDirectory $BinaryDirectory -Options $Options `
    -Profile (Join-Path $validation 'test.ini') -Report $report -TestHost -ExpectedExit $ExpectedExit
}
$hostProcess = Start-Process -FilePath (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe') -WindowStyle Hidden -PassThru
$hostProcess.WaitForInputIdle(5000) | Out-Null
try {
    $before = Invoke-SafetyCommand @('--status') 'safety-before.json'
    $valid = Invoke-SafetyCommand @('--apply','--size','1024','--lease','2') 'safety-applied.json'
    $invalid = Invoke-SafetyCommand @('--apply','--size','1056') 'safety-invalid.json' 1
    $stillActive = Invoke-SafetyCommand @('--status') 'safety-still-active.json'
    if (-not $stillActive.active -or $stillActive.imageWidth -ne 1024) {
        throw 'Invalid request changed the active patch'
    }
    Start-Sleep -Milliseconds 3100
    $expired = Invoke-SafetyCommand @('--status') 'safety-expired.json'
    if ($expired.active -or $expired.imageWidth -ne $before.imageWidth -or $expired.restorePositionFailures) {
        throw 'Timed rollback failed'
    }
    [pscustomobject]@{
        passed = $true
        invalidSizeRejected = $invalid.error -ne 0
        activePatchPreservedAfterInvalidRequest = $stillActive.active
        leaseExpired = -not $expired.active
        restoredWidth = $expired.imageWidth
        positionsRestored = $expired.restoreMatched
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $validation 'safety-summary.json') -Encoding utf8
    Get-Content -LiteralPath (Join-Path $validation 'safety-summary.json')
} finally {
    Invoke-SafetyCommand @('--restore') 'safety-final-state.json' | Out-Null
    $ownHost = Get-Process -Id $hostProcess.Id -ErrorAction SilentlyContinue
    if ($ownHost -and $ownHost.Path -eq (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe')) {
        Stop-Process -Id $ownHost.Id
    }
}
