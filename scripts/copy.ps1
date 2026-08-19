# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
Param(
    [Parameter(Mandatory=$false)]
    [Switch] $clean,

    [Parameter(Mandatory=$false)]
    [Switch] $log,

    [Parameter(Mandatory=$false)]
    [Switch] $useDebug,

    [Parameter(Mandatory=$false)]
    [Switch] $self,

    [Parameter(Mandatory=$false)]
    [Switch] $all,

    [Parameter(Mandatory=$false)]
    [String] $custom="",

    [Parameter(Mandatory=$false)]
    [String] $file="",

    [Parameter(Mandatory=$false)]
    [Switch] $help
)

$ErrorActionPreference = "Stop"

if ($help -eq $true) {
    Write-Output "`"Copy`" - Builds and copies your mod to your quest, and also starts Beat Saber with optional logging"
    Write-Output "`n-- Arguments --`n"

    Write-Output "-Clean `t`t Performs a clean build (equvilant to running `"build -clean`")"
    Write-Output "-UseDebug `t Copies the debug version of the mod to your quest"
    Write-Output "-Log `t`t Logs Beat Saber using the `"Start-Logging`" command"

    Write-Output "`n-- Logging Arguments --`n"

    & $PSScriptRoot/start-logging.ps1 -help -excludeHeader

    exit
}

# QPM developers often have ADB through SideQuest without adding it to the
# system PATH. Search environment-derived installation roots rather than
# embedding one computer's drive or Program Files location in this portable
# repository.
if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    $sideQuestCandidates = @()
    foreach ($programRoot in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ($programRoot) {
            $sideQuestCandidates += Join-Path $programRoot `
                "SideQuest/resources/app.asar.unpacked/build/platform-tools/adb.exe"
        }
    }
    if ($env:LOCALAPPDATA) {
        $sideQuestCandidates += Join-Path $env:LOCALAPPDATA `
            "Programs/SideQuest/resources/app.asar.unpacked/build/platform-tools/adb.exe"
        $sideQuestCandidates += Join-Path $env:LOCALAPPDATA `
            "SideQuest/resources/app.asar.unpacked/build/platform-tools/adb.exe"
    }
    $sideQuestAdb = $sideQuestCandidates |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
    if ($sideQuestAdb) {
        $env:PATH = (Split-Path -Parent $sideQuestAdb) +
            [IO.Path]::PathSeparator + $env:PATH
    }
}
if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    throw "ADB was not found. Install Android platform-tools or SideQuest before deploying."
}
. (Join-Path $PSScriptRoot "adb-target.ps1")
[void](Select-BigScreenAdbTarget "source deployment")

# The embedded video shader bundle must never be stale relative to its Unity
# source, or the build silently ships a broken screen material. When any
# shader-project input is newer than the built bundle, rebuild it here so the
# one-click workflow always embeds the current shader; if Unity 2022.3.33f1
# is not installed, fail loudly instead of deploying a known-stale shader.
$shaderAsset = Join-Path $PSScriptRoot "../assets/bigscreen_video_shader"
$shaderInputs = @(
    (Join-Path $PSScriptRoot "../tools/video-shader/Assets/BigScreenVideo.shader"),
    (Join-Path $PSScriptRoot "../tools/video-shader/Assets/Editor/BuildBigScreenVideoShader.cs"),
    (Join-Path $PSScriptRoot "../tools/video-shader/Packages/manifest.json"),
    (Join-Path $PSScriptRoot "../tools/video-shader/ProjectSettings/ProjectVersion.txt")
)
$shaderRebuildNeeded = -not (Test-Path -LiteralPath $shaderAsset)
if (-not $shaderRebuildNeeded) {
    $assetTime = (Get-Item -LiteralPath $shaderAsset).LastWriteTimeUtc
    foreach ($shaderInput in $shaderInputs) {
        if ((Test-Path -LiteralPath $shaderInput) -and
            (Get-Item -LiteralPath $shaderInput).LastWriteTimeUtc -gt $assetTime) {
            $shaderRebuildNeeded = $true
            break
        }
    }
}
if ($shaderRebuildNeeded) {
    Write-Output ""
    Write-Output "The embedded video shader source changed; rebuilding assets/bigscreen_video_shader with Unity..."
    $shaderBuildError = $null
    try {
        & $PSScriptRoot/build-video-shader.ps1
    } catch {
        $shaderBuildError = $_.Exception.Message
    }
    $assetFresh = (Test-Path -LiteralPath $shaderAsset) -and
        -not ($shaderInputs | Where-Object {
            (Test-Path -LiteralPath $_) -and
            (Get-Item -LiteralPath $_).LastWriteTimeUtc -gt
                (Get-Item -LiteralPath $shaderAsset).LastWriteTimeUtc })
    if ($shaderBuildError -or -not $assetFresh) {
        if ($shaderBuildError) { Write-Output "Shader bundle build failed: $shaderBuildError" }
        Write-Output "The Android video shader bundle could not be rebuilt."
        Write-Output "Install Unity 2022.3.33f1 through Unity Hub (one time), then run this again."
        Write-Output "Deploying with a stale shader bundle is refused because it produces an invisible or bloom-broken screen."
        exit 1
    }
    Write-Output "Android video shader bundle rebuilt successfully."
} else {
    Write-Output "Embedded video shader bundle is up to date."
}

& $PSScriptRoot/build.ps1 -clean:$clean

if ($LASTEXITCODE -ne 0) {
    Write-Output "Failed to build, exiting..."
    exit $LASTEXITCODE
}

& $PSScriptRoot/validate-modjson.ps1
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

# qpm qmod manifest intentionally starts from the tracked template, whose
# fileCopies list is empty. Populate it from the staged downloader before
# reading mod.json so a completely clean ADB deployment installs the same
# runtime as a QMOD instead of depending on files from an older installation.
$runtimeStage = Join-Path (Join-Path $PSScriptRoot "..") "build/downloader"
& $PSScriptRoot/stage-runtime-notices.ps1
if (-not $?) {
    exit 1
}
. (Join-Path $PSScriptRoot "sync-runtime-manifest.ps1")
[void](Sync-BigScreenRuntimeManifest `
    -ModJsonPath (Join-Path (Get-Location) "mod.json") `
    -RuntimeStage $runtimeStage)
& $PSScriptRoot/validate-modjson.ps1
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
$modJson = Get-Content "./mod.json" -Raw | ConvertFrom-Json

. (Join-Path $PSScriptRoot "source-install-ownership.ps1")
$deploymentPlan = @(Get-BigScreenDeploymentPlan `
    -Manifest $modJson `
    -RuntimeStage $runtimeStage `
    -UseDebug:$useDebug)
$classification = Get-BigScreenInstallClassification ([string]$modJson.packageVersion)
Write-Output "Detected Big Screen installation state: $($classification.State)"

if ($classification.State -eq "MBF_MANAGED" -or
    $classification.State -eq "MBF_REGISTERED_NOT_INSTALLED") {
    throw "Big Screen is registered with ModsBeforeFriday for this Beat Saber version. Source deployment is refused. Remove Big Screen from MBF's package list first; no Quest files were changed."
}
if ($classification.State -eq "MIXED_OR_AMBIGUOUS") {
    Write-BigScreenOwnershipDiagnostic $classification
    throw "Big Screen's source/MBF ownership is mixed or ambiguous. No Quest files were changed. Run Remove-BigScreen.bat or remove the MBF package before deploying source."
}

$priorReceipt = $null
if ($classification.State -eq "SOURCE_PARTIAL") {
    Assert-BigScreenPartialRecoverable $classification.PartialReceipt
    $priorReceipt = $classification.PartialReceipt
    Write-Output "A recoverable partial source deployment was found. Resuming from its preserved baseline."
} elseif ($classification.State -eq "SOURCE_MANAGED") {
    Assert-BigScreenManagedReceiptSafe $classification.CompleteReceipt
    $priorReceipt = $classification.CompleteReceipt
} elseif ($classification.State -eq "LEGACY_SOURCE") {
    Write-Output ""
    Write-Host "A pre-receipt Big Screen source install was found." -ForegroundColor Yellow
    Write-Output "Its executable and private runtime files must be refreshed once before ownership-safe deployment."
    Write-Output "Settings, downloaded videos, the Video Library, thumbnails, and logs will be preserved."
    $answer = Read-Host "Perform the one-time clean source migration? [Y/N]"
    if ($answer -notmatch '^(?i)y(?:es)?$') {
        Write-Output "No Quest files were changed."
        exit 0
    }
    $legacyPrivatePaths = @($deploymentPlan |
        Where-Object Ownership -eq "BigScreenExclusive" |
        ForEach-Object { [string]$_.Path })
    Remove-BigScreenLegacyExclusivePayload -AdditionalPaths $legacyPrivatePaths
    Remove-BigScreenLegacyRuntimePayload
}

$sourceCommit = (& git rev-parse HEAD 2>$null | Out-String).Trim()
if (-not $sourceCommit) { $sourceCommit = "unknown" }
$receipt = New-BigScreenSourceReceipt `
    -Plan $deploymentPlan `
    -Manifest $modJson `
    -SourceCommit $sourceCommit `
    -PriorReceipt $priorReceipt `
    -BuildType $(if ($useDebug) { "Debug" } else { "Release" })

Write-Output ""
Write-Output "Deploying Big Screen's native libraries and embedded downloader runtime to the Quest. Many files are copied individually, so this can take a few minutes on some USB connections."

# A receipt is written before the first destination changes, updated after
# every verified copy, and promoted only after the complete manifest succeeds.
# Repeated deployments retain the original pre-source baseline. Retired paths
# are removed/restored only when the prior installed hash proves ownership.
Install-BigScreenSourcePlan `
    -Receipt $receipt `
    -PriorReceipt $priorReceipt `
    -CurrentPlan $deploymentPlan


& $PSScriptRoot/restart-game.ps1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($log -eq $true) {
    & $PSScriptRoot/start-logging.ps1 -self:$self -all:$all -custom:$custom -file:$file
}
