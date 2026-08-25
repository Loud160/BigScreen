# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
. (Join-Path $root "scripts/deterministic-zip.ps1")

try {
    Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue
    Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction SilentlyContinue

    $testRoot = Join-Path ([IO.Path]::GetTempPath()) (
        "BigScreen-DeterministicZipTests-" + [Guid]::NewGuid().ToString("N"))
    [IO.Directory]::CreateDirectory($testRoot) | Out-Null

    $first = Join-Path $testRoot "first.bin"
    $second = Join-Path $testRoot "second.txt"
    [IO.File]::WriteAllBytes($first, [byte[]](0, 1, 2, 3, 254, 255))
    [IO.File]::WriteAllText(
        $second,
        (("Big Screen deterministic archive test`n" * 4096)),
        [Text.UTF8Encoding]::new($false))

    # Source timestamps and caller order must never influence package bytes.
    [IO.File]::SetLastWriteTimeUtc($first, [DateTime]::new(2024, 5, 6, 7, 8, 9,
        [DateTimeKind]::Utc))
    [IO.File]::SetLastWriteTimeUtc($second, [DateTime]::new(2030, 1, 2, 3, 4, 5,
        [DateTimeKind]::Utc))

    $archiveA = Join-Path $testRoot "a.zip"
    $archiveB = Join-Path $testRoot "b.zip"
    Write-BigScreenDeterministicZip -DestinationPath $archiveA `
        -SourcePaths @($second, $first) `
        -EntryNames @("z/second.txt", "a/first.bin")
    Write-BigScreenDeterministicZip -DestinationPath $archiveB `
        -SourcePaths @($first, $second) `
        -EntryNames @("a/first.bin", "z/second.txt")

    $hashA = (Get-FileHash -LiteralPath $archiveA -Algorithm SHA256).Hash
    $hashB = (Get-FileHash -LiteralPath $archiveB -Algorithm SHA256).Hash
    if ($hashA -ne $hashB) {
        throw "Deterministic ZIP bytes changed with caller order: $hashA != $hashB"
    }

    $archive = [IO.Compression.ZipFile]::OpenRead($archiveA)
    try {
        $names = @($archive.Entries | ForEach-Object { $_.FullName })
        if (($names -join "|") -ne "a/first.bin|z/second.txt") {
            throw "ZIP entries were not written in ordinal order: $($names -join ', ')"
        }
        $compressedEntries = @($archive.Entries | Where-Object {
            $_.CompressedLength -lt $_.Length
        })
        if ($compressedEntries.Count -eq 0) {
            throw "The deterministic ZIP writer did not use standard DEFLATE compression."
        }
        foreach ($entry in $archive.Entries) {
            if ($entry.LastWriteTime.Year -ne 2000 -or
                $entry.LastWriteTime.Month -ne 1 -or
                $entry.LastWriteTime.Day -ne 1) {
                throw "ZIP entry did not use the fixed package timestamp: $($entry.FullName)"
            }
        }

        $firstEntry = $archive.GetEntry("a/first.bin")
        $stream = $firstEntry.Open()
        try {
            $bytes = [byte[]]::new(6)
            if ($stream.Read($bytes, 0, $bytes.Length) -ne $bytes.Length -or
                ($bytes -join ",") -ne "0,1,2,3,254,255") {
                throw "ZIP entry content did not round-trip."
            }
        }
        finally {
            $stream.Dispose()
        }
    }
    finally {
        $archive.Dispose()
    }

    $duplicateRejected = $false
    try {
        Write-BigScreenDeterministicZip `
            -DestinationPath (Join-Path $testRoot "duplicate.zip") `
            -SourcePaths @($first, $second) `
            -EntryNames @("duplicate.bin", "duplicate.bin")
    }
    catch {
        $duplicateRejected = $_.Exception.Message -like "*Duplicate ZIP entry name*"
    }
    if (-not $duplicateRejected) {
        throw "Deterministic ZIP writer accepted duplicate entry names."
    }

    Write-Host "Deterministic ZIP tests passed: $hashA"
}
finally {
    if ($testRoot -and (Test-Path -LiteralPath $testRoot)) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
