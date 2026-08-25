# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

<#
.SYNOPSIS
Compatibility wrapper that creates a QMOD through the canonical Linux build.
#>

param(
    [string] $qmodName = "",
    [switch] $help
)

$ErrorActionPreference = "Stop"

if ($help) {
    Write-Output "Build and package Big Screen through the canonical Linux pipeline."
    Write-Output "  -QmodName <name>  Also copy the verified QMOD to this file name."
    exit 0
}

& (Join-Path $PSScriptRoot "invoke-canonical-linux.ps1") `
    -LinuxScript "scripts/build-linux.sh"
if (-not $?) { exit 1 }

if ($qmodName) {
    $repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    $source = Join-Path $repositoryRoot "Big Screen.qmod"
    $destination = if ([IO.Path]::IsPathRooted($qmodName)) {
        $qmodName
    } else {
        Join-Path $repositoryRoot $qmodName
    }
    Copy-Item -LiteralPath $source -Destination $destination -Force
}
