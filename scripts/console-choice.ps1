# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Cross-platform timed Y/N input for ADB cleanup. Windows retains choice.exe;
# native Linux uses Console.ReadKey polling so a missing response can safely
# default to No without leaving a launcher blocked forever.
function Read-BigScreenTimedYesNo(
    [string]$Prompt,
    [int]$TimeoutSeconds = 300) {
    $choiceCommand = Get-Command choice.exe -ErrorAction SilentlyContinue
    if ($choiceCommand) {
        $previous = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & $choiceCommand.Source /C YN /N /T $TimeoutSeconds /D N /M $Prompt |
                Out-Host
            $choiceExitCode = $LASTEXITCODE
            return $choiceExitCode -eq 1
        } finally {
            $ErrorActionPreference = $previous
        }
    }

    Write-Host -NoNewline $Prompt
    try {
        if ([Console]::IsInputRedirected) {
            Write-Host "N"
            return $false
        }
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while ([DateTime]::UtcNow -lt $deadline) {
            if ([Console]::KeyAvailable) {
                $key = [Console]::ReadKey($true)
                if ($key.KeyChar -match '^[Yy]$') {
                    Write-Host "Y"
                    return $true
                }
                if ($key.KeyChar -match '^[Nn]$' -or
                    $key.Key -eq [ConsoleKey]::Enter) {
                    Write-Host "N"
                    return $false
                }
            }
            Start-Sleep -Milliseconds 100
        }
    } catch {
        # A redirected or non-console host cannot supply an interactive answer.
        # Defaulting to No preserves the documented no-surprise behavior.
    }
    Write-Host "N"
    return $false
}
