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
    [Switch] $log,

    [Parameter(Mandatory=$false)]
    [Switch] $useDebug,

    [Parameter(Mandatory=$false)]
    [Switch] $self,

    [Parameter(Mandatory=$false)]
    [Switch] $all,

    [Parameter(Mandatory=$false)]
    [String] $custom="",

    [Parameter(Mandatory=$false)]
    [String] $file="",

    [Parameter(Mandatory=$false)]
    [Switch] $help
)

$ErrorActionPreference = "Stop"

if ($help -eq $true) {
    Write-Output "`"Copy`" - Builds and copies your mod to your quest, and also starts Beat Saber with optional logging"
    Write-Output "`n-- Arguments --`n"

    Write-Output "-Clean `t`t Performs a clean build (equvilant to running `"build -clean`")"
    Write-Output "-UseDebug `t Copies the debug version of the mod to your quest"
    Write-Output "-Log `t`t Logs Beat Saber using the `"Start-Logging`" command"

    Write-Output "`n-- Logging Arguments --`n"

    & $PSScriptRoot/start-logging.ps1 -help -excludeHeader

    exit
}

# QPM developers often have ADB through SideQuest without adding it to the
# system PATH. Search environment-derived installation roots rather than
# embedding one computer's drive or Program Files location in this portable
# repository.
if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    $sideQuestCandidates = @()
    foreach ($programRoot in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ($programRoot) {
            $sideQuestCandidates += Join-Path $programRoot `
                "SideQuest/resources/app.asar.unpacked/build/platform-tools/adb.exe"
        }
    }
    if ($env:LOCALAPPDATA) {
        $sideQuestCandidates += Join-Path $env:LOCALAPPDATA `
            "Programs/SideQuest/resources/app.asar.unpacked/build/platform-tools/adb.exe"
        $sideQuestCandidates += Join-Path $env:LOCALAPPDATA `
            "SideQuest/resources/app.asar.unpacked/build/platform-tools/adb.exe"
    }
    $sideQuestAdb = $sideQuestCandidates |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
    if ($sideQuestAdb) {
        $env:PATH = (Split-Path -Parent $sideQuestAdb) +
            [IO.Path]::PathSeparator + $env:PATH
    }
}
if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    throw "ADB was not found. Install Android platform-tools or SideQuest before deploying."
}
& adb get-state | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "No authorized Quest was available through ADB."
}

# The embedded video shader bundle must never be stale relative to its Unity
# source, or the build silently ships a broken screen material. When any
# shader-project input is newer than the built bundle, rebuild it here so the
# one-click workflow always embeds the current shader; if Unity 2022.3.33f1
# is not installed, fail loudly instead of deploying a known-stale shader.
$shaderAsset = Join-Path $PSScriptRoot "../assets/bigscreen_video_shader"
$shaderInputs = @(
    (Join-Path $PSScriptRoot "../tools/video-shader/Assets/BigScreenVideo.shader"),
    (Join-Path $PSScriptRoot "../tools/video-shader/Assets/Editor/BuildBigScreenVideoShader.cs"),
    (Join-Path $PSScriptRoot "../tools/video-shader/Packages/manifest.json"),
    (Join-Path $PSScriptRoot "../tools/video-shader/ProjectSettings/ProjectVersion.txt")
)
$shaderRebuildNeeded = -not (Test-Path -LiteralPath $shaderAsset)
if (-not $shaderRebuildNeeded) {
    $assetTime = (Get-Item -LiteralPath $shaderAsset).LastWriteTimeUtc
    foreach ($shaderInput in $shaderInputs) {
        if ((Test-Path -LiteralPath $shaderInput) -and
            (Get-Item -LiteralPath $shaderInput).LastWriteTimeUtc -gt $assetTime) {
            $shaderRebuildNeeded = $true
            break
        }
    }
}
if ($shaderRebuildNeeded) {
    Write-Output ""
    Write-Output "The embedded video shader source changed; rebuilding assets/bigscreen_video_shader with Unity..."
    $shaderBuildError = $null
    try {
        & $PSScriptRoot/build-video-shader.ps1
    } catch {
        $shaderBuildError = $_.Exception.Message
    }
    $assetFresh = (Test-Path -LiteralPath $shaderAsset) -and
        -not ($shaderInputs | Where-Object {
            (Test-Path -LiteralPath $_) -and
            (Get-Item -LiteralPath $_).LastWriteTimeUtc -gt
                (Get-Item -LiteralPath $shaderAsset).LastWriteTimeUtc })
    if ($shaderBuildError -or -not $assetFresh) {
        if ($shaderBuildError) { Write-Output "Shader bundle build failed: $shaderBuildError" }
        Write-Output "The Android video shader bundle could not be rebuilt."
        Write-Output "Install Unity 2022.3.33f1 through Unity Hub (one time), then run this again."
        Write-Output "Deploying with a stale shader bundle is refused because it produces an invisible or bloom-broken screen."
        exit 1
    }
    Write-Output "Android video shader bundle rebuilt successfully."
} else {
    Write-Output "Embedded video shader bundle is up to date."
}

& $PSScriptRoot/build.ps1 -clean:$clean

if ($LASTEXITCODE -ne 0) {
    Write-Output "Failed to build, exiting..."
    exit $LASTEXITCODE
}

& $PSScriptRoot/validate-modjson.ps1
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

# qpm qmod manifest intentionally starts from the tracked template, whose
# fileCopies list is empty. Populate it from the staged downloader before
# reading mod.json so a completely clean ADB deployment installs the same
# runtime as a QMOD instead of depending on files from an older installation.
$runtimeStage = Join-Path (Join-Path $PSScriptRoot "..") "build/downloader"
& $PSScriptRoot/stage-runtime-notices.ps1
if (-not $?) {
    exit 1
}
. (Join-Path $PSScriptRoot "sync-runtime-manifest.ps1")
[void](Sync-BigScreenRuntimeManifest `
    -ModJsonPath (Join-Path (Get-Location) "mod.json") `
    -RuntimeStage $runtimeStage)
& $PSScriptRoot/validate-modjson.ps1
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
$modJson = Get-Content "./mod.json" -Raw | ConvertFrom-Json

$modFiles = $modJson.modFiles
$lateModFiles = $modJson.lateModFiles

function Assert-QuestFileMatches {
    param(
        [Parameter(Mandatory=$true)][String] $LocalPath,
        [Parameter(Mandatory=$true)][String] $RemotePath
    )

    $localHash = (Get-FileHash -LiteralPath $LocalPath `
        -Algorithm SHA256).Hash.ToLowerInvariant()
    $remoteOutput = & adb shell sha256sum $RemotePath
    if ($LASTEXITCODE -ne 0 -or -not $remoteOutput) {
        throw "Could not verify deployed Quest file $RemotePath"
    }
    $remoteHash = ([String]$remoteOutput -split "\s+")[0].ToLowerInvariant()
    if ($localHash -ne $remoteHash) {
        throw "Deployment verification failed for $RemotePath. Local SHA-256 $localHash does not match Quest SHA-256 $remoteHash."
    }
    Write-Output "Verified active Quest file: $RemotePath ($localHash)"
}

Write-Output ""
Write-Output "Deploying Big Screen's native libraries and embedded downloader runtime to the Quest. Many files are copied individually, so this can take a few minutes on some USB connections."

# A development build can move a native mod between Scotland2's early and late
# phases while an older copy remains in the other folder. Scotland2 treats both
# files as independent mods, which caused Big Screen to initialize its embedded
# CPython runtime twice and abort Beat Saber during startup. The manifest is the
# source of truth: remove only this project's same-named file from the opposite
# phase before copying the selected build.
foreach ($fileName in $modFiles) {
    & adb shell rm -f -- "/sdcard/ModData/com.beatgames.beatsaber/Modloader/mods/$fileName"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    # This root-level directory is not a Scotland2 native-mod location. Remove
    # any same-named file left by an incorrect manual ADB deployment so nobody
    # can mistake an unused copy for the active build again.
    & adb shell rm -f -- "/sdcard/ModData/com.beatgames.beatsaber/Mods/$fileName"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
foreach ($fileName in $lateModFiles) {
    & adb shell rm -f -- "/sdcard/ModData/com.beatgames.beatsaber/Modloader/early_mods/$fileName"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & adb shell rm -f -- "/sdcard/ModData/com.beatgames.beatsaber/Mods/$fileName"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

foreach ($fileName in $modFiles) {
    $sourcePath = if ($useDebug -eq $true) {
        "build/debug/$fileName"
    } else {
        "build/$fileName"
    }
    $destinationPath = "/sdcard/ModData/com.beatgames.beatsaber/Modloader/early_mods/$fileName"
    & adb push $sourcePath $destinationPath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Assert-QuestFileMatches -LocalPath $sourcePath -RemotePath $destinationPath
}

foreach ($fileName in $lateModFiles) {
    $sourcePath = if ($useDebug -eq $true) {
        "build/debug/$fileName"
    } else {
        "build/$fileName"
    }
    $destinationPath = "/sdcard/ModData/com.beatgames.beatsaber/Modloader/mods/$fileName"
    & adb push $sourcePath $destinationPath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Assert-QuestFileMatches -LocalPath $sourcePath -RemotePath $destinationPath
}

# Project-owned runtime libraries must be deployed with the mod during local
# development. Dependency libraries already installed by QMOD are left alone;
# only files produced in this build directory are pushed. This is especially
# important for both private FFmpeg sets and their decoder backends because Big Screen no longer
# falls back to Hollywood's media runtime.
foreach ($fileName in $modJson.libraryFiles) {
    $builtLibrary = Join-Path "build" $fileName
    $packagedDependency = Join-Path "extern/libs" $fileName
    $librarySource = if (Test-Path -LiteralPath $builtLibrary) {
        $builtLibrary
    } elseif (Test-Path -LiteralPath $packagedDependency) {
        $packagedDependency
    } else {
        $null
    }
    if (-not $librarySource) {
        # Silently skipping a declared library can leave a different ABI on the
        # headset. That exact failure mixed CPython extensions requiring
        # _PyType_AllocNoTrack with an older libpython and disabled downloads.
        throw "No authoritative source was found for required runtime library $fileName"
    }
    & adb push $librarySource "/sdcard/ModData/com.beatgames.beatsaber/Modloader/libs/$fileName"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

# Mirror the QMOD installer's fileCopies during development deployments. A
# native-only push is insufficient for the embedded downloader: its Python
# standard library, CA bundle, yt-dlp baseline, and Big Screen QuickJS provider
# live in the mod-owned Runtime directory. Deriving the source from the fixed
# runtime destination preserves nested certifi/lib-dynload paths without a
# second hand-maintained manifest.
$runtimeDestination = "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Runtime/"
foreach ($copy in $modJson.fileCopies) {
    $destination = [string]$copy.destination
    if (-not $destination.StartsWith(
        $runtimeDestination,
        [System.StringComparison]::Ordinal)) {
        throw "Development deployment does not recognize fileCopy destination $destination"
    }
    $relative = $destination.Substring($runtimeDestination.Length).Replace('/', [IO.Path]::DirectorySeparatorChar)
    $source = Join-Path $runtimeStage $relative
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Development deployment could not find staged runtime file $source"
    }
    & adb push $source $destination
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}


# Clear the log buffer so the post-restart verification below reads only this
# boot. The mod logs its active video shader tier on the first main-menu tick.
# Everything in the verification is best-effort: it must never be able to turn
# a successful deployment into a reported failure.
$previousErrorPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try { & adb logcat -c 2>&1 | Out-Null } catch {}
$ErrorActionPreference = $previousErrorPreference

& $PSScriptRoot/restart-game.ps1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Post-deploy verification: wait for Beat Saber to reach the main menu and
# report which video shader tier the installed build selected. This removes
# any need to run a separate log tool after deploying - the answer is printed
# here and archived under diagnostics/ on every deployment.
$ErrorActionPreference = "Continue"
try {
    Write-Output ""
    Write-Output "Waiting for Beat Saber to report the active video shader tier (up to 120 seconds)..."
    $tierLine = $null
    $deadline = (Get-Date).AddSeconds(120)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 3
        $buffer = ((& adb logcat -d 2>&1) | Out-String)
        $match = [regex]::Match($buffer, '(?m)^.*Video shader tier: .*$')
        if ($match.Success) {
            $tierLine = $match.Value.Trim()
            break
        }
    }
    $diagnosticsDirectory = Join-Path $PSScriptRoot "../diagnostics"
    if (-not (Test-Path -LiteralPath $diagnosticsDirectory)) {
        New-Item -ItemType Directory -Path $diagnosticsDirectory -Force | Out-Null
    }
    $deployLogPath = Join-Path $diagnosticsDirectory "last-deploy-bigscreen.log"
    $bigScreenLines = (& adb logcat -d 2>&1) |
        Where-Object { "$_" -match "BigScreen|Big Screen|bigscreen" }
    Set-Content -LiteralPath $deployLogPath -Value (($bigScreenLines | ForEach-Object { "$_" }) -join "`r`n") -Encoding UTF8
    if ($tierLine) {
        Write-Output "============================================================"
        Write-Host $tierLine -ForegroundColor Cyan
        Write-Host "The reported tier identifies the selected shader path; verify the picture on the headset with Bloom on and off." -ForegroundColor Yellow
        Write-Output "============================================================"
    } else {
        Write-Host "Beat Saber did not report a video shader tier within 120 seconds." -ForegroundColor Yellow
        Write-Host "It may still be starting; the mod prints the tier on the first main-menu frame." -ForegroundColor Yellow
        Write-Host "This boot's Big Screen log lines were saved to diagnostics/last-deploy-bigscreen.log." -ForegroundColor Yellow
    }
} catch {
    Write-Host ("Post-deploy shader verification was skipped: " + $_.Exception.Message) -ForegroundColor Yellow
} finally {
    $ErrorActionPreference = $previousErrorPreference
}

if ($log -eq $true) {
    & $PSScriptRoot/start-logging.ps1 -self:$self -all:$all -custom:$custom -file:$file
}
