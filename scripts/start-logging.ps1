# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
Param(
    [Parameter(Mandatory=$false)]
    [Switch] $self,

    [Parameter(Mandatory=$false)]
    [Switch] $all,

    [Parameter(Mandatory=$false)]
    [String] $custom="",

    [Parameter(Mandatory=$false)]
    [String] $file="",

    [Parameter(Mandatory=$false)]
    [Switch] $help,

    [Parameter(Mandatory=$false)]
    [Switch] $excludeHeader
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "adb-target.ps1")

if ($help -eq $true) {
    if ($excludeHeader -eq $false) {
        Write-Output "`"Start-Logging`" - Logs Beat Saber using `"adb logcat`""
        Write-Output "`n-- Arguments --`n"
    }

    Write-Output "-Self `t`t Only Logs your mod and Crashes"
    Write-Output "-All `t`t Logs everything, including logs made by the Quest itself"
    Write-Output "-Custom `t Specify a specific logging pattern, e.g `"custom-types|questui`""
    Write-Output "`t`t NOTE: The pattern `"AndroidRuntime|CRASH|scotland2|Unity`" is always appended to a custom pattern"
    Write-Output "-File `t`t Saves the output of the log to the file name given"

    exit
}

[void](Select-BigScreenAdbTarget "live Beat Saber logging")

function Get-BeatSaberProcessId {
    $text = ((& adb shell pidof com.beatgames.beatsaber 2>&1) |
        ForEach-Object { $_.ToString() }) -join "`n"
    return $text.Trim()
}

$bspid = Get-BeatSaberProcessId
$logcatArguments = @("logcat")

if ($all -eq $false) {
    $loops = 0
    while ([string]::IsNullOrEmpty($bspid) -and $loops -lt 3) {
        Start-Sleep -Milliseconds 100
        $bspid = Get-BeatSaberProcessId
        $loops += 1
    }

    if ([string]::IsNullOrEmpty($bspid)) {
        Write-Output "Could not connect to adb, exiting..."
        exit 1
    }

    $logcatArguments += @("--pid", $bspid)
}

$pattern = $null
if ($all -eq $false) {
    $pattern = "("
    if ($self -eq $true) {
        $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
        Push-Location $repoRoot
        try {
            & $PSScriptRoot/validate-modjson.ps1
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        } finally {
            Pop-Location
        }
        $modID = (Get-Content (Join-Path $repoRoot "mod.json") -Raw |
            ConvertFrom-Json).id
        $pattern += "$modID|"
    }
    if (![string]::IsNullOrEmpty($custom)) {
        $pattern += "$custom|"
    }
    if ($pattern -eq "(") {
        $pattern = "( INFO| DEBUG| WARN| ERROR| CRITICAL|"
    }
    $pattern += "AndroidRuntime|CRASH|scotland2|Unity  )"
}

$outputPath = $null
if (![string]::IsNullOrEmpty($file)) {
    $outputPath = if ([IO.Path]::IsPathRooted($file)) {
        $file
    } else {
        Join-Path $PSScriptRoot $file
    }
}

Write-Output (
    "Logging selected Quest with adb {0}{1}{2}" -f
    ($logcatArguments -join " "),
    $(if ($pattern) { " and filter '$pattern'" } else { "" }),
    $(if ($outputPath) { " into '$outputPath'" } else { "" }))
& adb logcat -c
if ($LASTEXITCODE -ne 0) { throw "ADB could not clear the current logcat buffer." }

# Keep the live pipeline as real process arguments and PowerShell commands.
# Caller-provided regex/file text is never evaluated as PowerShell code.
if ($pattern -and $outputPath) {
    & adb @logcatArguments 2>&1 |
        Select-String -Pattern $pattern |
        Out-File -LiteralPath $outputPath
} elseif ($pattern) {
    & adb @logcatArguments 2>&1 | Select-String -Pattern $pattern
} elseif ($outputPath) {
    & adb @logcatArguments 2>&1 | Out-File -LiteralPath $outputPath
} else {
    & adb @logcatArguments
}
