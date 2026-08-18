# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
Param(
    [Parameter(Mandatory=$false)]
    [Switch] $Force
)

$ErrorActionPreference = "Stop"

# yt-dlp now requires a JavaScript engine for full YouTube support. Android
# prevents Beat Saber from executing a qjs program copied into writable app or
# shared storage, so Big Screen compiles QuickJS-NG directly into the mod and
# calls it in-process. Keep this source input pinned and hash-verified just like
# the CPython and yt-dlp runtime inputs.
$quickJsVersion = "0.16.1"
$archiveSha256 = "153f1940c5f61a59ab62703a6d13cf71ba0b2d2ba597683fe5315f14a64ed782"
$archiveUrl = "https://github.com/quickjs-ng/quickjs/releases/download/v$quickJsVersion/quickjs-amalgam.zip"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
& (Join-Path $PSScriptRoot "initialize-dependency-cache.ps1")
if (-not $?) {
    throw "Could not initialize Big Screen's portable dependency cache."
}
$dependencyRoot = Join-Path $repositoryRoot ".cache/dependencies/quickjs-ng"
$archive = Join-Path $dependencyRoot "quickjs-amalgam-$quickJsVersion.zip"
$sourceRoot = Join-Path $dependencyRoot "source"
$readyFile = Join-Path $dependencyRoot "quickjs-$quickJsVersion.ready"

New-Item -ItemType Directory -Force -Path $dependencyRoot | Out-Null

function Assert-Sha256([string] $Path, [string] $Expected) {
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) {
        throw "SHA-256 mismatch for $Path. Expected $Expected, received $actual."
    }
}

if ($Force -or -not (Test-Path -LiteralPath $archive)) {
    Write-Output "Downloading QuickJS-NG $quickJsVersion source from the official GitHub release."
    Write-Output "Source: $archiveUrl"
    Write-Output "The archive will be verified against its pinned SHA-256 before extraction."
    Invoke-WebRequest -Uri $archiveUrl -OutFile $archive
} else {
    Write-Output "Using cached QuickJS-NG $quickJsVersion archive."
}
Assert-Sha256 $archive $archiveSha256

$sourceFile = Join-Path $sourceRoot "quickjs-amalgam.c"
$headerFile = Join-Path $sourceRoot "quickjs.h"
if ($Force -or
    -not (Test-Path -LiteralPath $sourceFile) -or
    -not (Test-Path -LiteralPath $headerFile)) {
    if (Test-Path -LiteralPath $sourceRoot) {
        # This is a fixed child of the repository-owned ignored dependency
        # directory. Check that relationship before replacing an extraction.
        if ((Split-Path -Parent $sourceRoot) -ne $dependencyRoot) {
            throw "Refusing to replace unexpected QuickJS path $sourceRoot"
        }
        Remove-Item -LiteralPath $sourceRoot -Recurse -Force
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $sourceRoot
}

foreach ($required in @($sourceFile, $headerFile)) {
    if (-not (Test-Path -LiteralPath $required) -or
        (Get-Item -LiteralPath $required).Length -lt 1024) {
        throw "The verified QuickJS-NG archive did not produce $required."
    }
}

@{
    version = $quickJsVersion
    archiveUrl = $archiveUrl
    archiveSha256 = $archiveSha256
} | ConvertTo-Json | Set-Content -LiteralPath $readyFile -Encoding UTF8

Write-Output "Prepared QuickJS-NG $quickJsVersion in $sourceRoot"
