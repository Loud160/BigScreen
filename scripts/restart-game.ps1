# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
$ErrorActionPreference = "Stop"

if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    $sideQuestAdb = "$env:ProgramFiles\SideQuest\resources\app.asar.unpacked\build\platform-tools\adb.exe"
    if (Test-Path -LiteralPath $sideQuestAdb) {
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
