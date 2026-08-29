# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
Param(
    [Parameter(Mandatory=$false)]
    [String] $BuildDirectory=""
)

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([String]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repositoryRoot "build"
} else {
    $BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
}

$cachePath = Join-Path $BuildDirectory "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $cachePath)) {
    throw "FFmpeg ELF validation requires a configured build directory: $BuildDirectory"
}

$readElfPath = $null
foreach ($line in Get-Content -LiteralPath $cachePath) {
    if ($line -match '^CMAKE_READELF:FILEPATH=(.+)$') {
        $readElfPath = $matches[1]
        break
    }
}
if ([String]::IsNullOrWhiteSpace($readElfPath) -or
    -not (Test-Path -LiteralPath $readElfPath)) {
    throw "The configured Android llvm-readelf tool could not be found."
}

function Read-ElfText {
    Param(
        [Parameter(Mandatory=$true)][String] $File,
        [Parameter(Mandatory=$true)][String[]] $Arguments
    )
    if (-not (Test-Path -LiteralPath $File)) {
        throw "Required native library is missing: $File"
    }
    $output = & $readElfPath @Arguments $File
    if ($LASTEXITCODE -ne 0) {
        throw "llvm-readelf failed for $File"
    }
    return ($output -join "`n")
}

$mainPath = Join-Path $BuildDirectory "libbigscreen.so"
$mainDynamic = Read-ElfText -File $mainPath -Arguments @("-d")
foreach ($backendName in @(
    "libbigscreen-ffmpeg44-backend.so",
    "libbigscreen-ffmpeg9-backend.so")) {
    if ($mainDynamic -notmatch [Regex]::Escape($backendName)) {
        throw "libbigscreen.so does not require $backendName"
    }
}

foreach ($runtime in @(
    @{
        Tag = "44"; OtherTag = "9"; Namespace = "BIGSCREEN44_LIB";
        OtherNamespace = "BIGSCREEN9_LIB";
        Factories = @("CreateFrameDecoder44Backend")
    },
    @{
        Tag = "9"; OtherTag = "44"; Namespace = "BIGSCREEN9_LIB";
        OtherNamespace = "BIGSCREEN44_LIB";
        Factories = @(
            "CreateFrameDecoder9Backend",
            "CreateVideoTranscoder9Backend")
    })) {
    $backendName = "libbigscreen-ffmpeg$($runtime.Tag)-backend.so"
    $backendPath = Join-Path $BuildDirectory $backendName
    $dynamic = Read-ElfText -File $backendPath -Arguments @("-d")
    $versions = Read-ElfText -File $backendPath -Arguments @("--version-info")
    $symbols = Read-ElfText -File $backendPath -Arguments @("--dyn-syms", "--wide")

    foreach ($component in @("avformat", "avcodec", "avutil", "swscale")) {
        $expectedLibrary = "lib$component-bigscreen$($runtime.Tag).so"
        $wrongLibrary = "lib$component-bigscreen$($runtime.OtherTag).so"
        if ($dynamic -notmatch [Regex]::Escape($expectedLibrary)) {
            throw "$backendName does not require $expectedLibrary"
        }
        if ($dynamic -match [Regex]::Escape($wrongLibrary)) {
            throw "$backendName incorrectly requires $wrongLibrary"
        }
    }
    if ($versions -notmatch [Regex]::Escape($runtime.Namespace) -or
        $versions -match [Regex]::Escape($runtime.OtherNamespace)) {
        throw "$backendName is not isolated to $($runtime.Namespace)* symbols"
    }
    foreach ($factory in $runtime.Factories) {
        if ($symbols -notmatch [Regex]::Escape($factory)) {
            throw "$backendName does not export $factory"
        }
    }
}

Write-Output "Dual FFmpeg backend ELF isolation validated."
