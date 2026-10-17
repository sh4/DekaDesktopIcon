# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param([switch]$Check)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\Toolchain.ps1"
$repo = Split-Path $PSScriptRoot -Parent
$formatter = Find-ClangTool 'clang-format'
$files = Get-ChildItem "$repo\src", "$repo\tests" -File |
    Where-Object Extension -in '.cpp', '.h'

foreach ($file in $files) {
    if ($Check) {
        & $formatter --dry-run --Werror $file.FullName
    } else {
        & $formatter -i $file.FullName
    }
    if ($LASTEXITCODE -ne 0) {
        throw "C++ formatting failed: $($file.Name)"
    }
}
Write-Output 'C++ formatting passed.'
