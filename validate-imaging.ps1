# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin")

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\TestSupport.ps1"
$testDir = Join-Path $PSScriptRoot "build\validation\imaging-$PID"
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$image = Join-Path $testDir 'landscape.png'
& "$PSScriptRoot\tests\New-TestImage.ps1" -OutputPath $image -Width 1024 -Height 512 | Out-Null
$file = Join-Path $testDir 'file.deka-no-thumbnail'
Set-Content -LiteralPath $file -Value 'Icon fallback fixture' -Encoding utf8
$report = Join-Path $testDir 'summary.json'
$arguments = @('--check-imaging', $image, $file, $report) |
    ForEach-Object { ConvertTo-NativeArgument $_ }
$process = Start-Process -FilePath (Join-Path $BinaryDirectory 'DekaDesktopIconTestHost.exe') `
    -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(20000)) {
    throw 'Imaging checks timed out.'
}
$result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
if ($process.ExitCode -ne 0 -or -not $result.passed) {
    throw "Imaging checks failed: $($result.message)"
}
Get-Content -LiteralPath $report
