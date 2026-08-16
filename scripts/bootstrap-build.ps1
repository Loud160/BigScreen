# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

<#
.SYNOPSIS
Restores the pinned build inputs required by Build-And-Deploy.bat.

.DESCRIPTION
A source archive intentionally excludes generated QPM dependencies and both
host-specific Android NDK installations. This preflight makes the root BAT
work from a fresh clone or downloaded source archive by restoring those inputs
before CMake runs. Direct artifact downloads remain version-pinned and
hash-verified by their owning scripts; this file does not weaken or duplicate
those checks.
#>

$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Find-QpmExecutable {
    $command = Get-Command qpm -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @()
    if ($env:LOCALAPPDATA) {
        $candidates += Join-Path $env:LOCALAPPDATA "Programs/QPM/qpm.exe"
    }
    foreach ($programRoot in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ($programRoot) {
            $candidates += Join-Path $programRoot "QPM/qpm.exe"
        }
    }

    return $candidates |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
}

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Description,

        [Parameter(Mandatory = $true)]
        [scriptblock] $Command
    )

    Write-Output ""
    Write-Output "--- $Description ---"
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Get-ValidPinnedWindowsNdk {
    param(
        [Parameter(Mandatory = $true)]
        [string] $PathFile
    )

    if (-not (Test-Path -LiteralPath $PathFile)) {
        return $null
    }
    $candidate = (Get-Content -LiteralPath $PathFile -Raw).Trim()
    if (-not $candidate) {
        return $null
    }
    $properties = Join-Path $candidate "source.properties"
    $toolchain = Join-Path $candidate "build/cmake/android.toolchain.cmake"
    if (-not (Test-Path -LiteralPath $properties) -or
        -not (Test-Path -LiteralPath $toolchain)) {
        return $null
    }
    if ((Get-Content -LiteralPath $properties -Raw) -notmatch
        '(?m)^Pkg\.Revision\s*=\s*27\.3\.13750724\s*$') {
        return $null
    }
    return $candidate
}

$qpmExecutable = Find-QpmExecutable
if (-not $qpmExecutable) {
    throw @"
QPM CLI was not found. Install QPM before building Big Screen:
https://github.com/QuestPackageManager/QPM.CLI

After installation, re-run Build-And-Deploy.bat. The launcher recognizes QPM
from PATH or its standard per-user Windows installation directory.
"@
}

Push-Location $repositoryRoot
try {
    Write-Output "Using QPM: $qpmExecutable"

    # Source archives do not include QPM's generated extern/ directory. A
    # successful restore records the lockfile hash in the ignored .cache
    # directory. Re-run QPM only when the lock changes or a required generated
    # input is missing; this avoids network/package work on every BAT launch.
    $qpmLock = Join-Path $repositoryRoot "qpm.shared.json"
    $qpmRestoreCache = Join-Path $repositoryRoot ".cache"
    $qpmRestoreStamp = Join-Path $qpmRestoreCache "qpm-restore.sha256"
    $qpmLockHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $qpmLock).Hash.ToLowerInvariant()
    $requiredQpmInputs = @(
        "extern.cmake",
        "qpm_defines.cmake",
        "extern/includes/beatsaber-hook",
        "extern/includes/bs-cordl",
        "extern/includes/bsml",
        "extern/includes/custom-types",
        "extern/includes/fmt",
        "extern/includes/libil2cpp",
        "extern/includes/paper2_scotland2",
        "extern/includes/rapidjson",
        "extern/includes/scotland2",
        "extern/includes/songcore",
        "extern/includes/tinyxml2",
        "extern/libs/libbeatsaber-hook.so",
        "extern/libs/libbsml.so",
        "extern/libs/libcustom-types.so",
        "extern/libs/libpaper2_scotland2.so",
        "extern/libs/libsl2.so",
        "extern/libs/libsongcore.so")
    $qpmInputsComplete = $requiredQpmInputs | ForEach-Object {
        Test-Path -LiteralPath (Join-Path $repositoryRoot $_)
    } | Where-Object { -not $_ } | Select-Object -First 1
    $qpmStampMatches = (Test-Path -LiteralPath $qpmRestoreStamp) -and
        ((Get-Content -LiteralPath $qpmRestoreStamp -Raw).Trim() -eq $qpmLockHash)
    if ($qpmStampMatches -and $null -eq $qpmInputsComplete) {
        Write-Output "Using QPM dependencies already restored for the current lockfile."
    } else {
        Invoke-CheckedCommand "Restore pinned Quest headers and libraries" {
            & $qpmExecutable restore
        }
        foreach ($relativeInput in $requiredQpmInputs) {
            $restoredInput = Join-Path $repositoryRoot $relativeInput
            if (-not (Test-Path -LiteralPath $restoredInput)) {
                throw "QPM restore completed without required input $relativeInput"
            }
        }
        New-Item -ItemType Directory -Force -Path $qpmRestoreCache | Out-Null
        $qpmLockHash | Set-Content -LiteralPath $qpmRestoreStamp -Encoding Ascii
    }

    # QPM owns the Windows-host NDK used by CMake/Clang. Resolve writes the
    # portable, ignored ndkpath.txt consumed by android-ndk.cmake and downloads
    # r27d only when the matching cache is absent.
    $ndkPathFile = Join-Path $repositoryRoot "ndkpath.txt"
    $windowsNdk = Get-ValidPinnedWindowsNdk -PathFile $ndkPathFile
    if ($windowsNdk) {
        Write-Output "Using installed Windows Android NDK r27d: $windowsNdk"
    } else {
        Invoke-CheckedCommand "Resolve the Windows Android NDK r27d" {
            & $qpmExecutable ndk resolve --download
        }
        $windowsNdk = Get-ValidPinnedWindowsNdk -PathFile $ndkPathFile
    }
    if (-not $windowsNdk) {
        throw "QPM did not provide a complete Android NDK r27d (27.3.13750724). Run 'qpm doctor' for details."
    }
    Write-Output "Verified Windows Android NDK r27d: $windowsNdk"

    # FFmpeg's configure/make build runs inside Linux, so it cannot use QPM's
    # Windows-host NDK binaries. Install the matching official Linux archive in
    # WSL's user cache. The shell script reuses an existing verified archive.
    $wslCommand = Get-Command wsl.exe -ErrorAction SilentlyContinue
    if (-not $wslCommand) {
        throw @"
WSL 2 was not found. Install WSL with Ubuntu, then install the Linux build tools:
  wsl --install -d Ubuntu
  sudo apt update
  sudo apt install -y build-essential curl xz-utils unzip
"@
    }

    $requiredLinuxTools = @(
        "bash", "curl", "grep", "make", "nproc", "sed", "sha256sum",
        "tar", "unzip", "xz")
    $toolProbe = 'for tool in "$@"; do command -v "$tool" >/dev/null 2>&1 || printf "%s\n" "$tool"; done'
    $missingLinuxTools = @(
        & $wslCommand.Source -- bash -c $toolProbe bootstrap-probe @requiredLinuxTools
    ) | ForEach-Object { ([string]$_).Trim() } | Where-Object { $_ }
    if ($LASTEXITCODE -ne 0) {
        throw "WSL could not run the Linux prerequisite check. Open the distribution once, finish its setup, and retry."
    }
    if ($missingLinuxTools.Count -gt 0) {
        throw @"
WSL is missing required build tools: $($missingLinuxTools -join ', ')
Inside Ubuntu run:
  sudo apt update
  sudo apt install -y build-essential curl xz-utils unzip
Then re-run Build-And-Deploy.bat.
"@
    }

    $windowsInstallScript = (Resolve-Path (
        Join-Path $PSScriptRoot "install-pinned-ndk.sh")).Path
    $portableInstallScript = $windowsInstallScript.Replace('\', '/')
    $linuxInstallScript = (& $wslCommand.Source -- wslpath -a $portableInstallScript |
        Out-String).Trim()
    if (-not $linuxInstallScript) {
        throw "WSL could not translate the Linux NDK installer path $windowsInstallScript"
    }
    Invoke-CheckedCommand "Verify or install the Linux Android NDK r27d for FFmpeg" {
        & $wslCommand.Source -- bash $linuxInstallScript
    }

    Write-Output ""
    Write-Output "Build prerequisites are ready. Starting the Big Screen build and Quest deployment..."
}
finally {
    Pop-Location
}
