# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

<#
.SYNOPSIS
Requires an authorized Quest before the Windows source build begins.

.DESCRIPTION
ADB installation and Quest authorization are separate operations. Installing
ADB starts the server and may cause the headset to display its RSA debugging
prompt, but the user still has to approve that computer inside the headset.
This preflight retries only when the user explicitly chooses Retry; it never
lets a long build finish before discovering that deployment cannot proceed.
#>

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "adb-target.ps1")

$adbCommand = Get-Command adb.exe -ErrorAction SilentlyContinue
if (-not $adbCommand) {
    Write-Host "ERROR: ADB was not found after the prerequisite setup completed." -ForegroundColor Red
    exit 1
}

while ($true) {
    try {
        [void](Select-BigScreenAdbTarget `
            "source deployment preflight" -AdbCommand $adbCommand.Source)
        Write-Host "Quest connection preflight passed." -ForegroundColor Green
        exit 0
    }
    catch {
        Write-Host ""
        Write-Host "Quest connection check failed: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host ""
        Write-Host "To authorize and retry:" -ForegroundColor Yellow
        Write-Host "  1. Put on the Quest and keep the headset awake and unlocked."
        Write-Host "  2. Confirm Developer Mode is enabled for the headset."
        Write-Host "  3. Disconnect and reconnect the USB data cable if no prompt is visible."
        Write-Host '  4. In the USB debugging prompt, select "Always allow from this computer"'
        Write-Host "     if desired, then choose Allow. The ordinary USB/file-access notice is"
        Write-Host "     not the USB-debugging authorization prompt."
        Write-Host "  5. Return to this window and choose Retry."
        Write-Host ""
        & choice.exe /C RC /N /M "Retry the Quest connection check or cancel [R/C]? "
        if ($LASTEXITCODE -eq 1) {
            continue
        }
        Write-Host "Quest connection was cancelled. No build or deployment was started." -ForegroundColor Yellow
        exit 2
    }
}
