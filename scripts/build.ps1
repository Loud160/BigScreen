# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

<#
.SYNOPSIS
Compatibility wrapper for Big Screen's canonical Linux build.

.DESCRIPTION
There is no Windows-native build implementation. This historical QPM entry
point forwards to scripts/build-linux.sh through WSL on Windows or Bash on
Linux, producing the same tested QMOD as the root launchers and CI.
#>

param(
    [switch] $clean,
    [switch] $help
)

$ErrorActionPreference = "Stop"

if ($help) {
    Write-Output "Build Big Screen through the canonical Linux pipeline."
    Write-Output "  -Clean  Remove only generated native build output first."
    exit 0
}

$arguments = @()
if ($clean) { $arguments += "--clean" }
& (Join-Path $PSScriptRoot "invoke-canonical-linux.ps1") `
    -LinuxScript "scripts/build-linux.sh" -ArgumentList $arguments
if (-not $?) { exit 1 }
