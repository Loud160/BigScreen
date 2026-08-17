# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
<#
.SYNOPSIS
Finds ADB or offers a verified, portable Google Platform Tools download.

.DESCRIPTION
This bootstrap never performs a machine-wide installation and never changes
the user's persistent PATH. When ADB is missing, the approved archive is placed
under BigScreen Tools beside the launchers, verified, and extracted there.
#>
[CmdletBinding()]
param(
    [ValidateSet("Ask", "Install", "Decline")]
    [string] $MissingAdbAction = "Ask",

    [ValidateRange(1, 300)]
    [int] $PromptTimeoutSeconds = 300,

    [switch] $ForcePortableInstall,

    [string] $InstallRoot = ""
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
trap {
    Write-Host "`nERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

$platformToolsVersion = "37.0.0"
$platformToolsUrl = "https://dl.google.com/android/repository/platform-tools_r37.0.0-win.zip"
$platformToolsSha256 = "4fe305812db074cea32903a489d061eb4454cbc90a49e8fea677f4b7af764918"
$platformToolsDownloadMb = "7.8"
$platformToolsExpandedMb = "16.7"
$androidSdkTermsUrl = "https://developer.android.com/studio/terms"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $InstallRoot) {
    $InstallRoot = Join-Path $repoRoot "BigScreen Tools"
}
$portableAdb = Join-Path $InstallRoot "platform-tools\adb.exe"

function Invoke-DownloadWithConsoleProgress {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Uri,

        [Parameter(Mandatory = $true)]
        [string] $DestinationPath
    )

    # Invoke-WebRequest can remain silent for the entire transfer when this
    # script is launched from a BAT file. Stream the response ourselves so a
    # slow connection still produces an explicit progress line at least every
    # five seconds (and normally at every ten-percent boundary).
    $partialPath = "$DestinationPath.partial"
    if (Test-Path -LiteralPath $partialPath -PathType Leaf) {
        Remove-Item -LiteralPath $partialPath -Force
    }

    $request = [System.Net.HttpWebRequest] [System.Net.WebRequest]::Create($Uri)
    $request.UserAgent = "BigScreen-Build-Tools/1.0"
    $response = $null
    $sourceStream = $null
    $destinationStream = $null
    try {
        $response = $request.GetResponse()
        $totalBytes = [long] $response.ContentLength
        $sourceStream = $response.GetResponseStream()
        $destinationStream = [System.IO.File]::Open(
            $partialPath,
            [System.IO.FileMode]::Create,
            [System.IO.FileAccess]::Write,
            [System.IO.FileShare]::None)

        if ($totalBytes -gt 0) {
            Write-Host ("  Downloaded 0.0 MB / {0:N1} MB (0%)" -f ($totalBytes / 1MB)) -ForegroundColor Cyan
        } else {
            Write-Host "  Download started; the server did not report a total size." -ForegroundColor Cyan
        }

        $buffer = New-Object byte[] (256KB)
        $downloadedBytes = [long] 0
        $nextPercent = 10
        $lastReportTime = [DateTime]::UtcNow
        while ($true) {
            # Wait in one-second intervals instead of blocking indefinitely in
            # Read(). This lets a temporarily stalled connection emit a
            # heartbeat every five seconds with the last known byte count.
            $readTask = $sourceStream.ReadAsync($buffer, 0, $buffer.Length)
            while (-not $readTask.Wait(1000)) {
                $now = [DateTime]::UtcNow
                if (($now - $lastReportTime).TotalSeconds -ge 5) {
                    if ($totalBytes -gt 0) {
                        $waitingPercent = [int] [Math]::Min(
                            100,
                            [Math]::Floor(($downloadedBytes * 100.0) / $totalBytes))
                        Write-Host ("  Still downloading: {0:N1} MB / {1:N1} MB ({2}%)" -f
                            ($downloadedBytes / 1MB), ($totalBytes / 1MB), $waitingPercent) -ForegroundColor Cyan
                    } else {
                        Write-Host ("  Still downloading: {0:N1} MB received..." -f
                            ($downloadedBytes / 1MB)) -ForegroundColor Cyan
                    }
                    $lastReportTime = $now
                }
            }

            $bytesRead = $readTask.GetAwaiter().GetResult()
            if ($bytesRead -le 0) { break }
            $destinationStream.Write($buffer, 0, $bytesRead)
            $downloadedBytes += $bytesRead
            $now = [DateTime]::UtcNow

            if ($totalBytes -gt 0) {
                $percent = [int] [Math]::Min(100, [Math]::Floor(($downloadedBytes * 100.0) / $totalBytes))
                if ($percent -ge $nextPercent -or
                    ($now - $lastReportTime).TotalSeconds -ge 5 -or
                    $downloadedBytes -eq $totalBytes) {
                    Write-Host ("  Downloaded {0:N1} MB / {1:N1} MB ({2}%)" -f
                        ($downloadedBytes / 1MB), ($totalBytes / 1MB), $percent) -ForegroundColor Cyan
                    $nextPercent = ([int] [Math]::Floor($percent / 10.0) + 1) * 10
                    $lastReportTime = $now
                }
            } elseif (($now - $lastReportTime).TotalSeconds -ge 5) {
                Write-Host ("  Downloaded {0:N1} MB..." -f ($downloadedBytes / 1MB)) -ForegroundColor Cyan
                $lastReportTime = $now
            }
        }

        $destinationStream.Flush()
        $destinationStream.Dispose()
        $destinationStream = $null
        $sourceStream.Dispose()
        $sourceStream = $null
        $response.Dispose()
        $response = $null

        if ($totalBytes -ge 0 -and $downloadedBytes -ne $totalBytes) {
            throw "The Platform Tools download ended early ($downloadedBytes of $totalBytes bytes received)."
        }

        Move-Item -LiteralPath $partialPath -Destination $DestinationPath -Force
        Write-Host ("Download complete: {0:N1} MB received." -f ($downloadedBytes / 1MB)) -ForegroundColor Green
    } catch {
        if (Test-Path -LiteralPath $partialPath -PathType Leaf) {
            Remove-Item -LiteralPath $partialPath -Force -ErrorAction SilentlyContinue
        }
        throw
    } finally {
        if ($destinationStream) { $destinationStream.Dispose() }
        if ($sourceStream) { $sourceStream.Dispose() }
        if ($response) { $response.Dispose() }
    }
}

function Test-PortableAdb([string] $AdbPath) {
    try {
        if (-not (Test-Path -LiteralPath $AdbPath -PathType Leaf)) { return $false }
        $properties = Join-Path (Split-Path -Parent $AdbPath) "source.properties"
        if (-not (Test-Path -LiteralPath $properties -PathType Leaf) -or
            (Get-Content -LiteralPath $properties -Raw) -notmatch
                "(?m)^Pkg\.Revision=$([regex]::Escape($platformToolsVersion))\s*$") {
            return $false
        }
        $signature = Get-AuthenticodeSignature -LiteralPath $AdbPath
        return $signature.Status -eq "Valid" -and
            $signature.SignerCertificate -and
            $signature.SignerCertificate.Subject -match '(^|,\s*)O=Google LLC(,|$)'
    } catch {
        # A damaged portable copy is treated as missing so the user can replace
        # it through the normal disclosed and verified download flow.
        return $false
    }
}

function Find-ExistingAdb {
    if (-not $ForcePortableInstall) {
        $command = Get-Command adb.exe -ErrorAction SilentlyContinue
        if ($command) { return $command.Source }
    }

    # A previously downloaded portable copy is valid regardless of whether the
    # caller requested an isolated test of the portable-install path.
    if (Test-PortableAdb $portableAdb) { return $portableAdb }
    if ($ForcePortableInstall) { return $null }

    $candidates = New-Object System.Collections.Generic.List[string]
    foreach ($sdkRoot in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)) {
        if ($sdkRoot) { $candidates.Add((Join-Path $sdkRoot "platform-tools\adb.exe")) }
    }
    if ($env:LOCALAPPDATA) {
        $candidates.Add((Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"))
        $candidates.Add((Join-Path $env:LOCALAPPDATA "Programs\SideQuest\resources\app.asar.unpacked\build\platform-tools\adb.exe"))
        $candidates.Add((Join-Path $env:LOCALAPPDATA "SideQuest\resources\app.asar.unpacked\build\platform-tools\adb.exe"))
    }
    foreach ($programRoot in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ($programRoot) {
            $candidates.Add((Join-Path $programRoot "SideQuest\resources\app.asar.unpacked\build\platform-tools\adb.exe"))
        }
    }
    if ($env:APPDATA) {
        $qpmRoot = Join-Path $env:APPDATA "QPM-RS"
        if (Test-Path -LiteralPath $qpmRoot) {
            Get-ChildItem -LiteralPath $qpmRoot -Filter adb.exe -File -Recurse -ErrorAction SilentlyContinue |
                ForEach-Object { $candidates.Add($_.FullName) }
        }
    }
    return $candidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
}

$existingAdb = Find-ExistingAdb
if ($existingAdb) {
    Write-Host "Using ADB: $existingAdb"
    exit 0
}

Write-Host ""
Write-Host "ADB was not found." -ForegroundColor Yellow
Write-Host "Big Screen can download Google's official Android SDK Platform Tools $platformToolsVersion for Windows." -ForegroundColor Yellow
Write-Host "Download: approximately $platformToolsDownloadMb MB" -ForegroundColor Yellow
Write-Host "Installed size: approximately $platformToolsExpandedMb MB" -ForegroundColor Yellow
Write-Host "Source: $platformToolsUrl" -ForegroundColor Yellow
Write-Host "Destination: $InstallRoot" -ForegroundColor Yellow
Write-Host "This is a portable local copy. It does not need administrator access and does not modify the system PATH." -ForegroundColor Yellow
Write-Host "Google Android SDK terms: $androidSdkTermsUrl" -ForegroundColor Yellow
Write-Host "Choosing Y confirms that you have read and agree to Google's Android SDK terms." -ForegroundColor Yellow
Write-Host "Deleting the BigScreen Tools folder removes the downloaded copy." -ForegroundColor Yellow

$install = $false
switch ($MissingAdbAction) {
    "Install" { $install = $true }
    "Decline" { $install = $false }
    default {
        Write-Host "If no choice is made within five minutes, nothing will be downloaded." -ForegroundColor Yellow
        $choiceResult = 2
        $priorErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & choice.exe /C YN /N /T $PromptTimeoutSeconds /D N /M "Download and use the portable ADB tools? [Y/N] "
            $choiceResult = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $priorErrorActionPreference
        }
        $install = $choiceResult -eq 1
    }
}

if (-not $install) {
    throw "ADB is required. Nothing was downloaded. Install Android Platform Tools or SideQuest, or run the launcher again and approve the portable download."
}

$resolvedInstallRoot = [IO.Path]::GetFullPath($InstallRoot)
$archiveDirectory = Join-Path $resolvedInstallRoot "downloads"
$archivePath = Join-Path $archiveDirectory "platform-tools_r$platformToolsVersion-win.zip"
$stageRoot = Join-Path $resolvedInstallRoot ("installing-platform-tools-" + [Guid]::NewGuid().ToString("N"))
$finalDirectory = Join-Path $resolvedInstallRoot "platform-tools"

try {
    New-Item -ItemType Directory -Force -Path $archiveDirectory | Out-Null
    $archiveIsValid = $false
    if (Test-Path -LiteralPath $archivePath -PathType Leaf) {
        $archiveIsValid = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $platformToolsSha256
        if (-not $archiveIsValid) {
            Write-Host "The cached Platform Tools archive failed verification and will be replaced." -ForegroundColor Yellow
            Remove-Item -LiteralPath $archivePath -Force
        }
    }

    if (-not $archiveIsValid) {
        Write-Host "Downloading Google Android SDK Platform Tools $platformToolsVersion ($platformToolsDownloadMb MB)..." -ForegroundColor Cyan
        $priorProtocol = [Net.ServicePointManager]::SecurityProtocol
        try {
            [Net.ServicePointManager]::SecurityProtocol = $priorProtocol -bor [Net.SecurityProtocolType]::Tls12
            Invoke-DownloadWithConsoleProgress -Uri $platformToolsUrl -DestinationPath $archivePath
        } finally {
            [Net.ServicePointManager]::SecurityProtocol = $priorProtocol
        }
    } else {
        Write-Host "Using the verified cached Platform Tools archive."
    }

    Write-Host "Verifying downloaded archive SHA-256..." -ForegroundColor Cyan
    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $platformToolsSha256) {
        Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
        throw "The downloaded Platform Tools SHA-256 did not match the pinned official archive. The file will not be installed."
    }

    Write-Host "Archive SHA-256 verified. Extracting the portable tools..." -ForegroundColor Cyan
    Expand-Archive -LiteralPath $archivePath -DestinationPath $stageRoot
    $stagedDirectory = Join-Path $stageRoot "platform-tools"
    $stagedAdb = Join-Path $stagedDirectory "adb.exe"
    $sourceProperties = Join-Path $stagedDirectory "source.properties"
    if (-not (Test-Path -LiteralPath $stagedAdb -PathType Leaf) -or
        -not (Test-Path -LiteralPath $sourceProperties -PathType Leaf)) {
        throw "The verified archive did not contain the expected Platform Tools layout."
    }
    Write-Host "Checking extracted Platform Tools version metadata..." -ForegroundColor Cyan
    if ((Get-Content -LiteralPath $sourceProperties -Raw) -notmatch
        "(?m)^Pkg\.Revision=$([regex]::Escape($platformToolsVersion))\s*$") {
        throw "The extracted Platform Tools version did not match $platformToolsVersion."
    }
    Write-Host "Verifying the extracted adb.exe Google LLC digital signature..." -ForegroundColor Cyan
    $signature = Get-AuthenticodeSignature -LiteralPath $stagedAdb
    if ($signature.Status -ne "Valid" -or
        -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch '(^|,\s*)O=Google LLC(,|$)') {
        throw "The extracted adb.exe did not have a valid Google LLC Authenticode signature."
    }

    if (Test-Path -LiteralPath $finalDirectory) {
        $expectedPrefix = $resolvedInstallRoot.TrimEnd('\') + '\'
        $resolvedFinal = [IO.Path]::GetFullPath($finalDirectory)
        if (-not $resolvedFinal.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to replace a Platform Tools directory outside BigScreen Tools."
        }
        Remove-Item -LiteralPath $resolvedFinal -Recurse -Force
    }
    Write-Host "Installing the verified portable tools into BigScreen Tools..." -ForegroundColor Cyan
    Move-Item -LiteralPath $stagedDirectory -Destination $finalDirectory
    Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
    if ((Test-Path -LiteralPath $archiveDirectory -PathType Container) -and
        -not (Get-ChildItem -LiteralPath $archiveDirectory -Force)) {
        Remove-Item -LiteralPath $archiveDirectory -Force -ErrorAction SilentlyContinue
    }

    Write-Host "Portable ADB installed successfully:" -ForegroundColor Green
    Write-Host (Join-Path $finalDirectory "adb.exe") -ForegroundColor White
} finally {
    if (Test-Path -LiteralPath $stageRoot -PathType Container) {
        $expectedPrefix = $resolvedInstallRoot.TrimEnd('\') + '\'
        $resolvedStage = [IO.Path]::GetFullPath($stageRoot)
        if ($resolvedStage.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase) -and
            [IO.Path]::GetFileName($resolvedStage) -like "installing-platform-tools-*") {
            Remove-Item -LiteralPath $resolvedStage -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
