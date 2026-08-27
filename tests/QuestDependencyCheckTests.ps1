# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
. (Join-Path $root "scripts/quest-dependency-check.ps1")

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-False([bool]$Condition, [string]$Message) {
    if ($Condition) { throw $Message }
}

Assert-False (Test-BigScreenSemanticVersionRange "4.6.4" "^4.8.0") `
    "A version below a dependency's lower bound must be rejected."
Assert-True (Test-BigScreenSemanticVersionRange "4.8.0" "^4.8.0") `
    "A stable release must parse and satisfy its exact lower bound under StrictMode."
Assert-True (Test-BigScreenSemanticVersionRange "4.9.2" "^4.8.0") `
    "A compatible later release must satisfy the caret range."
Assert-False (Test-BigScreenSemanticVersionRange "5.0.0" "^4.8.0") `
    "A new major release must not silently satisfy the current caret range."
Assert-False (Test-BigScreenSemanticVersionRange "4.8.0-beta.1" "^4.8.0") `
    "A prerelease must remain below the matching stable lower bound."
Assert-True (Test-BigScreenSemanticVersionRange "0.4.55" "^0.4.54") `
    "Caret handling for 0.x BSML releases must retain the current minor line."
Assert-False (Test-BigScreenSemanticVersionRange "0.5.0" "^0.4.54") `
    "Caret handling for 0.x dependencies must reject the next minor line."

$requirements = @(Get-BigScreenDependencyRequirements (Join-Path $root "mod.json"))
$paperRequirement = @($requirements | Where-Object Id -eq "paper2_scotland2")
Assert-True ($paperRequirement.Count -eq 0) `
    "Big Screen must not reintroduce a direct Paper2 QMOD dependency."
$hookRequirement = @($requirements | Where-Object Id -eq "beatsaber-hook") |
    Select-Object -First 1
Assert-True ($null -ne $hookRequirement) `
    "The generated QMOD must retain its direct beatsaber-hook dependency."

$testRequirement = [pscustomobject]@{
    Id = "test-dependency"
    VersionRange = "^4.8.0"
}

$oldPackage = [pscustomobject]@{
    Id = "test-dependency"
    Version = "4.6.4"
    MissingFiles = @()
}
$status = @(Get-BigScreenDependencyStatuses @($testRequirement) @($oldPackage))[0]
Assert-False $status.Satisfied "An installed dependency below its minimum must block deployment."
Assert-True ($status.Message -match "does not satisfy") `
    "The failure must tell the user that the installed version is outdated."

$incompletePackage = [pscustomobject]@{
    Id = "test-dependency"
    Version = "4.8.0"
    MissingFiles = @("/sdcard/ModData/com.beatgames.beatsaber/Modloader/libs/libtest-dependency.so")
}
$status = @(Get-BigScreenDependencyStatuses @($testRequirement) @($incompletePackage))[0]
Assert-False $status.Satisfied "Compatible metadata with a missing payload must block deployment."
Assert-True ($status.Message -match "payload is incomplete") `
    "The failure must distinguish an incomplete package from an old version."

$completePackage = [pscustomobject]@{
    Id = "test-dependency"
    Version = "4.8.0"
    MissingFiles = @()
}
$status = @(Get-BigScreenDependencyStatuses @($testRequirement) @($completePackage))[0]
Assert-True $status.Satisfied "A complete compatible package must pass deployment preflight."

$diagnosis = Get-BigScreenDependencyDiagnosticReport `
    @($testRequirement) @($oldPackage)
Assert-True $diagnosis.HasFailures `
    "The support diagnosis must flag an installed dependency below its minimum."
Assert-True ($diagnosis.Text -match "PROBLEM: test-dependency") `
    "The support diagnosis must identify the failing package in plain language."
Assert-True ($diagnosis.Text -match "Open ModsBeforeFriday") `
    "The support diagnosis must tell a nontechnical user how to repair the dependency."

$diagnosis = Get-BigScreenDependencyDiagnosticReport `
    @($testRequirement) @($completePackage)
Assert-False $diagnosis.HasFailures `
    "A complete compatible dependency must not be reported as a possible load failure."
Assert-True ($diagnosis.Text -match "All dependencies") `
    "A healthy dependency snapshot must be explicit rather than empty."

Write-Output "Quest dependency preflight tests passed."
