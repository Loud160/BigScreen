Param(
    [ValidateSet("4.4.8", "9.0.1")]
    [String[]] $Versions = @("4.4.8", "9.0.1")
)

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$artifactRoot = Join-Path $repositoryRoot "artifacts/ffmpeg-comparison"
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null

# Build each supported runtime from a clean CMake tree. Reusing one tree would
# make it too easy for CMake or Ninja to retain headers or link inputs from the
# previous ABI, invalidating the on-headset performance comparison.
foreach ($version in $Versions) {
    & (Join-Path $PSScriptRoot "build.ps1") -Clean -FfmpegVersion $version
    if ($LASTEXITCODE -ne 0) {
        throw "Big Screen failed to build with FFmpeg $version."
    }

    $packageName = "Big Screen-ffmpeg-$version"
    & (Join-Path $PSScriptRoot "createqmod.ps1") `
        -QmodName $packageName `
        -FfmpegVersion $version
    if ($LASTEXITCODE -ne 0) {
        throw "Big Screen failed to package with FFmpeg $version."
    }

    $variantDirectory = Join-Path $artifactRoot $version
    New-Item -ItemType Directory -Path $variantDirectory -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "$packageName.qmod") `
        -Destination $variantDirectory -Force
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "build/libbigscreen.so") `
        -Destination (Join-Path $variantDirectory "libbigscreen.so") -Force
    $ffmpegRoot = if ($version -eq "4.4.8") {
        "extern/ffmpeg-lgpl"
    } else {
        "extern/ffmpeg-lgpl-$version"
    }
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "$ffmpegRoot/BUILD-INFO.txt") `
        -Destination $variantDirectory -Force
}

Write-Output "FFmpeg comparison artifacts are in $artifactRoot"
