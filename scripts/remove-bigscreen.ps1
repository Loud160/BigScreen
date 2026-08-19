# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen. Distributed under GPL-3.0-only with additional terms
# under GPLv3 section 7(b)/(c) and an interoperability permission under
# section 7; see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
[CmdletBinding()]
param(
    [switch]$ConfirmRemoval,
    [switch]$RemoveSettings,
    [switch]$NonInteractive
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    $portableAdb = Join-Path $repoRoot "BigScreen Tools/platform-tools/adb.exe"
    if (Test-Path -LiteralPath $portableAdb -PathType Leaf) {
        $env:PATH = (Split-Path -Parent $portableAdb) + [IO.Path]::PathSeparator + $env:PATH
    }
}
if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    throw "ADB was not found. Run scripts/ensure-adb.ps1 or Build-And-Deploy.bat first."
}

. (Join-Path $PSScriptRoot "source-install-ownership.ps1")
$manifestPath = Join-Path $repoRoot "mod.template.json"
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

$devices = @((& adb devices) | Select-String "`tdevice$")
if ($devices.Count -ne 1) {
    throw $(if ($devices.Count -eq 0) {
        "No authorized Quest was found. Connect one headset and accept its USB debugging prompt."
    } else {
        "More than one authorized Android device is connected. Disconnect all but the Quest to remove Big Screen safely."
    })
}

$classification = Get-BigScreenInstallClassification ([string]$manifest.packageVersion)
Write-Output "Detected Big Screen installation state: $($classification.State)"
if ($classification.State -eq "NOT_INSTALLED") {
    Write-Output "No source-managed Big Screen installation was found. Videos, library data, settings, and logs were not changed."
    exit 0
}
if ($classification.State -eq "MBF_MANAGED" -or
    $classification.State -eq "MBF_REGISTERED_NOT_INSTALLED") {
    throw "This Big Screen installation is managed by ModsBeforeFriday. Remove it through MBF; no files were changed."
}
if ($classification.State -eq "MIXED_OR_AMBIGUOUS" -and
    ($classification.ReceiptUnreadable -or
     @($classification.UnexpectedPhasePaths).Count -gt 0 -or
     (-not $classification.CompleteReceipt -and
      -not $classification.PartialReceipt))) {
    Write-BigScreenOwnershipDiagnostic $classification
    throw "Big Screen ownership is ambiguous and cannot be reconciled from readable receipts. No files were changed. Remove any MBF registration first, preserve the listed unknown files, and rerun this tool."
}

Write-Output ""
Write-Output "Big Screen source installation will be removed from the connected Quest."
Write-Output "Downloaded videos, the Video Library, thumbnails, logs, map-local videos, and other user media will NOT be removed."
if ($classification.State -eq "MIXED_OR_AMBIGUOUS") {
    Write-Host "MBF registration and a readable source receipt are both present." -ForegroundColor Yellow
    Write-Output "Only hash-proven source files that MBF does not claim will be reconciled. MBF-owned paths will be preserved for MBF to repair."
}
if (-not $ConfirmRemoval) {
    if ($NonInteractive) { Write-Output "No files were changed."; exit 2 }
    $answer = Read-Host "Continue? [Y/N]"
    if ($answer -notmatch '^(?i)y(?:es)?$') {
        Write-Output "No files were changed."
        exit 0
    }
}

if (-not $NonInteractive -and -not $RemoveSettings) {
    Write-Output ""
    Write-Output "Removing the settings file will NOT remove downloaded videos."
    $settingsAnswer = Read-Host "Also remove Big Screen settings? [y/N]"
    $RemoveSettings = $settingsAnswer -match '^(?i)y(?:es)?$'
}

[void](Invoke-BigScreenAdb @("shell", "am force-stop '$($script:BigScreenPackage)'"))
$ambiguous = @()
if ($classification.State -eq "SOURCE_MANAGED") {
    $ambiguous = @(Remove-BigScreenReceiptFiles $classification.CompleteReceipt)
} elseif ($classification.State -eq "SOURCE_PARTIAL") {
    Assert-BigScreenPartialRecoverable $classification.PartialReceipt
    $ambiguous = @(Remove-BigScreenReceiptFiles `
        -Receipt $classification.PartialReceipt `
        -Partial)
    if ($classification.CompleteReceipt) {
        # A partial receipt contains the new plan. Reconcile any paths retired
        # from the preceding complete plan as part of the same interrupted
        # transition, accepting the recorded baseline if it was already
        # restored before the interruption.
        $partialPaths = @{}
        foreach ($item in @($classification.PartialReceipt.files)) {
            $partialPaths[[string]$item.path] = $true
        }
        $retiredFiles = @($classification.CompleteReceipt.files |
            Where-Object { -not $partialPaths.ContainsKey([string]$_.path) })
        if ($retiredFiles.Count -gt 0) {
            $retiredReceipt = [pscustomobject]@{ files = $retiredFiles }
            $ambiguous += @(Remove-BigScreenReceiptFiles `
                -Receipt $retiredReceipt `
                -Partial)
        }
    }
} elseif ($classification.State -eq "LEGACY_SOURCE") {
    Write-Host "Removing only exact, uniquely named legacy Big Screen payload. Shared dependencies and user data remain." -ForegroundColor Yellow
    $privateNames = @(Get-BigScreenJsonArray $manifest "libraryFiles" |
        Where-Object { Test-BigScreenExclusiveLibraryName ([string]$_) })
    $privatePaths = @($privateNames | ForEach-Object {
        "$($script:BigScreenModData)/Modloader/libs/$_"
    })
    # Pre-receipt Build & Deploy mirrored every QMOD fileCopy into Big
    # Screen's private Runtime directory. These are exact manifest paths, not
    # a recursive data-root deletion; user videos/library/logs remain outside
    # this list and are always preserved.
    $runtimeRoot = "$($script:BigScreenModData)/BigScreen/Runtime/"
    $privatePaths += @(Get-BigScreenJsonArray $manifest "fileCopies" |
        ForEach-Object { [string]$_.destination } |
        Where-Object {
            $_.StartsWith($runtimeRoot, [StringComparison]::Ordinal)
        })
    Remove-BigScreenLegacyExclusivePayload -AdditionalPaths $privatePaths
    Remove-BigScreenLegacyRuntimePayload
} elseif ($classification.State -eq "MIXED_OR_AMBIGUOUS") {
    $mbfPaths = @{}
    foreach ($package in @($classification.MbfPackages)) {
        foreach ($path in @($package.RequiredFiles)) {
            $mbfPaths[[string]$path] = $true
        }
    }
    $receipt = if ($classification.PartialReceipt) {
        Assert-BigScreenPartialRecoverable $classification.PartialReceipt
        $classification.PartialReceipt
    } else { $classification.CompleteReceipt }
    $sourceOnlyFiles = @($receipt.files | Where-Object {
        -not $mbfPaths.ContainsKey([string]$_.path)
    })
    if ($sourceOnlyFiles.Count -gt 0) {
        $sourceOnlyReceipt = [pscustomobject]@{ files = $sourceOnlyFiles }
        $ambiguous += @(Remove-BigScreenReceiptFiles `
            -Receipt $sourceOnlyReceipt `
            -Partial:([bool]$classification.PartialReceipt))
    }
    if ($classification.PartialReceipt -and
        $classification.CompleteReceipt) {
        $partialPaths = @{}
        foreach ($item in @($classification.PartialReceipt.files)) {
            $partialPaths[[string]$item.path] = $true
        }
        $retiredSourceOnlyFiles = @(
            $classification.CompleteReceipt.files | Where-Object {
                -not $partialPaths.ContainsKey([string]$_.path) -and
                -not $mbfPaths.ContainsKey([string]$_.path)
            })
        if ($retiredSourceOnlyFiles.Count -gt 0) {
            $retiredSourceOnlyReceipt = [pscustomobject]@{
                files = $retiredSourceOnlyFiles
            }
            $ambiguous += @(Remove-BigScreenReceiptFiles `
                -Receipt $retiredSourceOnlyReceipt `
                -Partial)
        }
    }
    foreach ($item in @($receipt.files | Where-Object {
            $mbfPaths.ContainsKey([string]$_.path)
        })) {
        Write-Output "Preserved MBF-required path: $($item.path)"
    }
}

if ($ambiguous.Count -gt 0) {
    Write-Host "The following files changed after source deployment and were preserved:" -ForegroundColor Yellow
    $ambiguous | ForEach-Object { Write-Output "  $_" }
    throw "Source removal stopped with ambiguous files preserved. The ownership receipt was retained for diagnosis."
}

if ($RemoveSettings) {
    $settingsPath = "$($script:BigScreenModData)/Configs/bigscreen.json"
    [void](Invoke-BigScreenAdb @("shell", "rm -f -- '$settingsPath'"))
    Write-Output "Big Screen settings were removed by explicit request."
} else {
    Write-Output "Big Screen settings were preserved."
}

# This exact directory contains only source ownership receipts and their
# baseline backups. User media and Logs are sibling paths and are untouched.
[void](Invoke-BigScreenAdb @("shell", "rm -rf -- '$($script:SourceInstallRoot)'"))
Write-Output ""
Write-Output "Big Screen source installation removed."
Write-Output "Downloaded videos, library data, thumbnails, logs, and other user data were preserved."
if ($classification.State -eq "MIXED_OR_AMBIGUOUS") {
    Write-Output "Big Screen remains registered with ModsBeforeFriday. Use MBF to repair or remove its package before another source deployment."
} else {
    Write-Output "You may now install Big Screen normally through ModsBeforeFriday."
}
