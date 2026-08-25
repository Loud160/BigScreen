# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# PowerShell 5.1 and PowerShell 7 use different DEFLATE implementations, and
# native ZIP writers record host-specific metadata. Build and use Big Screen's
# tracked miniz-based host utility instead. Its ordinal ordering, fixed ZIP32
# headers, and deterministic raw DEFLATE streams make the package portable and
# reproducible without requiring or downloading a separate archive program.

function Write-BigScreenDeterministicZip {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $DestinationPath,

        [Parameter(Mandatory = $true)]
        [string[]] $SourcePaths,

        [Parameter(Mandatory = $true)]
        [string[]] $EntryNames,

        [Parameter(Mandatory = $false)]
        [string] $CompressorPath
    )

    if ($SourcePaths.Count -ne $EntryNames.Count) {
        throw "ZIP source paths and entry names must have equal lengths."
    }
    if ($SourcePaths.Count -gt [UInt16]::MaxValue) {
        throw "Deterministic ZIP32 cannot contain more than 65535 entries."
    }

    if ([string]::IsNullOrWhiteSpace($CompressorPath)) {
        if ([string]::IsNullOrWhiteSpace(
            $script:BigScreenDeterministicZipCompressorPath)) {
            $script:BigScreenDeterministicZipCompressorPath = & (
                Join-Path $PSScriptRoot "build-deterministic-zip-tool.ps1")
        }
        $CompressorPath = $script:BigScreenDeterministicZipCompressorPath
    }
    if ([string]::IsNullOrWhiteSpace($CompressorPath) -or
        -not (Test-Path -LiteralPath $CompressorPath -PathType Leaf)) {
        throw "Big Screen's deterministic ZIP compressor was not prepared."
    }

    $destination = [IO.Path]::GetFullPath($DestinationPath)
    $parent = [IO.Path]::GetDirectoryName($destination)
    if ($parent) {
        [IO.Directory]::CreateDirectory($parent) | Out-Null
    }
    $workRoot = $destination + "." + [Guid]::NewGuid().ToString("N") + ".work"
    $stageRoot = Join-Path $workRoot "stage"
    $listPath = Join-Path $workRoot "entries.txt"
    [IO.Directory]::CreateDirectory($stageRoot) | Out-Null

    try {
        $normalizedEntries = @()
        $seen = New-Object 'Collections.Generic.HashSet[string]' (
            [StringComparer]::Ordinal)
        for ($index = 0; $index -lt $SourcePaths.Count; $index++) {
            $source = [IO.Path]::GetFullPath([string]$SourcePaths[$index])
            if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
                throw "Deterministic ZIP input is missing: $source"
            }
            $name = ([string]$EntryNames[$index]).Replace('\', '/')
            if ([string]::IsNullOrWhiteSpace($name) -or
                $name.StartsWith('/', [StringComparison]::Ordinal) -or
                $name.EndsWith('/', [StringComparison]::Ordinal) -or
                $name.Contains('../') -or $name -eq '..' -or
                $name.Contains(':')) {
                throw "Unsafe ZIP entry name: $name"
            }
            if (-not $seen.Add($name)) {
                throw "Duplicate ZIP entry name: $name"
            }

            $staged = Join-Path $stageRoot (
                $name.Replace('/', [IO.Path]::DirectorySeparatorChar))
            $stagedParent = [IO.Path]::GetDirectoryName($staged)
            if ($stagedParent) {
                [IO.Directory]::CreateDirectory($stagedParent) | Out-Null
            }
            [IO.File]::Copy($source, $staged, $true)
            $normalizedEntries += $name
        }

        [Array]::Sort(
            [string[]]$normalizedEntries,
            [StringComparer]::Ordinal)
        [IO.File]::WriteAllText(
            $listPath,
            (($normalizedEntries -join "`n") + "`n"),
            (New-Object Text.UTF8Encoding($false)))

        & $CompressorPath $destination $stageRoot $listPath
        if ($LASTEXITCODE -ne 0 -or
            -not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            throw "Deterministic ZIP compression failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        if (Test-Path -LiteralPath $workRoot -PathType Container) {
            Remove-Item -LiteralPath $workRoot -Recurse -Force
        }
    }
}
