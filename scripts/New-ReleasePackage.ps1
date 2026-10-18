# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param(
    [string]$BinaryDirectory = "$PSScriptRoot\..\build\release-bin",
    [string]$OutputDirectory = "$PSScriptRoot\..\build\release"
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$BinaryDirectory = [IO.Path]::GetFullPath($BinaryDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$cmake = Get-Content -LiteralPath (Join-Path $repo 'CMakeLists.txt') -Raw
$versionMatch = [regex]::Match(
    $cmake, 'project\(DekaDesktopIcon VERSION (?<version>\d+\.\d+\.\d+) LANGUAGES CXX\)'
)
if (-not $versionMatch.Success) {
    throw 'The release version could not be read from CMakeLists.txt.'
}
$version = $versionMatch.Groups['version'].Value
$changelog = Get-Content -LiteralPath (Join-Path $repo 'CHANGELOG.md') -Raw -Encoding utf8
$notesMatch = [regex]::Match(
    $changelog, '(?ms)^# ' + [regex]::Escape($version) + ' — [^\r\n]+\r?\n(?<notes>.*?)(?=^# |\z)'
)
if (-not $notesMatch.Success -or [string]::IsNullOrWhiteSpace($notesMatch.Groups['notes'].Value)) {
    throw "Release notes for version $version were not found in CHANGELOG.md."
}
$commit = & git -C $repo rev-parse --verify HEAD
if ($LASTEXITCODE -ne 0 -or $commit -cnotmatch '^[0-9a-f]{40}$') {
    throw 'The source commit could not be determined.'
}
$packageName = "DekaDesktopIcon-$version-windows-x64"
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$staging = Join-Path $OutputDirectory ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staging | Out-Null

try {
    # Only ship the two runtime binaries, never test hosts or intermediate files.
    foreach ($name in @('DekaDesktopIcon.exe', 'DekaDesktopIconCore.dll')) {
        $source = Join-Path $BinaryDirectory $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or
            (Get-Item -LiteralPath $source).Length -eq 0) {
            throw "A required release binary is missing or empty: $name"
        }
        Copy-Item -LiteralPath $source -Destination $staging
    }
    foreach ($name in @('LICENSE', 'README.md', 'CHANGELOG.md')) {
        Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $staging
    }
    @(
        "Version: $version"
        'Platform: Windows x64'
        'Configuration: Release (static MSVC runtime)'
        "Source commit: $commit"
    ) | Set-Content -LiteralPath (Join-Path $staging 'BUILD-INFO.txt') -Encoding utf8

    $archivePath = Join-Path $OutputDirectory "$packageName.zip"
    Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $archivePath `
        -CompressionLevel Optimal -Force
    $hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $packageName.zip" |
        Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256SUMS.txt') -Encoding ascii
    $notesMatch.Groups['notes'].Value.Trim() |
        Set-Content -LiteralPath (Join-Path $OutputDirectory 'RELEASE-NOTES.md') -Encoding utf8

    if ($env:GITHUB_OUTPUT) {
        "package-name=$packageName" | Out-File -LiteralPath $env:GITHUB_OUTPUT -Append -Encoding utf8
        "version=$version" | Out-File -LiteralPath $env:GITHUB_OUTPUT -Append -Encoding utf8
    }
    Write-Output "Release package: $archivePath"
    Write-Output "SHA256: $hash"
} finally {
    # The staging directory only contains the flat, explicit file list above.
    Get-ChildItem -LiteralPath $staging -File | Remove-Item -Force
    Remove-Item -LiteralPath $staging
}
