# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4

function Assert-DekaSettingsPublish {
    param([Parameter(Mandatory)][string]$Directory)

    $configPath = Join-Path $Directory 'DekaDesktopIcon.Settings.runtimeconfig.json'
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
        throw 'The WinUI settings runtime configuration is missing. Rebuild the settings companion.'
    }
    $config = Get-Content -Raw -Encoding utf8 -LiteralPath $configPath | ConvertFrom-Json
    $options = $config.runtimeOptions
    $frameworks = @($options.framework) + @($options.frameworks)
    $framework = @($frameworks | Where-Object { $_.name -eq 'Microsoft.NETCore.App' })
    if ($options.includedFrameworks -or $framework.Count -ne 1 -or $framework[0].version -notmatch '^10\.') {
        throw 'Settings must use the installed .NET 10 runtime. Rebuild with --self-contained false.'
    }
    $runtimeNames = @('coreclr.dll', 'clrjit.dll', 'hostfxr.dll', 'hostpolicy.dll', 'System.Private.CoreLib.dll')
    $bundled = @(Get-ChildItem -LiteralPath $Directory -File -Recurse | Where-Object Name -in $runtimeNames)
    if ($bundled.Count) {
        throw "The settings publish folder contains bundled .NET runtime files: $($bundled.Name -join ', '). Rebuild it."
    }
    foreach ($name in @('DekaDesktopIcon.Settings.exe', 'DekaDesktopIcon.Settings.dll',
                         'DekaDesktopIcon.Settings.deps.json', 'Microsoft.ui.xaml.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Directory $name) -PathType Leaf)) {
            throw "A required settings runtime file is missing: $name"
        }
    }
    return $framework[0].version
}
