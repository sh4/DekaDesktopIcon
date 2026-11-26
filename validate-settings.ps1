# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([string]$BinaryDirectory = "$PSScriptRoot\build\bin")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\TestSupport.ps1"
$BinaryDirectory = [IO.Path]::GetFullPath($BinaryDirectory)
$settings = Join-Path $BinaryDirectory 'Settings\DekaDesktopIcon.Settings.exe'
if (-not (Test-Path -LiteralPath $settings -PathType Leaf)) {
    throw 'Build the WinUI settings companion before running this test.'
}
$validation = Join-Path $PSScriptRoot "build\validation\settings-$PID"
$fakeBin = Join-Path $validation 'fake'
Push-Location "$PSScriptRoot\src\Settings"
try {
    & dotnet publish Tools/FakeNative/FakeNative.csproj -c Release -r win-x64 --self-contained true `
        -p:Platform=x64 "-p:DekaSettingsBuildRoot=$validation\intermediate\" -o $fakeBin
    if ($LASTEXITCODE -ne 0) { throw 'Building the test-only controller failed.' }
} finally {
    Pop-Location
}
$variables = @('DEKA_FAKE_ACTIVE','DEKA_SMOKE_THEME','DEKA_SMOKE_WIDTH','DEKA_SMOKE_HEIGHT')
$previous = @{}
foreach ($name in $variables) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $cases = @(
        @{ Name='compact-light-inactive'; Active='0'; Theme='Light'; Width='600'; Height='700'; Exit=0 },
        @{ Name='wide-dark-active'; Active='1'; Theme='Dark'; Width='1100'; Height='1000'; Exit=0 },
        @{ Name='missing-controller'; Active='0'; Theme='Light'; Width='600'; Height='700'; Exit=1 }
    )
    $checks = @()
    foreach ($case in $cases) {
        $env:DEKA_FAKE_ACTIVE = $case.Active
        $env:DEKA_SMOKE_THEME = $case.Theme
        $env:DEKA_SMOKE_WIDTH = $case.Width
        $env:DEKA_SMOKE_HEIGHT = $case.Height
        $report = Join-Path $validation "$($case.Name).json"
        $native = if ($case.Exit -eq 0) { Join-Path $fakeBin 'DekaDesktopIcon.exe' } else { Join-Path $validation 'missing.exe' }
        $arguments = @('--native', $native, '--smoke-test', $report) | ForEach-Object { ConvertTo-NativeArgument $_ }
        $process = Start-Process -FilePath $settings -ArgumentList $arguments -PassThru
        if (-not $process.WaitForExit(30000)) {
            Stop-Process -Id $process.Id
            throw "Settings smoke test timed out: $($case.Name)"
        }
        $state = Get-Content -Raw -Encoding utf8 -LiteralPath $report | ConvertFrom-Json
        if ($process.ExitCode -ne $case.Exit -or $state.desktopChanged -or
            $state.theme -ne $case.Theme -or $state.backdrop -ne 'Microsoft.UI.Xaml.Media.MicaBackdrop') {
            throw "Settings smoke test failed: $($case.Name)"
        }
        if ($case.Exit -eq 0) {
            if (-not $state.ok -or $state.loadErrors.Count -ne 0 -or $state.itemCount -ne 3 -or
                $state.sizeOptions.Count -ne 5 -or $state.selectedSize -ne 'auto' -or
                -not $state.applyEnabled -or $state.disableEnabled -ne ($case.Active -eq '1')) {
                throw "Settings controls did not match the native state: $($case.Name)"
            }
        } elseif ($state.ok -or -not $state.infoBarOpen -or $state.infoBarSeverity -ne 'Error') {
            throw 'The settings window did not show its controller error inline.'
        }
        foreach ($suffix in @('.png','.bottom.png')) {
            $capture = [IO.Path]::ChangeExtension($report, $suffix)
            if (-not (Test-Path -LiteralPath $capture) -or (Get-Item -LiteralPath $capture).Length -lt 1000) {
                throw "The rendered settings capture is missing: $capture"
            }
        }
        $checks += [pscustomobject]@{ name=$case.Name; passed=$true; compact=$state.compact; theme=$state.theme }
    }
    [pscustomobject]@{ passed=$true; desktopChanged=$false; checks=$checks } | ConvertTo-Json -Depth 4
} finally {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
