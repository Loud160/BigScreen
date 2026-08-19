# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Shared ADB target selection is intentionally isolated from the stricter
# source-ownership policy module. copy.ps1 needs to select the Quest before it
# builds or validates the QMOD, and dot-sourcing the ownership module that early
# previously leaked its StrictMode setting into manifest validation.
function Resolve-BigScreenAdbTargetFromListing($Lines) {
    $authorized = New-Object System.Collections.Generic.List[string]
    $unauthorized = New-Object System.Collections.Generic.List[string]
    foreach ($line in @($Lines)) {
        $text = [string]$line
        if ($text -notmatch '^\s*(?<serial>\S+)\s+(?<state>device|unauthorized|offline|no permissions)\b') {
            continue
        }
        switch ($Matches.state) {
            "device" { $authorized.Add($Matches.serial) }
            "unauthorized" { $unauthorized.Add($Matches.serial) }
        }
    }
    if ($authorized.Count -eq 0) {
        $detail = if ($unauthorized.Count -gt 0) {
            " One connected device is waiting for USB-debugging authorization; put on the headset and accept the prompt."
        } else { "" }
        throw "No authorized Quest was available through ADB. Connect one headset and accept its USB debugging prompt.$detail"
    }
    if ($authorized.Count -gt 1) {
        throw "More than one authorized Android device is connected. Disconnect every device except the Quest before source deployment."
    }
    return $authorized[0]
}

function Select-BigScreenAdbTarget([string]$Purpose = "Big Screen operation") {
    # ADB refuses an unqualified command when one authorized Quest and even one
    # unauthorized/offline Android device coexist. Resolve the sole authorized
    # serial once, then set ADB's process-local selector so every helper call is
    # deterministic without leaking a machine-wide ANDROID_SERIAL setting.
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $listing = & adb devices 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($code -ne 0) {
        $detail = ($listing | ForEach-Object { $_.ToString() }) -join "`n"
        throw "ADB could not list devices for $Purpose.`n$detail"
    }
    $serial = Resolve-BigScreenAdbTargetFromListing $listing
    $env:ANDROID_SERIAL = $serial
    Write-Host "Using authorized Quest $serial for $Purpose."
    return $serial
}
