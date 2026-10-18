# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin",[switch]$Explorer,[switch]$DropFeedbackOnly)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\TestSupport.ps1"
$BinaryDirectory = [IO.Path]::GetFullPath($BinaryDirectory)
$validation = Join-Path $PSScriptRoot 'build\validation'
New-Item -ItemType Directory -Path $validation -Force | Out-Null
$targetName = if ($Explorer) {
    'explorer'
} else {
    'host'
}
$suite = if ($DropFeedbackOnly) { 'drop-feedback' } else { 'interaction' }
$testName = "$targetName-$suite"
$profile = Join-Path $validation "$testName.ini"
$sample = Get-DekaTestImage
if ($Explorer) {
    "[Images]`r`nCount=0`r`n" | Set-Content -LiteralPath $profile -Encoding unicode
}
else {
    "[Images]`r`nCount=1`r`n[Image0]`r`nTarget=C:\Windows`r`nPath=$sample`r`n" | Set-Content -LiteralPath $profile -Encoding unicode
}
function Invoke-Patch([string[]]$Options, [string]$Name) {
    $report = Join-Path $validation $Name
    return Invoke-DekaCommand -BinaryDirectory $BinaryDirectory -Options $Options `
    -Profile $profile -Report $report -TestHost:(-not $Explorer)
}
$hostProcess = $null
if (-not $Explorer) {
    $hostProcess = Start-Process -FilePath (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe') -WindowStyle Hidden -PassThru
    $hostProcess.WaitForInputIdle(5000) | Out-Null
}
$before = Invoke-Patch @('--status') "$testName-before.json"
if ($before.active) {
    throw 'An existing patch is active; restore it before validation'
}
$results = @()
try {
    $sizes = if ($DropFeedbackOnly) { @(64,256,512,1024) } else { @(512,1024) }
    $command = if ($DropFeedbackOnly) { '--validate-drop-feedback' } else { '--validate-interaction' }
    foreach ($size in $sizes) {
        $applied = Invoke-Patch @('--apply','--size',"$size",'--lease','60') "$testName-$size-applied.json"
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $status = Invoke-Patch @('--status') "$testName-$size-ready.json"
        } while ($status.cachedCount -lt $status.itemCount -and [DateTime]::UtcNow -lt $deadline)
        $testResult = Join-Path $validation "$testName-$size.json"
        Invoke-Patch @($command,$testResult) "$testName-$size-command.json" | Out-Null
        $result = Get-Content -Raw -LiteralPath $testResult | ConvertFrom-Json
        if (-not $result.passed) {
            throw 'Interaction regression failed'
        }
        $results += $result
        $restored = Invoke-Patch @('--restore') "$testName-$size-restored.json"
        if ($restored.active -or $restored.imageWidth -ne $before.imageWidth -or $restored.restorePositionFailures) {
            throw 'Interaction state did not restore'
        }
    }
    [pscustomobject]@{
        passed=$true
        process=$targetName
        pid=$before.pid
        results=$results
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $validation "$testName-summary.json") -Encoding utf8
    Get-Content -LiteralPath (Join-Path $validation "$testName-summary.json")
} finally {
    Invoke-Patch @('--restore') "$testName-final-state.json" | Out-Null
    if ($hostProcess) {
        $ownHost = Get-Process -Id $hostProcess.Id -ErrorAction SilentlyContinue
        if ($ownHost -and $ownHost.Path -eq (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe')) {
            Stop-Process -Id $ownHost.Id
        }
    }
}
