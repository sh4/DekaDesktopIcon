# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\TestSupport.ps1"
$BinaryDirectory = [IO.Path]::GetFullPath($BinaryDirectory)
$validation = Join-Path $PSScriptRoot 'build\validation'
New-Item -ItemType Directory -Path $validation -Force | Out-Null
$profile = Join-Path $validation 'display-scaling.ini'
$sample = Get-DekaTestImage
"[Images]`r`nCount=1`r`n[Image0]`r`nTarget=C:\Windows`r`nPath=$sample`r`n" | Set-Content -LiteralPath $profile -Encoding unicode
$testHostPath = Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe'
function Invoke-DisplayPatch([string[]]$Options, [string]$Name) {
    return Invoke-DekaCommand -BinaryDirectory $BinaryDirectory -Options $Options `
        -Profile $profile -Report (Join-Path $validation "display-$Name.json") -TestHost
}
function Set-TestDisplaySize([int]$Width, [int]$Height) {
    $process = Start-Process -FilePath $testHostPath -ArgumentList '--display-size',"$Width","$Height" -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(10000) -or $process.ExitCode -ne 0) {
        throw "Test display resize failed: $Width x $Height"
    }
}
function Get-DisplaySnapshot([string]$Name) {
    $report = Join-Path $validation "display-$Name.json"
    $process = Start-Process -FilePath $testHostPath -ArgumentList '--snapshot',(ConvertTo-NativeArgument $report) -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(10000) -or $process.ExitCode -ne 0) {
        throw 'Display snapshot failed'
    }
    return Get-Content -Raw -LiteralPath $report | ConvertFrom-Json
}
function Wait-DisplaySize([int]$Size, [string]$Name) {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $status = Invoke-DisplayPatch @('--status') $Name
        if ($status.active -and $status.size -eq $Size -and $status.imageWidth -eq $Size -and $status.imageHeight -eq $Size) {
            return $status
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Display size did not become $Size px: $($status | ConvertTo-Json -Compress)"
}
$unit = Start-Process -FilePath $testHostPath -ArgumentList '--check-display-scaling' -WindowStyle Hidden -PassThru
if (-not $unit.WaitForExit(10000) -or $unit.ExitCode -ne 0) {
    throw 'Display scaling calculation regression failed'
}
$hostProcess = Start-Process -FilePath $testHostPath -WindowStyle Hidden -PassThru
$hostProcess.WaitForInputIdle(5000) | Out-Null
$checks = @()
$autoChecks = @()
$eightKVerified = $false
try {
    Set-TestDisplaySize 3840 2160
    $before = Invoke-DisplayPatch @('--status') 'before'
    $originalItems = Get-DisplaySnapshot 'original-items'
    $applied = Invoke-DisplayPatch @('--apply','--size','1024','--lease','120') 'applied'
    $rollbackHash = (Get-FileHash -LiteralPath "$profile.rollback.ini").Hash
    $cases = @(
        @{ Width=1920; Height=1080; Size=512 },
        @{ Width=1280; Height=720; Size=336 },
        @{ Width=2560; Height=1440; Size=688 },
        @{ Width=2048; Height=1536; Size=544 },
        @{ Width=2160; Height=3840; Size=576 },
        @{ Width=0; Height=0; Size=576 },
        @{ Width=3840; Height=2160; Size=1024 },
        @{ Width=3840; Height=2160; Size=1024 },
        @{ Width=640; Height=360; Size=176 },
        @{ Width=1920; Height=1080; Size=512 },
        @{ Width=3840; Height=2160; Size=1024 }
    )
    foreach ($index in 0..($cases.Count - 1)) {
        $case = $cases[$index]
        Set-TestDisplaySize $case.Width $case.Height
        $status = Wait-DisplaySize $case.Size "step-$index"
        if ($status.leaseDeadline -ne $applied.leaseDeadline -or (Get-FileHash -LiteralPath "$profile.rollback.ini").Hash -ne $rollbackHash) {
            throw 'Resolution change renewed the lease or overwrote the rollback snapshot'
        }
        $snapshot = Get-DisplaySnapshot "items-$index"
        if ($snapshot.displayWidth -ne $case.Width -or $snapshot.displayHeight -ne $case.Height) {
            throw 'Test display dimensions changed unexpectedly'
        }
        if ($snapshot.spacingX -ne ($case.Size + 48) -or $snapshot.spacingY -ne ($case.Size + 68)) {
            throw 'Resolution change left stale icon spacing'
        }
        if ($case.Width -gt 0 -and $case.Height -gt 0) {
            $render = Invoke-DisplayPatch @('--capture',(Join-Path $validation "display-$index.png")) "render-$index"
            if ($render.lastDrawWidth -ne $case.Size -or $render.lastDrawHeight -ne $case.Size -or $render.nativeHitAtCenter -lt 0) {
                throw 'Drawing or hit testing uses the old resolution'
            }
        }
        $checks += [pscustomobject]@{ width=$case.Width; height=$case.Height; size=$status.size }
    }
    $restored = Invoke-DisplayPatch @('--restore') 'restored'
    $restoredItems = Get-DisplaySnapshot 'restored-items'
    if ($restored.active -or $restored.imageWidth -ne $before.imageWidth -or $restored.restorePositionFailures -or $restored.restoreMatched -ne 2 -or
        ($restoredItems.items | ConvertTo-Json -Compress) -ne ($originalItems.items | ConvertTo-Json -Compress)) {
        throw 'Resolution changes prevented restoring the original images or positions'
    }
    # A new application establishes its own baseline, including enlargement and the minimum size.
    Set-TestDisplaySize 1920 1080
    Invoke-DisplayPatch @('--apply','--size','512','--lease','20') 'reapplied' | Out-Null
    Set-TestDisplaySize 3840 2160
    Wait-DisplaySize 1024 'enlarged' | Out-Null
    Set-TestDisplaySize 96 54
    Wait-DisplaySize 64 'minimum' | Out-Null
    Set-TestDisplaySize 1920 1080
    Wait-DisplaySize 512 'new-baseline' | Out-Null
    Invoke-DisplayPatch @('--restore') 'reapplied-restored' | Out-Null
    # The original trial deadline must still expire while a scaled size is active.
    Invoke-DisplayPatch @('--apply','--size','512','--lease','2') 'leased' | Out-Null
    Set-TestDisplaySize 960 540
    Wait-DisplaySize 256 'leased-scaled' | Out-Null
    Start-Sleep -Milliseconds 3100
    $expired = Invoke-DisplayPatch @('--status') 'expired'
    if ($expired.active -or $expired.imageWidth -ne $before.imageWidth -or $expired.restorePositionFailures) {
        throw 'Trial rollback failed after resolution change'
    }
    # Automatic display-size enlargement (requestedSize=0 / --size auto): 1080p, 1440p, 4K, portrait, resize, 8K.
    Set-TestDisplaySize 1920 1080
    $autoApplied = Invoke-DisplayPatch @('--apply','--size','auto','--lease','120') 'auto-applied'
    if (-not $autoApplied.active -or $autoApplied.size -ne 896 -or $autoApplied.imageWidth -ne 896 -or $autoApplied.imageHeight -ne 896) {
        throw "Automatic apply at 1080p produced unexpected status: $($autoApplied | ConvertTo-Json -Compress)"
    }
    $autoRollbackHash = (Get-FileHash -LiteralPath "$profile.rollback.ini").Hash
    $autoInitialSnapshot = Get-DisplaySnapshot 'auto-initial-items'
    if ($autoInitialSnapshot.spacingX -ne (896 + 48) -or $autoInitialSnapshot.spacingY -ne (896 + 68)) {
        throw 'Automatic apply at 1080p set unexpected icon spacing'
    }
    $autoInitialRender = Invoke-DisplayPatch @('--capture',(Join-Path $validation 'display-auto-initial.png')) 'auto-initial-render'
    if ($autoInitialRender.lastDrawWidth -ne 896 -or $autoInitialRender.lastDrawHeight -ne 896 -or $autoInitialRender.nativeHitAtCenter -lt 0) {
        throw 'Automatic apply at 1080p failed drawing or hit testing'
    }
    $autoChecks += [pscustomobject]@{ width=1920; height=1080; size=$autoApplied.size; captured=$true }
    $autoCases = @(
        @{ Width=2560; Height=1440; Size=1216; Capture=$true },
        @{ Width=3840; Height=2160; Size=1856; Capture=$true },
        @{ Width=1080; Height=1920; Size=880; Capture=$true },
        @{ Width=0; Height=0; Size=880; Capture=$false },
        @{ Width=5120; Height=1440; Size=1344; Capture=$false },
        @{ Width=1920; Height=1080; Size=896; Capture=$true }
    )
    foreach ($index in 0..($autoCases.Count - 1)) {
        $case = $autoCases[$index]
        Set-TestDisplaySize $case.Width $case.Height
        $status = Wait-DisplaySize $case.Size "auto-step-$index"
        if ($status.leaseDeadline -ne $autoApplied.leaseDeadline -or (Get-FileHash -LiteralPath "$profile.rollback.ini").Hash -ne $autoRollbackHash) {
            throw 'Automatic resolution change renewed the lease or overwrote the rollback snapshot'
        }
        $snapshot = Get-DisplaySnapshot "auto-items-$index"
        if ($snapshot.displayWidth -ne $case.Width -or $snapshot.displayHeight -ne $case.Height) {
            throw 'Test display dimensions changed unexpectedly during automatic mode'
        }
        if ($snapshot.spacingX -ne ($case.Size + 48) -or $snapshot.spacingY -ne ($case.Size + 68)) {
            throw 'Automatic resolution change left stale icon spacing'
        }
        if ($case.Capture) {
            $render = Invoke-DisplayPatch @('--capture',(Join-Path $validation "display-auto-$index.png")) "auto-render-$index"
            if ($render.lastDrawWidth -ne $case.Size -or $render.lastDrawHeight -ne $case.Size -or $render.nativeHitAtCenter -lt 0) {
                throw 'Automatic drawing or hit testing uses the old resolution'
            }
        }
        $autoChecks += [pscustomobject]@{ width=$case.Width; height=$case.Height; size=$status.size; captured=$case.Capture }
    }
    # Verify 8K automatic size if the test host accepts 7680x4320, without capturing a huge bitmap.
    Set-TestDisplaySize 7680 4320
    $eightKProbe = Get-DisplaySnapshot 'auto-8k-probe'
    if ($eightKProbe.displayWidth -eq 7680 -and $eightKProbe.displayHeight -eq 4320) {
        $eightKStatus = Wait-DisplaySize 3776 'auto-8k'
        if ($eightKStatus.leaseDeadline -ne $autoApplied.leaseDeadline -or (Get-FileHash -LiteralPath "$profile.rollback.ini").Hash -ne $autoRollbackHash) {
            throw 'Automatic 8K resize renewed the lease or overwrote the rollback snapshot'
        }
        $eightKSnapshot = Get-DisplaySnapshot 'auto-8k-items'
        if ($eightKSnapshot.spacingX -ne (3776 + 48) -or $eightKSnapshot.spacingY -ne (3776 + 68)) {
            throw 'Automatic 8K resize left stale icon spacing'
        }
        $autoChecks += [pscustomobject]@{ width=7680; height=4320; size=$eightKStatus.size; captured=$false }
        $eightKVerified = $true
    }
    $autoRestored = Invoke-DisplayPatch @('--restore') 'auto-restored'
    $autoRestoredItems = Get-DisplaySnapshot 'auto-restored-items'
    if ($autoRestored.active -or $autoRestored.imageWidth -ne $before.imageWidth -or $autoRestored.restorePositionFailures -or $autoRestored.restoreMatched -ne 2 -or
        ($autoRestoredItems.items | ConvertTo-Json -Compress) -ne ($originalItems.items | ConvertTo-Json -Compress)) {
        throw 'Automatic mode prevented restoring the original images or positions'
    }
    # Direct sentinel --size 0 apply on portrait display, followed by landscape resize and restore.
    Set-TestDisplaySize 1080 1920
    $portraitApplied = Invoke-DisplayPatch @('--apply','--size','0','--lease','20') 'auto-portrait-applied'
    if (-not $portraitApplied.active -or $portraitApplied.size -ne 880 -or $portraitApplied.imageWidth -ne 880 -or $portraitApplied.imageHeight -ne 880) {
        throw "Automatic apply on portrait produced unexpected status: $($portraitApplied | ConvertTo-Json -Compress)"
    }
    Set-TestDisplaySize 1920 1080
    Wait-DisplaySize 896 'auto-portrait-to-landscape' | Out-Null
    $portraitRestored = Invoke-DisplayPatch @('--restore') 'auto-portrait-restored'
    if ($portraitRestored.active -or $portraitRestored.imageWidth -ne $before.imageWidth -or $portraitRestored.restorePositionFailures) {
        throw 'Restore failed after portrait automatic apply'
    }
    [pscustomobject]@{
        passed=$true
        checks=$checks
        automaticChecks=$autoChecks
        automaticEightKVerified=$eightKVerified
        automaticPortraitApplyVerified=$true
        originalPositionsRestored=$true
        newBaselineVerified=$true
        scaledLeaseExpired=$true
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $validation 'display-summary.json') -Encoding utf8
    Get-Content -LiteralPath (Join-Path $validation 'display-summary.json')
} finally {
    Invoke-DisplayPatch @('--restore') 'final-state' | Out-Null
    $ownHost = Get-Process -Id $hostProcess.Id -ErrorAction SilentlyContinue
    if ($ownHost -and $ownHost.Path -eq $testHostPath) {
        Stop-Process -Id $ownHost.Id
    }
}
