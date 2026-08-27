# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
<#
.SYNOPSIS
Collects a freshness-labelled Big Screen, Beat Saber, and Quest crash bundle.

.DESCRIPTION
The collector deliberately does not treat the newest available file as proof
that it belongs to the reported crash. It compares file and event timestamps
against the Quest's own clock and a user-selected incident window. Fresh data,
older context, and missing categories are separated in both REPORT.txt and the
archive directory structure.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 1440)]
    [Nullable[int]] $SinceMinutes = $null,

    [switch] $NoExplorer,

    [string] $OutputRoot = "",

    [ValidateSet("Ask", "Stop", "Leave")]
    [string] $ExistingAdbAction = "Ask",

    [ValidateRange(1, 300)]
    [int] $AdbPromptTimeoutSeconds = 300
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "adb-target.ps1")
. (Join-Path $PSScriptRoot "console-choice.ps1")
. (Join-Path $PSScriptRoot "quest-dependency-check.ps1")

$script:PackageName = "com.beatgames.beatsaber"
$script:ModDataRoot = "/sdcard/ModData/$($script:PackageName)"
$script:BigScreenLogRoot = "$($script:ModDataRoot)/BigScreen/Logs"
$script:Manifest = New-Object System.Collections.Generic.List[object]
$script:ReportLines = New-Object System.Collections.Generic.List[string]
$script:Adb = $null
$script:AdbWasRunningAtStart = $null -ne (Get-Process adb -ErrorAction SilentlyContinue)
$script:AdbWasUsed = $false
$script:AdbLifecycleCompleted = $false
$script:DeviceNowEpoch = 0L
$script:CutoffEpoch = 0L
$script:DeviceOffset = "+00:00"
$script:StageRoot = $null

function Add-ReportLine([string] $Text = "") {
    $script:ReportLines.Add($Text)
}

function Write-Utf8File([string] $Path, [string] $Text) {
    $parent = Split-Path -Parent $Path
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    # UTF8 without a BOM keeps the files friendly to Windows PowerShell, modern
    # editors, GitHub, and command-line crash-analysis tools.
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

function Find-Adb {
    $command = Get-Command adb -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $candidates = New-Object System.Collections.Generic.List[string]
    $candidates.Add((Join-Path $PSScriptRoot "../BigScreen Tools/platform-tools/adb"))
    $candidates.Add((Join-Path $PSScriptRoot "../platform-tools/adb"))
    $candidates.Add((Join-Path $PSScriptRoot "../../platform-tools/adb"))
    $candidates.Add((Join-Path $PSScriptRoot "../BigScreen Tools/platform-tools/adb.exe"))
    $candidates.Add((Join-Path $PSScriptRoot "../platform-tools/adb.exe"))
    $candidates.Add((Join-Path $PSScriptRoot "../../platform-tools/adb.exe"))

    foreach ($sdkRoot in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)) {
        if ($sdkRoot) {
            $candidates.Add((Join-Path $sdkRoot "platform-tools/adb"))
            $candidates.Add((Join-Path $sdkRoot "platform-tools/adb.exe"))
        }
    }
    if ($env:LOCALAPPDATA) {
        $candidates.Add((Join-Path $env:LOCALAPPDATA "Android/Sdk/platform-tools/adb.exe"))
        $candidates.Add((Join-Path $env:LOCALAPPDATA "Programs/SideQuest/resources/app.asar.unpacked/build/platform-tools/adb.exe"))
        $candidates.Add((Join-Path $env:LOCALAPPDATA "SideQuest/resources/app.asar.unpacked/build/platform-tools/adb.exe"))
    }
    foreach ($programRoot in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ($programRoot) {
            $candidates.Add((Join-Path $programRoot "SideQuest/resources/app.asar.unpacked/build/platform-tools/adb.exe"))
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
        Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -First 1
}

function Invoke-Adb {
    param(
        [Parameter(Mandatory=$true)] [string[]] $Arguments,
        [switch] $AllowFailure
    )
    # ADB writes normal lifecycle notices such as "daemon not running;
    # starting now" to stderr even when the command succeeds. Windows
    # PowerShell converts redirected native stderr into ErrorRecord objects;
    # with the script's stop-on-error policy, that used to abort a legitimate
    # first run before ADB could finish starting. Native commands are judged by
    # their exit code here, while stderr remains captured for useful failures.
    $priorErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = & $script:Adb @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $priorErrorActionPreference
    }
    $text = ($output | ForEach-Object { $_.ToString() }) -join "`n"
    if (-not $AllowFailure -and $exitCode -ne 0) {
        throw "ADB failed while running: adb $($Arguments -join ' ')`n$text"
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Text = $text }
}

function Invoke-AdbShell([string] $Command, [switch] $AllowFailure) {
    return Invoke-Adb -Arguments @("shell", $Command) -AllowFailure:$AllowFailure
}

function Collect-DiagnosticSessions([string] $SessionType) {
    $remoteDirectory = "$($script:BigScreenLogRoot)/Sessions/$SessionType"
    $localDirectory = Join-Path $script:StageRoot "Sessions/$SessionType"
    $listing = Invoke-AdbShell "find '$remoteDirectory' -maxdepth 1 -type f -name '*.jsonl' -print 2>/dev/null" -AllowFailure
    if (-not $listing -or $listing.ExitCode -ne 0 -or
        [string]::IsNullOrWhiteSpace($listing.Text)) {
        return 0
    }

    New-Item -ItemType Directory -Force -Path $localDirectory | Out-Null
    $count = 0
    foreach ($remotePath in ($listing.Text -split "`r?`n")) {
        $remotePath = $remotePath.Trim()
        if (-not $remotePath) { continue }
        # The runtime controls these names, but reduce to a leaf before making
        # a local path so malformed device output cannot escape the stage dir.
        $fileName = Split-Path -Leaf $remotePath
        if (-not $fileName.EndsWith(".jsonl", [StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        $destination = Join-Path $localDirectory $fileName
        $pull = Invoke-Adb -Arguments @("pull", $remotePath, $destination) -AllowFailure
        if ($pull.ExitCode -eq 0 -and (Test-Path -LiteralPath $destination -PathType Leaf)) {
            $count++
        } else {
            Add-ReportLine "Could not collect diagnostic session: $remotePath"
        }
    }
    return $count
}

function Stop-AdbServer([string] $Reason) {
    if (-not (Get-Process adb -ErrorAction SilentlyContinue)) { return }
    Write-Host $Reason -ForegroundColor Cyan
    if ($script:Adb) {
        [void](Invoke-Adb -Arguments @("kill-server") -AllowFailure)
        Start-Sleep -Milliseconds 300
    }
    # kill-server is normally sufficient. The fallback handles a damaged or
    # mismatched daemon without asking a nontechnical user to run taskkill.
    Get-Process adb -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
}

function Complete-AdbSession {
    if ($script:AdbLifecycleCompleted) { return }
    $script:AdbLifecycleCompleted = $true
    if (-not $script:AdbWasUsed -or
        -not (Get-Process adb -ErrorAction SilentlyContinue)) {
        return
    }

    if (-not $script:AdbWasRunningAtStart) {
        Stop-AdbServer "Stopping the ADB server started by this collector..."
        Write-Host "ADB was stopped." -ForegroundColor Green
        return
    }

    $stopExisting = $false
    switch ($ExistingAdbAction) {
        "Stop" { $stopExisting = $true }
        "Leave" { $stopExisting = $false }
        default {
            Write-Host ""
            Write-Host "ADB was already running before log collection." -ForegroundColor Yellow
            Write-Host "Stopping it can help ModsBeforeFriday connect. If no choice is made within five minutes, ADB will be left running." -ForegroundColor Yellow
            $stopExisting = Read-BigScreenTimedYesNo `
                -Prompt "Stop ADB now? [Y/N] " `
                -TimeoutSeconds $AdbPromptTimeoutSeconds
        }
    }

    if ($stopExisting) {
        Stop-AdbServer "Stopping the existing ADB server..."
        Write-Host "ADB was stopped." -ForegroundColor Green
    } else {
        Write-Host "ADB was left running." -ForegroundColor DarkGray
    }
}

function Convert-DeviceTimestampToEpoch([string] $Timestamp) {
    try {
        $value = [DateTimeOffset]::ParseExact(
            "$Timestamp $($script:DeviceOffset)",
            "yyyy-MM-dd HH:mm:ss zzz",
            [Globalization.CultureInfo]::InvariantCulture)
        return $value.ToUnixTimeSeconds()
    } catch {
        return $null
    }
}

function Get-AgeText([Nullable[long]] $Epoch) {
    if ($null -eq $Epoch) { return "unknown age" }
    $seconds = [Math]::Max(0L, $script:DeviceNowEpoch - [long]$Epoch)
    if ($seconds -lt 120) { return "$seconds seconds old" }
    $minutes = [Math]::Round($seconds / 60.0, 1)
    if ($minutes -lt 120) { return "$minutes minutes old" }
    return "$([Math]::Round($minutes / 60.0, 1)) hours old"
}

function Get-Freshness([Nullable[long]] $Epoch) {
    if ($null -eq $Epoch) { return "UNKNOWN" }
    if ([long]$Epoch -ge $script:CutoffEpoch -and [long]$Epoch -le ($script:DeviceNowEpoch + 120)) {
        return "FRESH"
    }
    return "OLDER CONTEXT"
}

function Add-ManifestEntry {
    param(
        [string] $Category,
        [string] $Name,
        [string] $Status,
        [string] $Source,
        [Nullable[long]] $Epoch,
        [string] $ArchivePath,
        [string] $Note
    )
    $isoTime = $null
    if ($null -ne $Epoch) {
        $isoTime = [DateTimeOffset]::FromUnixTimeSeconds([long]$Epoch).ToString("o")
    }
    $script:Manifest.Add([pscustomobject]@{
        category = $Category
        name = $Name
        status = $Status
        source = $Source
        deviceEpoch = if ($null -ne $Epoch) { [long]$Epoch } else { $null }
        utcTime = $isoTime
        age = Get-AgeText $Epoch
        archivePath = $ArchivePath
        note = $Note
    })
}

function Get-StatusDirectory([string] $Category, [string] $Status) {
    $statusName = switch ($Status) {
        "FRESH" { "Fresh" }
        "OLDER CONTEXT" { "Older-Context" }
        default { "Unavailable" }
    }
    $path = Join-Path $script:StageRoot (Join-Path $Category $statusName)
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    return $path
}

function Add-TextArtifact {
    param(
        [string] $Category,
        [string] $Name,
        [string] $Text,
        [Nullable[long]] $Epoch,
        [string] $Source,
        [string] $Note
    )
    $status = Get-Freshness $Epoch
    $directory = Get-StatusDirectory $Category $status
    $path = Join-Path $directory $Name
    Write-Utf8File $path $Text
    $relative = $path.Substring($script:StageRoot.Length + 1).Replace("\", "/")
    Add-ManifestEntry $Category $Name $status $Source $Epoch $relative $Note
    return $status
}

function Add-MissingMarker {
    param([string] $Category, [string] $Name, [string] $Explanation)
    $directory = Get-StatusDirectory $Category "NOT FOUND"
    $path = Join-Path $directory $Name
    Write-Utf8File $path ($Explanation + "`r`n")
    $relative = $path.Substring($script:StageRoot.Length + 1).Replace("\", "/")
    Add-ManifestEntry $Category $Name "NOT FOUND" "" $null $relative $Explanation
}

function Get-RemoteFileInfo([string] $RemotePath) {
    # All callers pass fixed Quest paths owned by this script. Keeping those
    # paths single-quoted prevents shell expansion without accepting arbitrary
    # shell input from the user.
    $result = Invoke-AdbShell "if [ -f '$RemotePath' ]; then stat -c '%Y|%s' '$RemotePath'; fi" -AllowFailure
    if ($result.ExitCode -ne 0 -or -not $result.Text.Trim()) { return $null }
    if ($result.Text.Trim() -notmatch '^(\d+)\|(\d+)$') { return $null }
    return [pscustomobject]@{
        Path = $RemotePath
        Epoch = [long] $Matches[1]
        Size = [long] $Matches[2]
    }
}

function Pull-RemoteArtifact {
    param(
        [string] $Category,
        [string] $RemotePath,
        [Nullable[long]] $EvidenceEpoch,
        [string] $Name,
        [string] $Note
    )
    $info = Get-RemoteFileInfo $RemotePath
    if (-not $info) { return $false }
    $epoch = if ($null -ne $EvidenceEpoch) { [long]$EvidenceEpoch } else { [long]$info.Epoch }
    $status = Get-Freshness $epoch
    $directory = Get-StatusDirectory $Category $status
    $destination = Join-Path $directory $Name
    $pull = Invoke-Adb -Arguments @("pull", $RemotePath, $destination) -AllowFailure
    if ($pull.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $destination)) { return $false }
    $relative = $destination.Substring($script:StageRoot.Length + 1).Replace("\", "/")
    Add-ManifestEntry $Category $Name $status $RemotePath $epoch $relative $Note
    return $true
}

function Get-LatestTimestampedEntry([string] $RemotePath) {
    $tail = Invoke-AdbShell "if [ -f '$RemotePath' ]; then tail -n 500 '$RemotePath'; fi" -AllowFailure
    if ($tail.ExitCode -ne 0) { return $null }
    $latest = $null
    foreach ($match in [regex]::Matches($tail.Text, '(?m)^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})(?:\.\d{3})?\]')) {
        $parsed = Convert-DeviceTimestampToEpoch $match.Groups[1].Value
        if ($null -ne $parsed -and ($null -eq $latest -or $parsed -gt $latest)) {
            $latest = $parsed
        }
    }
    return $latest
}

function Select-EpochLogRecords([string] $Text) {
    $fresh = New-Object System.Collections.Generic.List[string]
    $older = New-Object System.Collections.Generic.List[string]
    $currentTarget = $null
    $latestEpoch = $null
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^\s*(\d{10})(?:\.\d+)') {
            $epoch = [long] $Matches[1]
            if ($null -eq $latestEpoch -or $epoch -gt $latestEpoch) { $latestEpoch = $epoch }
            $currentTarget = if ($epoch -ge $script:CutoffEpoch) { $fresh } else { $older }
        }
        if ($null -ne $currentTarget) { $currentTarget.Add($line) }
    }
    return [pscustomobject]@{
        Fresh = ($fresh -join "`r`n").Trim()
        Older = ($older -join "`r`n").Trim()
        LatestEpoch = $latestEpoch
    }
}

function Split-TimestampedBlocks([string] $Text, [string] $StartPattern) {
    $blocks = New-Object System.Collections.Generic.List[object]
    $current = New-Object System.Collections.Generic.List[string]
    $currentEpoch = $null
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match $StartPattern) {
            if ($current.Count -gt 0 -and $null -ne $currentEpoch) {
                $blocks.Add([pscustomobject]@{ Epoch = $currentEpoch; Text = ($current -join "`r`n").Trim() })
            }
            $current.Clear()
            $currentEpoch = Convert-DeviceTimestampToEpoch $Matches[1]
        }
        if ($null -ne $currentEpoch) { $current.Add($line) }
    }
    if ($current.Count -gt 0 -and $null -ne $currentEpoch) {
        $blocks.Add([pscustomobject]@{ Epoch = $currentEpoch; Text = ($current -join "`r`n").Trim() })
    }
    return $blocks
}

function Get-LatestCrashExitBlock([string] $Text) {
    $blocks = $Text -split '(?m)(?=\s*ApplicationExitInfo #\d+:)'
    $crashes = New-Object System.Collections.Generic.List[object]
    foreach ($block in $blocks) {
        if ($block -notmatch 'timestamp=(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)') { continue }
        $timestamp = $Matches[1]
        # Android prints either symbolic reasons or a numeric value followed by
        # the symbol, for example "reason=2 (SIGNALED)" on Quest. SIGNALED and
        # native/Java crash reasons are evidence. USER REQUESTED, package
        # updates, and normal exits are retained in raw context only.
        if ($block -notmatch 'reason=(?:\d+\s+\()?((?:CRASH NATIVE)|SIGNALED|CRASH|ANR)\)?') { continue }
        try {
            $date = [DateTimeOffset]::ParseExact(
                "$timestamp $($script:DeviceOffset)",
                "yyyy-MM-dd HH:mm:ss.fff zzz",
                [Globalization.CultureInfo]::InvariantCulture)
            $epoch = $date.ToUnixTimeSeconds()
            $crashes.Add([pscustomobject]@{ Epoch = $epoch; Text = $block.Trim() })
        } catch { }
    }
    return $crashes | Sort-Object Epoch -Descending | Select-Object -First 1
}

try {
    if ($null -eq $SinceMinutes) {
        $answer = Read-Host "How many minutes ago did the problem happen? Press Enter for 30"
        if ([string]::IsNullOrWhiteSpace($answer)) {
            $SinceMinutes = 30
        } else {
            $parsedMinutes = 0
            if (-not [int]::TryParse($answer, [ref]$parsedMinutes) -or
                $parsedMinutes -lt 1 -or $parsedMinutes -gt 1440) {
                throw "Enter a whole number from 1 through 1440 minutes."
            }
            $SinceMinutes = $parsedMinutes
        }
    }

    $script:Adb = Find-Adb
    if (-not $script:Adb) {
        throw "ADB was not found. Install Android platform-tools or SideQuest, then run this collector again. You do not need to learn ADB commands."
    }

    Write-Host "`nChecking the Quest connection..." -ForegroundColor Cyan
    $script:AdbWasUsed = $true
    # Use the same identity-safe selector as source deployment/removal. A phone
    # connected for Android development is ignored; multiple actual Quests are
    # listed for an explicit numbered choice before any logs are read.
    [void](Select-BigScreenAdbTarget `
        "support log collection" -AdbCommand $script:Adb)

    $epochText = (Invoke-AdbShell "date +%s").Text.Trim()
    if ($epochText -notmatch '^\d+$') { throw "The Quest did not return a usable clock value." }
    $script:DeviceNowEpoch = [long] $epochText
    $script:CutoffEpoch = $script:DeviceNowEpoch - ([long]$SinceMinutes * 60L)
    $offsetText = (Invoke-AdbShell "date +%z" -AllowFailure).Text.Trim()
    if ($offsetText -match '^([+-]\d{2})(\d{2})$') {
        $script:DeviceOffset = "$($Matches[1]):$($Matches[2])"
    }

    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    if (-not $OutputRoot) { $OutputRoot = Join-Path $repoRoot "BigScreen Support Logs" }
    New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
    $stamp = [DateTimeOffset]::FromUnixTimeSeconds($script:DeviceNowEpoch).ToString("yyyyMMdd-HHmmss")
    $script:StageRoot = Join-Path ([IO.Path]::GetTempPath()) ("BigScreen-Support-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $script:StageRoot | Out-Null
    $zipPath = Join-Path $OutputRoot "BigScreen-Support-$stamp.zip"

    Add-ReportLine "BIG SCREEN SUPPORT LOG REPORT"
    Add-ReportLine "================================"
    Add-ReportLine "Collection window: last $SinceMinutes minutes"
    Add-ReportLine "Quest collection time (UTC): $([DateTimeOffset]::FromUnixTimeSeconds($script:DeviceNowEpoch).ToString('u'))"
    Add-ReportLine "Fresh-data cutoff (UTC): $([DateTimeOffset]::FromUnixTimeSeconds($script:CutoffEpoch).ToString('u'))"
    Add-ReportLine ""
    Add-ReportLine "FRESH means the timestamp falls inside the selected incident window."
    Add-ReportLine "OLDER CONTEXT is included for comparison but must not be mistaken for this incident."
    Add-ReportLine "NOT FOUND means that source had no usable record. One layer can be missing even when another caught the crash."
    Add-ReportLine ""

    Write-Host "Collecting headset and Beat Saber details..." -ForegroundColor Cyan
    $model = (Invoke-AdbShell "getprop ro.product.model" -AllowFailure).Text.Trim()
    $build = (Invoke-AdbShell "getprop ro.build.display.id" -AllowFailure).Text.Trim()
    $uptime = (Invoke-AdbShell "cat /proc/uptime" -AllowFailure).Text.Trim()
    $packageDump = (Invoke-AdbShell "dumpsys package $($script:PackageName) | grep -E 'versionName=|versionCode=' | head -n 4" -AllowFailure).Text.Trim()
    $metadata = @(
        "Quest model: $model",
        "Quest OS build: $build",
        "Quest uptime: $uptime",
        "Beat Saber package: $($script:PackageName)",
        $packageDump,
        "Collector window: $SinceMinutes minutes"
    ) -join "`r`n"
    Write-Utf8File (Join-Path $script:StageRoot "DEVICE-AND-GAME.txt") ($metadata + "`r`n")

    Write-Host "Checking Big Screen dependency versions..." -ForegroundColor Cyan
    try {
        # Hard loader failures happen before libbigscreen.so can initialize its
        # private logger or create a dialog. Re-run the same manifest and
        # payload audit used by source deployment here so the support ZIP still
        # explains an old, missing, or incomplete shared dependency plainly.
        $manifestPath = Join-Path $repoRoot "mod.json"
        $localManifest = Get-Content -LiteralPath $manifestPath -Raw |
            ConvertFrom-Json -ErrorAction Stop
        $requirements = @(Get-BigScreenDependencyRequirements $manifestPath)
        $packages = @(Get-BigScreenQuestDependencyPackages `
            ([string]$localManifest.packageVersion) `
            -AdbCommand $script:Adb)
        $dependencyDiagnosis = Get-BigScreenDependencyDiagnosticReport `
            $requirements $packages
        $dependencyPath = Join-Path $script:StageRoot "DEPENDENCY-DIAGNOSIS.txt"
        Write-Utf8File $dependencyPath $dependencyDiagnosis.Text
        Add-ManifestEntry "Dependencies" "DEPENDENCY-DIAGNOSIS.txt" "FRESH" `
            "Quest package registrations and payload files" `
            ([Nullable[long]]$script:DeviceNowEpoch) `
            "DEPENDENCY-DIAGNOSIS.txt" `
            "Plain-language compatibility audit that does not require Big Screen to load."
        Add-ReportLine "DEPENDENCY CHECK"
        Add-ReportLine "----------------"
        if ($dependencyDiagnosis.HasFailures) {
            Add-ReportLine "PROBLEM FOUND: Review DEPENDENCY-DIAGNOSIS.txt first. An incompatible, missing, or incomplete shared dependency may have prevented Big Screen from loading."
        }
        else {
            Add-ReportLine "All dependencies declared by this Big Screen build are registered, compatible, and complete."
        }
        Add-ReportLine ""
    }
    catch {
        $dependencyFailure = "The collector could not complete the dependency audit: $($_.Exception.Message)"
        Write-Utf8File (Join-Path $script:StageRoot "DEPENDENCY-DIAGNOSIS.txt") `
            ($dependencyFailure + "`r`n")
        Add-ReportLine "DEPENDENCY CHECK"
        Add-ReportLine "----------------"
        Add-ReportLine $dependencyFailure
        Add-ReportLine ""
    }

    Write-Host "Collecting Big Screen diagnostics..." -ForegroundColor Cyan
    # These are Big Screen's owned general logs. Pull them before third-party
    # logs so the support report naturally points at the authoritative source.
    # A missing native log can still be valid for an older Big Screen build, so
    # the collector continues with every independent evidence layer.
    $nativeGeneralPath = "$($script:BigScreenLogRoot)/bigscreen-native.log"
    $nativeGeneralEpoch = Get-LatestTimestampedEntry $nativeGeneralPath
    $nativeGeneralClassificationEpoch = if ($null -ne $nativeGeneralEpoch) { $nativeGeneralEpoch } else { 0L }
    $pulledNativeGeneral = Pull-RemoteArtifact "Big-Screen" $nativeGeneralPath $nativeGeneralClassificationEpoch "bigscreen-native.log" "Big Screen's authoritative first-party asynchronous general log."
    if (-not $pulledNativeGeneral) {
        Add-MissingMarker "Big-Screen" "NO_BIG_SCREEN_NATIVE_LOG.txt" "The first-party Big Screen general log was not present. This can be expected on older builds."
    }
    $nativePreviousPath = "$($script:BigScreenLogRoot)/bigscreen-native.previous.log"
    $nativePreviousEpoch = Get-LatestTimestampedEntry $nativePreviousPath
    $nativePreviousClassificationEpoch = if ($null -ne $nativePreviousEpoch) { $nativePreviousEpoch } else { 0L }
    [void](Pull-RemoteArtifact "Big-Screen" $nativePreviousPath $nativePreviousClassificationEpoch "bigscreen-native.previous.log" "One retained rotation of Big Screen's first-party general log.")

    $bigScreenCurrent = "$($script:BigScreenLogRoot)/error-history.log"
    $bigScreenEntryEpoch = Get-LatestTimestampedEntry $bigScreenCurrent
    # A zero evidence time intentionally classifies a session-header-only file
    # as older context. Its current mtime is not evidence that an error occurred.
    $bigScreenClassificationEpoch = if ($null -ne $bigScreenEntryEpoch) { $bigScreenEntryEpoch } else { 0L }
    $pulledBigScreen = Pull-RemoteArtifact "Big-Screen" $bigScreenCurrent $bigScreenClassificationEpoch "error-history.log" "Freshness is based on the newest timestamped error, not file modification time; Big Screen adds a session header on every start."
    if (-not $pulledBigScreen) {
        Add-MissingMarker "Big-Screen" "NO_BIG_SCREEN_ERROR_HISTORY.txt" "Big Screen's persistent error history was not present or could not be read."
    } elseif ($bigScreenClassificationEpoch -lt $script:CutoffEpoch) {
        Add-MissingMarker "Big-Screen" "NO_FRESH_BIG_SCREEN_ERROR.txt" "Big Screen had no timestamped error inside the selected incident window. Its history was included only as older context."
    }
    $previousPath = "$($script:BigScreenLogRoot)/error-history.previous.log"
    $previousEpoch = Get-LatestTimestampedEntry $previousPath
    $previousClassificationEpoch = if ($null -ne $previousEpoch) { $previousEpoch } else { 0L }
    [void](Pull-RemoteArtifact "Big-Screen" $previousPath $previousClassificationEpoch "error-history.previous.log" "Rotated Big Screen error history; normally older context.")
    $performancePath = "$($script:BigScreenLogRoot)/performance-history.log"
    [void](Pull-RemoteArtifact "Big-Screen" $performancePath ([Nullable[long]]$null) "performance-history.log" "Optional playback and gameplay performance context.")
    $menuSessionCount = Collect-DiagnosticSessions "Menu"
    $downloadSessionCount = Collect-DiagnosticSessions "Download"
    Add-ReportLine "Detailed diagnostic sessions collected: $menuSessionCount Menu, $downloadSessionCount Download."

    Write-Host "Collecting Beat Saber logs and process-exit evidence..." -ForegroundColor Cyan
    # Legacy Paper files remain useful historical or third-party context even
    # though current Big Screen builds no longer emit through Paper2. Other
    # dependencies may still use Paper normally, so collect these independently
    # without presenting them as Big Screen's authoritative log.
    foreach ($logName in @("PaperLog.log", "beatsaber-hook.log")) {
        $remote = "$($script:ModDataRoot)/logs/$logName"
        [void](Pull-RemoteArtifact "Beat-Saber" $remote ([Nullable[long]]$null) "legacy-$logName" "LEGACY pre-paper2 log location; expected to be stale. Kept for historical comparison only.")
    }
    foreach ($paper2Dir in @("$($script:ModDataRoot)/logs/paper2", "$($script:ModDataRoot)/logs2")) {
        foreach ($logName in @("PaperLog.log", "BigScreen.log")) {
            $remote = "$paper2Dir/$logName"
            [void](Pull-RemoteArtifact "Beat-Saber" $remote ([Nullable[long]]$null) ("paper2-" + ($paper2Dir -split '/')[-1] + "-$logName") "Optional Paper2 third-party or historical log; current Big Screen builds use the first-party log above.")
        }
    }

    # Both logger backends can emit to Android logcat. Capture the recent buffer
    # so a support ZIP still has live process context if either file sink fails.
    Write-Host "Capturing recent logcat buffer..." -ForegroundColor Cyan
    $logcatDump = Invoke-Adb -Arguments @("logcat", "-d", "-t", "6000") -AllowFailure
    if ($logcatDump -and $logcatDump.ExitCode -eq 0 -and
        -not [string]::IsNullOrWhiteSpace($logcatDump.Text)) {
        $logcatDirectory = Get-StatusDirectory "Quest-OS" "FRESH"
        $logcatPath = Join-Path $logcatDirectory "logcat-recent.txt"
        Write-Utf8File $logcatPath $logcatDump.Text
        $logcatRelative = $logcatPath.Substring($script:StageRoot.Length + 1).Replace("\", "/")
        Add-ManifestEntry "Quest-OS" "logcat-recent.txt" "FRESH" "adb logcat -d -t 6000" $script:DeviceNowEpoch $logcatRelative "Recent Android log buffer captured at collection time. Contains every mod's live log lines, including Big Screen's shader-tier and screen-creation messages."
    } else {
        Add-MissingMarker "Quest-OS" "NO_LOGCAT_BUFFER.txt" "The Android log buffer could not be read."
    }

    $tombstoneListing = Invoke-AdbShell "find /sdcard/Android/data/$($script:PackageName)/files -maxdepth 1 -type f -name 'tombstone_*' -printf '%T@|%p\n' 2>/dev/null | sort -nr" -AllowFailure
    $latestTombstone = $null
    foreach ($line in ($tombstoneListing.Text -split "`r?`n")) {
        if ($line -match '^(\d+)(?:\.\d+)?\|(.+)$') {
            $latestTombstone = [pscustomobject]@{ Epoch = [long]$Matches[1]; Path = $Matches[2].Trim() }
            break
        }
    }
    if ($latestTombstone) {
        [void](Pull-RemoteArtifact "Beat-Saber" $latestTombstone.Path ([Nullable[long]]$latestTombstone.Epoch) (Split-Path -Leaf $latestTombstone.Path) "Newest Beat Saber app tombstone. Check its freshness label before associating it with the incident.")
        if ($latestTombstone.Epoch -lt $script:CutoffEpoch) {
            Add-MissingMarker "Beat-Saber" "NO_FRESH_BEAT_SABER_TOMBSTONE.txt" "The newest Beat Saber app tombstone is older than the selected incident window. It was included only under Older-Context."
        }
    } else {
        Add-MissingMarker "Beat-Saber" "NO_BEAT_SABER_TOMBSTONE.txt" "No Beat Saber app tombstone was present."
    }

    $exitInfo = Invoke-AdbShell "dumpsys activity exit-info $($script:PackageName)" -AllowFailure
    Write-Utf8File (Join-Path $script:StageRoot "Beat-Saber/exit-info-all-context.txt") ($exitInfo.Text + "`r`n")
    $latestCrashExit = Get-LatestCrashExitBlock $exitInfo.Text
    if ($latestCrashExit) {
        $status = Add-TextArtifact "Beat-Saber" "latest-crash-exit-info.txt" $latestCrashExit.Text ([Nullable[long]]$latestCrashExit.Epoch) "dumpsys activity exit-info" "Android's recorded Beat Saber process exit; normal user-requested exits are excluded from crash selection."
        if ($status -ne "FRESH") {
            Add-MissingMarker "Beat-Saber" "NO_FRESH_CRASH_EXIT.txt" "Android's newest recorded Beat Saber crash exit is older than the selected incident window."
        }
    } else {
        Add-MissingMarker "Beat-Saber" "NO_RECORDED_CRASH_EXIT.txt" "Android had no Beat Saber process exit classified as SIGNALED, CRASH, native crash, or ANR."
    }

    Write-Host "Collecting Quest OS crash evidence..." -ForegroundColor Cyan
    $crashLogcat = Invoke-Adb -Arguments @("logcat", "-b", "crash", "-d", "-v", "epoch") -AllowFailure
    $crashRecords = Select-EpochLogRecords $crashLogcat.Text
    if ($crashRecords.Fresh) {
        [void](Add-TextArtifact "Quest-OS" "logcat-crash-buffer.txt" $crashRecords.Fresh ([Nullable[long]]$crashRecords.LatestEpoch) "logcat -b crash" "Fresh records from Android's dedicated crash buffer.")
    } else {
        Add-MissingMarker "Quest-OS" "NO_FRESH_LOGCAT_CRASH.txt" "Android's crash log buffer contained no records inside the selected incident window."
        if ($crashRecords.Older) {
            [void](Add-TextArtifact "Quest-OS" "logcat-crash-buffer-older.txt" $crashRecords.Older ([Nullable[long]]$crashRecords.LatestEpoch) "logcat -b crash" "Older crash-buffer context; do not associate it with this incident without corroboration.")
        }
    }

    $targetedLogcat = Invoke-Adb -Arguments @(
        "logcat", "-d", "-v", "epoch",
        "AndroidRuntime:E", "DEBUG:F", "libc:F", "ActivityManager:I", "Unity:D", "bigscreen:V", "BigScreen:V", "*:S"
    ) -AllowFailure
    $targetedRecords = Select-EpochLogRecords $targetedLogcat.Text
    if ($targetedRecords.Fresh) {
        [void](Add-TextArtifact "Quest-OS" "targeted-recent-logcat.txt" $targetedRecords.Fresh ([Nullable[long]]$targetedRecords.LatestEpoch) "targeted Android logcat" "Recent fatal/runtime/Unity/Big Screen log messages only; unrelated full system logcat is not collected.")
    }

    $dropboxIndex = Invoke-AdbShell "dumpsys dropbox" -AllowFailure
    $dropboxLines = New-Object System.Collections.Generic.List[string]
    $latestDropboxEpoch = $null
    foreach ($line in ($dropboxIndex.Text -split "`r?`n")) {
        if ($line -match '^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s+(.+)$') {
            $epoch = Convert-DeviceTimestampToEpoch $Matches[1]
            if ($null -ne $epoch -and $epoch -ge $script:CutoffEpoch -and
                $Matches[2] -match '(crash|tombstone|watchdog|ANR|last_kmsg)') {
                $dropboxLines.Add($line)
                if ($null -eq $latestDropboxEpoch -or $epoch -gt $latestDropboxEpoch) { $latestDropboxEpoch = $epoch }
            }
        }
    }
    if ($dropboxLines.Count -gt 0) {
        [void](Add-TextArtifact "Quest-OS" "dropbox-crash-index.txt" ($dropboxLines -join "`r`n") ([Nullable[long]]$latestDropboxEpoch) "dumpsys dropbox" "Fresh Quest DropBox crash/tombstone/ANR metadata. Binary tombstones are represented by their index entry when Android cannot print them as text.")
    } else {
        Add-MissingMarker "Quest-OS" "NO_FRESH_DROPBOX_CRASH.txt" "Quest DropBox contained no crash, tombstone, watchdog, ANR, or last-kmsg entry inside the selected incident window."
    }

    foreach ($tag in @("data_app_crash", "system_app_crash", "system_server_crash", "SYSTEM_SERVER_WATCHDOG", "SYSTEM_LAST_KMSG")) {
        $printed = Invoke-AdbShell "dumpsys dropbox --print $tag" -AllowFailure
        if ($printed.ExitCode -ne 0 -or -not $printed.Text.Trim()) { continue }
        $blocks = Split-TimestampedBlocks $printed.Text '^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s+'
        $freshBlocks = @($blocks | Where-Object { $_.Epoch -ge $script:CutoffEpoch })
        if ($freshBlocks.Count -gt 0) {
            $latest = ($freshBlocks | Measure-Object -Property Epoch -Maximum).Maximum
            $body = ($freshBlocks | ForEach-Object { $_.Text }) -join "`r`n`r`n========================================`r`n`r`n"
            [void](Add-TextArtifact "Quest-OS" ("dropbox-" + $tag + ".txt") $body ([Nullable[long]]$latest) "dumpsys dropbox --print $tag" "Printable fresh Quest DropBox entries for $tag.")
        }
    }

    Add-ReportLine "COLLECTED ARTIFACTS"
    Add-ReportLine "-------------------"
    foreach ($entry in $script:Manifest) {
        Add-ReportLine ("[{0}] {1} / {2} - {3}" -f $entry.status, $entry.category, $entry.name, $entry.age)
        if ($entry.note) { Add-ReportLine ("    " + $entry.note) }
    }
    Add-ReportLine ""
    Add-ReportLine "PRIVACY NOTE"
    Add-ReportLine "------------"
    Add-ReportLine "Logs can contain map/song names, local file paths, video URLs, usernames, and other diagnostic context. Review the extracted text files before sharing the ZIP publicly."
    Add-ReportLine ""
    Add-ReportLine "Send the entire ZIP to the Big Screen maintainer. Do not rename an OLDER CONTEXT artifact as a current crash."

    Write-Utf8File (Join-Path $script:StageRoot "REPORT.txt") (($script:ReportLines -join "`r`n") + "`r`n")
    $manifestObject = [pscustomobject]@{
        collectorVersion = 1
        package = $script:PackageName
        windowMinutes = $SinceMinutes
        deviceCollectionEpoch = $script:DeviceNowEpoch
        cutoffEpoch = $script:CutoffEpoch
        # PowerShell 7 can throw "Argument types do not match" when @(...)
        # enumerates a generic List[object] inside an object initializer. An
        # explicit array conversion behaves consistently in 5.1 and 7+.
        artifacts = [object[]]$script:Manifest
    }
    Write-Utf8File (Join-Path $script:StageRoot "manifest.json") ($manifestObject | ConvertTo-Json -Depth 6)

    Write-Host "Creating the support ZIP..." -ForegroundColor Cyan
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    Compress-Archive -Path (Join-Path $script:StageRoot "*") -DestinationPath $zipPath -CompressionLevel Optimal

    Write-Host "`nSupport bundle created:" -ForegroundColor Green
    Write-Host $zipPath -ForegroundColor White
    Write-Host "`nReview REPORT.txt first. It explains which records are fresh, stale, or missing." -ForegroundColor Yellow
    Write-Host "Logs can contain song names, paths, URLs, or usernames; review them before posting publicly." -ForegroundColor Yellow

    # Resolve ADB ownership before Explorer takes focus away from this console;
    # otherwise a user could miss the shutdown question for five minutes.
    try { Complete-AdbSession } catch {
        Write-Host "ADB cleanup warning: $($_.Exception.Message)" -ForegroundColor Yellow
    }
    if (-not $NoExplorer) {
        Start-Process explorer.exe -ArgumentList "/select,`"$zipPath`""
    }
    exit 0
} catch {
    Write-Host "`nERROR: $($_.Exception.Message)" -ForegroundColor Red
    if ($_.InvocationInfo.PositionMessage) {
        Write-Host $_.InvocationInfo.PositionMessage -ForegroundColor DarkGray
    }
    exit 1
} finally {
    if ($script:StageRoot -and (Test-Path -LiteralPath $script:StageRoot) -and
        ([IO.Path]::GetFileName($script:StageRoot) -like "BigScreen-Support-*")) {
        Remove-Item -LiteralPath $script:StageRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
    try { Complete-AdbSession } catch {
        Write-Host "ADB cleanup warning: $($_.Exception.Message)" -ForegroundColor Yellow
    }
}
