# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4

function Get-MsvcToolset {
    $toolset = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*' -Directory |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1

    if (-not $toolset) {
        throw 'Visual C++ x64 tools were not found.'
    }
    return $toolset.FullName
}

function Initialize-MsvcEnvironment {
    $toolset = Get-MsvcToolset
    $sdk = 'C:\Program Files (x86)\Windows Kits\10'
    $sdkVersion = Get-ChildItem "$sdk\Include" -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName 'um\Windows.h') } |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1 -ExpandProperty Name

    if (-not $sdkVersion) {
        throw 'A Windows SDK with desktop headers was not found.'
    }

    $env:PATH = "$toolset\bin\Hostx64\x64;$sdk\bin\$sdkVersion\x64;$env:PATH"
    $env:INCLUDE = "$toolset\include;$sdk\Include\$sdkVersion\ucrt;$sdk\Include\$sdkVersion\shared;$sdk\Include\$sdkVersion\um;$sdk\Include\$sdkVersion\winrt"
    $env:LIB = "$toolset\lib\x64;$sdk\Lib\$sdkVersion\ucrt\x64;$sdk\Lib\$sdkVersion\um\x64"
    return "$toolset\bin\Hostx64\x64\cl.exe"
}

function Find-ClangTool([string]$Name) {
    $command = Get-Command "$Name.exe" -ErrorAction SilentlyContinue
    $candidates = @()
    if ($command) {
        $candidates += Get-Item -LiteralPath $command.Source
    }
    $candidates += Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\$Name.exe" -ErrorAction SilentlyContinue
    $versionedTools = foreach ($candidate in $candidates) {
        $versionText = $candidate.VersionInfo.ProductVersion
        if ($versionText -notmatch '^(\d+\.\d+(?:\.\d+)?)') {
            $versionText = & $candidate.FullName --version | Select-Object -First 1
            if ($versionText -notmatch 'version (\d+\.\d+(?:\.\d+)?)') {
                continue
            }
        }
        [pscustomobject]@{ Path = $candidate.FullName; Version = [version]$Matches[1] }
    }
    $tool = $versionedTools |
        Sort-Object Version -Descending |
        Select-Object -First 1
    if (-not $tool) {
        throw "$Name was not found. Install the LLVM tools or add them to PATH."
    }
    if ($tool.Version -lt [version]'22.0') {
        throw "$Name 22 or newer is required by the source-style configuration."
    }
    return $tool.Path
}
