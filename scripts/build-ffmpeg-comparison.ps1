# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$artifactRoot = Join-Path $repositoryRoot "artifacts/ffmpeg-comparison"
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null

# The comparison is now performed from one build and one installed QMOD. This
# script retains the old entry-point name for developer muscle memory while it
# captures the dual-runtime artifact and both reproducibility records.
& (Join-Path $PSScriptRoot "build.ps1") -Clean
if ($LASTEXITCODE -ne 0) { throw "Big Screen dual FFmpeg build failed." }

# build.ps1 already forwards through the one complete Linux pipeline, which
# builds, validates, and packages both runtimes. Copy that verified package;
# invoking createqmod.ps1 here would repeat the entire canonical build.
$comparisonQmod = Join-Path $repositoryRoot "Big Screen-ffmpeg-comparison.qmod"
Copy-Item -LiteralPath (Join-Path $repositoryRoot "Big Screen.qmod") `
    -Destination $comparisonQmod -Force

Copy-Item -LiteralPath (Join-Path $repositoryRoot "Big Screen-ffmpeg-comparison.qmod") `
    -Destination $artifactRoot -Force
Copy-Item -LiteralPath (Join-Path $repositoryRoot "build/libbigscreen.so") `
    -Destination $artifactRoot -Force
foreach ($record in @(
    @{ Source = ".cache/dependencies/ffmpeg-lgpl/BUILD-INFO.txt"; Name = "FFmpeg-4.4.8-BUILD-INFO.txt" },
    @{ Source = ".cache/dependencies/ffmpeg-lgpl-9.0.1/BUILD-INFO.txt"; Name = "FFmpeg-9.0.1-BUILD-INFO.txt" })) {
    Copy-Item -LiteralPath (Join-Path $repositoryRoot $record.Source) `
        -Destination (Join-Path $artifactRoot $record.Name) -Force
}

Write-Output "FFmpeg comparison artifacts are in $artifactRoot"
