# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
$ErrorActionPreference = "Stop"

# Keep legal notices beside both exact embedded runtimes used by QMOD
# packaging and direct Quest deployment. A clean build recreates
# build/downloader, so staging these files only in createqmod.ps1 made the
# normal copy.ps1 path fail immediately after a clean compile.
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$runtimeStage = Join-Path $repositoryRoot "build/downloader"
$ffmpeg44Runtime = Join-Path $repositoryRoot ".cache/dependencies/ffmpeg-lgpl"
$ffmpeg9Runtime = Join-Path $repositoryRoot ".cache/dependencies/ffmpeg-lgpl-9.0.1"
$noticeSources = @{
    "BIGSCREEN-LICENSE.txt" = Join-Path $repositoryRoot "LICENSE"
    "BIGSCREEN-ADDITIONAL-TERMS.md" = Join-Path $repositoryRoot "LICENSE-ADDITIONAL-TERMS.md"
    "BIGSCREEN-NOTICE.txt" = Join-Path $repositoryRoot "NOTICE"
    "THIRD-PARTY-NOTICES.md" = Join-Path $repositoryRoot "THIRD_PARTY_NOTICES.md"
    "FFMPEG-LGPL-2.1-OR-LATER.txt" = Join-Path $ffmpeg44Runtime "COPYING.LGPLv2.1"
    "FFMPEG-4.4.8-BUILD-INFO.txt" = Join-Path $ffmpeg44Runtime "BUILD-INFO.txt"
    "FFMPEG-4.4.8-CHANGES.diff" = Join-Path $ffmpeg44Runtime "bigscreen-ffmpeg-changes.diff"
    "FFMPEG-9.0.1-BUILD-INFO.txt" = Join-Path $ffmpeg9Runtime "BUILD-INFO.txt"
    "FFMPEG-9.0.1-CHANGES.diff" = Join-Path $ffmpeg9Runtime "bigscreen-ffmpeg-changes.diff"
    "CERTIFI-MPL-2.0.txt" = Join-Path $repositoryRoot "licenses/CERTIFI-MPL-2.0.txt"
    "MPL-2.0.txt" = Join-Path $repositoryRoot "licenses/MPL-2.0.txt"
    "YT-DLP-UNLICENSE.txt" = Join-Path $repositoryRoot "licenses/YT-DLP-UNLICENSE.txt"
    "QUICKJS-NG-MIT.txt" = Join-Path $repositoryRoot "licenses/QUICKJS-NG-MIT.txt"
    "OPENSSL-APACHE-2.0.txt" = Join-Path $repositoryRoot "licenses/OPENSSL-APACHE-2.0.txt"
    "SQLITE-PUBLIC-DOMAIN.txt" = Join-Path $repositoryRoot "licenses/SQLITE-PUBLIC-DOMAIN.txt"
}

if (-not (Test-Path -LiteralPath $runtimeStage)) {
    throw "Downloader runtime must be staged before its notices: $runtimeStage"
}
foreach ($notice in $noticeSources.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $notice.Value)) {
        throw "Required runtime notice is missing: $($notice.Value)"
    }
    Copy-Item -LiteralPath $notice.Value `
        -Destination (Join-Path $runtimeStage $notice.Key) -Force
}
