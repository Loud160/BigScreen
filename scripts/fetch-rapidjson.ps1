# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
Param(
    [Parameter(Mandatory=$false)]
    [string] $DestinationRoot
)

$ErrorActionPreference = "Stop"

# Quest builds normally receive RapidJSON through QPM. GitHub's host-test job
# does not restore the full Quest dependency graph, so fetch the exact upstream
# revision represented by qpm.shared.json instead of silently testing against a
# different distro package.
$rapidJsonCommit = "24b5e7a8b27f42fa16b96fc70aade9106cf7102f"
$rapidJsonUrl = "https://github.com/Tencent/rapidjson.git"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($DestinationRoot)) {
    $dependencyRoot = Join-Path $repositoryRoot "extern/includes/rapidjson/rapidjson"
} else {
    $dependencyRoot = [System.IO.Path]::GetFullPath($DestinationRoot)
}
$requiredHeader = Join-Path $dependencyRoot "include/rapidjson/document.h"
$readyFile = Join-Path $dependencyRoot ".bigscreen-rapidjson-ready"

if ((Test-Path -LiteralPath $requiredHeader) -and
    (Test-Path -LiteralPath $readyFile) -and
    ((Get-Content -LiteralPath $readyFile -Raw).Trim() -eq $rapidJsonCommit)) {
    Write-Output "Using cached RapidJSON source at pinned commit $rapidJsonCommit."
    exit 0
}

# A QPM-restored checkout already contains the exact dependency but predates
# this script's marker. Accept it when Git proves its revision, then record the
# marker so subsequent invocations are network-free.
if ((Test-Path -LiteralPath $requiredHeader) -and
    (Test-Path -LiteralPath (Join-Path $dependencyRoot ".git"))) {
    $existingCommit = (& git -C $dependencyRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -eq 0 -and $existingCommit -eq $rapidJsonCommit) {
        $rapidJsonCommit | Set-Content -LiteralPath $readyFile -Encoding Ascii
        Write-Output "Using QPM-restored RapidJSON source at pinned commit $rapidJsonCommit."
        exit 0
    }
}

if (Test-Path -LiteralPath $dependencyRoot) {
    # This target is either the fixed ignored dependency location or an
    # explicit caller-provided test destination. Never broaden this removal.
    Remove-Item -LiteralPath $dependencyRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $dependencyRoot | Out-Null

Write-Output "Downloading pinned RapidJSON source required by host tests."
Write-Output "Source: $rapidJsonUrl"
Write-Output "Commit: $rapidJsonCommit"
& git -C $dependencyRoot init --quiet
if ($LASTEXITCODE -ne 0) { throw "Could not initialize the RapidJSON source directory." }
& git -C $dependencyRoot remote add origin $rapidJsonUrl
if ($LASTEXITCODE -ne 0) { throw "Could not configure the RapidJSON upstream." }
& git -C $dependencyRoot fetch --depth 1 origin $rapidJsonCommit
if ($LASTEXITCODE -ne 0) { throw "Could not fetch pinned RapidJSON commit $rapidJsonCommit." }
& git -C $dependencyRoot checkout --detach --quiet FETCH_HEAD
if ($LASTEXITCODE -ne 0) { throw "Could not check out pinned RapidJSON source." }

$actualCommit = (& git -C $dependencyRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $rapidJsonCommit) {
    throw "RapidJSON revision mismatch. Expected $rapidJsonCommit, received $actualCommit."
}
if (-not (Test-Path -LiteralPath $requiredHeader)) {
    throw "Pinned RapidJSON source did not contain $requiredHeader."
}

$rapidJsonCommit | Set-Content -LiteralPath $readyFile -Encoding Ascii
Write-Output "Prepared RapidJSON at pinned commit $rapidJsonCommit in $dependencyRoot"
