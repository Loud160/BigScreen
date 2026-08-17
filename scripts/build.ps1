# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
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
$buildDirectory = Join-Path $repositoryRoot "build"
$wslCommand = Get-Command wsl.exe -ErrorAction SilentlyContinue
foreach ($runtime in @(
    @{ Version = "4.4.8"; Directory = "extern/ffmpeg-lgpl"; Tag = "44" },
    @{ Version = "9.0.1"; Directory = "extern/ffmpeg-lgpl-9.0.1"; Tag = "9" })) {
    $ffmpegRoot = Join-Path $repositoryRoot $runtime.Directory
    $ffmpegStamp = Join-Path $ffmpegRoot "bigscreen-ffmpeg-$($runtime.Version).ready"
    $ffmpegSums = Join-Path $ffmpegRoot "SHA256SUMS"
    $ffmpegInvalidReason = "the ready stamp or SHA-256 manifest is missing"
    $ffmpegValid = (Test-Path -LiteralPath $ffmpegStamp) -and
        (Test-Path -LiteralPath $ffmpegSums)
    if ($ffmpegValid) {
        $ffmpegInvalidReason = "a staged library is missing or failed SHA-256 validation"
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
            $ffmpegInvalidReason = "the recorded FFmpeg configuration is missing"
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
                    $ffmpegInvalidReason = "the recorded configuration is missing $required"
                    break
                }
            }
            # Pinned FFmpeg 4 emits many diagnostics under a modern Clang,
            # including deprecated internal APIs and qualifier/constant
            # conversions in code Big Screen does not own. Require FFmpeg's
            # recorded CFLAGS to contain the standalone -w switch so an older
            # noisy staged runtime is rebuilt. Compiler errors and FFmpeg's
            # configure/feature checks remain active.
            if ($configText -notmatch '(?m)^CFLAGS=.*(?:^|\s)-w(?:\s|$)') {
                $ffmpegValid = $false
                $ffmpegInvalidReason = "the recorded CFLAGS do not contain the third-party warning policy"
            }
            # Match the exact software-decoder key. Contains() also matched
            # CONFIG_HEVC_MEDIACODEC_DECODER=yes and therefore rebuilt the
            # valid FFmpeg 9 runtime on every invocation.
            if ($configText -match '(?m)^CONFIG_HEVC_DECODER=yes$') {
                $ffmpegValid = $false
                $ffmpegInvalidReason = "the forbidden software HEVC decoder is enabled"
            }
        }
    }
    if (-not $ffmpegValid) {
        Write-Output "Rebuilding FFmpeg $($runtime.Version): $ffmpegInvalidReason."
        Write-Output "NOTE: Compiling FFmpeg $($runtime.Version) is a long first-build step and can take several minutes. Compiler output may pause between groups of files; please wait for a success or error message."
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
            "BIGSCREEN_FFMPEG_VERSION=$($runtime.Version)" bash $linuxScript --force
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}

# Stage the pinned Android CPython, yt-dlp, certificate bundle, and native
# extension modules before CMake tries to include or link the runtime.
Write-Output ""
Write-Output "Preparing the embedded downloader runtime. Missing first-run archives will be downloaded, verified, and extracted; this can take several minutes."
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
# Android NDK r27d's own toolchain files still declare compatibility with old
# CMake policy versions. CMake 4 warns about that upstream metadata on every
# configure even though Big Screen already requires CMake 3.22. Hide only
# dependency deprecation warnings; project errors and compiler diagnostics are
# unchanged.
& $cmakeExe -Wno-deprecated -G "Ninja" -DCMAKE_BUILD_TYPE="RelWithDebInfo" `
    -DBIGSCREEN_UP_DOWN_SHOWCASE=ON -S $repositoryRoot -B $buildDirectory
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Output ""
Write-Output "Building Big Screen's native Quest libraries. A clean build can take several minutes."
Write-Output "The final link/LTO step may remain on the last Ninja progress line for a while; this is normal and the build is still working."
$nativeBuildStarted = Get-Date
# Keep CMake/Ninja attached to this console so their normal [x/y] progress is
# still visible. Wait in bounded intervals around the process to add a
# heartbeat during single, internally silent operations such as ThinLTO and the
# final native link. The linker does not expose a truthful sub-percentage, so
# elapsed time is more useful than a fabricated progress bar.
if ($env:OS -eq "Windows_NT") {
    $nativeBuildProcess = Start-Process `
        -FilePath $cmakeExe `
        -ArgumentList @("--build", "build") `
        -WorkingDirectory $repositoryRoot `
        -NoNewWindow `
        -PassThru
    while (-not $nativeBuildProcess.WaitForExit(15000)) {
        $elapsed = [int]((Get-Date) - $nativeBuildStarted).TotalSeconds
        Write-Output "Still building Big Screen... $elapsed seconds elapsed. The current compiler or linker step is still running."
    }
    $nativeBuildProcess.WaitForExit()
    $nativeBuildProcess.Refresh()
    if ($nativeBuildProcess.ExitCode -ne 0) {
        exit $nativeBuildProcess.ExitCode
    }
} else {
    # Start-Process argument forwarding differs between Windows PowerShell and
    # PowerShell Core on Linux. CI does not need the Windows-only heartbeat, so
    # invoke CMake directly with the absolute build path and preserve its exit
    # code and normal Ninja progress output.
    & $cmakeExe --build $buildDirectory
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

# Prove that each separately compiled decoder retained requirements for its
# matching private FFmpeg namespace. Without this audit a valid-looking build
# could still route both toggle states through whichever FFmpeg loaded first.
& (Join-Path $PSScriptRoot "validate-ffmpeg-elf.ps1")
exit $LASTEXITCODE
