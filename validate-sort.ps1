# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\TestSupport.ps1"
$BinaryDirectory = [IO.Path]::GetFullPath($BinaryDirectory)
$validation = Join-Path $PSScriptRoot 'build\validation'
New-Item -ItemType Directory -Path $validation -Force | Out-Null
$profile = Join-Path $validation 'sort-test.ini'
$sample = Get-DekaTestImage
"[Images]`r`nCount=1`r`n[Image0]`r`nTarget=C:\Windows`r`nPath=$sample`r`n" | Set-Content -LiteralPath $profile -Encoding unicode
function Invoke-Patch([string[]]$Options, [string]$Name) {
    $report = Join-Path $validation $Name
    return Invoke-DekaCommand -BinaryDirectory $BinaryDirectory -Options $Options `
    -Profile $profile -Report $report -TestHost
}
function Snapshot([string]$Name) {
    $report = Join-Path $validation $Name
    $process = Start-Process -FilePath (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe') -ArgumentList '--snapshot',('"' + $report + '"') -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(10000) -or $process.ExitCode -ne 0) {
        throw 'Native snapshot failed'
    }
    return Get-Content -Raw -LiteralPath $report | ConvertFrom-Json
}
$hostProcess = Start-Process -FilePath (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe') -WindowStyle Hidden -PassThru
$hostProcess.WaitForInputIdle(5000) | Out-Null
$checks = @()
try {
    foreach ($size in @(512,1024)) {
        $before = Invoke-Patch @('--status') "sort-$size-before.json"
        $applied = Invoke-Patch @('--apply','--size',"$size",'--lease','60') "sort-$size-applied.json"
        $rollbackHash = (Get-FileHash -LiteralPath "$profile.rollback.ini").Hash
        $items = Snapshot "sort-$size-items-before.json"
        foreach ($iteration in 1..12) {
            $operation = @('--exercise-refresh','--exercise-layout','--exercise-null-refresh','--exercise-resize')[($iteration - 1) % 4]
            $exercise = Start-Process -FilePath (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe') -ArgumentList $operation -WindowStyle Hidden -PassThru
            if (-not $exercise.WaitForExit(10000) -or $exercise.ExitCode -ne 0) {
                throw "Native refresh/image-list ownership failed: $operation"
            }
            $status = Invoke-Patch @('--status') "sort-$size-step-$iteration.json"
            if (-not $status.active -or -not $status.subclassInstalled -or $status.size -ne $size -or $status.imageWidth -ne $size -or $status.imageHeight -ne $size) {
                throw "Resize reset after $operation"
            }
            if ($status.leaseDeadline -ne $applied.leaseDeadline) {
                throw 'Sorting changed the lease deadline'
            }
            if ((Get-FileHash -LiteralPath "$profile.rollback.ini").Hash -ne $rollbackHash) {
                throw 'Sorting overwrote the original rollback snapshot'
            }
            $next = Snapshot "sort-$size-items-$iteration.json"
            if ($next.items[0].identity -ne $items.items[1].identity -or $next.items[1].identity -ne $items.items[0].identity) {
                throw 'Native item order did not reverse'
            }
            if ($next.spacingX -ne ($size + 48) -or $next.spacingY -ne ($size + 68)) {
                throw 'Icon spacing reset after sorting'
            }
            if (($next.items[1].x - $next.items[0].x) -lt $size -and ($next.items[1].y - $next.items[0].y) -lt $size) {
                throw 'Icons overlap after sorting'
            }
            $items = $next
            $checks += [pscustomobject]@{
                size=$size
                iteration=$iteration
                operation=$operation
                imageWidth=$status.imageWidth
                leaseUnchanged=$true
                rollbackUnchanged=$true
            }
        }
        $capture = Join-Path $validation "sort-native-$size.png"
        $render = Invoke-Patch @('--capture',$capture) "sort-$size-rendered.json"
        if ($render.lastDrawWidth -ne $size -or $render.lastDrawHeight -ne $size -or $render.customPaintCount -lt 1 -or $render.nativeHitAtCenter -lt 0) {
            throw 'Custom drawing/hit testing failed after sorting'
        }
        $restore = Invoke-Patch @('--restore') "sort-$size-restored.json"
        if ($restore.active -or $restore.imageWidth -ne $before.imageWidth -or $restore.restoreMatched -ne 2 -or $restore.restoreMissing -or $restore.restorePositionFailures) {
            throw 'Latest native list or original positions did not restore'
        }
        $settings = Snapshot "sort-$size-settings-restored.json"
        if (-not $settings.autoArrange -or -not $settings.snapToGrid) {
            throw 'Restoring the patch discarded updated alignment settings'
        }
    }
    [pscustomobject]@{
        passed=$true
        checks=$checks
        positionsRestored=2
        restorePositionFailures=0
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $validation 'sort-summary.json') -Encoding utf8
    Get-Content -LiteralPath (Join-Path $validation 'sort-summary.json')
} finally {
    Invoke-Patch @('--restore') 'sort-final-state.json' | Out-Null
    $ownHost = Get-Process -Id $hostProcess.Id -ErrorAction SilentlyContinue
    if ($ownHost -and $ownHost.Path -eq (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe')) {
        Stop-Process -Id $ownHost.Id
    }
}
