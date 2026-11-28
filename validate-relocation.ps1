# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin", [switch]$Explorer,
      [string]$LegacyBinaryDirectory)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\TestSupport.ps1"
$BinaryDirectory = [IO.Path]::GetFullPath($BinaryDirectory)
$root = Join-Path $PSScriptRoot ('build\validation\relocation-' + [guid]::NewGuid().ToString('N'))
$original = Join-Path $root 'original'
$renamed = Join-Path $root '名前を変更したアプリ'
$running = Join-Path $root '起動中に移動したアプリ'
$foreign = Join-Path $root 'another-copy'
$profile = Join-Path $root 'profile.ini'
if (-not ('DekaRelocationTest' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DekaRelocationTest {
    public delegate bool Visit(IntPtr window, IntPtr context);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindow(string kind, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string kind, string title);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] static extern bool EnumWindows(Visit visit, IntPtr context);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern uint RegisterWindowMessage(string name);
    public static uint Resident(bool test) {
        var window = FindWindow(test ? "DekaDesktopIcon.Resident.Test.v1" : "DekaDesktopIcon.Resident.v1", null);
        uint pid = 0;
        if (window != IntPtr.Zero) GetWindowThreadProcessId(window, out pid);
        return pid;
    }
    public static bool HasTestHost() { return FindWindow("DekaDesktopIcon.TestHost", null) != IntPtr.Zero; }
    public static void EmergencyRestore(bool test, uint expectedPid) {
        var command = RegisterWindowMessage("DekaDesktopIcon.Command.v5.20261128");
        EnumWindows((top, context) => {
            var parent = test ? (top == FindWindow("DekaDesktopIcon.TestHost", null) ? top : IntPtr.Zero)
                              : FindWindowEx(top, IntPtr.Zero, "SHELLDLL_DefView", null);
            if (parent == IntPtr.Zero) return true;
            var list = FindWindowEx(parent, IntPtr.Zero, "SysListView32", null);
            uint pid;
            GetWindowThreadProcessId(list, out pid);
            if (list != IntPtr.Zero && pid == expectedPid) {
                PostMessage(list, command, new IntPtr(2), IntPtr.Zero);
                return false;
            }
            return true;
        }, IntPtr.Zero);
    }
}
'@
}
if ([DekaRelocationTest]::Resident(-not $Explorer) -or (-not $Explorer -and [DekaRelocationTest]::HasTestHost())) {
    throw 'Close the matching resident/test host before running relocation validation.'
}
New-Item -ItemType Directory -Path $root -Force | Out-Null
$stableHost = Join-Path $root 'DekaDesktopIconTestHost.exe'
if (-not $Explorer) {
    Copy-Item -LiteralPath (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe') -Destination $stableHost
}
function New-AppCopy([string]$Directory) {
    New-Item -ItemType Directory -Path $Directory | Out-Null
    foreach ($name in @('DekaDesktopIcon.exe','DekaDesktopIconCore.dll')) {
        Copy-Item -LiteralPath (Join-Path $BinaryDirectory $name) -Destination $Directory
    }
    if (-not $Explorer) {
        # The host stays at a stable path while the controller/DLL directory moves.
        # Hard links let target validation prove it is the bundled executable file.
        New-Item -ItemType HardLink -Path (Join-Path $Directory 'DekaDesktopIconTestHost.exe') -Target $stableHost | Out-Null
    }
}
function Move-App([string]$From,[string]$To) {
    foreach ($path in @($From,$To)) {
        if ((Split-Path ([IO.Path]::GetFullPath($path)) -Parent) -ne $root) {
            throw 'A relocation path escaped the validation directory.'
        }
    }
    Move-Item -LiteralPath $From -Destination $To
}
function Invoke-App([string]$Directory,[string[]]$Options,[string]$Name,[int]$ExpectedExit=0) {
    Invoke-DekaCommand -BinaryDirectory $Directory -Options $Options -Profile $profile `
        -Report (Join-Path $root "$Name.json") -TestHost:(-not $Explorer) -ExpectedExit $ExpectedExit
}
function Get-Identity($Status) {
    return "$($Status.patchFileVolume):$($Status.patchFileIndexHigh):$($Status.patchFileIndexLow)"
}
function Wait-ResidentExit($Process) {
    if (-not $Process.WaitForExit(10000)) { throw 'The resident did not exit.' }
    if ([DekaRelocationTest]::Resident(-not $Explorer)) { throw 'A resident window survived exit.' }
}
New-AppCopy $original
New-AppCopy $foreign
$current = $original
$hostProcess = $null
$residentProcess = $null
$settingsProcess = $null
$ownPatch = $false
$targetPid = 0
if (-not $Explorer) {
    $hostProcess = Start-Process -FilePath $stableHost -WindowStyle Hidden -PassThru
    $hostProcess.WaitForInputIdle(5000) | Out-Null
}
try {
    $before = Invoke-App $original @('--status') 'before'
    if ($before.active) { throw 'Restore the existing desktop patch before validation.' }
    $targetPid = $before.pid
    $legacyVerified = $false
    if ($LegacyBinaryDirectory) {
        if ($Explorer) { throw 'Legacy compatibility validation uses only the independent host.' }
        foreach ($name in @('DekaDesktopIcon.exe','DekaDesktopIconCore.dll')) {
            Copy-Item -LiteralPath (Join-Path $LegacyBinaryDirectory $name) -Destination $root
        }
        try {
            Invoke-App $root @('--apply','--size','256','--lease','30') 'legacy-active' | Out-Null
            $rejected = Invoke-App $original @('--status') 'legacy-blocked' 2
            if ($rejected.message -notmatch 'previous tool is active') { throw 'An active legacy patch was not rejected.' }
            $legacyStatus = Invoke-App $root @('--status') 'legacy-preserved'
            if (-not $legacyStatus.active -or $legacyStatus.size -ne 256) { throw 'Legacy rejection changed the existing patch.' }
            $legacyVerified = $true
        } finally {
            Invoke-App $root @('--restore') 'legacy-restored' | Out-Null
        }
    }
    $first = Invoke-App $original @('--enable','--size','512','--lease','60') 'initial-enable'
    $ownPatch = $true
    $identity = Get-Identity $first
    if (-not $first.active -or $identity -eq '0:0:0') { throw 'The initial DLL identity is invalid.' }
    $residentProcess = Get-Process -Id ([DekaRelocationTest]::Resident(-not $Explorer))
    $exit = Invoke-App $original @('--exit') 'initial-exit'
    Wait-ResidentExit $residentProcess
    $ownPatch = $false
    if ($exit.active -or $exit.restorePositionFailures) { throw 'The initial desktop was not restored.' }
    Move-App $original $renamed
    $current = $renamed
    $restarted = Invoke-App $current @('--enable','--size','512','--lease','60') 'renamed-enable'
    $ownPatch = $true
    $residentProcess = Get-Process -Id ([DekaRelocationTest]::Resident(-not $Explorer))
    $queried = Invoke-App $current @('--status') 'renamed-status'
    if (-not $queried.active -or (Get-Identity $queried) -ne $identity -or
        $queried.patchModule -ne (Join-Path $current 'DekaDesktopIconCore.dll')) {
        throw 'The renamed DLL was mistaken for another copy or retained its old path.'
    }
    # A copied DLL must remain blocked even if it reuses the original load-time path.
    New-AppCopy $original
    $blocked = Invoke-App $original @('--apply','--size','256','--lease','20') 'reused-old-path' 2
    $other = Invoke-App $foreign @('--apply','--size','256','--lease','20') 'foreign-copy' 2
    if ($blocked.message -notmatch 'Another copy is active' -or $other.message -notmatch 'Another copy is active') {
        throw 'A genuinely different DLL copy was not rejected.'
    }
    $changed = Invoke-App $current @('--enable','--size','768','--lease','60') 'changed-settings'
    $state = Invoke-App $current @('--resident-state') 'changed-state'
    if ($changed.size -ne 768 -or $state.requestedSize -ne 768) { throw 'Settings could not be changed after relocation.' }
    $disabled = Invoke-App $current @('--disable') 'disabled'
    if ($disabled.active -or $disabled.imageWidth -ne $before.imageWidth -or $disabled.restorePositionFailures) {
        throw 'Disabling after relocation did not restore the original desktop.'
    }
    Invoke-App $current @('--enable','--size','512','--lease','60') 'before-live-move' | Out-Null
    Move-App $current $running
    $current = $running
    $live = Invoke-App $current @('--enable','--size','256','--lease','60') 'live-move-settings'
    if ($live.size -ne 256 -or (Get-Identity $live) -ne $identity -or
        $residentProcess.Id -ne [DekaRelocationTest]::Resident(-not $Explorer)) {
        throw 'The running resident could not use its renamed executable/DLL directory.'
    }
    $guiVerified = $false
    if ($Explorer -and (Test-Path -LiteralPath (Join-Path $BinaryDirectory 'Settings\DekaDesktopIcon.Settings.exe'))) {
        Copy-Item -LiteralPath (Join-Path $BinaryDirectory 'Settings') -Destination $current -Recurse
        $open = Start-Process -FilePath (Join-Path $current 'DekaDesktopIcon.exe') `
            -ArgumentList '--settings','--profile',(ConvertTo-NativeArgument $profile) -WindowStyle Hidden -PassThru
        if (-not $open.WaitForExit(10000) -or $open.ExitCode -ne 0) { throw 'Opening settings after relocation failed.' }
        $settingsPath = Join-Path $current 'Settings\DekaDesktopIcon.Settings.exe'
        $deadline = [DateTime]::UtcNow.AddSeconds(8)
        do {
            $child = Get-CimInstance Win32_Process -Filter "ParentProcessId = $($residentProcess.Id)" |
                Where-Object ExecutablePath -eq $settingsPath | Select-Object -First 1
            if ($child) { $settingsProcess = Get-Process -Id $child.ProcessId -ErrorAction SilentlyContinue }
            if ($settingsProcess -and -not $settingsProcess.HasExited) { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if (-not $settingsProcess -or $settingsProcess.HasExited) {
            throw 'The resident did not launch the settings companion from its new directory.'
        }
        $report = Join-Path $root 'settings-smoke.json'
        $smoke = Start-Process -FilePath (Join-Path $current 'Settings\DekaDesktopIcon.Settings.exe') `
            -ArgumentList '--profile',(ConvertTo-NativeArgument $profile),'--smoke-test',(ConvertTo-NativeArgument $report) -PassThru
        if (-not $smoke.WaitForExit(25000) -or $smoke.ExitCode -ne 0) { throw 'Settings could not read the relocated resident.' }
        $ui = Get-Content -Raw -Encoding utf8 -LiteralPath $report | ConvertFrom-Json
        if (-not $ui.ok -or -not $ui.active -or $ui.profile -ne $profile) { throw 'The relocated settings state is incorrect.' }
        $guiVerified = $true
    }
    $final = Invoke-App $current @('--exit') 'final-exit'
    Wait-ResidentExit $residentProcess
    if ($settingsProcess -and -not $settingsProcess.WaitForExit(10000)) {
        throw 'The relocated settings companion survived its resident owner.'
    }
    $ownPatch = $false
    if ($final.active -or $final.imageWidth -ne $before.imageWidth -or $final.restorePositionFailures -or
        $final.restoreMatched -ne $before.itemCount) {
        throw 'Final exit did not restore the original image list and positions.'
    }
    [pscustomobject]@{
        passed=$true; explorer=[bool]$Explorer; targetPid=$targetPid
        restartAfterRename=$true; stableDllIdentity=$true; settingsChange=$true
        foreignCopyBlocked=$true; reusedOldPathBlocked=$true; liveRename=$true
        settingsWindowVerified=$guiVerified; originalPositionsRestored=$final.restoreMatched
        legacyActiveRejected=$legacyVerified
        explorerRestarted=$false
    } | ConvertTo-Json | Set-Content -Encoding utf8 -LiteralPath (Join-Path $root 'summary.json')
    Get-Content -Encoding utf8 -LiteralPath (Join-Path $root 'summary.json')
} finally {
    if ($ownPatch) {
        try {
            Invoke-App $current @('--exit') 'cleanup' | Out-Null
        } catch {
            [DekaRelocationTest]::EmergencyRestore(-not $Explorer, $targetPid)
            if ($residentProcess -and -not $residentProcess.HasExited) { Stop-Process -Id $residentProcess.Id }
        }
    }
    if ($hostProcess -and -not $hostProcess.HasExited) { Stop-Process -Id $hostProcess.Id }
}
