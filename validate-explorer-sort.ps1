# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\TestSupport.ps1"
$BinaryDirectory = [IO.Path]::GetFullPath($BinaryDirectory)
$validation = Join-Path $PSScriptRoot 'build\validation'
New-Item -ItemType Directory -Path $validation -Force | Out-Null
$profile = Join-Path $validation 'explorer-sort-test.ini'
"[Images]`r`nCount=0`r`n" | Set-Content -LiteralPath $profile -Encoding unicode
function Invoke-Patch([string[]]$Options, [string]$Name) {
    $report = Join-Path $validation $Name
    return Invoke-DekaCommand -BinaryDirectory $BinaryDirectory -Options $Options `
    -Profile $profile -Report $report
}
function Invoke-SortHelper([string[]]$Options) {
    $process = Start-Process -FilePath (Join-Path $BinaryDirectory 'DekaDesktopIconSortTest.exe') -ArgumentList $Options -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(15000) -or $process.ExitCode -ne 0) {
        throw "Native sort helper failed: $Options"
    }
}
function Read-Ini([string]$Path) {
    $data = @{
    }
    $section = ''
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\[(.+)\]$') {
            $section = $Matches[1]
            $data[$section] = @{
            }
        }
        elseif ($line -match '^([^=]+)=(.*)$') {
            $data[$section][$Matches[1]] = $Matches[2]
        }
    }
    return $data
}
$before = Invoke-Patch @('--status') 'explorer-sort-before.json'
if ($before.active) {
    throw 'An existing patch is active; refusing to replace it for validation'
}
$savedSort = Join-Path $validation 'explorer-sort-original.ini'
Invoke-SortHelper @('--save-sort',('"' + $savedSort + '"'))
$checks = @()
$needsRestore = $false
try {
    foreach ($size in @(512,1024)) {
        $needsRestore = $true
        $applied = Invoke-Patch @('--apply','--size',"$size",'--lease','60') "explorer-sort-$size-applied.json"
        $rollbackHash = (Get-FileHash -LiteralPath "$profile.rollback.ini").Hash
        $menuPath = Join-Path $validation "explorer-sort-$size-menu.ini"
        Invoke-SortHelper @('--list',('"' + $menuPath + '"'))
        $menu = Read-Ini $menuPath
        $sortEntries = @(0..([int]$menu.Menu.Count - 1) | ForEach-Object {
            $menu["Entry$_"]
        } | Where-Object {
            $_.Parent -match '並べ替え|Sort by'
        })
        if ($sortEntries.Count -lt 4) {
            throw 'Four native sort commands were not found'
        }
        $iteration = 0
        foreach ($round in 1..2) {
            foreach ($entry in $sortEntries) {
                $iteration++
                $invocation = Join-Path $validation "explorer-sort-$size-invoke-$iteration.ini"
                Invoke-SortHelper @('--invoke-sort-id',$entry.Id,('"' + $invocation + '"'))
                $status = Invoke-Patch @('--status') "explorer-sort-$size-step-$iteration.json"
                if (-not $status.active -or -not $status.subclassInstalled -or $status.imageWidth -ne $size -or $status.imageHeight -ne $size) {
                    throw "Native context-menu sort reset the size: $($entry.Title)"
                }
                if ($status.leaseDeadline -ne $applied.leaseDeadline -or (Get-FileHash -LiteralPath "$profile.rollback.ini").Hash -ne $rollbackHash) {
                    throw 'Sorting changed lease or rollback snapshot'
                }
                $checks += [pscustomobject]@{
                    size=$size
                    command=$entry.Title
                    round=$round
                    active=$status.active
                    imageWidth=$status.imageWidth
                    leaseUnchanged=$true
                    rollbackUnchanged=$true
                }
            }
        }
        $capture = Join-Path $validation "explorer-sort-native-$size.png"
        $render = Invoke-Patch @('--capture',$capture) "explorer-sort-$size-rendered.json"
        if ($render.lastDrawWidth -ne $size -or $render.lastDrawHeight -ne $size -or $render.customPaintCount -lt 1 -or $render.nativeHitAtCenter -lt 0) {
            throw 'Native drawing/hit test failed after sorting'
        }
        Invoke-SortHelper @('--restore-sort',('"' + $savedSort + '"'))
        $restored = Invoke-Patch @('--restore') "explorer-sort-$size-restored.json"
        if ($restored.active -or $restored.imageWidth -ne $before.imageWidth -or $restored.restoreMatched -ne $before.itemCount -or $restored.restoreMissing -or $restored.restorePositionFailures) {
            throw 'Original desktop state was not restored'
        }
        $needsRestore = $false
    }
    [pscustomobject]@{
        passed=$true
        targetPid=$before.pid
        checks=$checks
        restoredWidth=$restored.imageWidth
        positionsRestored=$restored.restoreMatched
        restorePositionFailures=$restored.restorePositionFailures
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $validation 'explorer-sort-summary.json') -Encoding utf8
    Get-Content -LiteralPath (Join-Path $validation 'explorer-sort-summary.json')
} finally {
    if ($needsRestore) {
        try {
            Invoke-SortHelper @('--restore-sort',('"' + $savedSort + '"'))
        }
        finally {
            Invoke-Patch @('--restore') 'explorer-sort-final-state.json' | Out-Null
        }
    } else {
        Invoke-Patch @('--status') 'explorer-sort-final-state.json' | Out-Null
    }
}
