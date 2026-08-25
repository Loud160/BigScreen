# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
. (Join-Path $root "scripts/quest-dependency-check.ps1")

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-False([bool]$Condition, [string]$Message) {
    if ($Condition) { throw $Message }
}

Assert-False (Test-BigScreenSemanticVersionRange "4.6.4" "^4.8.0") `
    "Paper2 4.6.4 must not satisfy Big Screen's 4.8.0 ABI requirement."
Assert-False (Test-BigScreenSemanticVersionRange "4.7.0" "^4.8.0") `
    "Paper2 4.7.0 must not satisfy Big Screen's 4.8.0 ABI requirement."
Assert-True (Test-BigScreenSemanticVersionRange "4.8.0" "^4.8.0") `
    "Paper2 4.8.0 must satisfy its exact lower bound."
Assert-True (Test-BigScreenSemanticVersionRange "4.9.2" "^4.8.0") `
    "A compatible later Paper2 4.x release must satisfy the caret range."
Assert-False (Test-BigScreenSemanticVersionRange "5.0.0" "^4.8.0") `
    "A new Paper2 major version must not silently satisfy the 4.x ABI range."
Assert-True (Test-BigScreenSemanticVersionRange "0.4.55" "^0.4.54") `
    "Caret handling for 0.x BSML releases must retain the current minor line."
Assert-False (Test-BigScreenSemanticVersionRange "0.5.0" "^0.4.54") `
    "Caret handling for 0.x dependencies must reject the next minor line."

$requirements = @(Get-BigScreenDependencyRequirements (Join-Path $root "mod.json"))
$paperRequirement = @($requirements | Where-Object Id -eq "paper2_scotland2") |
    Select-Object -First 1
Assert-True ($null -ne $paperRequirement) "The generated QMOD must require Paper2."
Assert-True ($paperRequirement.VersionRange -eq "^4.8.0") `
    "The generated QMOD must not regress below Paper2 4.8.0."

$oldPaper = [pscustomobject]@{
    Id = "paper2_scotland2"
    Version = "4.6.4"
    MissingFiles = @()
}
$status = @(Get-BigScreenDependencyStatuses @($paperRequirement) @($oldPaper))[0]
Assert-False $status.Satisfied "An installed but ABI-incompatible Paper2 must block deployment."
Assert-True ($status.Message -match "does not satisfy") `
    "The failure must tell the user that the installed version is outdated."

$incompletePaper = [pscustomobject]@{
    Id = "paper2_scotland2"
    Version = "4.8.0"
    MissingFiles = @("/sdcard/ModData/com.beatgames.beatsaber/Modloader/libs/libpaper2_scotland2.so")
}
$status = @(Get-BigScreenDependencyStatuses @($paperRequirement) @($incompletePaper))[0]
Assert-False $status.Satisfied "Compatible metadata with a missing library must block deployment."
Assert-True ($status.Message -match "payload is incomplete") `
    "The failure must distinguish an incomplete package from an old version."

$completePaper = [pscustomobject]@{
    Id = "paper2_scotland2"
    Version = "4.8.0"
    MissingFiles = @()
}
$status = @(Get-BigScreenDependencyStatuses @($paperRequirement) @($completePaper))[0]
Assert-True $status.Satisfied "A complete Paper2 4.8.0 package must pass deployment preflight."

Write-Output "Quest dependency preflight tests passed."
