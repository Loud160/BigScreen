# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet(0, 1)]
    [int] $WasRunningAtStart,

    [ValidateSet("Ask", "Stop", "Leave")]
    [string] $ExistingAdbAction = "Ask",

    [ValidateRange(1, 300)]
    [int] $PromptTimeoutSeconds = 300
)

$ErrorActionPreference = "Stop"

function Stop-AdbProcesses {
    param([string] $Message)
    Write-Host $Message
    $processes = @(Get-Process adb -ErrorAction SilentlyContinue)
    if ($processes.Count -eq 0) { return }

    # Prefer ADB's graceful shutdown using the executable that owns the active
    # daemon. Fall back to ending only adb.exe if the daemon is mismatched or
    # no longer responds; users should never need to discover taskkill syntax.
    $adbPath = $processes |
        ForEach-Object { try { $_.Path } catch { $null } } |
        Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
        Select-Object -First 1
    if ($adbPath) {
        $priorErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & $adbPath kill-server 2>&1 | Out-Null
        } finally {
            $ErrorActionPreference = $priorErrorActionPreference
        }
        Start-Sleep -Milliseconds 300
    }
    Get-Process adb -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
}

if (-not (Get-Process adb -ErrorAction SilentlyContinue)) {
    Write-Host "ADB is not running."
    exit 0
}

if ($WasRunningAtStart -eq 0) {
    Stop-AdbProcesses "Stopping the ADB server started by deployment..."
    Write-Host "ADB was stopped."
    exit 0
}

$stopExisting = $false
switch ($ExistingAdbAction) {
    "Stop" { $stopExisting = $true }
    "Leave" { $stopExisting = $false }
    default {
        Write-Host ""
        Write-Host "ADB was already running before deployment."
        Write-Host "Stopping it can help ModsBeforeFriday connect. If no choice is made within five minutes, ADB will be left running."
        $choiceResult = 2
        $priorErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & choice.exe /C YN /N /T $PromptTimeoutSeconds /D N /M "Stop ADB now? [Y/N] "
            $choiceResult = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $priorErrorActionPreference
        }
        $stopExisting = $choiceResult -eq 1
    }
}

if ($stopExisting) {
    Stop-AdbProcesses "Stopping the existing ADB server..."
    Write-Host "ADB was stopped."
} else {
    Write-Host "ADB was left running."
}
