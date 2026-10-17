# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin",[switch]$Explorer)
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
$profile = Join-Path $validation "$targetName-shell-behavior.ini"
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
$before = Invoke-Patch @('--status') "$targetName-shell-behavior-before.json"
if ($before.active) {
    throw 'An existing patch is active; restore it before validation'
}
$results = @()
try {
    foreach ($size in @(512,1024)) {
        $applied = Invoke-Patch @('--apply','--size',"$size",'--lease','60') "$targetName-shell-behavior-$size-applied.json"
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $status = Invoke-Patch @('--status') "$targetName-shell-behavior-$size-ready.json"
        } while ($status.cachedCount -lt $status.itemCount -and [DateTime]::UtcNow -lt $deadline)
        $testResult = Join-Path $validation "$targetName-shell-behavior-$size.json"
        Invoke-Patch @('--validate-shell-behavior',$testResult) "$targetName-shell-behavior-$size-command.json" | Out-Null
        $result = Get-Content -Raw -LiteralPath $testResult | ConvertFrom-Json
        if (-not $result.passed) {
            throw 'Shell behavior regression failed'
        }
        $results += $result
        $restored = Invoke-Patch @('--restore') "$targetName-shell-behavior-$size-restored.json"
        if ($restored.active -or $restored.imageWidth -ne $before.imageWidth -or $restored.restorePositionFailures) {
            throw 'Interaction state did not restore'
        }
    }
    [pscustomobject]@{
        passed=$true
        process=$targetName
        pid=$before.pid
        results=$results
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $validation "$targetName-shell-behavior-summary.json") -Encoding utf8
    Get-Content -LiteralPath (Join-Path $validation "$targetName-shell-behavior-summary.json")
} finally {
    Invoke-Patch @('--restore') "$targetName-shell-behavior-final-state.json" | Out-Null
    if ($hostProcess) {
        $ownHost = Get-Process -Id $hostProcess.Id -ErrorAction SilentlyContinue
        if ($ownHost -and $ownHost.Path -eq (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe')) {
            Stop-Process -Id $ownHost.Id
        }
    }
}
