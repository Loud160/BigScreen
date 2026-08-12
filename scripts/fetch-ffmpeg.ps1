param(
    [switch] $Force
)

$ErrorActionPreference = "Stop"

# Big Screen uses FFmpeg only through the same public Android binaries shipped
# by Hollywood 1.0.1. The headers and link-time copies are build inputs; they
# are deliberately kept under the ignored extern directory rather than copied
# into this repository's source history.
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ffmpegRoot = Join-Path $repositoryRoot "extern/ffmpeg"
$includeRoot = Join-Path $ffmpegRoot "include"
$libraryRoot = Join-Path $ffmpegRoot "lib"
$stampFile = Join-Path $ffmpegRoot "v4.4.1.ready"
$cacheRoot = Join-Path $repositoryRoot ".cache/ffmpeg-v4.4.1"

if ((Test-Path -LiteralPath $stampFile) -and -not $Force) {
    return
}

# Resolve and validate every directory before any recursive removal. This
# keeps the maintenance script safe even if it is launched from another cwd.
foreach ($candidate in @($ffmpegRoot, $cacheRoot)) {
    $parent = Split-Path -Parent $candidate
    if ($parent -notlike "$repositoryRoot*") {
        throw "Refusing to modify a path outside the repository: $candidate"
    }
}

if (Test-Path -LiteralPath $ffmpegRoot) {
    Remove-Item -LiteralPath $ffmpegRoot -Recurse -Force
}
if (Test-Path -LiteralPath $cacheRoot) {
    Remove-Item -LiteralPath $cacheRoot -Recurse -Force
}

New-Item -ItemType Directory -Path $cacheRoot, $includeRoot, $libraryRoot -Force | Out-Null

$headersArchive = Join-Path $cacheRoot "ffmpeg-kit-headers.zip"
$runtimeArchive = Join-Path $cacheRoot "ffmpeg-kit-runtime.zip"
$headersExtract = Join-Path $cacheRoot "headers"
$runtimeExtract = Join-Path $cacheRoot "runtime"

$releaseBase = "https://github.com/Fernthedev/ffmpeg-kit-speed/releases/download/v4.4.1"
Invoke-WebRequest -UseBasicParsing -Uri "$releaseBase/ffmpeg-kit-headers.zip" -OutFile $headersArchive
Invoke-WebRequest -UseBasicParsing -Uri "$releaseBase/ffmpeg-kit.aar" -OutFile $runtimeArchive

# Pinning URLs is not sufficient if a release asset is replaced. Verify the
# exact files used for the known Beat Saber 1.37/Hollywood ABI before unpacking
# anything into the compiler search path.
$expectedHeadersHash = "44EE7E35FBDEBE2A05C0AF8E6191D838AF99F4E4B348531FB6BD78D35C1DA0B6"
$expectedRuntimeHash = "4377AA40BC5BF85D1A95DFF09D56079329162DFE950397E367DB8B98F0FDAC60"
$actualHeadersHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $headersArchive).Hash
$actualRuntimeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeArchive).Hash
if ($actualHeadersHash -ne $expectedHeadersHash -or $actualRuntimeHash -ne $expectedRuntimeHash) {
    throw "The downloaded FFmpeg files did not match the pinned SHA-256 values."
}

Expand-Archive -LiteralPath $headersArchive -DestinationPath $headersExtract
Expand-Archive -LiteralPath $runtimeArchive -DestinationPath $runtimeExtract

$downloadedInclude = Join-Path $headersExtract "include"
if (-not (Test-Path -LiteralPath $downloadedInclude)) {
    throw "The FFmpeg header archive did not contain the expected include directory."
}
Copy-Item -Path (Join-Path $downloadedInclude "*") -Destination $includeRoot -Recurse -Force

$androidLibraries = Join-Path $runtimeExtract "jni/arm64-v8a"
$requiredLibraries = @("libavcodec.so", "libavformat.so", "libavutil.so", "libswscale.so")
foreach ($libraryName in $requiredLibraries) {
    $source = Join-Path $androidLibraries $libraryName
    if (-not (Test-Path -LiteralPath $source)) {
        throw "The FFmpeg Android archive did not contain $libraryName."
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $libraryRoot $libraryName) -Force
}

# A stamp makes normal builds fast while still allowing an explicit refresh
# when the pinned media toolchain changes.
Set-Content -LiteralPath $stampFile -Value "ffmpeg-kit-speed v4.4.1" -Encoding ASCII
