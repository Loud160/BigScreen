# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
Param(
    [Parameter(Mandatory=$false)]
    [String] $qmodName="",

    [Parameter(Mandatory=$false)]
    [Switch] $help
)

if ($help -eq $true) {
    Write-Output "`"createqmod`" - Creates a .qmod file with your compiled libraries and mod.json."
    Write-Output "`n-- Arguments --`n"

    Write-Output "-QmodName `t The file name of your qmod"

    exit
}

# The runtime files are generated from pinned, hash-checked upstream artifacts.
# Re-run staging here so packaging can never accidentally use a stale partial
# download even when createqmod is invoked without a preceding build command.
& $PSScriptRoot/fetch-quickjs-ng.ps1
if (-not $?) {
    exit 1
}
& $PSScriptRoot/fetch-downloader-runtime.ps1
if (-not $?) {
    exit 1
}

$mod = "./mod.json"
. (Join-Path $PSScriptRoot "sync-runtime-manifest.ps1")

# Older Big Screen packagers wrote mod.json with Windows PowerShell's UTF-8
# byte-order mark. Normalize an existing generated manifest before validation
# so upgrading the build scripts repairs the workspace instead of requiring a
# manual deletion. Later validation rejects any writer that reintroduces it.
if (Test-Path -LiteralPath $mod -PathType Leaf) {
    $resolvedModPath = (Resolve-Path $mod).Path
    $existingModTimestampUtc =
        (Get-Item -LiteralPath $resolvedModPath).LastWriteTimeUtc
    $existingModJson = [System.IO.File]::ReadAllText($resolvedModPath)
    Write-BigScreenUtf8NoBom -Path $resolvedModPath -Content $existingModJson
    # Normalization is not a metadata refresh. Preserve the prior timestamp so
    # validate-modjson.ps1 still asks QPM to regenerate the manifest whenever
    # mod.template.json or qpm.shared.json is newer.
    [System.IO.File]::SetLastWriteTimeUtc(
        $resolvedModPath, $existingModTimestampUtc)
}

# Refuse to package a stale or incorrectly linked comparison build even when
# createqmod.ps1 is called independently from build.ps1.
& $PSScriptRoot/validate-ffmpeg-elf.ps1
if (-not $?) {
    exit 1
}

& $PSScriptRoot/validate-modjson.ps1
if (-not $?) {
    exit 1
}
$modJson = Get-Content $mod -Raw | ConvertFrom-Json
$templateJson = Get-Content "./mod.template.json" -Raw | ConvertFrom-Json
$modJson.version = $templateJson.version
# Hollywood was previously declared only to supply its GPL-enabled FFmpeg
# runtime. Big Screen now owns a completely isolated LGPL runtime, so retaining
# that package dependency would install unused GPL software and obscure the
# actual licensing boundary. Filter stale generated mod.json files as well as
# removing Hollywood from qpm.json so offline packaging is deterministic.
$modJson.dependencies = @($modJson.dependencies | Where-Object { $_.id -ne "hollywood" })
$requiredLibraries = @(
    "libbigscreen-ffmpeg44-backend.so",
    "libbigscreen-ffmpeg9-backend.so",
    "libavformat-bigscreen44.so",
    "libavcodec-bigscreen44.so",
    "libavutil-bigscreen44.so",
    "libswscale-bigscreen44.so",
    "libavformat-bigscreen9.so",
    "libavcodec-bigscreen9.so",
    "libavutil-bigscreen9.so",
    "libswscale-bigscreen9.so",
    "libbeatsaber-hook.so",
    "libpython3.14.so",
    "libssl_python.so",
    "libcrypto_python.so",
    "libsqlite3_python.so"
)

$ErrorActionPreference = "Stop"
$modJson.libraryFiles = $requiredLibraries
$serializedModJson = $modJson | ConvertTo-Json -Depth 10
Write-BigScreenUtf8NoBom `
    -Path (Resolve-Path $mod).Path `
    -Content $serializedModJson

# QMOD fileCopies install pure Python and native extension modules into the
# mod-owned durable runtime folder. The shared synchronizer is also used by
# direct ADB deployment so the two installation paths cannot drift apart.
$runtimeStage = Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")).Path "build/downloader"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
# Install redistributable notices beside the embedded runtime. Users and mod
# managers should not need the Git repository to discover dependency terms.
& $PSScriptRoot/stage-runtime-notices.ps1
if (-not $?) {
    exit 1
}
$runtimeSourcePaths = @(Sync-BigScreenRuntimeManifest `
    -ModJsonPath (Resolve-Path $mod).Path `
    -RuntimeStage $runtimeStage)
$modJson = Get-Content -LiteralPath $mod -Raw | ConvertFrom-Json -ErrorAction Stop
& $PSScriptRoot/validate-modjson.ps1
if (-not $?) {
    exit 1
}

if ($qmodName -eq "") {
    $qmodName = $modJson.name
}
if ([string]::IsNullOrWhiteSpace($qmodName) -or
    [IO.Path]::GetFileName($qmodName) -ne $qmodName -or
    $qmodName.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    throw "QMOD name must be one valid file name without a directory path."
}

$filelist = @($mod)

$cover = "./" + $modJson.coverImage
if ((-not ($cover -eq "./")) -and (Test-Path $cover)) {
    $filelist += ,$cover
}

foreach ($mod in $modJson.modFiles) {
    $path = "./build/" + $mod
    if (-not (Test-Path $path)) {
        $path = "./extern/libs/" + $mod
    }
    if (-not (Test-Path $path)) {
        Write-Output "Error: could not find dependency: $path"
        exit 1
    }
    $filelist += $path
}

foreach ($mod in $modJson.lateModFiles) {
    $path = "./build/" + $mod
    if (-not (Test-Path $path)) {
        $path = "./extern/libs/" + $mod
    }
    if (-not (Test-Path $path)) {
        Write-Output "Error: could not find dependency: $path"
        exit 1
    }
    $filelist += $path
}


foreach ($lib in $modJson.libraryFiles) {
    $path = "./build/" + $lib
    if (-not (Test-Path $path)) {
        $path = "./extern/libs/" + $lib
    }
    if (-not (Test-Path $path)) {
        Write-Output "Error: could not find dependency: $path"
        exit 1
    }
    $filelist += $path
}

foreach ($path in $runtimeSourcePaths) {
    if (-not (Test-Path $path)) {
        Write-Output "Error: could not find runtime file: $path"
        exit 1
    }
    $filelist += $path
}

$expectedEntries = @($filelist | ForEach-Object {
    [IO.Path]::GetFileName([string]$_)
})
$duplicates = @($expectedEntries | Group-Object | Where-Object Count -gt 1)
if ($duplicates.Count -gt 0) {
    throw "QMOD inputs contain duplicate archive names: $($duplicates.Name -join ', ')"
}

# Build a completely new archive beside the final QMOD. Compress-Archive's
# -Update mode can preserve files removed from the manifest, so it is never
# suitable for a release artifact. Validate the fresh ZIP before atomically
# replacing an existing QMOD; a failed package leaves the last good file intact.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$qmod = Join-Path $repositoryRoot ($qmodName + ".qmod")
$temporaryZip = Join-Path $repositoryRoot (
    "." + $qmodName + "." + [Guid]::NewGuid().ToString("N") + ".zip")
try {
    Write-Output "Packaging the complete mod and embedded runtime into the QMOD. Compression can take a little while; please wait."
    Compress-Archive -LiteralPath $filelist -DestinationPath $temporaryZip
    $archive = [System.IO.Compression.ZipFile]::OpenRead($temporaryZip)
    try {
        $actualEntries = @($archive.Entries | ForEach-Object FullName)
        if ($actualEntries.Count -ne $expectedEntries.Count -or
            @($actualEntries | Group-Object | Where-Object Count -gt 1).Count -gt 0 -or
            (Compare-Object ($expectedEntries | Sort-Object) ($actualEntries | Sort-Object))) {
            throw "Fresh QMOD validation failed because its entries do not exactly match mod.json."
        }
        # Empty files are valid package inputs (for example Python's py.typed
        # marker). Exact names, uniqueness, and count are the reliable archive
        # completeness checks; every source path was already required above.
    }
    finally {
        $archive.Dispose()
    }

    if (Test-Path -LiteralPath $qmod) {
        $backupQmod = $qmod + ".replacement-backup"
        if (Test-Path -LiteralPath $backupQmod) {
            Remove-Item -LiteralPath $backupQmod -Force
        }
        [IO.File]::Replace($temporaryZip, $qmod, $backupQmod)
        Remove-Item -LiteralPath $backupQmod -Force
    }
    else {
        [IO.File]::Move($temporaryZip, $qmod)
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryZip) {
        Remove-Item -LiteralPath $temporaryZip -Force
    }
}
Write-Output "Created validated QMOD $qmod"
