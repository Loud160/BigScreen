# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

<#
.SYNOPSIS
Initializes Big Screen's portable, repository-local dependency cache.

.DESCRIPTION
QPM owns and may replace the repository's extern directory during restore.
Big Screen previously stored FFmpeg, QuickJS, CPython, and downloader caches
under that same directory, so a harmless QPM restore deleted valid downloads
and forced the next build to retrieve or rebuild them again.

The durable cache now lives under .cache/dependencies. That location remains
inside the repository folder for portability, is ignored by Git, and is not
managed by QPM. Existing caches are moved once before QPM can replace extern.
#>

$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cacheRoot = Join-Path $repositoryRoot ".cache/dependencies"
New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null

$migrations = @(
    @{ Legacy = "extern/ffmpeg-lgpl"; Current = "ffmpeg-lgpl" },
    @{ Legacy = "extern/ffmpeg-lgpl-9.0.1"; Current = "ffmpeg-lgpl-9.0.1" },
    @{ Legacy = "extern/quickjs-ng"; Current = "quickjs-ng" },
    @{ Legacy = "extern/downloader"; Current = "downloader" },
    @{ Legacy = "extern/downloader-source"; Current = "downloader-source" }
)

foreach ($migration in $migrations) {
    $legacyPath = Join-Path $repositoryRoot $migration.Legacy
    $currentPath = Join-Path $cacheRoot $migration.Current
    if (-not (Test-Path -LiteralPath $legacyPath -PathType Container) -or
        (Test-Path -LiteralPath $currentPath)) {
        continue
    }

    if ((Split-Path -Parent $currentPath) -ne $cacheRoot) {
        throw "Refusing to migrate a dependency outside $cacheRoot"
    }

    Write-Output "Moving the existing $($migration.Current) cache outside QPM's extern directory."
    Move-Item -LiteralPath $legacyPath -Destination $currentPath
}

Write-Output "Big Screen dependency cache: $cacheRoot"
