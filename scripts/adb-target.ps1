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
function Get-BigScreenAdbDevicesFromListing($Lines) {
    $devices = New-Object System.Collections.Generic.List[object]
    foreach ($line in @($Lines)) {
        $text = [string]$line
        if ($text -notmatch '^\s*(?<serial>\S+)\s+(?<state>device|unauthorized|offline|no permissions)\b') {
            continue
        }
        $serial = $Matches.serial
        $state = $Matches.state
        $metadata = @{}
        foreach ($match in [regex]::Matches($text, '(?<key>product|model|device):(?<value>\S+)')) {
            $metadata[$match.Groups['key'].Value] =
                $match.Groups['value'].Value.Replace('_', ' ')
        }
        $devices.Add([pscustomobject]@{
            Serial = $serial
            State = $state
            Product = [string]$metadata['product']
            Model = [string]$metadata['model']
            Device = [string]$metadata['device']
        })
    }
    return $devices.ToArray()
}

function Resolve-BigScreenAdbTargetFromListing($Lines) {
    # Retained as the transport-only policy used by isolated tests and callers
    # that have no permission to probe a device. The source deployer uses the
    # stronger Quest/Beat Saber classification below when several authorized
    # Android devices are attached.
    $devices = @(Get-BigScreenAdbDevicesFromListing $Lines)
    $authorized = @($devices | Where-Object State -eq 'device')
    $unauthorized = @($devices | Where-Object State -eq 'unauthorized')
    if ($authorized.Count -eq 0) {
        $detail = if ($unauthorized.Count -gt 0) {
            " One connected device is waiting for USB-debugging authorization; put on the headset and accept the prompt."
        } else { "" }
        throw "No authorized Quest was available through ADB. Connect one headset and accept its USB debugging prompt.$detail"
    }
    if ($authorized.Count -gt 1) {
        throw "More than one authorized Android device is connected. Disconnect every device except the Quest before source deployment."
    }
    return $authorized[0].Serial
}

function Invoke-BigScreenAdbTargetProbe(
    [string]$Serial,
    [string[]]$Arguments,
    [string]$AdbCommand = 'adb') {
    $previous = $ErrorActionPreference
    try {
        # Every probe is explicitly serial-qualified. This is the important
        # safety property when a phone and Quest are both authorized.
        $ErrorActionPreference = "Continue"
        $output = & $AdbCommand -s $Serial @Arguments 2>&1
        if ($LASTEXITCODE -ne 0) { return "" }
        return (($output | ForEach-Object { $_.ToString() }) -join "`n").Trim()
    } finally {
        $ErrorActionPreference = $previous
    }
}

function Test-BigScreenQuestIdentity([string]$Manufacturer, [string]$Model) {
    # Current standalone headsets identify their manufacturer as Oculus/Meta
    # and their model as Quest. Accept either identity signal because firmware
    # revisions have changed the manufacturer string over the product line.
    return $Manufacturer -match '(?i)^(oculus|meta)(?:\s|$)' -or
        $Model -match '(?i)(?:^|\s)quest(?:\s|$)'
}

function Get-BigScreenQuestCandidates($Devices, [string]$AdbCommand = 'adb') {
    $candidates = New-Object System.Collections.Generic.List[object]
    foreach ($device in @($Devices | Where-Object State -eq 'device')) {
        $model = Invoke-BigScreenAdbTargetProbe $device.Serial `
            @('shell', 'getprop', 'ro.product.model') $AdbCommand
        $manufacturer = Invoke-BigScreenAdbTargetProbe $device.Serial `
            @('shell', 'getprop', 'ro.product.manufacturer') $AdbCommand
        $product = Invoke-BigScreenAdbTargetProbe $device.Serial `
            @('shell', 'getprop', 'ro.product.name') $AdbCommand
        $beatSaberPath = Invoke-BigScreenAdbTargetProbe $device.Serial `
            @('shell', 'pm', 'path', 'com.beatgames.beatsaber') $AdbCommand

        if (-not $model) { $model = $device.Model }
        if (-not $product) { $product = $device.Product }
        $isQuest = Test-BigScreenQuestIdentity $manufacturer $model
        $hasBeatSaber = $beatSaberPath -match '(?m)^package:'
        if (-not $isQuest -or -not $hasBeatSaber) {
            Write-Host (
                "Ignoring Android device {0} ({1}); it is not an authorized Quest with Beat Saber installed." -f
                $device.Serial,
                $(if ($model) { $model } else { 'unknown model' })) `
                -ForegroundColor DarkGray
            continue
        }

        $candidates.Add([pscustomobject]@{
            Serial = [string]$device.Serial
            Model = $(if ($model) { $model } else { 'Meta Quest' })
            Manufacturer = $manufacturer
            Product = $product
        })
    }
    return $candidates.ToArray()
}

function Select-BigScreenQuestCandidate($Candidates, [switch]$NonInteractive) {
    $choices = @($Candidates)
    if ($choices.Count -eq 0) { return $null }
    if ($choices.Count -eq 1) { return $choices[0] }

    $description = ($choices | ForEach-Object {
        "{0} ({1})" -f $_.Model, $_.Serial
    }) -join ', '
    if ($NonInteractive -or $env:CI) {
        throw "More than one Quest with Beat Saber is connected: $description. Interactive selection is disabled, so no device was chosen."
    }

    Write-Host ""
    Write-Host "More than one Quest with Beat Saber is connected:" -ForegroundColor Yellow
    for ($index = 0; $index -lt $choices.Count; ++$index) {
        Write-Host ("  [{0}] {1} - serial {2}" -f
            ($index + 1), $choices[$index].Model, $choices[$index].Serial)
    }
    while ($true) {
        $answer = Read-Host "Choose the Quest to use [1-$($choices.Count)] (or press Enter to cancel)"
        if ([string]::IsNullOrWhiteSpace($answer)) {
            throw "Quest selection was cancelled. No device was changed."
        }
        $selection = 0
        if ([int]::TryParse($answer, [ref]$selection) -and
            $selection -ge 1 -and $selection -le $choices.Count) {
            return $choices[$selection - 1]
        }
        Write-Host "Enter a number from 1 through $($choices.Count)." -ForegroundColor Yellow
    }
}

function Select-BigScreenAdbTarget(
    [string]$Purpose = "Big Screen operation",
    [switch]$NonInteractive,
    [string]$AdbCommand = 'adb') {
    # List extended device metadata, then explicitly probe each authorized
    # serial. Phones no longer block deployment merely because their ADB link
    # is active. Multiple actual Quests remain an explicit user decision.
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $listing = & $AdbCommand devices -l 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($code -ne 0) {
        $detail = ($listing | ForEach-Object { $_.ToString() }) -join "`n"
        throw "ADB could not list devices for $Purpose.`n$detail"
    }

    $devices = @(Get-BigScreenAdbDevicesFromListing $listing)
    $authorized = @($devices | Where-Object State -eq 'device')
    $unauthorized = @($devices | Where-Object State -eq 'unauthorized')
    if ($authorized.Count -eq 0) {
        $detail = if ($unauthorized.Count -gt 0) {
            " One connected device is waiting for USB-debugging authorization; put on the headset and accept the prompt."
        } else { "" }
        throw "No authorized Quest was available through ADB. Connect one headset and accept its USB debugging prompt.$detail"
    }

    $questCandidates = @(Get-BigScreenQuestCandidates $devices $AdbCommand)
    if ($questCandidates.Count -eq 0) {
        throw "ADB found authorized Android device(s), but none identified as a Quest with Beat Saber installed. No device was changed."
    }
    $selected = Select-BigScreenQuestCandidate $questCandidates `
        -NonInteractive:$NonInteractive
    $env:ANDROID_SERIAL = $selected.Serial
    Write-Host (
        "Using {0} ({1}) for {2}." -f
        $selected.Model, $selected.Serial, $Purpose) -ForegroundColor Green
    return $selected.Serial
}
