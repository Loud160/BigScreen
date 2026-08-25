# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

<#
.SYNOPSIS
Compatibility bridge from Windows PowerShell to Big Screen's canonical Linux scripts.

.DESCRIPTION
This file does not implement a second build. On Windows it selects the same
supported Ubuntu/WSL environment as Build-And-Deploy.bat and invokes the named
Bash entry point. On native Linux it invokes that Bash file directly. Keeping
the bridge small prevents old QPM commands or developer habits from silently
returning to the removed Windows-native compiler/package path.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $LinuxScript,

    [Parameter(Mandatory = $false)]
    [string[]] $ArgumentList = @()
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$normalizedScript = $LinuxScript.Replace("\", "/").TrimStart([char[]]"./")
$scriptPath = Join-Path $repositoryRoot $normalizedScript
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "Canonical Linux entry point was not found: $scriptPath"
}

if ($env:OS -eq "Windows_NT") {
    $wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue
    if (-not $wsl) {
        throw "WSL was not found. Run Build-And-Deploy.bat once to audit and install the supported Ubuntu build environment."
    }

    $selectedDistro = $null
    foreach ($candidate in @("Ubuntu-24.04", "Ubuntu")) {
        & $wsl.Source -d $candidate -e true *> $null
        if ($LASTEXITCODE -eq 0) {
            $selectedDistro = $candidate
            break
        }
    }
    if (-not $selectedDistro) {
        throw "No supported Ubuntu-24.04 or Ubuntu WSL distribution is ready. Run Build-And-Deploy.bat to complete the prerequisite audit."
    }

    Write-Output "Forwarding to the canonical Linux pipeline in $selectedDistro."
    & $wsl.Source -d $selectedDistro --cd $repositoryRoot -e bash $normalizedScript @ArgumentList
} else {
    $bash = Get-Command bash -ErrorAction SilentlyContinue
    if (-not $bash) {
        throw "Bash was not found. Big Screen's supported build host is x86-64 Linux."
    }
    Push-Location $repositoryRoot
    try {
        & $bash.Source $normalizedScript @ArgumentList
    }
    finally {
        Pop-Location
    }
}

if ($LASTEXITCODE -ne 0) {
    throw "The canonical Linux pipeline failed with exit code $LASTEXITCODE."
}
