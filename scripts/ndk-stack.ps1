# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
Param(
    [Parameter(Mandatory=$false)]
    [String] $logName = "RecentCrash.log",

    [Parameter(Mandatory=$false)]
    [Switch] $help
)

if ($help -eq $true) {
    Write-Output "`"NDK-Stack`" - Processes a tombstone using the debug .so to find file locations"
    Write-Output "`n-- Arguments --`n"
    
    Write-Output "LogName `t`t The file name of the tombstone to process"

    exit
}

if (Test-Path "./ndkpath.txt") {
    $NDKPath = Get-Content ./ndkpath.txt
} else {
    $NDKPath = $ENV:ANDROID_NDK_HOME
}

$stackScript = "$NDKPath/ndk-stack"
if (-not ($PSVersionTable.PSEdition -eq "Core")) {
    $stackScript += ".cmd"
}

Get-Content $logName | & $stackScript -sym ./build/debug/ > "$($logName)_processed.log"
