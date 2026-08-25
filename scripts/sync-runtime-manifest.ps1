# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

function Write-BigScreenUtf8NoBom {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path,

        [Parameter(Mandatory = $true)]
        [string] $Content
    )

    # Windows PowerShell 5.1's `Set-Content -Encoding UTF8` prepends a byte
    # order mark. PowerShell and QPM accept that marker, but Mods Before
    # Friday's strict JSON reader treats it as an invalid first token and
    # reports "expected value at line 1 column 1". Use the explicit .NET
    # encoding so every generated manifest is portable across both parsers.
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $encoding)
}

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
    # Compact JSON avoids PowerShell 5.1/Core indentation and newline
    # differences. Property order comes from the generated manifest and the
    # explicitly ordered fileCopies array, so both hosts write identical bytes.
    $serializedModJson = $modJson | ConvertTo-Json -Depth 10 -Compress
    Write-BigScreenUtf8NoBom -Path $ModJsonPath -Content $serializedModJson

    # Write-Host keeps this progress message out of the function's return
    # pipeline; QMOD packaging consumes that pipeline as the source-path list.
    Write-Host "Synchronized $($copies.Count) Big Screen runtime files with mod.json."
    return $sourcePaths
}
