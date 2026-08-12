Param(
    [Parameter(Mandatory=$false)]
    [Switch] $Force
)

$ErrorActionPreference = "Stop"

# Big Screen embeds the official CPython Android build instead of relying on
# Termux or an executable that does not exist on a stock Quest. Keep every
# network artifact pinned and hash-checked so a normal source build is
# reproducible and cannot silently package a partial or substituted download.
$pythonVersion = "3.14.6"
$pythonSha256 = "38bbe77d3167b5cd554e03b1021324926f09f3825202b065951dd7638e9c37e5"
$ytDlpVersion = "2026.07.04"
$ytDlpSha256 = "495be29ff4d9d4e9be7eabdfef225221e5d5282e77f2f505abc6dca80349f3fd"
$certifiVersion = "2026.7.22"
$certifiSha256 = "62f22742b58a1a33014a2b6b706588a8d7e2a88ae7bd1a6ebe8c992928483775"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$downloadRoot = Join-Path $repositoryRoot "extern/downloader"
$pythonArchive = Join-Path $downloadRoot "python-$pythonVersion-aarch64-linux-android.tar.gz"
$pythonExtractRoot = Join-Path $downloadRoot "python-$pythonVersion"
$pythonPrefix = Join-Path $pythonExtractRoot "prefix"
$ytDlpPackage = Join-Path $downloadRoot "yt-dlp-$ytDlpVersion"
$certifiPackage = Join-Path $downloadRoot "certifi-$certifiVersion-py3-none-any.whl"
$stageRoot = Join-Path $repositoryRoot "build/downloader"
$nativeLibraryStage = Join-Path $repositoryRoot "extern/libs"

New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null
New-Item -ItemType Directory -Force -Path $nativeLibraryStage | Out-Null

function Assert-Sha256([string] $Path, [string] $Expected) {
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) {
        throw "SHA-256 mismatch for $Path. Expected $Expected, received $actual."
    }
}

if ($Force -or -not (Test-Path -LiteralPath $pythonArchive)) {
    Invoke-WebRequest `
        -Uri "https://www.python.org/ftp/python/$pythonVersion/python-$pythonVersion-aarch64-linux-android.tar.gz" `
        -OutFile $pythonArchive
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
if (-not (Test-Path -LiteralPath (Join-Path $pythonPrefix "lib/libpython3.14.so"))) {
    New-Item -ItemType Directory -Force -Path $pythonExtractRoot | Out-Null
    & tar -xzf $pythonArchive -C $pythonExtractRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Could not extract the official Python Android package."
    }
}

if ($Force -or -not (Test-Path -LiteralPath $ytDlpPackage)) {
    Invoke-WebRequest `
        -Uri "https://github.com/yt-dlp/yt-dlp/releases/download/$ytDlpVersion/yt-dlp" `
        -OutFile $ytDlpPackage
}
Assert-Sha256 $ytDlpPackage $ytDlpSha256

if ($Force -or -not (Test-Path -LiteralPath $certifiPackage)) {
    Invoke-WebRequest `
        -Uri "https://files.pythonhosted.org/packages/0b/a7/71ac2cff56fec219ed242bb11b8efb69fcc4bec75db06fb7bfe35de520e6/certifi-$certifiVersion-py3-none-any.whl" `
        -OutFile $certifiPackage
}
Assert-Sha256 $certifiPackage $certifiSha256

# OpenSSL cannot read a PEM file through Python's zipimport path. Extract the
# small production certifi package from the already hash-verified wheel while
# staging the QMOD. Its normal where() function will then return a physical
# on-device path without runtime monkey-patching.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
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
@(
    "libpython3.14.so",
    "libssl_python.so",
    "libcrypto_python.so",
    "libsqlite3_python.so"
) | ForEach-Object {
    Copy-Item -LiteralPath (Join-Path $pythonLib $_) -Destination $nativeLibraryStage -Force
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

# Extension modules are copied individually by the QMOD installer because
# Android must dlopen them as real files. Test-only modules are deliberately
# omitted, while the rest of the standard library stays available for future
# yt-dlp releases without requiring another Big Screen build.
$dynamicStage = Join-Path $stageRoot "lib-dynload"
New-Item -ItemType Directory -Force -Path $dynamicStage | Out-Null
$dynamicRoot = Join-Path $stdlibRoot "lib-dynload"
Get-ChildItem -LiteralPath $dynamicRoot -File -Filter "*.so" |
    Where-Object {
        $_.Name -notlike "_test*" -and
        $_.Name -notlike "xx*" -and
        $_.Name -ne "_xxtestfuzz.cpython-314-aarch64-linux-android.so" -and
        $_.Name -ne "_remote_debugging.cpython-314-aarch64-linux-android.so"
    } |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $dynamicStage -Force
    }

@{
    pythonVersion = $pythonVersion
    pythonSha256 = $pythonSha256
    ytDlpVersion = $ytDlpVersion
    ytDlpSha256 = $ytDlpSha256
    certifiVersion = $certifiVersion
    certifiSha256 = $certifiSha256
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stageRoot "runtime-manifest.json") -Encoding UTF8

Write-Output "Prepared CPython $pythonVersion and yt-dlp $ytDlpVersion in $stageRoot"
