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
& adb get-state | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "No authorized Quest was available through ADB."
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
$modJson = Get-Content "./mod.json" -Raw | ConvertFrom-Json

$modFiles = $modJson.modFiles
$lateModFiles = $modJson.lateModFiles

# A development build can move a native mod between Scotland2's early and late
# phases while an older copy remains in the other folder. Scotland2 treats both
# files as independent mods, which caused Big Screen to initialize its embedded
# CPython runtime twice and abort Beat Saber during startup. The manifest is the
# source of truth: remove only this project's same-named file from the opposite
# phase before copying the selected build.
foreach ($fileName in $modFiles) {
    & adb shell rm -f -- "/sdcard/ModData/com.beatgames.beatsaber/Modloader/mods/$fileName"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
foreach ($fileName in $lateModFiles) {
    & adb shell rm -f -- "/sdcard/ModData/com.beatgames.beatsaber/Modloader/early_mods/$fileName"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

foreach ($fileName in $modFiles) {
    if ($useDebug -eq $true) {
        & adb push build/debug/$fileName /sdcard/ModData/com.beatgames.beatsaber/Modloader/early_mods/$fileName
    } else {
        & adb push build/$fileName /sdcard/ModData/com.beatgames.beatsaber/Modloader/early_mods/$fileName
    }
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

foreach ($fileName in $lateModFiles) {
    if ($useDebug -eq $true) {
        & adb push build/debug/$fileName /sdcard/ModData/com.beatgames.beatsaber/Modloader/mods/$fileName
    } else {
        & adb push build/$fileName /sdcard/ModData/com.beatgames.beatsaber/Modloader/mods/$fileName
    }
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# Project-owned runtime libraries must be deployed with the mod during local
# development. Dependency libraries already installed by QMOD are left alone;
# only files produced in this build directory are pushed. This is especially
# important for both private FFmpeg sets and their decoder backends because Big Screen no longer
# falls back to Hollywood's media runtime.
foreach ($fileName in $modJson.libraryFiles) {
    $builtLibrary = Join-Path "build" $fileName
    $packagedDependency = Join-Path "extern/libs" $fileName
    $librarySource = if (Test-Path -LiteralPath $builtLibrary) {
        $builtLibrary
    } elseif (Test-Path -LiteralPath $packagedDependency) {
        $packagedDependency
    } else {
        $null
    }
    if (-not $librarySource) {
        # Silently skipping a declared library can leave a different ABI on the
        # headset. That exact failure mixed CPython extensions requiring
        # _PyType_AllocNoTrack with an older libpython and disabled downloads.
        throw "No authoritative source was found for required runtime library $fileName"
    }
    & adb push $librarySource "/sdcard/ModData/com.beatgames.beatsaber/Modloader/libs/$fileName"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

# Mirror the QMOD installer's fileCopies during development deployments. A
# native-only push is insufficient for the embedded downloader: its Python
# standard library, CA bundle, yt-dlp baseline, and Big Screen QuickJS provider
# live in the mod-owned Runtime directory. Deriving the source from the fixed
# runtime destination preserves nested certifi/lib-dynload paths without a
# second hand-maintained manifest.
$runtimeDestination = "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Runtime/"
$runtimeStage = Join-Path (Join-Path $PSScriptRoot "..") "build/downloader"
# createqmod.ps1 used to be the only path that staged redistributable notices.
# Direct development deployment must do the same after a clean build because
# mod.json correctly lists those notices as runtime file copies.
& $PSScriptRoot/stage-runtime-notices.ps1
if (-not $?) {
    exit 1
}
foreach ($copy in $modJson.fileCopies) {
    $destination = [string]$copy.destination
    if (-not $destination.StartsWith(
        $runtimeDestination,
        [System.StringComparison]::Ordinal)) {
        throw "Development deployment does not recognize fileCopy destination $destination"
    }
    $relative = $destination.Substring($runtimeDestination.Length).Replace('/', [IO.Path]::DirectorySeparatorChar)
    $source = Join-Path $runtimeStage $relative
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Development deployment could not find staged runtime file $source"
    }
    & adb push $source $destination
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}


& $PSScriptRoot/restart-game.ps1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($log -eq $true) {
    & adb logcat -c
    & $PSScriptRoot/start-logging.ps1 -self:$self -all:$all -custom:$custom -file:$file
}
