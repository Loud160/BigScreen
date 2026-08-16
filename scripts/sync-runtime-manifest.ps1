# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

function Sync-BigScreenRuntimeManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ModJsonPath,

        [Parameter(Mandatory = $true)]
        [string] $RuntimeStage
    )

    # Direct ADB deployment and QMOD packaging must install the identical
    # downloader runtime. Keeping the manifest construction here prevents a
    # fresh development deployment from silently relying on files left behind
    # by an earlier QMOD install.
    $runtimeDestination =
        "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Runtime/"
    $runtimeFiles = @(
        "python314.zip",
        "yt-dlp-shipped",
        "certifi.whl",
        "runtime-manifest.json",
        "CPYTHON-LICENSE.txt",
        "bigscreen_jsc_provider.py",
        "BIGSCREEN-LICENSE.txt",
        "BIGSCREEN-ADDITIONAL-TERMS.md",
        "BIGSCREEN-NOTICE.txt",
        "THIRD-PARTY-NOTICES.md",
        "FFMPEG-LGPL-2.1-OR-LATER.txt",
        "FFMPEG-4.4.8-BUILD-INFO.txt",
        "FFMPEG-4.4.8-CHANGES.diff",
        "FFMPEG-9.0.1-BUILD-INFO.txt",
        "FFMPEG-9.0.1-CHANGES.diff",
        "CERTIFI-MPL-2.0.txt",
        "MPL-2.0.txt",
        "YT-DLP-UNLICENSE.txt",
        "QUICKJS-NG-MIT.txt",
        "OPENSSL-APACHE-2.0.txt",
        "SQLITE-PUBLIC-DOMAIN.txt"
    )

    if (-not (Test-Path -LiteralPath $ModJsonPath -PathType Leaf)) {
        throw "Runtime manifest synchronization could not find $ModJsonPath"
    }
    if (-not (Test-Path -LiteralPath $RuntimeStage -PathType Container)) {
        throw "Runtime manifest synchronization could not find $RuntimeStage"
    }

    $copies = @()
    $sourcePaths = @()
    foreach ($name in $runtimeFiles) {
        $source = Join-Path $RuntimeStage $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Required Big Screen runtime file is missing: $source"
        }
        $copies += [PSCustomObject]@{
            name = $name
            destination = $runtimeDestination + $name
        }
        $sourcePaths += $source
    }

    foreach ($subdirectory in @("certifi", "lib-dynload")) {
        $directory = Join-Path $RuntimeStage $subdirectory
        if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
            throw "Required Big Screen runtime directory is missing: $directory"
        }
        Get-ChildItem -LiteralPath $directory -File |
            Where-Object {
                $subdirectory -ne "lib-dynload" -or $_.Extension -eq ".so"
            } |
            Sort-Object Name |
            ForEach-Object {
                $copies += [PSCustomObject]@{
                    name = $_.Name
                    destination = $runtimeDestination +
                        "$subdirectory/$($_.Name)"
                }
                $sourcePaths += $_.FullName
            }
    }

    # QMOD entries are flat and use `name`, so two runtime files with the same
    # basename would overwrite one another even if their destinations differ.
    $duplicates = @($copies.name | Group-Object | Where-Object Count -gt 1)
    if ($duplicates.Count -gt 0) {
        throw "Runtime manifest contains duplicate archive names: $($duplicates.Name -join ', ')"
    }

    $modJson = Get-Content -LiteralPath $ModJsonPath -Raw |
        ConvertFrom-Json -ErrorAction Stop
    if ($modJson.PSObject.Properties["fileCopies"]) {
        $modJson.fileCopies = @($copies)
    }
    else {
        $modJson | Add-Member -NotePropertyName fileCopies -NotePropertyValue @($copies)
    }
    $modJson | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath $ModJsonPath -Encoding UTF8

    # Write-Host keeps this progress message out of the function's return
    # pipeline; QMOD packaging consumes that pipeline as the source-path list.
    Write-Host "Synchronized $($copies.Count) Big Screen runtime files with mod.json."
    return $sourcePaths
}
