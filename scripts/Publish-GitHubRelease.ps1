# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string]$Commit,
    [Parameter(Mandatory)][ValidatePattern('^[\w.-]+/[\w.-]+$')][string]$Repository,
    [string]$PackageDirectory = "$PSScriptRoot\..\build\release"
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($env:GH_TOKEN)) {
    throw 'GH_TOKEN is required to publish a release.'
}
$PackageDirectory = [IO.Path]::GetFullPath($PackageDirectory)
$archive = Join-Path $PackageDirectory "DekaDesktopIcon-$Version-windows-x64.zip"
$checksums = Join-Path $PackageDirectory 'SHA256SUMS.txt'
$notes = Join-Path $PackageDirectory 'RELEASE-NOTES.md'
foreach ($path in @($archive, $checksums, $notes)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
        throw "A required release file is missing or empty: $path"
    }
}
$expectedChecksum = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() +
    '  ' + [IO.Path]::GetFileName($archive)
if ((Get-Content -LiteralPath $checksums -Raw).Trim() -cne $expectedChecksum) {
    throw 'The release archive does not match SHA256SUMS.txt.'
}
$api = if ($env:GITHUB_API_URL) { $env:GITHUB_API_URL.TrimEnd('/') } else { 'https://api.github.com' }
$headers = @{
    Accept = 'application/vnd.github+json'
    Authorization = "Bearer $env:GH_TOKEN"
    'X-GitHub-Api-Version' = '2026-03-10'
}
function Get-ReleaseResource([string]$Endpoint) {
    try {
        return Invoke-RestMethod -Uri "$api/repos/$Repository/$Endpoint" -Headers $headers `
            -TimeoutSec 30 -MaximumRedirection 0
    } catch {
        if ($_.Exception.Response -and [int]$_.Exception.Response.StatusCode -eq 404) {
            return $null
        }
        throw
    }
}

$tag = "v$Version"
$reference = Get-ReleaseResource "git/ref/tags/$tag"
if ($reference) {
    $target = $reference.object
    for ($depth = 0; $target.type -eq 'tag' -and $depth -lt 16; $depth++) {
        $annotation = Get-ReleaseResource "git/tags/$($target.sha)"
        if (-not $annotation) {
            throw "Cannot resolve annotated tag $tag."
        }
        $target = $annotation.object
    }
    if ($target.type -cne 'commit' -or $target.sha -cne $Commit) {
        throw "Tag $tag does not point to the build commit. Increment the version before releasing."
    }
}
$release = Get-ReleaseResource "releases/tags/$tag"
if (-not $release) {
    # The by-tag REST endpoint only returns published releases. Find a draft
    # left by an interrupted upload, including one whose tag is still pending.
    for ($page = 1; ; $page++) {
        $releases = @(Get-ReleaseResource "releases?per_page=100&page=$page")
        $release = $releases | Where-Object { $_.tag_name -ceq $tag -and $_.draft } |
            Select-Object -First 1
        if ($release -or $releases.Count -lt 100) { break }
    }
}
if ($release) {
    if (-not $reference) {
        if (-not $release.draft) {
            throw "Release $tag has no matching Git tag."
        }
        if ($release.target_commitish -cne $Commit) {
            throw "Draft $tag does not target the build commit."
        }
    }
    # A failed run can leave a draft or a partially uploaded release behind.
    & gh release upload $tag $archive $checksums --repo $Repository --clobber
    if ($LASTEXITCODE -ne 0) {
        throw "Uploading assets to release $tag failed."
    }
    & gh release edit $tag --repo $Repository --title "DekaDesktopIcon $Version" `
        --notes-file $notes --draft=false
} else {
    & gh release create $tag $archive $checksums --repo $Repository --target $Commit `
        --title "DekaDesktopIcon $Version" --notes-file $notes
}
if ($LASTEXITCODE -ne 0) {
    throw "Publishing release $tag failed."
}
