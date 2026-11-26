# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\TestSupport.ps1"
$BinaryDirectory = [IO.Path]::GetFullPath($BinaryDirectory)
$validation = Join-Path $PSScriptRoot "build\validation\resident-$PID"
New-Item -ItemType Directory -Path $validation -Force | Out-Null
$profile = Join-Path $validation 'profile.ini'
$controller = Join-Path $BinaryDirectory 'DekaDesktopIcon.exe'
$testHostPath = Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe'
if (-not ('DekaResidentValidation' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DekaResidentValidation {
    public delegate bool EnumWindow(IntPtr window, IntPtr context);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr FindWindow(string kind, string title);
    public static IntPtr Find(string kind) { return FindWindow(kind, null); }
    public static uint ProcessId(IntPtr window) {
        uint pid;
        GetWindowThreadProcessId(window, out pid);
        return pid;
    }
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindow callback, IntPtr context);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string name);
    public static int VisibleWindows(uint process) {
        int count = 0;
        EnumWindows((window, context) => {
            uint pid;
            GetWindowThreadProcessId(window, out pid);
            if (pid == process && IsWindowVisible(window)) count++;
            return true;
        }, IntPtr.Zero);
        return count;
    }
}
'@
}
if ([DekaResidentValidation]::Find('DekaDesktopIcon.Resident.Test.v1') -ne [IntPtr]::Zero -or
    [DekaResidentValidation]::Find('DekaDesktopIcon.TestHost') -ne [IntPtr]::Zero) {
    throw 'Close the independent test host/resident before running this test.'
}
function Invoke-Resident([string[]]$Options, [string]$Name) {
    Invoke-DekaCommand -BinaryDirectory $BinaryDirectory -Options $Options -Profile $profile `
        -Report (Join-Path $validation "$Name.json") -TestHost
}
function Wait-Active([bool]$Expected, [string]$Name) {
    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    do {
        $status = Invoke-Resident @('--status') $Name
        if ([bool]$status.active -eq $Expected) { return $status }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "The patch did not become active=$Expected."
}
function Start-TestDesktop {
    $process = Start-Process -FilePath $testHostPath -WindowStyle Hidden -PassThru
    $process.WaitForInputIdle(5000) | Out-Null
    return $process
}
$hostProcess = Start-TestDesktop
$residentProcess = $null
try {
    $before = Invoke-Resident @('--status') 'before'
    $arguments = @('--resident', '--test-host', '--profile', (ConvertTo-NativeArgument $profile))
    # Starting through --enable must wait for the initial automatic apply, rather than
    # racing the resident's posted initialization message.
    Invoke-Resident @('--enable','--size','auto','--lease','0') 'enable-at-startup' | Out-Null
    $startedWindow = [DekaResidentValidation]::Find('DekaDesktopIcon.Resident.Test.v1')
    $residentProcess = Get-Process -Id ([DekaResidentValidation]::ProcessId($startedWindow))
    $applied = Wait-Active $true 'startup'
    $state = Invoke-Resident @('--resident-state') 'state'
    if (-not $state.running -or -not $state.automatic -or $state.trialSeconds -ne 0 -or
        $applied.size -ne $state.recommendedSize -or $applied.leaseDeadline -ne 0 -or
        [DekaResidentValidation]::VisibleWindows($residentProcess.Id) -ne 0) {
        throw 'Startup did not apply automatic sizing silently and without a trial.'
    }
    $second = Start-Process -FilePath $controller -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $second.WaitForExit(5000) -or $second.ExitCode -ne 0 -or $residentProcess.HasExited) {
        throw 'A second launch did not preserve the existing resident.'
    }
    $window = [DekaResidentValidation]::Find('DekaDesktopIcon.Resident.Test.v1')
    # Exercise the same command routing used by the notification-area menu.
    [DekaResidentValidation]::PostMessage($window, 0x111, [IntPtr]103, [IntPtr]::Zero) | Out-Null
    $disabled = Wait-Active $false 'menu-disabled'
    if ($disabled.imageWidth -ne $before.imageWidth -or $disabled.restorePositionFailures -or
        $disabled.restoreMatched -ne 2 -or $residentProcess.HasExited) {
        throw 'Disabling from the tray did not restore the original desktop and keep the resident.'
    }
    [DekaResidentValidation]::PostMessage($window, 0x111, [IntPtr]102, [IntPtr]::Zero) | Out-Null
    Wait-Active $true 'menu-enabled' | Out-Null
    $manual = Invoke-Resident @('--enable','--size','512','--lease','0') 'manual'
    $manualState = Invoke-Resident @('--resident-state') 'manual-state'
    if ($manual.size -ne 512 -or $manualState.automatic -or $manualState.requestedSize -ne 512) {
        throw 'Settings could not apply a manual size through the resident.'
    }
    Invoke-Resident @('--enable','--size','auto','--lease','1') 'trial' | Out-Null
    Start-Sleep -Milliseconds 3100
    Wait-Active $false 'trial-expired' | Out-Null
    Start-Sleep -Milliseconds 2100
    if ((Invoke-Resident @('--status') 'trial-still-disabled').active) {
        throw 'The resident restarted an expired trial.'
    }
    Invoke-Resident @('--enable','--size','auto','--lease','0') 'reenabled' | Out-Null
    Stop-Process -Id $hostProcess.Id
    $hostProcess.WaitForExit(5000) | Out-Null
    $hostProcess = Start-TestDesktop
    $restarted = Wait-Active $true 'new-desktop'
    if (-not $restarted.active -or $residentProcess.HasExited) {
        throw 'The resident did not reapply after the desktop process restarted.'
    }
    $taskbarCreated = [DekaResidentValidation]::RegisterWindowMessage('TaskbarCreated')
    [DekaResidentValidation]::PostMessage($window, $taskbarCreated, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $exited = Invoke-Resident @('--exit') 'exit'
    if (-not $residentProcess.WaitForExit(5000) -or $exited.active -or $exited.restorePositionFailures) {
        throw 'Exit did not restore the desktop and stop the resident.'
    }
    [pscustomobject]@{
        passed = $true
        automaticSize = $applied.size
        startupWindowCount = 0
        singletonVerified = $true
        trayEnableDisableVerified = $true
        trialExpirationVerified = $true
        desktopRestartVerified = $true
        exitRestored = $true
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $validation 'summary.json') -Encoding utf8
    Get-Content -LiteralPath (Join-Path $validation 'summary.json')
} finally {
    if ($residentProcess -and -not $residentProcess.HasExited) {
        Invoke-Resident @('--exit') 'cleanup' | Out-Null
        $residentProcess.WaitForExit(5000) | Out-Null
    }
    if ($hostProcess -and -not $hostProcess.HasExited) {
        Stop-Process -Id $hostProcess.Id
    }
}
