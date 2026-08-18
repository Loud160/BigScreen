# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
Param(
    [Parameter(Mandatory=$false)]
    [Switch] $Force
)

$ErrorActionPreference = "Stop"

# Big Screen embeds the official CPython Android build instead of relying on
# Termux or an executable that does not exist on a stock Quest. Keep every
# network artifact pinned and hash-checked so a normal source build is
# reproducible and cannot silently package a partial or substituted download.
$pythonVersion = "3.14.7"
$pythonSha256 = "6d50cc3aa66e414a439594089bcdfb5f1264358155c70c1f00471c24cfb477fb"
# Temporary August 2026 recovery baseline: this nightly contains the upstream
# extractor changes used with Big Screen's explicit `-android_vr` client
# exclusion. Do not return this pin to stable 2026.07.04; doing so restores the
# partial-download HTTP 403 failure on Quest.
$ytDlpVersion = "2026.08.18.122307"
$ytDlpSha256 = "7c2e017b19c249447445e776913d54bcea81b85b21b51d50ff36b7b8cae956e1"
$ytDlpRepository = "yt-dlp/yt-dlp-nightly-builds"
$ytDlpEjsVersion = "0.8.0"
$certifiVersion = "2026.7.22"
$certifiSha256 = "62f22742b58a1a33014a2b6b706588a8d7e2a88ae7bd1a6ebe8c992928483775"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
& (Join-Path $PSScriptRoot "initialize-dependency-cache.ps1")
if (-not $?) {
    throw "Could not initialize Big Screen's portable dependency cache."
}
$downloadRoot = Join-Path $repositoryRoot ".cache/dependencies/downloader"
$pythonArchive = Join-Path $downloadRoot "python-$pythonVersion-aarch64-linux-android.tar.gz"
$pythonExtractRoot = Join-Path $downloadRoot "python-$pythonVersion"
$pythonPrefix = Join-Path $pythonExtractRoot "prefix"
$ytDlpPackage = Join-Path $downloadRoot "yt-dlp-$ytDlpVersion"
$certifiPackage = Join-Path $downloadRoot "certifi-$certifiVersion-py3-none-any.whl"
$stageRoot = Join-Path $repositoryRoot "build/downloader"
$nativeLibraryStage = Join-Path $repositoryRoot "extern/libs"
$buildLibraryStage = Join-Path $repositoryRoot "build"

New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null
New-Item -ItemType Directory -Force -Path $nativeLibraryStage | Out-Null
New-Item -ItemType Directory -Force -Path $buildLibraryStage | Out-Null

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Assert-Sha256([string] $Path, [string] $Expected) {
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) {
        throw "SHA-256 mismatch for $Path. Expected $Expected, received $actual."
    }
}

if ($Force -or -not (Test-Path -LiteralPath $pythonArchive)) {
    Write-Output "Downloading CPython $pythonVersion for Android ARM64 from python.org."
    Write-Output "The archive will be verified against its pinned SHA-256 before extraction."
    Invoke-WebRequest `
        -Uri "https://www.python.org/ftp/python/$pythonVersion/python-$pythonVersion-aarch64-linux-android.tar.gz" `
        -OutFile $pythonArchive
} else {
    Write-Output "Using cached CPython $pythonVersion Android archive."
}
Assert-Sha256 $pythonArchive $pythonSha256

if ($Force -and (Test-Path -LiteralPath $pythonExtractRoot)) {
    # The target is a fixed child of this repository's ignored extern folder.
    # Verify that relationship before removing an earlier extraction.
    if ((Split-Path -Parent $pythonExtractRoot) -ne $downloadRoot) {
        throw "Refusing to replace unexpected Python path $pythonExtractRoot"
    }
    Remove-Item -LiteralPath $pythonExtractRoot -Recurse -Force
}
$requiredPythonFiles = @(
    "lib/libpython3.14.so",
    "lib/libssl_python.so",
    "lib/libcrypto_python.so",
    "lib/libsqlite3_python.so",
    "lib/python3.14/os.py",
    "include/python3.14/Python.h"
)
$pythonExtractionComplete = $true
foreach ($relative in $requiredPythonFiles) {
    $candidate = Join-Path $pythonPrefix $relative
    if (-not (Test-Path -LiteralPath $candidate) -or
        (Get-Item -LiteralPath $candidate).Length -eq 0) {
        $pythonExtractionComplete = $false
        break
    }
}
if (-not $pythonExtractionComplete) {
    if (Test-Path -LiteralPath $pythonExtractRoot) {
        if ((Split-Path -Parent $pythonExtractRoot) -ne $downloadRoot) {
            throw "Refusing to repair unexpected Python path $pythonExtractRoot"
        }
        Remove-Item -LiteralPath $pythonExtractRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $pythonExtractRoot | Out-Null
    & tar -xzf $pythonArchive -C $pythonExtractRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Could not extract the official Python Android package."
    }
}

if ($Force -or -not (Test-Path -LiteralPath $ytDlpPackage)) {
    Write-Output "Downloading pinned yt-dlp nightly $ytDlpVersion with bundled yt-dlp-ejs $ytDlpEjsVersion from the official GitHub release."
    Write-Output "The package will be verified against its pinned SHA-256 before use."
    Invoke-WebRequest `
        -Uri "https://github.com/$ytDlpRepository/releases/download/$ytDlpVersion/yt-dlp" `
        -OutFile $ytDlpPackage
} else {
    Write-Output "Using cached yt-dlp $ytDlpVersion package."
}
Assert-Sha256 $ytDlpPackage $ytDlpSha256

# Current yt-dlp YouTube challenge support depends on the separately versioned
# yt-dlp-ejs package. Official standalone yt-dlp releases bundle that package,
# so do not add a second wheel that could drift away from the downloader. Make
# the bundled dependency and both solver payloads a hard packaging requirement.
# yt-dlp's Unix zipimport release prepends a Python shebang. Python's zipfile
# accounts for that prefix, while the .NET Framework ZIP reader used by Windows
# PowerShell 5 expects the first local header at offset zero. Present only the
# ZIP segment to .NET so the build works consistently on PowerShell 5 and 7.
$ytDlpBytes = [System.IO.File]::ReadAllBytes($ytDlpPackage)
$zipStart = -1
for ($index = 0; $index -le $ytDlpBytes.Length - 4; $index++) {
    if ($ytDlpBytes[$index] -eq 0x50 -and
        $ytDlpBytes[$index + 1] -eq 0x4b -and
        $ytDlpBytes[$index + 2] -eq 0x03 -and
        $ytDlpBytes[$index + 3] -eq 0x04) {
        $zipStart = $index
        break
    }
}
if ($zipStart -lt 0) {
    throw "The verified yt-dlp package does not contain a ZIP payload."
}
$ytDlpStream = [System.IO.MemoryStream]::new(
    $ytDlpBytes,
    $zipStart,
    $ytDlpBytes.Length - $zipStart,
    $false,
    $true)
$ytDlpArchive = [System.IO.Compression.ZipArchive]::new(
    $ytDlpStream,
    [System.IO.Compression.ZipArchiveMode]::Read,
    $false)
try {
    foreach ($entryName in @(
        "yt_dlp_ejs/__init__.py",
        "yt_dlp_ejs/_version.py",
        "yt_dlp_ejs/yt/solver/core.min.js",
        "yt_dlp_ejs/yt/solver/lib.min.js"
    )) {
        if ($null -eq $ytDlpArchive.GetEntry($entryName)) {
            throw "The verified yt-dlp package does not contain its required $entryName dependency."
        }
    }

    $versionEntry = $ytDlpArchive.GetEntry("yt_dlp_ejs/_version.py")
    $versionReader = [System.IO.StreamReader]::new($versionEntry.Open())
    try {
        $versionSource = $versionReader.ReadToEnd()
    }
    finally {
        $versionReader.Dispose()
    }
    if ($versionSource -notmatch "__version__\s*=\s*version\s*=\s*'$([regex]::Escape($ytDlpEjsVersion))'") {
        throw "The verified yt-dlp package does not contain expected yt-dlp-ejs $ytDlpEjsVersion."
    }
}
finally {
    $ytDlpArchive.Dispose()
}

if ($Force -or -not (Test-Path -LiteralPath $certifiPackage)) {
    Write-Output "Downloading certifi $certifiVersion from Python Package Index."
    Write-Output "The wheel will be verified against its pinned SHA-256 before use."
    Invoke-WebRequest `
        -Uri "https://files.pythonhosted.org/packages/0b/a7/71ac2cff56fec219ed242bb11b8efb69fcc4bec75db06fb7bfe35de520e6/certifi-$certifiVersion-py3-none-any.whl" `
        -OutFile $certifiPackage
} else {
    Write-Output "Using cached certifi $certifiVersion wheel."
}
Assert-Sha256 $certifiPackage $certifiSha256

# OpenSSL cannot read a PEM file through Python's zipimport path. Extract the
# small production certifi package from the already hash-verified wheel while
# staging the QMOD. Its normal where() function will then return a physical
# on-device path without runtime monkey-patching.
$certifiStage = Join-Path $stageRoot "certifi"
if (Test-Path -LiteralPath $certifiStage) {
    if ((Split-Path -Parent $certifiStage) -ne $stageRoot) {
        throw "Refusing to replace unexpected certifi staging path $certifiStage"
    }
    Remove-Item -LiteralPath $certifiStage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $certifiStage | Out-Null
$certifiArchive = [System.IO.Compression.ZipFile]::OpenRead($certifiPackage)
try {
    foreach ($name in @("__init__.py", "__main__.py", "core.py", "py.typed", "cacert.pem")) {
        $entry = $certifiArchive.GetEntry("certifi/$name")
        if ($null -eq $entry) {
            throw "The verified certifi wheel does not contain certifi/$name."
        }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile(
            $entry,
            (Join-Path $certifiStage $name))
    }
}
finally {
    $certifiArchive.Dispose()
}

# Link-time/native-loader files remain ordinary QMOD libraries. Pure Python is
# kept in one zip and yt-dlp in its upstream zipimport form, which means a
# downloader update replaces roughly 3 MB rather than another Python runtime.
$pythonLib = Join-Path $pythonPrefix "lib"
$pythonNativeLibraries = @(
    "libpython3.14.so",
    "libssl_python.so",
    "libcrypto_python.so",
    "libsqlite3_python.so"
)
$pythonNativeLibraries | ForEach-Object {
    $sourceLibrary = Join-Path $pythonLib $_
    # QMOD packaging and direct deployment read build/. Only libpython is a
    # direct dependency of libbigscreen; OpenSSL and SQLite are loaded by the
    # CPython extension modules at runtime and must not be swept into qpm's
    # generated link glob as unnecessary DT_NEEDED entries.
    if ($_ -eq "libpython3.14.so") {
        Copy-Item -LiteralPath $sourceLibrary -Destination $nativeLibraryStage -Force
    }
    Copy-Item -LiteralPath $sourceLibrary -Destination $buildLibraryStage -Force
}
foreach ($runtimeOnlyLibrary in @(
    "libssl_python.so", "libcrypto_python.so", "libsqlite3_python.so")) {
    $staleLinkCopy = Join-Path $nativeLibraryStage $runtimeOnlyLibrary
    if (Test-Path -LiteralPath $staleLinkCopy) {
        Remove-Item -LiteralPath $staleLinkCopy -Force
    }
}

$stdlibRoot = Join-Path $pythonLib "python3.14"
$stdlibZip = Join-Path $stageRoot "python314.zip"
if ($Force -or
    -not (Test-Path -LiteralPath $stdlibZip) -or
    (Get-Item -LiteralPath $stdlibZip).Length -lt 1024) {
    if (Test-Path -LiteralPath $stdlibZip) {
        Remove-Item -LiteralPath $stdlibZip -Force
    }

    $excludedRoots = @(
        "ensurepip/",
        "idlelib/",
        "lib-dynload/",
        "site-packages/",
        "test/",
        "tkinter/",
        "turtledemo/",
        "venv/"
    )
    $archive = [System.IO.Compression.ZipFile]::Open(
        $stdlibZip,
        [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $stdlibRoot -Recurse -File) {
            # Windows PowerShell 5 runs on a .NET Framework version that does
            # not expose Path.GetRelativePath. Both values are already fully
            # resolved children of the pinned extraction root, so a checked
            # prefix removal is deterministic on every supported host.
            if (-not $file.FullName.StartsWith(
                $stdlibRoot,
                [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Unexpected stdlib file outside $stdlibRoot`: $($file.FullName)"
            }
            $relative = $file.FullName.Substring($stdlibRoot.Length).TrimStart('\', '/').Replace('\', '/')
            if ($relative.Contains('/__pycache__/') -or $relative.EndsWith('.pyc')) {
                continue
            }
            $excluded = $false
            foreach ($root in $excludedRoots) {
                if ($relative.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
                    $excluded = $true
                    break
                }
            }
            if (-not $excluded) {
                [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $archive,
                    $file.FullName,
                    $relative,
                    [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
            }
        }
    }
    finally {
        $archive.Dispose()
    }
}

Copy-Item -LiteralPath $ytDlpPackage -Destination (Join-Path $stageRoot "yt-dlp-shipped") -Force
Copy-Item -LiteralPath $certifiPackage -Destination (Join-Path $stageRoot "certifi.whl") -Force
Copy-Item -LiteralPath (Join-Path $stdlibRoot "LICENSE.txt") -Destination (Join-Path $stageRoot "CPYTHON-LICENSE.txt") -Force
Copy-Item `
    -LiteralPath (Join-Path $repositoryRoot "python/bigscreen_jsc_provider.py") `
    -Destination (Join-Path $stageRoot "bigscreen_jsc_provider.py") `
    -Force

# Extension modules are copied individually by the QMOD installer because
# Android must dlopen them as real files. Test-only modules are deliberately
# omitted, while the rest of the standard library stays available for future
# yt-dlp releases without requiring another Big Screen build.
$dynamicStage = Join-Path $stageRoot "lib-dynload"
if (Test-Path -LiteralPath $dynamicStage) {
    if ((Split-Path -Parent $dynamicStage) -ne $stageRoot) {
        throw "Refusing to replace unexpected native-extension staging path $dynamicStage"
    }
    Remove-Item -LiteralPath $dynamicStage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $dynamicStage | Out-Null
$dynamicRoot = Join-Path $stdlibRoot "lib-dynload"
$productionExtensions = @(Get-ChildItem -LiteralPath $dynamicRoot -File -Filter "*.so" |
    Where-Object {
        $_.Name -notlike "_test*" -and
        $_.Name -notlike "*_test.*" -and
        $_.Name -notlike "xx*" -and
        $_.Name -ne "_xxtestfuzz.cpython-314-aarch64-linux-android.so" -and
        $_.Name -ne "_remote_debugging.cpython-314-aarch64-linux-android.so"
    } | Sort-Object Name)
foreach ($extension in $productionExtensions) {
    Copy-Item -LiteralPath $extension.FullName -Destination $dynamicStage -Force
}

@{
    pythonVersion = $pythonVersion
    pythonSha256 = $pythonSha256
    ytDlpVersion = $ytDlpVersion
    ytDlpSha256 = $ytDlpSha256
    ytDlpEjsVersion = $ytDlpEjsVersion
    certifiVersion = $certifiVersion
    certifiSha256 = $certifiSha256
    quickJsVersion = "0.16.1"
    nativeExtensions = @($productionExtensions | ForEach-Object Name)
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stageRoot "runtime-manifest.json") -Encoding UTF8

Write-Output "Prepared CPython $pythonVersion and yt-dlp $ytDlpVersion in $stageRoot"
