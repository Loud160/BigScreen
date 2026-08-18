# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
Param(
    [Parameter(Mandatory=$false)]
    [Switch] $Force,

    [Parameter(Mandatory=$false)]
    [string] $Python = "python"
)

$ErrorActionPreference = "Stop"

# These pins intentionally mirror fetch-downloader-runtime.ps1. Update them as
# one reviewed change whenever Big Screen moves to another downloader release.
$ytDlpVersion = "2026.08.18.122307"
$ytDlpSourceSha256 = "e9169887a9863bc635e1d3760f90cb37588dad2111064d454c790aaaa121349a"
$ytDlpReleaseSha256 = "7c2e017b19c249447445e776913d54bcea81b85b21b51d50ff36b7b8cae956e1"
$ytDlpRepository = "yt-dlp/yt-dlp-nightly-builds"
$ejsVersion = "0.8.0"
$ejsSourceSha256 = "2704dfcac899fcb443d3b80cb8bb6337c5eb98d09ebf0760295d1c58e9461e53"
$ejsCoreSha256 = "18da6ce0758b416e7ae645084f4f8801f9f9d59d6c477c05eaa0ff94ebd8cc00"
$ejsLibrarySha256 = "c55987fe697e5b9ee18830163f7af85327e9bb5c3e674b969d38c8d205eaa577"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
& (Join-Path $PSScriptRoot "initialize-dependency-cache.ps1")
if (-not $?) {
    throw "Could not initialize Big Screen's portable dependency cache."
}
$sourceRoot = Join-Path $repositoryRoot ".cache/dependencies/downloader-source"
$ytDlpArchive = Join-Path $sourceRoot "yt-dlp-$ytDlpVersion.tar.gz"
$ejsArchive = Join-Path $sourceRoot "yt-dlp-ejs-$ejsVersion.tar.gz"
$ytDlpExtractRoot = Join-Path $sourceRoot "yt-dlp-$ytDlpVersion-source"
$ejsExtractRoot = Join-Path $sourceRoot "yt-dlp-ejs-$ejsVersion-source"
$ytDlpSource = Join-Path $ytDlpExtractRoot "yt-dlp"
$ejsSource = Join-Path $ejsExtractRoot "ejs-$ejsVersion"
$releaseRuntime = Join-Path $sourceRoot "yt-dlp-$ytDlpVersion-release"
$output = Join-Path $repositoryRoot "build/downloader-source/yt-dlp-source-built"

New-Item -ItemType Directory -Force -Path $sourceRoot | Out-Null

function Assert-Sha256([string] $Path, [string] $Expected) {
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) {
        throw "SHA-256 mismatch for $Path. Expected $Expected, received $actual."
    }
}

function Get-VerifiedFile([string] $Url, [string] $Path, [string] $Hash) {
    if ($Force -or -not (Test-Path -LiteralPath $Path)) {
        Invoke-WebRequest -Uri $Url -OutFile $Path
    }
    Assert-Sha256 $Path $Hash
}

function Expand-VerifiedTar([string] $Archive, [string] $Destination) {
    if ($Force -and (Test-Path -LiteralPath $Destination)) {
        # Only remove the fixed versioned directory immediately beneath the
        # ignored dependency root; never accept a computed broad target.
        if ((Split-Path -Parent $Destination) -ne $sourceRoot) {
            throw "Refusing to replace unexpected source path $Destination"
        }
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    if (-not (Test-Path -LiteralPath $Destination)) {
        New-Item -ItemType Directory -Path $Destination | Out-Null
        & tar -xzf $Archive -C $Destination
        if ($LASTEXITCODE -ne 0) {
            throw "Could not extract verified source archive $Archive."
        }
    }
}

Get-VerifiedFile `
    "https://github.com/$ytDlpRepository/releases/download/$ytDlpVersion/yt-dlp.tar.gz" `
    $ytDlpArchive `
    $ytDlpSourceSha256
Get-VerifiedFile `
    "https://github.com/yt-dlp/ejs/archive/refs/tags/$ejsVersion.tar.gz" `
    $ejsArchive `
    $ejsSourceSha256
Get-VerifiedFile `
    "https://github.com/$ytDlpRepository/releases/download/$ytDlpVersion/yt-dlp" `
    $releaseRuntime `
    $ytDlpReleaseSha256

Expand-VerifiedTar $ytDlpArchive $ytDlpExtractRoot
Expand-VerifiedTar $ejsArchive $ejsExtractRoot

# Upstream's build hook uses its committed lockfile and chooses pnpm, Deno,
# Bun, or npm. It produces the exact two minified scripts consumed by yt-dlp;
# fixed output hashes catch non-reproducible tooling or dependency drift.
Push-Location $ejsSource
try {
    & $Python "hatch_build.py"
    if ($LASTEXITCODE -ne 0) {
        throw "yt-dlp-ejs source bundling failed."
    }
}
finally {
    Pop-Location
}

$ejsCore = Join-Path $ejsSource "dist/yt.solver.core.min.js"
$ejsLibrary = Join-Path $ejsSource "dist/yt.solver.lib.min.js"
Assert-Sha256 $ejsCore $ejsCoreSha256
Assert-Sha256 $ejsLibrary $ejsLibrarySha256

& $Python `
    (Join-Path $PSScriptRoot "build_downloader_runtime.py") `
    --yt-dlp-source $ytDlpSource `
    --ejs-source $ejsSource `
    --ejs-version $ejsVersion `
    --reference $releaseRuntime `
    --output $output
if ($LASTEXITCODE -ne 0) {
    throw "Could not assemble the downloader runtime from source."
}

# A final execution check validates the ZIP preamble/central directory and the
# root __main__.py, not merely the individual files compared above.
$reportedVersion = (& $Python $output --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $reportedVersion -ne $ytDlpVersion) {
    throw "Source-built downloader reported '$reportedVersion' instead of $ytDlpVersion."
}

Write-Output "Rebuilt yt-dlp $ytDlpVersion with yt-dlp-ejs $ejsVersion from pinned source."
Write-Output "Output: $output"
