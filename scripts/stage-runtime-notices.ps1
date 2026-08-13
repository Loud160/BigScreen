Param(
    [ValidateSet("4.4.8", "9.0.1")]
    [String] $FfmpegVersion = $(if ($env:BIGSCREEN_FFMPEG_VERSION) { $env:BIGSCREEN_FFMPEG_VERSION } else { "4.4.8" })
)

$ErrorActionPreference = "Stop"

# Keep legal notices beside the exact embedded runtime used by both QMOD
# packaging and direct Quest deployment. A clean build recreates
# build/downloader, so staging these files only in createqmod.ps1 made the
# normal copy.ps1 path fail immediately after a clean compile.
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$runtimeStage = Join-Path $repositoryRoot "build/downloader"
$ffmpegRuntime = if ($FfmpegVersion -eq "4.4.8") {
    Join-Path $repositoryRoot "extern/ffmpeg-lgpl"
} else {
    Join-Path $repositoryRoot "extern/ffmpeg-lgpl-$FfmpegVersion"
}
$noticeSources = @{
    "BIGSCREEN-LICENSE.txt" = Join-Path $repositoryRoot "LICENSE"
    "THIRD-PARTY-NOTICES.md" = Join-Path $repositoryRoot "THIRD_PARTY_NOTICES.md"
    "FFMPEG-LGPL-2.1-OR-LATER.txt" = Join-Path $ffmpegRuntime "COPYING.LGPLv2.1"
    "FFMPEG-BUILD-INFO.txt" = Join-Path $ffmpegRuntime "BUILD-INFO.txt"
    "FFMPEG-CHANGES.diff" = Join-Path $ffmpegRuntime "bigscreen-ffmpeg-changes.diff"
    "CERTIFI-MPL-2.0.txt" = Join-Path $repositoryRoot "licenses/CERTIFI-MPL-2.0.txt"
    "MPL-2.0.txt" = Join-Path $repositoryRoot "licenses/MPL-2.0.txt"
    "YT-DLP-UNLICENSE.txt" = Join-Path $repositoryRoot "licenses/YT-DLP-UNLICENSE.txt"
    "QUICKJS-NG-MIT.txt" = Join-Path $repositoryRoot "licenses/QUICKJS-NG-MIT.txt"
}

if (-not (Test-Path -LiteralPath $runtimeStage)) {
    throw "Downloader runtime must be staged before its notices: $runtimeStage"
}
foreach ($notice in $noticeSources.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $notice.Value)) {
        throw "Required runtime notice is missing: $($notice.Value)"
    }
    Copy-Item -LiteralPath $notice.Value `
        -Destination (Join-Path $runtimeStage $notice.Key) -Force
}
