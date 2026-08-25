# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Pure ownership-policy tests. No ADB command is called by this file.
Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
. (Join-Path $root "scripts/source-install-ownership.ps1")

function Assert-Equal([string]$Expected, [string]$Actual, [string]$Case) {
    if ($Expected -ne $Actual) {
        throw "$Case expected $Expected but received $Actual"
    }
}

# ADB target parsing remains deterministic before any live device probe.
$selectedSerial = Resolve-BigScreenAdbTargetFromListing @(
    "List of devices attached",
    "QUEST123`tdevice",
    "PHONE456`tunauthorized")
Assert-Equal "QUEST123" $selectedSerial `
    "one authorized Quest plus an unauthorized device"
$multipleAuthorizedRejected = $false
try {
    [void](Resolve-BigScreenAdbTargetFromListing @(
        "QUEST123`tdevice",
        "QUEST456`tdevice"))
} catch {
    $multipleAuthorizedRejected = $_.Exception.Message -match
        "More than one authorized Android device"
}
if (-not $multipleAuthorizedRejected) {
    throw "Multiple authorized ADB targets were not rejected."
}
$unauthorizedOnlyRejected = $false
try {
    [void](Resolve-BigScreenAdbTargetFromListing @(
        "PHONE456`tunauthorized"))
} catch {
    $unauthorizedOnlyRejected = $_.Exception.Message -match
        "waiting for USB-debugging authorization"
}
if (-not $unauthorizedOnlyRejected) {
    throw "An unauthorized-only ADB listing did not explain the headset prompt."
}

# Extended ADB metadata and identity checks distinguish a Quest from an
# authorized phone. Interactive selection is split into a pure helper so the
# multi-headset policy can be tested without reading from the console.
$parsedDevices = @(Get-BigScreenAdbDevicesFromListing @(
    "QUEST123`tdevice product:hollywood model:Quest_2 device:hollywood",
    "PHONE456`tdevice product:e1quew model:SM-S921U1 device:e1q"))
Assert-Equal "Quest 2" $parsedDevices[0].Model "ADB model normalization"
if (-not (Test-BigScreenQuestIdentity "Oculus" "Quest 2")) {
    throw "An Oculus Quest 2 was not recognized as a Quest."
}
if (Test-BigScreenQuestIdentity "samsung" "SM-S921U1") {
    throw "A Samsung phone was incorrectly recognized as a Quest."
}
$candidateOne = [pscustomobject]@{ Serial="QUEST123"; Model="Quest 2" }
$candidateTwo = [pscustomobject]@{ Serial="QUEST456"; Model="Quest 3" }
$singleCandidate = Select-BigScreenQuestCandidate @($candidateOne) -NonInteractive
Assert-Equal "QUEST123" $singleCandidate.Serial "one Quest candidate"
$nonInteractiveMultipleRejected = $false
try {
    [void](Select-BigScreenQuestCandidate @($candidateOne, $candidateTwo) `
        -NonInteractive)
} catch {
    $nonInteractiveMultipleRejected = $_.Exception.Message -match
        "More than one Quest with Beat Saber"
}
if (-not $nonInteractiveMultipleRejected) {
    throw "Noninteractive multi-Quest selection did not fail safely."
}

$cases = @(
    @{ Name="not installed"; Expected="NOT_INSTALLED"; Complete=$false; Partial=$false; Mbf=$false; MbfComplete=$false; Legacy=0 },
    @{ Name="source managed"; Expected="SOURCE_MANAGED"; Complete=$true; Partial=$false; Mbf=$false; MbfComplete=$false; Legacy=1 },
    @{ Name="source partial"; Expected="SOURCE_PARTIAL"; Complete=$false; Partial=$true; Mbf=$false; MbfComplete=$false; Legacy=1 },
    @{ Name="MBF managed"; Expected="MBF_MANAGED"; Complete=$false; Partial=$false; Mbf=$true; MbfComplete=$true; Legacy=1 },
    @{ Name="MBF registered only"; Expected="MBF_REGISTERED_NOT_INSTALLED"; Complete=$false; Partial=$false; Mbf=$true; MbfComplete=$false; Legacy=0 },
    @{ Name="legacy source"; Expected="LEGACY_SOURCE"; Complete=$false; Partial=$false; Mbf=$false; MbfComplete=$false; Legacy=1; Runtime=$false },
    @{ Name="runtime-only legacy source"; Expected="LEGACY_SOURCE"; Complete=$false; Partial=$false; Mbf=$false; MbfComplete=$false; Legacy=0; Runtime=$true },
    @{ Name="opposite phase duplicates"; Expected="MIXED_OR_AMBIGUOUS"; Complete=$false; Partial=$false; Mbf=$false; MbfComplete=$false; Legacy=2 },
    @{ Name="MBF plus source receipt"; Expected="MIXED_OR_AMBIGUOUS"; Complete=$true; Partial=$false; Mbf=$true; MbfComplete=$true; Legacy=1 }
)
foreach ($case in $cases) {
    $actual = Resolve-BigScreenInstallState `
        -HasCompleteReceipt $case.Complete `
        -HasPartialReceipt $case.Partial `
        -HasMbfMetadata $case.Mbf `
        -MbfPayloadComplete $case.MbfComplete `
        -LegacyPhaseCopies $case.Legacy `
        -HasLegacyRuntime $(if ($case.ContainsKey("Runtime")) { $case.Runtime } else { $false })
    Assert-Equal $case.Expected $actual $case.Name
}
Assert-Equal "MIXED_OR_AMBIGUOUS" `
    (Resolve-BigScreenInstallState -HasCompleteReceipt $true `
        -HasPartialReceipt $false -HasMbfMetadata $false `
        -MbfPayloadComplete $false -LegacyPhaseCopies 2 `
        -HasUnexpectedPhaseCopy $true) `
    "source receipt plus unowned opposite-phase copy"
Assert-Equal "MIXED_OR_AMBIGUOUS" `
    (Resolve-BigScreenInstallState -HasCompleteReceipt $true `
        -HasPartialReceipt $false -HasMbfMetadata $false `
        -MbfPayloadComplete $false -LegacyPhaseCopies 1 `
        -ReceiptUnreadable $true) `
    "unreadable source receipt"

# Partial cleanup must distinguish a completed source write from a file whose
# original baseline is already present. Complete-receipt cleanup remains more
# conservative because a later baseline match could have been written by a
# different installer after the source deployment finished.
$exclusiveFixture = [pscustomobject]@{
    path = "/example/libbigscreen.so"
    ownership = "BigScreenExclusive"
    previousState = "present"
    previousSha256 = "baseline"
    preDeployState = "present"
    preDeploySha256 = "old-source"
    preDeployWasSourceOwned = $true
    installedSha256 = "source"
}
Assert-Equal "RemoveExclusive" `
    (Resolve-BigScreenReceiptRemovalAction $exclusiveFixture "source" -Partial) `
    "partial completed source write"
Assert-Equal "RemoveExclusive" `
    (Resolve-BigScreenReceiptRemovalAction $exclusiveFixture "baseline" -Partial) `
    "partial baseline bytes do not block removal"
Assert-Equal "RemoveExclusive" `
    (Resolve-BigScreenReceiptRemovalAction $exclusiveFixture "old-source" -Partial) `
    "partial prior source build"
Assert-Equal "RemoveExclusive" `
    (Resolve-BigScreenReceiptRemovalAction $exclusiveFixture "unknown" -Partial) `
    "partial changed hash does not block removal"
Assert-Equal "RemoveExclusive" `
    (Resolve-BigScreenReceiptRemovalAction $exclusiveFixture "baseline") `
    "complete receipt changed after deployment does not block removal"
Assert-Equal "AlreadyAbsent" `
    (Resolve-BigScreenReceiptRemovalAction $exclusiveFixture $null) `
    "already absent private payload"
$sharedFixture = $exclusiveFixture.PSObject.Copy()
$sharedFixture.ownership = "SharedDependency"
Assert-Equal "PreserveShared" `
    (Resolve-BigScreenReceiptRemovalAction $sharedFixture "source" -Partial) `
    "shared dependency preservation"

# A later deployment must update only the installed hash while retaining the
# baseline captured when source-development mode first began.
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("BigScreenOwnershipTests-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $fixtureRoot | Out-Null
try {
    $firstFile = Join-Path $fixtureRoot "first.bin"
    $secondFile = Join-Path $fixtureRoot "second.bin"
    [IO.File]::WriteAllBytes($firstFile, [byte[]](1, 2, 3))
    [IO.File]::WriteAllBytes($secondFile, [byte[]](4, 5, 6))
    $script:RemoteHashFixture = $null
    function Get-BigScreenRemoteHash([string]$Path) {
        return $script:RemoteHashFixture
    }
    $manifest = [pscustomobject]@{
        version = "test"
        packageVersion = "1.40.8_7379"
    }
    $destination = "/sdcard/ModData/com.beatgames.beatsaber/Modloader/early_mods/libbigscreen.so"
    $firstPlan = @([pscustomobject]@{
        LocalPath=$firstFile; Path=$destination; Category="EarlyMod"; Ownership="BigScreenExclusive"
    })
    $firstReceipt = New-BigScreenSourceReceipt $firstPlan $manifest "first" $null
    if ($firstReceipt.files[0].previousState -ne "absent" -or
        $null -ne $firstReceipt.files[0].previousSha256) {
        throw "A clean source install did not preserve an absent baseline."
    }
    $firstReceipt.state = "complete"
    $firstReceipt.files[0].copyCompleted = $true
    $script:RemoteHashFixture = $firstReceipt.files[0].installedSha256
    $secondPlan = @([pscustomobject]@{
        LocalPath=$secondFile; Path=$destination; Category="EarlyMod"; Ownership="BigScreenExclusive"
    })
    $secondReceipt = New-BigScreenSourceReceipt $secondPlan $manifest "second" $firstReceipt
    if ($secondReceipt.files[0].previousState -ne "absent" -or
        $null -ne $secondReceipt.files[0].previousSha256) {
        throw "Repeated source deployment redefined the original baseline."
    }
    if ($secondReceipt.files[0].installedSha256 -eq $firstReceipt.files[0].installedSha256) {
        throw "Repeated source deployment did not update the installed hash."
    }
    if (-not $secondReceipt.files[0].preDeployWasSourceOwned -or
        $secondReceipt.files[0].preDeploySha256 -ne
            $firstReceipt.files[0].installedSha256) {
        throw "Repeated source deployment did not preserve the immediate prior source hash for partial recovery."
    }
} finally {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$remover = Get-Content -LiteralPath (Join-Path $root "scripts/remove-bigscreen.ps1") -Raw
foreach ($protected in @("BigScreen/Thumbnails", "BigScreen/Video Import", "library.json", "BigScreen/Logs")) {
    if ($remover -match [regex]::Escape("rm") + ".*" + [regex]::Escape($protected)) {
        throw "Removal script contains a deletion path for protected user data: $protected"
    }
}
if ($remover -match 'rm\s+-rf\s+[^\r\n]*BigScreen[\x27\x22]?\s*$') {
    throw "Removal script contains a broad BigScreen data-root deletion."
}
if ($remover -notmatch [regex]::Escape("Also remove Big Screen's downloaded videos? [y/N]") -or
    $remover -notmatch [regex]::Escape('$expectedVideosPath = "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Videos"')) {
    throw "Removal script does not gate the exact managed-video directory behind confirmation."
}

Write-Output "Source ownership classifier and removal-safety tests passed."
