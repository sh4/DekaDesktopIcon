# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot\SettingsPublish.ps1"
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$outputParent = Split-Path $OutputDirectory -Parent
if (-not $outputParent -or $OutputDirectory -eq $repo) {
    throw 'Use a dedicated output directory for the settings companion.'
}
if (Test-Path -LiteralPath $OutputDirectory) {
    $existing = Get-Item -LiteralPath $OutputDirectory
    if (-not $existing.PSIsContainer -or ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'The settings output must be a regular directory.'
    }
    if (@(Get-ChildItem -LiteralPath $OutputDirectory -Force).Count -and
        (-not (Test-Path -LiteralPath (Join-Path $OutputDirectory 'DekaDesktopIcon.Settings.runtimeconfig.json')) -or
         -not (Test-Path -LiteralPath (Join-Path $OutputDirectory 'DekaDesktopIcon.Settings.exe')) -or
         (Test-Path -LiteralPath (Join-Path $OutputDirectory 'DekaDesktopIconCore.dll')))) {
        throw 'The output directory contains files from something other than a settings publish.'
    }
}
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw 'The .NET 10 SDK is required to build the WinUI 3 settings window.'
}
$publishId = [guid]::NewGuid().ToString('N')
$temporary = Join-Path $outputParent ".settings-publish-$publishId"
$previous = Join-Path $outputParent ".settings-previous-$publishId"
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
Push-Location (Join-Path $repo 'src\Settings')
try {
    # Run from this directory so global.json selects the stable .NET 10 SDK.
    & dotnet publish DekaDesktopIcon.Settings.csproj --configuration $Configuration `
        --runtime win-x64 --self-contained false --output $temporary `
        -p:Platform=x64 -p:WindowsAppSDKSelfContained=true
    if ($LASTEXITCODE -ne 0) {
        throw 'Building the WinUI 3 settings window failed.'
    }
    $null = Assert-DekaSettingsPublish -Directory $temporary
    # Replace the previous publish as a whole: dotnet publish does not remove obsolete
    # self-contained runtime files or dependencies that are no longer referenced.
    foreach ($path in @($OutputDirectory, $temporary, $previous)) {
        if ((Split-Path ([IO.Path]::GetFullPath($path)) -Parent) -ne $outputParent) {
            throw 'A settings publish path resolved outside the intended output parent.'
        }
    }
    if (Test-Path -LiteralPath $OutputDirectory) {
        Move-Item -LiteralPath $OutputDirectory -Destination $previous
    }
    try {
        Move-Item -LiteralPath $temporary -Destination $OutputDirectory
    } catch {
        if (Test-Path -LiteralPath $previous) {
            Move-Item -LiteralPath $previous -Destination $OutputDirectory
        }
        throw
    }
    if (Test-Path -LiteralPath $previous) {
        Remove-Item -LiteralPath $previous -Recurse -Force
    }
} finally {
    Pop-Location
    if (Test-Path -LiteralPath $temporary) {
        $resolved = [IO.Path]::GetFullPath($temporary)
        if ((Split-Path $resolved -Parent) -ne $outputParent -or
            (Split-Path $resolved -Leaf) -ne ".settings-publish-$publishId") {
            throw 'Refusing to remove an unexpected temporary publish directory.'
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
