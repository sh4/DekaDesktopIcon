# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param(
    [string]$OutputDirectory = "$PSScriptRoot\build\bin",
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\Toolchain.ps1"
$compiler = Initialize-MsvcEnvironment
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$common = @(
    '/nologo', '/std:c++17', '/EHsc', '/W4', '/permissive-', '/utf-8',
    '/DUNICODE', '/D_UNICODE', '/DWINVER=0x0A00', '/D_WIN32_WINNT=0x0A00',
    "/I$PSScriptRoot\src", "/I$PSScriptRoot\tests"
)
if ($Configuration -eq 'Debug') {
    $common += @('/MTd', '/Od', '/Zi')
} else {
    $common += @('/MT', '/O2')
}
$libraries = @(
    'user32.lib', 'gdi32.lib', 'comctl32.lib', 'ole32.lib', 'oleaut32.lib',
    'shell32.lib', 'uuid.lib', 'gdiplus.lib', 'windowscodecs.lib', 'comdlg32.lib'
)
$targets = @(
    @{ Name = 'DekaDesktopIconCore'; Dll = $true; Sources = @('src\DekaDesktopIconCore.cpp') },
    @{ Name = 'DekaDesktopIcon'; Dll = $false; Sources = @('src\DekaDesktopIcon.cpp', 'src\CommandClient.cpp') },
    @{ Name = 'DekaDesktopIconTestHost'; Dll = $false; Sources = @('tests\DekaDesktopIconTestHost.cpp') },
    @{ Name = 'DekaDesktopIconSortTest'; Dll = $false; Sources = @('tests\DekaDesktopIconSortTest.cpp') }
)

foreach ($target in $targets) {
    $objects = @()
    foreach ($source in $target.Sources) {
        $object = Join-Path $OutputDirectory ([IO.Path]::GetFileNameWithoutExtension($source) + '.obj')
        & $compiler @common /c (Join-Path $PSScriptRoot $source) "/Fo$object" "/Fd$OutputDirectory\compiler.pdb"
        if ($LASTEXITCODE -ne 0) {
            throw "Compilation failed: $source"
        }
        $objects += $object
    }

    $extension = if ($target.Dll) { '.dll' } else { '.exe' }
    $output = Join-Path $OutputDirectory ($target.Name + $extension)
    $linkOptions = @('/nologo', '/MANIFEST:EMBED', '/DYNAMICBASE', '/NXCOMPAT', '/INCREMENTAL:NO', "/OUT:$output")
    if ($target.Dll) {
        $linkOptions += '/DLL'
    } else {
        $linkOptions += '/SUBSYSTEM:WINDOWS'
    }
    if ($Configuration -eq 'Debug') {
        $linkOptions += @('/DEBUG', "/PDB:$OutputDirectory\$($target.Name).pdb")
    }
    & link.exe @linkOptions @objects @libraries
    if ($LASTEXITCODE -ne 0) {
        throw "Linking failed: $($target.Name)"
    }
}
Write-Output "Built DekaDesktopIcon: $OutputDirectory"
