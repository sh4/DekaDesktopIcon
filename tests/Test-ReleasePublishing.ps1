# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$testDir = Join-Path $repo "build\validation\release-publishing-$PID"
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
# Typed HTTP errors exercise the production 404 handling without making requests.
if (-not ('DekaReleaseTestException' -as [type])) {
    Add-Type @'
public class DekaReleaseTestException : System.Exception {
    public DekaReleaseTestResponse Response { get; private set; }
    public DekaReleaseTestException(int status) : base("Simulated API error " + status) {
        Response = new DekaReleaseTestResponse { StatusCode = status };
    }
}
public class DekaReleaseTestResponse { public int StatusCode { get; set; } }
'@
}

function Test-PublishingCase {
    param(
        [string]$Name,
        [switch]$ExistingTag,
        [switch]$AnnotatedTag,
        [switch]$ExistingRelease,
        [switch]$DraftRelease,
        [switch]$WrongCommit,
        [switch]$BadChecksum,
        [switch]$MissingArchive,
        [int]$ApiStatus = 0,
        [int]$FailCommand = 0,
        [string]$ExpectedError,
        [string[]]$ExpectedCommands = @('create')
    )
    $commit = 'a' * 40
    $targetCommit = if ($WrongCommit) { 'b' * 40 } else { $commit }
    $archive = Join-Path $testDir 'DekaDesktopIcon-1.4.2-windows-x64.zip'
    Set-Content -LiteralPath $archive -Value 'Test package' -Encoding ascii
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($BadChecksum) { $hash = '0' * 64 }
    "$hash  DekaDesktopIcon-1.4.2-windows-x64.zip" |
        Set-Content -LiteralPath (Join-Path $testDir 'SHA256SUMS.txt') -Encoding ascii
    Set-Content -LiteralPath (Join-Path $testDir 'RELEASE-NOTES.md') -Value 'Release notes' -Encoding utf8
    if ($MissingArchive) { Remove-Item -LiteralPath $archive }
    $calls = [Collections.Generic.List[object]]::new()

    function Invoke-RestMethod {
        param($Uri, $Headers, $TimeoutSec, $MaximumRedirection)
        if ($Headers.Authorization -cne 'Bearer test-only' -or
            $TimeoutSec -ne 30 -or $MaximumRedirection -ne 0) {
            throw 'Unexpected API request settings.'
        }
        if ($ApiStatus) { throw [DekaReleaseTestException]::new($ApiStatus) }
        switch ($Uri) {
            'https://api.github.invalid/repos/sh4/DekaDesktopIcon/git/ref/tags/v1.4.2' {
                if ($ExistingTag) {
                    $type = if ($AnnotatedTag) { 'tag' } else { 'commit' }
                    $sha = if ($AnnotatedTag) { 'c' * 40 } else { $targetCommit }
                    return [pscustomobject]@{ object = [pscustomobject]@{ type = $type; sha = $sha } }
                }
            }
            ('https://api.github.invalid/repos/sh4/DekaDesktopIcon/git/tags/' + ('c' * 40)) {
                return [pscustomobject]@{ object = [pscustomobject]@{ type = 'commit'; sha = $targetCommit } }
            }
            'https://api.github.invalid/repos/sh4/DekaDesktopIcon/releases/tags/v1.4.2' {
                if ($ExistingRelease -and -not $DraftRelease) {
                    return [pscustomobject]@{ tag_name = 'v1.4.2'; draft = $false }
                }
            }
            'https://api.github.invalid/repos/sh4/DekaDesktopIcon/releases?per_page=100&page=1' {
                if ($ExistingRelease -and $DraftRelease) {
                    return [pscustomobject]@{
                        tag_name = 'v1.4.2'; draft = $true; target_commitish = $targetCommit
                    }
                }
                return @()
            }
            default { throw "Unexpected API endpoint: $Uri" }
        }
        throw [DekaReleaseTestException]::new(404)
    }

    function gh {
        $calls.Add(@($args))
        $global:LASTEXITCODE = if ($FailCommand -eq $calls.Count) { 1 } else { 0 }
    }

    $failure = $null
    try {
        & "$repo\scripts\Publish-GitHubRelease.ps1" -Version '1.4.2' -Commit $commit `
            -Repository 'sh4/DekaDesktopIcon' -PackageDirectory $testDir
    } catch {
        $failure = $_.Exception.Message
    }
    if ($ExpectedError) {
        if (-not $failure -or $failure -notmatch $ExpectedError) {
            throw "${Name}: expected '$ExpectedError', got '$failure'."
        }
    } elseif ($failure) {
        throw "${Name}: unexpectedly failed: $failure"
    }
    if ($calls.Count -ne $ExpectedCommands.Count) {
        throw "${Name}: expected $($ExpectedCommands.Count) CLI calls, got $($calls.Count)."
    }
    for ($index = 0; $index -lt $calls.Count; $index++) {
        $command = $calls[$index]
        if ($command[0] -cne 'release' -or $command[1] -cne $ExpectedCommands[$index] -or
            $command[2] -cne 'v1.4.2' -or $command[$command.IndexOf('--repo') + 1] -cne 'sh4/DekaDesktopIcon') {
            throw "${Name}: unexpected release command."
        }
        if ($command[1] -in @('create', 'upload') -and
            ($command[3] -cne $archive -or $command[4] -cne (Join-Path $testDir 'SHA256SUMS.txt'))) {
            throw "${Name}: release assets were not selected explicitly."
        }
        if ($command[1] -eq 'create' -and $command[$command.IndexOf('--target') + 1] -cne $commit) {
            throw "${Name}: release tag is not pinned to the build commit."
        }
        if ($command[1] -eq 'upload' -and '--clobber' -notin $command) {
            throw "${Name}: a repeated upload cannot replace an incomplete asset."
        }
        if ($command[1] -eq 'edit' -and '--draft=false' -notin $command) {
            throw "${Name}: an incomplete draft was not published."
        }
        if ($command[1] -in @('create', 'edit') -and
            $command[$command.IndexOf('--notes-file') + 1] -cne (Join-Path $testDir 'RELEASE-NOTES.md')) {
            throw "${Name}: version-specific release notes are missing."
        }
    }
    Write-Output "Passed: $Name"
}

$previousToken = $env:GH_TOKEN
$previousApi = $env:GITHUB_API_URL
$previousExitCode = $global:LASTEXITCODE
try {
    $env:GH_TOKEN = 'test-only'
    $env:GITHUB_API_URL = 'https://api.github.invalid'
    Test-PublishingCase -Name 'Create release and tag at the build commit'
    Test-PublishingCase -Name 'Create release for an existing lightweight tag' -ExistingTag
    Test-PublishingCase -Name 'Resolve an existing annotated tag' -ExistingTag -AnnotatedTag
    Test-PublishingCase -Name 'Update an existing release' -ExistingTag -ExistingRelease `
        -ExpectedCommands @('upload', 'edit')
    Test-PublishingCase -Name 'Recover a draft with an existing tag' -ExistingTag -ExistingRelease -DraftRelease `
        -ExpectedCommands @('upload', 'edit')
    Test-PublishingCase -Name 'Recover a draft whose tag is still pending' -ExistingRelease -DraftRelease `
        -ExpectedCommands @('upload', 'edit')
    Test-PublishingCase -Name 'Reject a pending draft for another commit' -ExistingRelease -DraftRelease -WrongCommit `
        -ExpectedError 'does not target the build commit' -ExpectedCommands @()
    Test-PublishingCase -Name 'Reject a tag for another commit' -ExistingTag -WrongCommit `
        -ExpectedError 'does not point to the build commit' -ExpectedCommands @()
    Test-PublishingCase -Name 'Reject an annotated tag for another commit' -ExistingTag -AnnotatedTag -WrongCommit `
        -ExpectedError 'does not point to the build commit' -ExpectedCommands @()
    Test-PublishingCase -Name 'Reject an existing release with a missing tag' -ExistingRelease `
        -ExpectedError 'has no matching Git tag' -ExpectedCommands @()
    foreach ($status in @(403, 500)) {
        Test-PublishingCase -Name "Stop on API error $status" -ApiStatus $status `
            -ExpectedError "Simulated API error $status" -ExpectedCommands @()
    }
    Test-PublishingCase -Name 'Reject a mismatched checksum' -BadChecksum `
        -ExpectedError 'does not match SHA256SUMS.txt' -ExpectedCommands @()
    Test-PublishingCase -Name 'Reject a missing package' -MissingArchive `
        -ExpectedError 'missing or empty' -ExpectedCommands @()
    Test-PublishingCase -Name 'Stop on release creation failure' -FailCommand 1 `
        -ExpectedError 'Publishing release v1.4.2 failed'
    Test-PublishingCase -Name 'Do not publish after an upload failure' -ExistingTag -ExistingRelease -FailCommand 1 `
        -ExpectedError 'Uploading assets to release v1.4.2 failed' -ExpectedCommands @('upload')
    Test-PublishingCase -Name 'Stop on draft publication failure' -ExistingTag -ExistingRelease -FailCommand 2 `
        -ExpectedError 'Publishing release v1.4.2 failed' -ExpectedCommands @('upload', 'edit')
} finally {
    $env:GH_TOKEN = $previousToken
    $env:GITHUB_API_URL = $previousApi
    $global:LASTEXITCODE = $previousExitCode
}
