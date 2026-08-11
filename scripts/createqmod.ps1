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
$requiredLibraries = @(
    "libswscale.so",
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
$runtimeStage = Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")).Path "build\downloader"
$runtimeFiles = @(
    "python314.zip",
    "yt-dlp-shipped",
    "certifi.whl",
    "runtime-manifest.json",
    "CPYTHON-LICENSE.txt"
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
