# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
param()

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\Toolchain.ps1"
& "$PSScriptRoot\Format-Source.ps1" -Check
$null = Initialize-MsvcEnvironment
$tidy = Find-ClangTool 'clang-tidy'
$repo = Split-Path $PSScriptRoot -Parent
$sources = Get-ChildItem "$repo\src", "$repo\tests" -Filter '*.cpp' -File
$arguments = @(
    '--', '-std=c++17', '-fexceptions', '-fcxx-exceptions',
    '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
    "-I$repo\src", "-I$repo\tests"
)

foreach ($source in $sources) {
    & $tidy $source.FullName @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "C++ brace analysis failed: $($source.Name)"
    }
}

foreach ($script in Get-ChildItem $repo -Recurse -Filter '*.ps1' -File | Where-Object FullName -notmatch '\\build[^\\]*\\') {
    $tokens = $null
    $parseErrors = $null
    $null = [Management.Automation.Language.Parser]::ParseFile($script.FullName, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count) {
        throw "PowerShell parsing failed: $($script.FullName): $parseErrors"
    }
}
Write-Output 'Explicit braces and PowerShell syntax passed.'
