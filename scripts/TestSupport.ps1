# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4

$script:DekaRepositoryRoot = Split-Path $PSScriptRoot -Parent

function ConvertTo-NativeArgument([string]$Value) {
    # CommandLineToArgvW quoting, including backslashes before a closing quote.
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Invoke-DekaCommand {
    param(
        [Parameter(Mandatory)][string]$BinaryDirectory,
        [Parameter(Mandatory)][string[]]$Options,
        [Parameter(Mandatory)][string]$Profile,
        [Parameter(Mandatory)][string]$Report,
        [switch]$TestHost,
        [int]$ExpectedExit = 0
    )

    $controller = Join-Path $BinaryDirectory 'DekaDesktopIcon.exe'
    $arguments = @($Options) + @('--profile', $Profile, '--report', $Report)
    if ($TestHost) {
        $arguments += '--test-host'
    }
    $quoted = $arguments | ForEach-Object { ConvertTo-NativeArgument $_ }
    $process = Start-Process -FilePath $controller -ArgumentList $quoted -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(20000)) {
        throw "DekaDesktopIcon timed out: $($Options -join ' ')"
    }
    if (-not (Test-Path -LiteralPath $Report)) {
        throw "Missing command report: $Report"
    }

    $result = Get-Content -Raw -LiteralPath $Report | ConvertFrom-Json
    if ($process.ExitCode -ne $ExpectedExit) {
        throw "$(Split-Path $Report -Leaf): exit $($process.ExitCode) / $($result.message)"
    }
    return $result
}

function Get-DekaTestImage {
    $repo = $script:DekaRepositoryRoot
    $image = Join-Path $repo "build\test-assets\DekaDesktopIcon-1024-$PID.png"
    if (-not (Test-Path -LiteralPath $image)) {
        & "$repo\tests\New-TestImage.ps1" -OutputPath $image | Out-Null
    }
    return $image
}
