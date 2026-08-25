# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sourceRoot = Join-Path $repositoryRoot "tools/deterministic-zip"

function Find-BigScreenCMake {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    if ($env:OS -eq "Windows_NT") {
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
            $vsInstall = & $vswhere -latest -products * -property installationPath
            if ($vsInstall) {
                $candidate = Join-Path $vsInstall `
                    "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    return $candidate
                }
            }
        }
    }
    return $null
}

function Get-BigScreenToolSourceHash {
    $sources = @(
        "CMakeLists.txt",
        "main.cpp",
        "miniz_export.h",
        "vendor/miniz-3.1.2/LICENSE",
        "vendor/miniz-3.1.2/miniz.c",
        "vendor/miniz-3.1.2/miniz.h",
        "vendor/miniz-3.1.2/miniz_common.h",
        "vendor/miniz-3.1.2/miniz_tdef.c",
        "vendor/miniz-3.1.2/miniz_tdef.h",
        "vendor/miniz-3.1.2/miniz_tinfl.c",
        "vendor/miniz-3.1.2/miniz_tinfl.h",
        "vendor/miniz-3.1.2/miniz_zip.h")
    $records = foreach ($relative in $sources) {
        $path = Join-Path $sourceRoot $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Tracked deterministic ZIP source is missing: $path"
        }
        "$relative=$((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash)"
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($records -join "`n"))
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString(
            $sha256.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $sha256.Dispose()
    }
}

$hostId = if ($env:OS -eq "Windows_NT") {
    "windows-x64"
} else {
    $architecture = (& uname -m).Trim()
    "linux-$architecture"
}
$hostRoot = Join-Path $repositoryRoot ".cache/build-tools/deterministic-zip/$hostId"
$buildRoot = Join-Path $hostRoot "build"
$stampPath = Join-Path $hostRoot "source.sha256"
$executableName = if ($env:OS -eq "Windows_NT") {
    "bigscreen-deterministic-zip.exe"
} else {
    "bigscreen-deterministic-zip"
}
$executablePath = Join-Path $buildRoot "bin/$executableName"
$sourceHash = Get-BigScreenToolSourceHash
$cachedHash = if (Test-Path -LiteralPath $stampPath -PathType Leaf) {
    (Get-Content -LiteralPath $stampPath -Raw).Trim()
} else {
    ""
}

if ($cachedHash -ne $sourceHash -or
    -not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    $cacheParent = [IO.Path]::GetFullPath((Join-Path $repositoryRoot ".cache/build-tools/deterministic-zip"))
    $resolvedHostRoot = [IO.Path]::GetFullPath($hostRoot)
    if ([IO.Path]::GetDirectoryName($resolvedHostRoot) -ne $cacheParent) {
        throw "Refusing to rebuild an unexpected host-tool path: $resolvedHostRoot"
    }
    if (Test-Path -LiteralPath $hostRoot) {
        Remove-Item -LiteralPath $hostRoot -Recurse -Force
    }
    [IO.Directory]::CreateDirectory($hostRoot) | Out-Null

    $cmake = Find-BigScreenCMake
    if (-not $cmake) {
        throw @"
CMake was not found. Big Screen builds its tracked deterministic ZIP compressor
with the same C/C++ toolchain already required for source builds; no separate
archive program is required. Install the documented build prerequisites and
try again.
"@
    }

    Write-Host "Building Big Screen's tracked deterministic ZIP compressor."
    Write-Host "This one-time host build normally takes only a few seconds."
    & $cmake -S $sourceRoot -B $buildRoot -DCMAKE_BUILD_TYPE=Release |
        ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        throw "Could not configure the deterministic ZIP compressor."
    }
    & $cmake --build $buildRoot --config Release |
        ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0 -or
        -not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
        throw "Could not build the deterministic ZIP compressor."
    }
    [IO.File]::WriteAllText(
        $stampPath,
        $sourceHash,
        (New-Object Text.UTF8Encoding($false)))
}

Write-Output $executablePath
