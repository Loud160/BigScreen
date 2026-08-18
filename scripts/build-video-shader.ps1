# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
[CmdletBinding()]
param(
    [string] $UnityEditor
)

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$project = Join-Path $repositoryRoot "tools/video-shader"
$output = Join-Path $project "Build/Android"
$assetDirectory = Join-Path $repositoryRoot "assets"
$asset = Join-Path $assetDirectory "bigscreen_video_shader"

# Beat Saber 1.40.8 ships Unity 2022.3.33f1 (verified from the APK's
# globalgamemanagers). A bundle built with a NEWER editor than the game's
# runtime can be rejected by AssetBundle.LoadFromMemory, which is why the
# editor version below must match the game, not simply the latest 2022.3.
if (-not $UnityEditor) {
    $UnityEditor = Join-Path $env:ProgramFiles `
        "Unity/Hub/Editor/2022.3.33f1/Editor/Unity.exe"
}
if (-not (Test-Path -LiteralPath $UnityEditor -PathType Leaf)) {
    throw "Unity 2022.3.33f1 was not found. Pass -UnityEditor with the matching Unity.exe path."
}

New-Item -ItemType Directory -Path $output -Force | Out-Null
New-Item -ItemType Directory -Path $assetDirectory -Force | Out-Null
$env:BIGSCREEN_VIDEO_SHADER_OUTPUT = $output

Write-Output "Building Big Screen's Android video shader with Unity 2022.3.33f1."
Write-Output "Unity may take several minutes while importing the small shader project."
$unityLog = Join-Path $project "Build/unity-video-shader.log"
$arguments = @(
    "-batchmode",
    "-nographics",
    "-quit",
    "-projectPath", ('"' + $project + '"'),
    "-executeMethod", "BuildBigScreenVideoShader.BuildAndroid",
    "-logFile", ('"' + $unityLog + '"'))
$process = Start-Process `
    -FilePath $UnityEditor `
    -ArgumentList $arguments `
    -PassThru `
    -Wait `
    -WindowStyle Hidden
if ($process.ExitCode -ne 0) {
    if (Test-Path -LiteralPath $unityLog) {
        Get-Content -LiteralPath $unityLog -Tail 80
    }
    throw "Unity failed to build the Big Screen Android video shader (exit $($process.ExitCode))."
}

$built = Join-Path $output "bigscreen_video_shader"
if (-not (Test-Path -LiteralPath $built -PathType Leaf)) {
    throw "Unity completed without producing $built"
}
Copy-Item -LiteralPath $built -Destination $asset -Force
$hash = (Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output "Built $asset"
Write-Output "SHA-256: $hash"
