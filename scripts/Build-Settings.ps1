# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw 'The .NET 10 SDK is required to build the WinUI 3 settings window.'
}
Push-Location (Join-Path $repo 'src\Settings')
try {
    # Run from this directory so global.json selects the stable .NET 10 SDK.
    & dotnet publish DekaDesktopIcon.Settings.csproj --configuration $Configuration `
        --runtime win-x64 --self-contained true --output $OutputDirectory `
        -p:Platform=x64 -p:WindowsAppSDKSelfContained=true
    if ($LASTEXITCODE -ne 0) {
        throw 'Building the WinUI 3 settings window failed.'
    }
} finally {
    Pop-Location
}
