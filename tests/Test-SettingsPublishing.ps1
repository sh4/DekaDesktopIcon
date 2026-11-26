# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\..\build\bin")

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
. "$repo\scripts\SettingsPublish.ps1"
$BinaryDirectory = [IO.Path]::GetFullPath($BinaryDirectory)
$null = Assert-DekaSettingsPublish -Directory (Join-Path $BinaryDirectory 'Settings')
$testRoot = Join-Path $repo ('build\validation\settings-publishing-' + [guid]::NewGuid().ToString('N'))
$fixture = Join-Path $testRoot 'bin'
$settings = Join-Path $fixture 'Settings'
New-Item -ItemType Directory -Path $settings -Force | Out-Null
foreach ($name in @('DekaDesktopIcon.exe', 'DekaDesktopIconCore.dll')) {
    Copy-Item -LiteralPath (Join-Path $BinaryDirectory $name) -Destination $fixture
}
$nativeHash = (Get-FileHash -LiteralPath (Join-Path $fixture 'DekaDesktopIconCore.dll')).Hash
Set-Content -LiteralPath (Join-Path $fixture 'parent-sentinel.txt') -Value 'Keep the native output directory.'
Copy-Item -LiteralPath (Join-Path $BinaryDirectory 'Settings\DekaDesktopIcon.Settings.exe') -Destination $settings
$configPath = Join-Path $settings 'DekaDesktopIcon.Settings.runtimeconfig.json'
'{"runtimeOptions":{"tfm":"net10.0","includedFrameworks":[{"name":"Microsoft.NETCore.App","version":"10.0.0"}]}}' |
    Set-Content -LiteralPath $configPath -Encoding utf8
foreach ($name in @('coreclr.dll', 'onnxruntime.dll')) {
    Set-Content -LiteralPath (Join-Path $settings $name) -Value 'Obsolete publish fixture; never executed.'
}
New-Item -ItemType Directory -Path (Join-Path $settings 'obsolete') | Out-Null
Set-Content -LiteralPath (Join-Path $settings 'obsolete\old-resource.txt') -Value 'Obsolete resource.'

function Test-PackageRejection([string]$Name, [string]$ExpectedError) {
    $destination = Join-Path $testRoot $Name
    $failure = $null
    try {
        & "$repo\scripts\New-ReleasePackage.ps1" -BinaryDirectory $fixture -OutputDirectory $destination | Out-Null
    } catch {
        $failure = $_.Exception.Message
    }
    if (-not $failure -or $failure -notmatch $ExpectedError) {
        throw "${Name}: expected '$ExpectedError', got '$failure'."
    }
    if (Get-ChildItem -LiteralPath $destination -Filter '*.zip' -File) {
        throw "${Name}: an invalid settings deployment was packaged."
    }
}

Test-PackageRejection 'self-contained' 'must use the installed .NET 10 runtime'
'{"runtimeOptions":{"tfm":"net10.0","framework":{"name":"Microsoft.NETCore.App","version":"10.0.0"}}}' |
    Set-Content -LiteralPath $configPath -Encoding utf8
Test-PackageRejection 'stale-runtime' 'contains bundled .NET runtime files'

# Publish over an old output. The entire previous settings output, including stale
# dependencies and nested resources, must disappear without touching the native parent.
& "$repo\scripts\Build-Settings.ps1" -OutputDirectory $settings
$runtime = Assert-DekaSettingsPublish -Directory $settings
if ((Test-Path -LiteralPath (Join-Path $settings 'onnxruntime.dll')) -or
    (Test-Path -LiteralPath (Join-Path $settings 'obsolete'))) {
    throw 'Republishing left obsolete dependencies or resources behind.'
}
if ((Get-FileHash -LiteralPath (Join-Path $fixture 'DekaDesktopIconCore.dll')).Hash -ne $nativeHash -or
    (Get-Content -Raw -LiteralPath (Join-Path $fixture 'parent-sentinel.txt')).Trim() -ne 'Keep the native output directory.') {
    throw 'Republishing changed the native parent directory.'
}
if (Get-ChildItem -LiteralPath $fixture -Directory -Force | Where-Object Name -like '.settings-*') {
    throw 'Republishing left temporary publish directories behind.'
}

$unrelated = Join-Path $testRoot 'unrelated'
New-Item -ItemType Directory -Path $unrelated | Out-Null
Set-Content -LiteralPath (Join-Path $unrelated 'keep.txt') -Value 'Unrelated file.'
$failure = $null
try {
    & "$repo\scripts\Build-Settings.ps1" -OutputDirectory $unrelated
} catch {
    $failure = $_.Exception.Message
}
if (-not $failure -or $failure -notmatch 'contains files from something other than a settings publish' -or
    (Get-Content -Raw -LiteralPath (Join-Path $unrelated 'keep.txt')).Trim() -ne 'Unrelated file.') {
    throw 'The settings publisher did not preserve an unrelated output directory.'
}

[pscustomobject]@{
    passed = $true
    requiredRuntime = $runtime
    selfContainedPackageRejected = $true
    staleRuntimePackageRejected = $true
    republishRemovedStaleFiles = $true
    nativeParentPreserved = $true
    unrelatedDirectoryPreserved = $true
} | ConvertTo-Json
