# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
# Exercise the actual pre-checkout script without credentials or network access.
$workflow = Get-Content -LiteralPath (Join-Path $repo '.github\workflows\release.yml')
$guardFound = $false
$readingScript = $false
$scriptLines = @()
foreach ($line in $workflow) {
    if ($line -eq '      - name: Require repository administrators') {
        $guardFound = $true
    }
    if ($guardFound -and $line -eq '        run: |') {
        $readingScript = $true
        continue
    }
    if ($readingScript) {
        if ($line.StartsWith('          ')) {
            $scriptLines += $line.Substring(10)
        } elseif (-not [string]::IsNullOrWhiteSpace($line)) {
            break
        }
    }
}
if ($scriptLines.Count -eq 0) {
    throw 'The inline release authorization script was not found.'
}
$authorization = [scriptblock]::Create($scriptLines -join "`n")

function Test-AuthorizationCase {
    param(
        [string]$Name,
        [string]$Actor = 'administrator',
        [string]$TriggeringActor = 'administrator',
        [hashtable]$Permissions = @{ administrator = 'admin'; otheradmin = 'admin' },
        [string]$EventName = 'workflow_dispatch',
        [string]$Ref = 'refs/heads/main',
        [switch]$ApiFailure,
        [string]$ExpectedError,
        [int]$ExpectedRequests = 1
    )
    $env:RELEASE_ACTOR = $Actor
    $env:RELEASE_TRIGGERING_ACTOR = $TriggeringActor
    $env:GITHUB_EVENT_NAME = $EventName
    $env:GITHUB_REF = $Ref
    $requests = [Collections.Generic.List[string]]::new()

    function Invoke-RestMethod {
        param($Uri, $Headers, $TimeoutSec, $MaximumRedirection)
        if ($Uri -notmatch '^https://api\.github\.invalid/repos/sh4/DekaDesktopIcon/collaborators/([^/]+)/permission$') {
            throw "Unexpected permission endpoint: $Uri"
        }
        $login = [Uri]::UnescapeDataString($Matches[1])
        $requests.Add($login)
        if ($Headers.Authorization -cne 'Bearer test-only' -or
            $TimeoutSec -ne 30 -or $MaximumRedirection -ne 0) {
            throw 'Unexpected API request settings.'
        }
        if ($ApiFailure) {
            throw 'Simulated GitHub API failure.'
        }
        return [pscustomobject]@{ permission = $Permissions[$login] }
    }

    $failure = $null
    try {
        & $authorization | Out-Null
    } catch {
        $failure = $_.Exception.Message
    }
    if ($ExpectedError) {
        if (-not $failure -or $failure -notmatch $ExpectedError) {
            throw "${Name}: expected '$ExpectedError', got '$failure'."
        }
    } elseif ($failure) {
        throw "${Name}: unexpectedly rejected: $failure"
    }
    if ($requests.Count -ne $ExpectedRequests) {
        throw "${Name}: expected $ExpectedRequests API calls, got $($requests.Count)."
    }
    Write-Output "Passed: $Name"
}

$environment = @{
    GH_TOKEN = 'test-only'
    GITHUB_API_URL = 'https://api.github.invalid'
    GITHUB_REPOSITORY = 'sh4/DekaDesktopIcon'
    DEFAULT_BRANCH = 'main'
    RELEASE_ACTOR = 'administrator'
    RELEASE_TRIGGERING_ACTOR = 'administrator'
    GITHUB_EVENT_NAME = 'workflow_dispatch'
    GITHUB_REF = 'refs/heads/main'
}
$previousEnvironment = @{}
try {
    foreach ($name in $environment.Keys) {
        $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $environment[$name], 'Process')
    }
    Test-AuthorizationCase -Name 'Administrator dispatch'
    Test-AuthorizationCase -Name 'Another administrator re-runs the build' `
        -TriggeringActor otheradmin -ExpectedRequests 2
    foreach ($role in @('write', 'maintain', 'read', 'triage', 'none', '', 'Admin')) {
        $access = @{ administrator = 'admin'; contributor = $role }
        Test-AuthorizationCase -Name "Reject initial actor with permission '$role'" `
            -Actor contributor -Permissions $access -ExpectedError 'admin permission is required'
        Test-AuthorizationCase -Name "Reject re-run actor with permission '$role'" `
            -TriggeringActor contributor -Permissions $access -ExpectedRequests 2 `
            -ExpectedError 'admin permission is required'
    }
    Test-AuthorizationCase -Name 'Reject a revoked initial administrator on re-run' `
        -TriggeringActor otheradmin -Permissions @{ administrator = 'write'; otheradmin = 'admin' } `
        -ExpectedError 'admin permission is required'
    Test-AuthorizationCase -Name 'Reject a missing initial actor' -Actor '' `
        -ExpectedError 'actor could not be determined' -ExpectedRequests 0
    Test-AuthorizationCase -Name 'Reject a missing re-run actor' -TriggeringActor '' `
        -ExpectedError 'actor could not be determined' -ExpectedRequests 0
    Test-AuthorizationCase -Name 'Reject an unknown API response' -Permissions @{} `
        -ExpectedError 'admin permission is required'
    Test-AuthorizationCase -Name 'Stop on API failure' -ApiFailure `
        -ExpectedError 'Simulated GitHub API failure'
    Test-AuthorizationCase -Name 'Reject a pull request event' -EventName pull_request `
        -ExpectedError 'manual dispatch on the default branch' -ExpectedRequests 0
    Test-AuthorizationCase -Name 'Reject another branch' -Ref refs/heads/untrusted `
        -ExpectedError 'manual dispatch on the default branch' -ExpectedRequests 0
    Test-AuthorizationCase -Name 'Reject a release tag' -Ref refs/tags/v1.4.1 `
        -ExpectedError 'manual dispatch on the default branch' -ExpectedRequests 0
} finally {
    foreach ($name in $previousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process')
    }
}
