# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
$ErrorActionPreference = "Stop"

if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    # Resolve optional SideQuest installations from Windows environment roots;
    # never commit a developer-machine drive or install path.
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
    throw "ADB was not found. Install Android platform-tools or SideQuest before restarting Beat Saber."
}

& adb shell am force-stop com.beatgames.beatsaber
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& adb shell am start com.beatgames.beatsaber/com.unity3d.player.UnityPlayerActivity
exit $LASTEXITCODE
