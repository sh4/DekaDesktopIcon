# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin", [switch]$Explorer)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\TestSupport.ps1"
$BinaryDirectory = [IO.Path]::GetFullPath($BinaryDirectory)
$root = Join-Path $PSScriptRoot ('build\validation\unload-' + [guid]::NewGuid().ToString('N'))
$app = Join-Path $root 'app'
$profile = Join-Path $root 'profile.ini'
New-Item -ItemType Directory -Path $app -Force | Out-Null
foreach ($name in @('DekaDesktopIcon.exe','DekaDesktopIconCore.dll','DekaDesktopIconTestHost.exe')) {
    Copy-Item -LiteralPath (Join-Path $BinaryDirectory $name) -Destination $app
}
if (-not ('DekaUnloadTest' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DekaUnloadTest {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr FindWindow(string kind, string title);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    public static uint Resident(bool test) {
        uint pid = 0;
        var window = FindWindow(test ? "DekaDesktopIcon.Resident.Test.v1" : "DekaDesktopIcon.Resident.v1", null);
        if (window != IntPtr.Zero) GetWindowThreadProcessId(window, out pid);
        return pid;
    }
    [DllImport("user32.dll")] static extern bool EnumThreadWindows(uint thread, Visit visit, IntPtr context);
    public delegate bool Visit(IntPtr window, IntPtr context);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, System.Text.StringBuilder name, int length);
    public static bool HasMenu(uint thread) {
        bool found = false;
        EnumThreadWindows(thread, (window, context) => {
            var name = new System.Text.StringBuilder(256);
            GetClassName(window, name, name.Capacity);
            if (name.ToString() == "#32768") found = true;
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
}
if ([DekaUnloadTest]::Resident(-not $Explorer) -or
    (-not $Explorer -and [DekaUnloadTest]::FindWindow('DekaDesktopIcon.TestHost', $null) -ne [IntPtr]::Zero)) {
    throw 'Close the matching resident/test host before unload validation.'
}
function Invoke-App([string[]]$Options, [string]$Name) {
    Invoke-DekaCommand -BinaryDirectory $app -Options $Options -Profile $profile `
        -Report (Join-Path $root "$Name.json") -TestHost:(-not $Explorer)
}
$hostProcess = $null
$residentProcess = $null
$targetPid = 0
$dll = Join-Path $app 'DekaDesktopIconCore.dll'
function Assert-Core([bool]$Loaded) {
    $target = Get-Process -Id $targetPid
    $modules = @($target.Modules | Where-Object FileName -eq $dll)
    if ([bool]$modules.Count -ne $Loaded) { throw "Expected desktop DLL loaded=$Loaded, found $($modules.Count)." }
    if (-not $Loaded) {
        # An enumerated module can disappear before its final file handles close.
        # Requiring an exclusive writable open also proves the binary is replaceable.
        $file = [IO.File]::Open($dll, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $file.Dispose()
    }
}
function Assert-Exited($Result) {
    if (-not $residentProcess.WaitForExit(5000) -or $Result.active -or $Result.restorePositionFailures -or
        $Result.imageWidth -ne $before.imageWidth -or $Result.restoreMatched -ne $before.itemCount) {
        throw 'Exit did not restore the desktop and stop the resident.'
    }
    Assert-Core $false
}
function Exit-App([string]$Name) {
    Assert-Exited (Invoke-App @('--exit') $Name)
}
try {
    if (-not $Explorer) {
        $hostProcess = Start-Process -FilePath (Join-Path $app 'DekaDesktopIconTestHost.exe') -WindowStyle Hidden -PassThru
        $hostProcess.WaitForInputIdle(5000) | Out-Null
    }
    $before = Invoke-App @('--status') 'before'
    if ($before.active) { throw 'Restore the existing desktop patch before unload validation.' }
    $targetPid = $before.pid
    Assert-Core $false
    for ($cycle = 0; $cycle -lt 8; $cycle++) {
        Invoke-App @('--enable','--size','512','--lease','0') "enable-$cycle" | Out-Null
        $residentProcess = Get-Process -Id ([DekaUnloadTest]::Resident(-not $Explorer))
        Assert-Core $true
        if ($cycle -eq 0) {
            $disabled = Invoke-App @('--disable') 'disabled'
            if ($disabled.active -or $disabled.restorePositionFailures -or $residentProcess.HasExited) {
                throw 'Disabling did not preserve the resident and restore the desktop.'
            }
            Assert-Core $false
            Copy-Item -LiteralPath (Join-Path $BinaryDirectory 'DekaDesktopIconCore.dll') -Destination $dll -Force
            Invoke-App @('--enable','--size','256','--lease','0') 'reload-after-replacement' | Out-Null
            Assert-Core $true
        }
        if ($cycle -eq 1 -and -not $Explorer) {
            Invoke-App @('--show-context-menu','background') 'open-menu' | Out-Null
            $deadline = [DateTime]::UtcNow.AddSeconds(5)
            while (-not [DekaUnloadTest]::HasMenu($before.thread) -and [DateTime]::UtcNow -lt $deadline) {
                Start-Sleep -Milliseconds 20
            }
            if (-not [DekaUnloadTest]::HasMenu($before.thread)) { throw 'The nested test menu never opened.' }
        }
        Exit-App "exit-$cycle"
        # This overwrite failed with the old permanently retained DLL reference.
        Copy-Item -LiteralPath (Join-Path $BinaryDirectory 'DekaDesktopIconCore.dll') -Destination $dll -Force
    }
    if (-not $Explorer) {
        # A pipe-backed image deliberately stalls WIC's read without adding test
        # delays to production code. Exit must wait while the desktop stays alive.
        $pipeName = 'DekaUnload-' + [guid]::NewGuid().ToString('N')
        $pipe = [IO.Pipes.NamedPipeServerStream]::new($pipeName, [IO.Pipes.PipeDirection]::Out, 1,
            [IO.Pipes.PipeTransmissionMode]::Byte, [IO.Pipes.PipeOptions]::Asynchronous)
        $connection = $pipe.BeginWaitForConnection($null, $null)
        $workerProfile = Join-Path $root 'worker.ini'
        "[Images]`r`nCount=1`r`n[Image0]`r`nTarget=C:\Windows`r`nPath=\\.\pipe\$pipeName`r`n" |
            Set-Content -Encoding Unicode -LiteralPath $workerProfile
        try {
            Invoke-DekaCommand -BinaryDirectory $app -Options @('--enable','--size','256','--lease','0') `
                -Profile $workerProfile -Report (Join-Path $root 'worker-applied.json') -TestHost | Out-Null
            $residentProcess = Get-Process -Id ([DekaUnloadTest]::Resident($true))
            if (-not $connection.AsyncWaitHandle.WaitOne(5000)) { throw 'The image worker never opened its pipe.' }
            $pipe.EndWaitForConnection($connection)
            $report = Join-Path $root 'worker-exit.json'
            $arguments = @('--exit','--test-host','--profile',$workerProfile,'--report',$report) |
                ForEach-Object { ConvertTo-NativeArgument $_ }
            $exitProcess = Start-Process -FilePath (Join-Path $app 'DekaDesktopIcon.exe') `
                -ArgumentList $arguments -WindowStyle Hidden -PassThru
            Start-Sleep -Milliseconds 300
            if ($exitProcess.HasExited -or $residentProcess.HasExited) {
                throw 'Exit completed while the image worker was still reading.'
            }
            Assert-Core $true
        } finally {
            $pipe.Dispose() # Let WIC finish, then cancel the stale generation.
        }
        if (-not $exitProcess.WaitForExit(15000) -or $exitProcess.ExitCode -ne 0) {
            throw 'Exit did not finish after the image worker was released.'
        }
        Assert-Exited (Get-Content -Raw -Encoding utf8 -LiteralPath $report | ConvertFrom-Json)
    }
    Invoke-App @('--enable','--size','256','--lease','1') 'trial' | Out-Null
    $residentProcess = Get-Process -Id ([DekaUnloadTest]::Resident(-not $Explorer))
    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    do {
        Start-Sleep -Milliseconds 100
        $modules = @((Get-Process -Id $targetPid).Modules | Where-Object FileName -eq $dll)
    } while ($modules.Count -and [DateTime]::UtcNow -lt $deadline)
    Assert-Core $false
    Exit-App 'exit-after-expiry'
    [pscustomobject]@{
        passed = $true; explorer = [bool]$Explorer; targetPid = $targetPid
        disableUnloaded = $true; exitCycles = 8; binaryReplaceable = $true
        nestedMenuExit = (-not $Explorer); imageWorkerDrain = (-not $Explorer)
        trialUnloaded = $true; explorerRestarted = $false
    } | ConvertTo-Json | Set-Content -Encoding utf8 -LiteralPath (Join-Path $root 'summary.json')
    Get-Content -Encoding utf8 -LiteralPath (Join-Path $root 'summary.json')
} finally {
    if ($residentProcess -and -not $residentProcess.HasExited) {
        Invoke-App @('--exit') 'cleanup' | Out-Null
        $residentProcess.WaitForExit(5000) | Out-Null
    }
    if ($hostProcess -and -not $hostProcess.HasExited) { Stop-Process -Id $hostProcess.Id }
}
