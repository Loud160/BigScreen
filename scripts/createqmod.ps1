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
& $PSScriptRoot/fetch-downloader-runtime.ps1
if (-not $?) {
    exit 1
}

$mod = "./mod.json"

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
    "libavformat-bigscreen.so",
    "libavcodec-bigscreen.so",
    "libavutil-bigscreen.so",
    "libswscale-bigscreen.so",
    "libbeatsaber-hook_5_1_9.so",
    "libpython3.14.so",
    "libssl_python.so",
    "libcrypto_python.so",
    "libsqlite3_python.so"
)
$modJson.libraryFiles = $requiredLibraries

# QMOD fileCopies install pure Python and native extension modules into the
# mod-owned durable runtime folder. Construct the list from the staged files so
# every official extension ships without maintaining a fragile hand-written
# manifest list.
$runtimeStage = Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")).Path "build/downloader"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
# Install redistributable notices beside the embedded runtime. Users and mod
# managers should not need the Git repository to discover dependency terms.
$noticeSources = @{
    "BIGSCREEN-LICENSE.txt" = Join-Path $repositoryRoot "LICENSE"
    "THIRD-PARTY-NOTICES.md" = Join-Path $repositoryRoot "THIRD_PARTY_NOTICES.md"
    "FFMPEG-LGPL-2.1-OR-LATER.txt" = Join-Path $repositoryRoot "extern/ffmpeg-lgpl/COPYING.LGPLv2.1"
    "FFMPEG-BUILD-INFO.txt" = Join-Path $repositoryRoot "extern/ffmpeg-lgpl/BUILD-INFO.txt"
    "FFMPEG-CHANGES.diff" = Join-Path $repositoryRoot "extern/ffmpeg-lgpl/bigscreen-ffmpeg-changes.diff"
    "CERTIFI-MPL-2.0.txt" = Join-Path $repositoryRoot "licenses/CERTIFI-MPL-2.0.txt"
    "MPL-2.0.txt" = Join-Path $repositoryRoot "licenses/MPL-2.0.txt"
    "YT-DLP-UNLICENSE.txt" = Join-Path $repositoryRoot "licenses/YT-DLP-UNLICENSE.txt"
}
foreach ($notice in $noticeSources.GetEnumerator()) {
    Copy-Item -LiteralPath $notice.Value -Destination (Join-Path $runtimeStage $notice.Key) -Force
}
$runtimeFiles = @(
    "python314.zip",
    "yt-dlp-shipped",
    "certifi.whl",
    "runtime-manifest.json",
    "CPYTHON-LICENSE.txt"
    "BIGSCREEN-LICENSE.txt"
    "THIRD-PARTY-NOTICES.md"
    "FFMPEG-LGPL-2.1-OR-LATER.txt"
    "FFMPEG-BUILD-INFO.txt"
    "FFMPEG-CHANGES.diff"
    "CERTIFI-MPL-2.0.txt"
    "MPL-2.0.txt"
    "YT-DLP-UNLICENSE.txt"
)
$copies = @()
$runtimeSourcePaths = @()
foreach ($name in $runtimeFiles) {
    $copies += [PSCustomObject]@{
        name = $name
        destination = "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Runtime/$name"
    }
    $runtimeSourcePaths += Join-Path $runtimeStage $name
}
Get-ChildItem -LiteralPath (Join-Path $runtimeStage "certifi") -File |
    Sort-Object Name |
    ForEach-Object {
        $copies += [PSCustomObject]@{
            name = $_.Name
            destination = "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Runtime/certifi/$($_.Name)"
        }
        $runtimeSourcePaths += $_.FullName
    }
Get-ChildItem -LiteralPath (Join-Path $runtimeStage "lib-dynload") -File -Filter "*.so" |
    Sort-Object Name |
    ForEach-Object {
        $copies += [PSCustomObject]@{
            name = $_.Name
            destination = "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Runtime/lib-dynload/$($_.Name)"
        }
        $runtimeSourcePaths += $_.FullName
    }
$modJson.fileCopies = $copies
$modJson | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $mod -Encoding UTF8

if ($qmodName -eq "") {
    $qmodName = $modJson.name
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

$zip = $qmodName + ".zip"
$qmod = $qmodName + ".qmod"

Compress-Archive -Path $filelist -DestinationPath $zip -Update
Move-Item $zip $qmod -Force
