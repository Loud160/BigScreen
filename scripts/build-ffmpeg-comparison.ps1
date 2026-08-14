$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$artifactRoot = Join-Path $repositoryRoot "artifacts/ffmpeg-comparison"
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null

# The comparison is now performed from one build and one installed QMOD. This
# script retains the old entry-point name for developer muscle memory while it
# captures the dual-runtime artifact and both reproducibility records.
& (Join-Path $PSScriptRoot "build.ps1") -Clean
if ($LASTEXITCODE -ne 0) { throw "Big Screen dual FFmpeg build failed." }
& (Join-Path $PSScriptRoot "createqmod.ps1") -QmodName "Big Screen-ffmpeg-comparison"
if ($LASTEXITCODE -ne 0) { throw "Big Screen comparison QMOD failed." }

Copy-Item -LiteralPath (Join-Path $repositoryRoot "Big Screen-ffmpeg-comparison.qmod") `
    -Destination $artifactRoot -Force
Copy-Item -LiteralPath (Join-Path $repositoryRoot "build/libbigscreen.so") `
    -Destination $artifactRoot -Force
foreach ($record in @(
    @{ Source = "extern/ffmpeg-lgpl/BUILD-INFO.txt"; Name = "FFmpeg-4.4.8-BUILD-INFO.txt" },
    @{ Source = "extern/ffmpeg-lgpl-9.0.1/BUILD-INFO.txt"; Name = "FFmpeg-9.0.1-BUILD-INFO.txt" })) {
    Copy-Item -LiteralPath (Join-Path $repositoryRoot $record.Source) `
        -Destination (Join-Path $artifactRoot $record.Name) -Force
}

Write-Output "FFmpeg comparison artifacts are in $artifactRoot"
