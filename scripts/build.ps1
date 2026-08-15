Param(
    [Parameter(Mandatory=$false)]
    [Switch] $clean,

    [Parameter(Mandatory=$false)]
    [Switch] $help
)

if ($help -eq $true) {
    Write-Output "`"Build`" - Copiles your mod into a `".so`" or a `".a`" library"
    Write-Output "`n-- Arguments --`n"

    Write-Output "-Clean `t`t Deletes the `"build`" folder, so that the entire library is rebuilt"

    exit
}

# If the caller requests a clean build, delete only this repository's build
# directory. Resolve the path first and verify its parent so a malformed
# working directory can never turn this into a broad recursive deletion.
if ($clean.IsPresent) {
    $repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    $buildDirectory = Join-Path $repositoryRoot "build"
    if ((Split-Path -Parent $buildDirectory) -ne $repositoryRoot) {
        throw "Refusing to clean an unexpected build path: $buildDirectory"
    }
    if (Test-Path -LiteralPath $buildDirectory) {
        Remove-Item -LiteralPath $buildDirectory -Recurse -Force
    }
}

# QPM launches this script in environments that do not always inherit Visual
# Studio's CMake and Ninja paths. Prefer normal PATH discovery, then use the
# latest Visual Studio installation as a deterministic Windows fallback.
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakeExe = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }

if (-not $cmakeExe) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vsInstall = & $vswhere -latest -products * -property installationPath
        if ($vsInstall) {
            $candidate = Join-Path $vsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            $ninjaDirectory = Join-Path $vsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
            if (Test-Path -LiteralPath $candidate) {
                $cmakeExe = $candidate
                $env:PATH = "$ninjaDirectory;$env:PATH"
            }
        }
    }
}

if (-not $cmakeExe) {
    throw "CMake was not found in PATH or the latest Visual Studio installation."
}

# Build and verify both isolated LGPL FFmpeg runtimes. The in-game selector is
# meaningful only when one QMOD contains both complete ABI-isolated sets.
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$wslCommand = Get-Command wsl.exe -ErrorAction SilentlyContinue
foreach ($runtime in @(
    @{ Version = "4.4.8"; Directory = "extern/ffmpeg-lgpl"; Tag = "44" },
    @{ Version = "9.0.1"; Directory = "extern/ffmpeg-lgpl-9.0.1"; Tag = "9" })) {
    $ffmpegRoot = Join-Path $repositoryRoot $runtime.Directory
    $ffmpegStamp = Join-Path $ffmpegRoot "bigscreen-ffmpeg-$($runtime.Version).ready"
    $ffmpegSums = Join-Path $ffmpegRoot "SHA256SUMS"
    $ffmpegValid = (Test-Path -LiteralPath $ffmpegStamp) -and
        (Test-Path -LiteralPath $ffmpegSums)
    if ($ffmpegValid) {
        $expectedHashes = @{}
        foreach ($line in Get-Content -LiteralPath $ffmpegSums) {
            if ($line -match '^([0-9a-fA-F]{64})\s+(.+)$') {
                $expectedHashes[[IO.Path]::GetFileName($matches[2])] =
                    $matches[1].ToLowerInvariant()
            }
        }
        foreach ($component in @("avformat", "avcodec", "avutil", "swscale")) {
            $name = "lib$component-bigscreen$($runtime.Tag).so"
            $library = Join-Path $ffmpegRoot "lib/$name"
            if (-not (Test-Path -LiteralPath $library) -or
                -not $expectedHashes.ContainsKey($name) -or
                (Get-FileHash -Algorithm SHA256 -LiteralPath $library).Hash.ToLowerInvariant() -ne
                    $expectedHashes[$name]) {
                $ffmpegValid = $false
                break
            }
        }
        # A hash-valid staged runtime may still predate a configure change.
        # Verify the component contract recorded by FFmpeg itself so changing
        # this repository's allowlist automatically rebuilds both versions.
        $configMak = Join-Path $ffmpegRoot "bigscreen-ffmpeg-config.mak"
        if (-not (Test-Path -LiteralPath $configMak)) {
            $ffmpegValid = $false
        } else {
            $configText = Get-Content -LiteralPath $configMak -Raw
            foreach ($required in @(
                "CONFIG_H264_DECODER=yes",
                "CONFIG_H264_MEDIACODEC_DECODER=yes",
                "CONFIG_HEVC_MEDIACODEC_DECODER=yes",
                "CONFIG_VP8_DECODER=yes",
                "CONFIG_VP8_MEDIACODEC_DECODER=yes",
                "CONFIG_VP9_DECODER=yes",
                "CONFIG_VP9_MEDIACODEC_DECODER=yes",
                "CONFIG_MATROSKA_DEMUXER=yes")) {
                if (-not $configText.Contains($required)) {
                    $ffmpegValid = $false
                    break
                }
            }
            if ($configText.Contains("CONFIG_HEVC_DECODER=yes")) {
                $ffmpegValid = $false
            }
        }
    }
    if (-not $ffmpegValid) {
        if (-not $wslCommand) {
            throw "FFmpeg $($runtime.Version) is not staged and WSL was not found. Run scripts/build-ffmpeg-lgpl.sh in Linux for both supported versions."
        }
        $windowsScript = (Resolve-Path (
            Join-Path $PSScriptRoot "build-ffmpeg-lgpl.sh")).Path
        # wsl.exe treats unquoted Windows backslashes as shell escapes when it
        # dispatches a Linux command. A forward-slash drive path is accepted by
        # wslpath and survives that boundary unchanged.
        $portableWindowsScript = $windowsScript.Replace('\', '/')
        $linuxScriptOutput = & $wslCommand.Source -- wslpath -a $portableWindowsScript
        $linuxScript = if ($null -eq $linuxScriptOutput) {
            ""
        } else {
            ([string]$linuxScriptOutput).Trim()
        }
        if (-not $linuxScript) {
            throw "WSL could not translate the FFmpeg build-script path $windowsScript"
        }
        & $wslCommand.Source -- env `
            "BIGSCREEN_FFMPEG_VERSION=$($runtime.Version)" bash $linuxScript
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}

# Stage the pinned Android CPython, yt-dlp, certificate bundle, and native
# extension modules before CMake tries to include or link the runtime.
& (Join-Path $PSScriptRoot "fetch-quickjs-ng.ps1")
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& (Join-Path $PSScriptRoot "fetch-downloader-runtime.ps1")
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

# The current development package intentionally includes the hard-coded
# Up & Down proof of concept. Pass this explicitly instead of relying only on
# CMake's default: an existing cache can otherwise preserve OFF across every
# later build and silently replace the showcase with ordinary single-screen
# playback.
& $cmakeExe -G "Ninja" -DCMAKE_BUILD_TYPE="RelWithDebInfo" `
    -DBIGSCREEN_UP_DOWN_SHOWCASE=ON -B build
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $cmakeExe --build build
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

# Prove that each separately compiled decoder retained requirements for its
# matching private FFmpeg namespace. Without this audit a valid-looking build
# could still route both toggle states through whichever FFmpeg loaded first.
& (Join-Path $PSScriptRoot "validate-ffmpeg-elf.ps1")
exit $LASTEXITCODE
