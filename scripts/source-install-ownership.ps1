# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen. Distributed under GPL-3.0-only with additional terms
# under GPLv3 section 7(b)/(c) and an interoperability permission under
# section 7; see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Shared by source deployment, source removal, and isolated policy tests.
# It deliberately owns decisions and receipts only; callers own build UX.
Set-StrictMode -Version 2.0

$script:BigScreenPackage = "com.beatgames.beatsaber"
$script:BigScreenModData = "/sdcard/ModData/$($script:BigScreenPackage)"
$script:SourceInstallRoot = "$($script:BigScreenModData)/BigScreen/SourceInstall"
$script:CompleteReceiptPath = "$($script:SourceInstallRoot)/source-install.json"
$script:PartialReceiptPath = "$($script:SourceInstallRoot)/source-install.partial.json"
$script:BaselineRoot = "$($script:SourceInstallRoot)/Baseline"

function Get-BigScreenJsonArray($Object, [string]$Name) {
    if (-not $Object) { return @() }
    $property = $Object.PSObject.Properties[$Name]
    if (-not $property -or $null -eq $property.Value) { return @() }
    return @($property.Value)
}

function Get-BigScreenObjectProperty($Object, [string]$Name, $Default = $null) {
    if (-not $Object) { return $Default }
    $property = $Object.PSObject.Properties[$Name]
    if (-not $property -or $null -eq $property.Value) { return $Default }
    return $property.Value
}

function Test-BigScreenExclusiveLibraryName([string]$Name) {
    # libbeatsaber-hook is supplied by the shared Quest mod ecosystem. Every
    # other current library below is built/bundled specifically by Big Screen
    # and must be reversible with its source deployment.
    return $Name.StartsWith("libbigscreen-") -or
        $Name.StartsWith("libavformat-bigscreen") -or
        $Name.StartsWith("libavcodec-bigscreen") -or
        $Name.StartsWith("libavutil-bigscreen") -or
        $Name.StartsWith("libswscale-bigscreen") -or
        $Name -in @(
            "libpython3.14.so",
            "libssl_python.so",
            "libcrypto_python.so",
            "libsqlite3_python.so")
}

function Invoke-BigScreenAdb {
    param([Parameter(Mandatory=$true)][string[]]$Arguments, [switch]$AllowFailure)
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = & adb @Arguments 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    $text = ($output | ForEach-Object { $_.ToString() }) -join "`n"
    if (-not $AllowFailure -and $code -ne 0) {
        throw "ADB failed: adb $($Arguments -join ' ')`n$text"
    }
    [pscustomobject]@{ ExitCode = $code; Text = $text }
}

function Assert-BigScreenRemotePath([string]$Path) {
    $root = "$($script:BigScreenModData)/"
    if ([string]::IsNullOrWhiteSpace($Path) -or
        -not $Path.StartsWith($root, [StringComparison]::Ordinal) -or
        $Path.Contains("'") -or $Path.Contains('"') -or
        $Path.Contains("`r") -or $Path.Contains("`n") -or
        @($Path.Split('/') | Where-Object { $_ -eq ".." }).Count -gt 0) {
        throw "Unsafe Quest path in Big Screen ownership metadata: $Path"
    }
}

function Test-BigScreenRemoteFile([string]$Path) {
    Assert-BigScreenRemotePath $Path
    $result = Invoke-BigScreenAdb @("shell", "test -f '$Path'") -AllowFailure
    return $result.ExitCode -eq 0
}

function Test-BigScreenRemoteDirectory([string]$Path) {
    Assert-BigScreenRemotePath $Path
    $result = Invoke-BigScreenAdb @("shell", "test -d '$Path'") -AllowFailure
    return $result.ExitCode -eq 0
}

function Get-BigScreenRemoteHash([string]$Path) {
    if (-not (Test-BigScreenRemoteFile $Path)) { return $null }
    $result = Invoke-BigScreenAdb @("shell", "sha256sum '$Path'") -AllowFailure
    if ($result.ExitCode -ne 0 -or $result.Text -notmatch '^([0-9A-Fa-f]{64})') {
        return $null
    }
    return $Matches[1].ToLowerInvariant()
}

function Get-BigScreenRemoteJson([string]$Path) {
    Assert-BigScreenRemotePath $Path
    $result = Invoke-BigScreenAdb @("exec-out", "cat", $Path) -AllowFailure
    if ($result.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($result.Text)) {
        return $null
    }
    try { return $result.Text | ConvertFrom-Json } catch { return $null }
}

function Resolve-BigScreenInstallState {
    param(
        [bool]$HasCompleteReceipt,
        [bool]$HasPartialReceipt,
        [bool]$HasMbfMetadata,
        [bool]$MbfPayloadComplete,
        [int]$LegacyPhaseCopies,
        [bool]$HasLegacyRuntime = $false,
        [bool]$ReceiptUnreadable = $false,
        [bool]$HasUnexpectedPhaseCopy = $false
    )
    if ($ReceiptUnreadable -or
        (($HasCompleteReceipt -or $HasPartialReceipt) -and
         $HasUnexpectedPhaseCopy)) {
        return "MIXED_OR_AMBIGUOUS"
    }
    if ($HasMbfMetadata -and ($HasCompleteReceipt -or $HasPartialReceipt)) {
        return "MIXED_OR_AMBIGUOUS"
    }
    if ($HasMbfMetadata) {
        return $(if ($MbfPayloadComplete) { "MBF_MANAGED" } else { "MBF_REGISTERED_NOT_INSTALLED" })
    }
    if ($HasPartialReceipt) { return "SOURCE_PARTIAL" }
    if ($HasCompleteReceipt) { return "SOURCE_MANAGED" }
    if ($LegacyPhaseCopies -gt 1) { return "MIXED_OR_AMBIGUOUS" }
    if ($LegacyPhaseCopies -eq 1 -or $HasLegacyRuntime) {
        return "LEGACY_SOURCE"
    }
    return "NOT_INSTALLED"
}

function Get-MbfBigScreenRegistration([string]$GameVersion) {
    $packageRoot = "$($script:BigScreenModData)/Packages/$GameVersion"
    $listing = Invoke-BigScreenAdb @(
        "shell", "find '$packageRoot' -type f -name mod.json -print 2>/dev/null") -AllowFailure
    $matches = @()
    foreach ($path in ($listing.Text -split "`r?`n")) {
        $path = $path.Trim()
        if (-not $path) { continue }
        $manifest = Get-BigScreenRemoteJson $path
        if (-not $manifest -or [string]$manifest.id -ne "bigscreen") { continue }
        $required = @()
        foreach ($name in @(Get-BigScreenJsonArray $manifest "modFiles")) {
            $required += "$($script:BigScreenModData)/Modloader/early_mods/$name"
        }
        foreach ($name in @(Get-BigScreenJsonArray $manifest "lateModFiles")) {
            $required += "$($script:BigScreenModData)/Modloader/mods/$name"
        }
        foreach ($name in @(Get-BigScreenJsonArray $manifest "libraryFiles")) {
            $required += "$($script:BigScreenModData)/Modloader/libs/$name"
        }
        foreach ($copy in @(Get-BigScreenJsonArray $manifest "fileCopies")) {
            if ($copy.destination) { $required += [string]$copy.destination }
        }
        $installed = $required.Count -gt 0
        foreach ($requiredPath in $required) {
            if (-not (Test-BigScreenRemoteFile $requiredPath)) { $installed = $false; break }
        }
        $matches += [pscustomobject]@{
            ManifestPath = $path
            Installed = $installed
            RequiredFiles = $required
        }
    }
    return @($matches)
}

function Get-BigScreenInstallClassification([string]$GameVersion) {
    $hasComplete = Test-BigScreenRemoteFile $script:CompleteReceiptPath
    $hasPartial = Test-BigScreenRemoteFile $script:PartialReceiptPath
    $completeReceipt = if ($hasComplete) {
        Get-BigScreenRemoteJson $script:CompleteReceiptPath
    } else { $null }
    $partialReceipt = if ($hasPartial) {
        Get-BigScreenRemoteJson $script:PartialReceiptPath
    } else { $null }
    $mbf = @(Get-MbfBigScreenRegistration $GameVersion)
    $legacyPaths = @(
        "$($script:BigScreenModData)/Modloader/early_mods/libbigscreen.so",
        "$($script:BigScreenModData)/Modloader/mods/libbigscreen.so",
        "$($script:BigScreenModData)/Mods/libbigscreen.so"
    )
    $presentLegacyPaths = @($legacyPaths | Where-Object {
        Test-BigScreenRemoteFile $_
    })
    # Old Build & Deploy revisions installed this private runtime without an
    # ownership receipt. Treat a runtime-only remnant as a legacy source
    # install so removal/migration does not mistake it for an uninstalled mod
    # and later preserve every runtime file as an external baseline.
    $legacyRuntimeRoot = "$($script:BigScreenModData)/BigScreen/Runtime"
    $hasLegacyRuntime = Test-BigScreenRemoteDirectory $legacyRuntimeRoot
    $receiptPaths = @{}
    foreach ($receipt in @($completeReceipt, $partialReceipt)) {
        if (-not $receipt) { continue }
        foreach ($item in @($receipt.files)) {
            $receiptPaths[[string]$item.path] = $true
        }
    }
    $unexpectedPhasePaths = @()
    if ($hasComplete -or $hasPartial) {
        $unexpectedPhasePaths = @($presentLegacyPaths | Where-Object {
            -not $receiptPaths.ContainsKey([string]$_)
        })
    }
    $receiptUnreadable = (($hasComplete -and -not $completeReceipt) -or
        ($hasPartial -and -not $partialReceipt))
    $state = Resolve-BigScreenInstallState `
        -HasCompleteReceipt $hasComplete `
        -HasPartialReceipt $hasPartial `
        -HasMbfMetadata ($mbf.Count -gt 0) `
        -MbfPayloadComplete (@($mbf | Where-Object Installed).Count -gt 0) `
        -LegacyPhaseCopies $presentLegacyPaths.Count `
        -HasLegacyRuntime $hasLegacyRuntime `
        -ReceiptUnreadable $receiptUnreadable `
        -HasUnexpectedPhaseCopy ($unexpectedPhasePaths.Count -gt 0)
    [pscustomobject]@{
        State = $state
        MbfPackages = $mbf
        LegacyPaths = $presentLegacyPaths
        HasLegacyRuntime = $hasLegacyRuntime
        UnexpectedPhasePaths = $unexpectedPhasePaths
        ReceiptUnreadable = $receiptUnreadable
        CompleteReceipt = $completeReceipt
        PartialReceipt = $partialReceipt
    }
}

function Write-BigScreenOwnershipDiagnostic($Classification) {
    Write-Output "Ownership diagnostic:"
    Write-Output "  State: $($Classification.State)"
    Write-Output "  Complete source receipt: $([bool]$Classification.CompleteReceipt)"
    Write-Output "  Partial source receipt: $([bool]$Classification.PartialReceipt)"
    foreach ($package in @($Classification.MbfPackages)) {
        Write-Output "  MBF manifest: $($package.ManifestPath) (payload complete: $($package.Installed))"
    }
    foreach ($path in @($Classification.LegacyPaths)) {
        Write-Output "  Native Big Screen path: $path"
    }
}

function Get-BigScreenDeploymentPlan {
    param(
        [Parameter(Mandatory=$true)]$Manifest,
        [Parameter(Mandatory=$true)][string]$RuntimeStage,
        [switch]$UseDebug
    )
    $plan = @()
    foreach ($name in @(Get-BigScreenJsonArray $Manifest "modFiles")) {
        $local = Join-Path "build" $name
        if ($UseDebug) { $local = Join-Path "build/debug" $name }
        $plan += [pscustomobject]@{ LocalPath=$local; Path="$($script:BigScreenModData)/Modloader/early_mods/$name"; Category="EarlyMod"; Ownership="BigScreenExclusive" }
    }
    foreach ($name in @(Get-BigScreenJsonArray $Manifest "lateModFiles")) {
        $local = Join-Path "build" $name
        if ($UseDebug) { $local = Join-Path "build/debug" $name }
        $plan += [pscustomobject]@{ LocalPath=$local; Path="$($script:BigScreenModData)/Modloader/mods/$name"; Category="LateMod"; Ownership="BigScreenExclusive" }
    }
    foreach ($name in @(Get-BigScreenJsonArray $Manifest "libraryFiles")) {
        $built = Join-Path "build" $name
        $packaged = Join-Path "extern/libs" $name
        $local = if (Test-Path -LiteralPath $built) { $built } elseif (Test-Path -LiteralPath $packaged) { $packaged } else { $null }
        if (-not $local) { throw "No authoritative source was found for required runtime library $name" }
        $exclusive = Test-BigScreenExclusiveLibraryName $name
        $plan += [pscustomobject]@{ LocalPath=$local; Path="$($script:BigScreenModData)/Modloader/libs/$name"; Category="Library"; Ownership=$(if ($exclusive) { "BigScreenExclusive" } else { "SharedDependency" }) }
    }
    $runtimeDestination = "$($script:BigScreenModData)/BigScreen/Runtime/"
    foreach ($copy in @(Get-BigScreenJsonArray $Manifest "fileCopies")) {
        $destination = [string]$copy.destination
        if (-not $destination.StartsWith($runtimeDestination, [StringComparison]::Ordinal)) {
            throw "Development deployment does not recognize fileCopy destination $destination"
        }
        $relative = $destination.Substring($runtimeDestination.Length).Replace('/', [IO.Path]::DirectorySeparatorChar)
        $local = Join-Path $RuntimeStage $relative
        if (-not (Test-Path -LiteralPath $local -PathType Leaf)) { throw "Missing staged runtime file $local" }
        $plan += [pscustomobject]@{ LocalPath=$local; Path=$destination; Category="DownloaderRuntime"; Ownership="BigScreenExclusive" }
    }
    return @($plan)
}

function Write-BigScreenRemoteReceipt($Receipt, [string]$RemotePath) {
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ("bigscreen-receipt-" + [Guid]::NewGuid().ToString("N") + ".json")
    try {
        # Windows PowerShell's `-Encoding UTF8` emits a BOM. Keep receipts as
        # plain UTF-8 JSON so Android-side tools and future non-PowerShell
        # readers do not need to special-case the leading U+FEFF marker.
        $json = $Receipt | ConvertTo-Json -Depth 8
        [IO.File]::WriteAllText($temporary, $json, (New-Object Text.UTF8Encoding($false)))
        [void](Invoke-BigScreenAdb @("shell", "mkdir -p '$($script:SourceInstallRoot)'") )
        $upload = "$RemotePath.upload"
        [void](Invoke-BigScreenAdb @("push", $temporary, $upload))
        [void](Invoke-BigScreenAdb @("shell", "mv '$upload' '$RemotePath'"))
    } finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}

function New-BigScreenSourceReceipt {
    param(
        [Parameter(Mandatory=$true)]$Plan,
        [Parameter(Mandatory=$true)]$Manifest,
        [Parameter(Mandatory=$true)][string]$SourceCommit,
        $PriorReceipt,
        [ValidateSet("Release", "Debug")]
        [string]$BuildType = "Release"
    )
    $priorByPath = @{}
    if ($PriorReceipt) { foreach ($item in @($PriorReceipt.files)) { $priorByPath[[string]$item.path] = $item } }
    $files = @()
    foreach ($item in $Plan) {
        $localHash = (Get-FileHash -LiteralPath $item.LocalPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $prior = $priorByPath[[string]$item.Path]
        $currentHash = Get-BigScreenRemoteHash $item.Path
        if ($prior) {
            $previousState = [string]$prior.previousState
            $previousHash = $prior.previousSha256
            $backupPath = $prior.previousBackupPath
        } else {
            $previousState = if ($currentHash) { "present" } else { "absent" }
            $previousHash = $currentHash
            $backupPath = if ($currentHash -and
                [string]$item.Ownership -eq "BigScreenExclusive") {
                "$($script:BaselineRoot)/$currentHash.bin"
            } else { $null }
        }
        $priorSourceHashes = @()
        if ($prior) {
            $priorSourceHashes += [string]$prior.installedSha256
            if ([bool](Get-BigScreenObjectProperty `
                    $prior "preDeployWasSourceOwned" $false)) {
                $priorSourceHashes += [string](Get-BigScreenObjectProperty `
                    $prior "preDeploySha256" "")
            }
        }
        $files += [pscustomobject]@{
            path = [string]$item.Path
            category = [string]$item.Category
            ownership = [string]$item.Ownership
            previousState = $previousState
            previousSha256 = $previousHash
            previousBackupPath = $backupPath
            preDeployState = if ($currentHash) { "present" } else { "absent" }
            preDeploySha256 = $currentHash
            preDeployWasSourceOwned = [bool]($currentHash -and
                $priorSourceHashes -contains $currentHash)
            installedSha256 = $localHash
            copyCompleted = $false
        }
    }
    [pscustomobject]@{
        schemaVersion = 1
        state = "partial"
        modId = "bigscreen"
        bigScreenVersion = [string]$Manifest.version
        sourceCommit = $SourceCommit
        buildType = $BuildType
        gameVersion = [string]$Manifest.packageVersion
        installedAtUtc = [DateTime]::UtcNow.ToString("o")
        files = $files
    }
}

function Backup-BigScreenBaseline($Item) {
    if ($Item.previousState -ne "present" -or -not $Item.previousBackupPath) { return }
    if (Test-BigScreenRemoteFile ([string]$Item.previousBackupPath)) { return }
    $parent = ([string]$Item.previousBackupPath).Substring(0, ([string]$Item.previousBackupPath).LastIndexOf('/'))
    [void](Invoke-BigScreenAdb @("shell", "mkdir -p '$parent' && cp '$($Item.path)' '$($Item.previousBackupPath)'"))
    $backupHash = Get-BigScreenRemoteHash ([string]$Item.previousBackupPath)
    if ($backupHash -ne [string]$Item.previousSha256) {
        throw "Could not preserve the pre-source baseline for $($Item.path)."
    }
}

function Install-BigScreenSourcePlan {
    param(
        [Parameter(Mandatory=$true)]$Receipt,
        $PriorReceipt,
        $CurrentPlan
    )
    Write-BigScreenRemoteReceipt $Receipt $script:PartialReceiptPath
    # A planned receipt now exists before any payload destination changes.
    # The prior complete receipt remains the proof for retired paths until the
    # new deployment has verified and atomically replaced it.
    if ($CurrentPlan) {
        Remove-BigScreenRetiredReceiptFiles $PriorReceipt $CurrentPlan
    }
    foreach ($item in @($Receipt.files)) {
        $planItem = @($CurrentPlan | Where-Object {
            [string]$_.Path -eq [string]$item.path
        }) | Select-Object -First 1
        if (-not $planItem) {
            throw "The deployment plan no longer contains receipt path $($item.path)."
        }
        Backup-BigScreenBaseline $item
        [void](Invoke-BigScreenAdb @("push", [string]$planItem.LocalPath, [string]$item.path))
        $remoteHash = Get-BigScreenRemoteHash ([string]$item.path)
        if ($remoteHash -ne [string]$item.installedSha256) {
            throw "Deployment verification failed for $($item.path)."
        }
        $item.copyCompleted = $true
        Write-BigScreenRemoteReceipt $Receipt $script:PartialReceiptPath
        Write-Output "Verified deployed payload: $($item.path) ($remoteHash)"
    }
    $Receipt.state = "complete"
    $Receipt.installedAtUtc = [DateTime]::UtcNow.ToString("o")
    Write-BigScreenRemoteReceipt $Receipt $script:PartialReceiptPath
    [void](Invoke-BigScreenAdb @("shell", "mv '$($script:PartialReceiptPath)' '$($script:CompleteReceiptPath)'"))
}

function Assert-BigScreenPartialRecoverable($Receipt) {
    foreach ($item in @($Receipt.files)) {
        $current = Get-BigScreenRemoteHash ([string]$item.path)
        $preDeployState = [string](Get-BigScreenObjectProperty `
            $item "preDeployState" $item.previousState)
        $preDeployHash = if ($preDeployState -eq "absent") {
            $null
        } else {
            [string](Get-BigScreenObjectProperty `
                $item "preDeploySha256" $item.previousSha256)
        }
        $baselineHash = if ([string]$item.previousState -eq "absent") {
            $null
        } else { [string]$item.previousSha256 }
        if ($current -ne [string]$item.installedSha256 -and
            $current -ne $preDeployHash -and
            $current -ne $baselineHash) {
            throw "Partial source deployment is ambiguous at $($item.path). Current content matches neither the intended source file, the state captured immediately before deployment, nor the original source-development baseline. The file was preserved."
        }
    }
}

function Assert-BigScreenManagedReceiptSafe($Receipt) {
    foreach ($item in @($Receipt.files)) {
        $current = Get-BigScreenRemoteHash ([string]$item.path)
        if ($current -eq [string]$item.installedSha256) { continue }
        # A missing source-exclusive file is recoverable by redeployment only
        # when its baseline was also absent. Changed or restored content has
        # unknown ownership and must never be overwritten automatically.
        if (-not $current -and [string]$item.previousState -eq "absent") { continue }
        throw "Source ownership is ambiguous at $($item.path). Its current hash no longer matches the source receipt, so deployment was refused without changing Quest files."
    }
}

function Remove-BigScreenRetiredReceiptFiles($PriorReceipt, $CurrentPlan) {
    if (-not $PriorReceipt) { return }
    $currentPaths = @{}
    foreach ($item in $CurrentPlan) { $currentPaths[[string]$item.Path] = $true }
    foreach ($item in @($PriorReceipt.files)) {
        if ($currentPaths.ContainsKey([string]$item.path) -or
            [string]$item.ownership -ne "BigScreenExclusive") { continue }
        $current = Get-BigScreenRemoteHash ([string]$item.path)
        if (-not $current) { continue }
        if ($current -ne [string]$item.installedSha256) {
            throw "Retired source payload is ambiguous at $($item.path); it was preserved and deployment was refused."
        }
        if ([string]$item.previousState -eq "absent") {
            [void](Invoke-BigScreenAdb @("shell", "rm -f -- '$($item.path)'"))
        } elseif ($item.previousBackupPath -and
                  (Get-BigScreenRemoteHash ([string]$item.previousBackupPath)) -eq [string]$item.previousSha256) {
            [void](Invoke-BigScreenAdb @("shell", "cp '$($item.previousBackupPath)' '$($item.path)'"))
        } else {
            throw "The baseline for retired source payload $($item.path) is unavailable; it was preserved."
        }
    }
}

function Resolve-BigScreenReceiptRemovalAction {
    param(
        [Parameter(Mandatory=$true)]$Item,
        [AllowNull()][string]$CurrentSha256,
        [switch]$Partial
    )
    if ([string]$Item.ownership -ne "BigScreenExclusive") {
        return "PreserveShared"
    }
    if ($CurrentSha256 -eq [string]$Item.installedSha256) {
        return "RemoveOrRestore"
    }
    if (-not $CurrentSha256) {
        # A missing destination contains no source payload. During a partial
        # install it also exactly represents an originally absent baseline.
        return "AlreadyAbsent"
    }
    if ($Partial -and [string]$Item.previousState -eq "present" -and
        $CurrentSha256 -eq [string]$Item.previousSha256) {
        # The planned copy never happened, or its original bytes were already
        # restored. Treat this as successfully reconciled, not ambiguity.
        return "AlreadyBaseline"
    }
    if ($Partial -and [bool](Get-BigScreenObjectProperty `
            $Item "preDeployWasSourceOwned" $false) -and
        $CurrentSha256 -eq [string](Get-BigScreenObjectProperty `
            $Item "preDeploySha256" "")) {
        # An interrupted repeated source deploy may still contain the prior
        # source build. Its hash was proven by the preceding complete receipt,
        # so removal may still restore the original development baseline.
        return "RemoveOrRestore"
    }
    return "PreserveAmbiguous"
}

function Remove-BigScreenReceiptFiles {
    param(
        [Parameter(Mandatory=$true)]$Receipt,
        [switch]$Partial
    )
    $ambiguous = @()
    foreach ($item in @($Receipt.files)) {
        $current = Get-BigScreenRemoteHash ([string]$item.path)
        $action = Resolve-BigScreenReceiptRemovalAction `
            -Item $item `
            -CurrentSha256 $current `
            -Partial:$Partial
        if ($action -eq "PreserveShared" -or
            $action -eq "AlreadyAbsent" -or
            $action -eq "AlreadyBaseline") { continue }
        if ($action -eq "PreserveAmbiguous") {
            $ambiguous += [string]$item.path
            continue
        }
        if ([string]$item.previousState -eq "absent") {
            [void](Invoke-BigScreenAdb @("shell", "rm -f -- '$($item.path)'"))
        } elseif ($item.previousBackupPath -and
                  (Get-BigScreenRemoteHash ([string]$item.previousBackupPath)) -eq [string]$item.previousSha256) {
            [void](Invoke-BigScreenAdb @("shell", "cp '$($item.previousBackupPath)' '$($item.path)'"))
        } else {
            $ambiguous += [string]$item.path
        }
    }
    return @($ambiguous)
}

function Remove-BigScreenLegacyExclusivePayload {
    param([string[]]$AdditionalPaths = @())
    $paths = @(
        "$($script:BigScreenModData)/Modloader/early_mods/libbigscreen.so",
        "$($script:BigScreenModData)/Modloader/mods/libbigscreen.so",
        "$($script:BigScreenModData)/Mods/libbigscreen.so"
    ) + $AdditionalPaths
    foreach ($path in ($paths | Sort-Object -Unique)) {
        # Never accept a directory or wildcard from a caller.
        if (-not $path -or $path.Contains('*') -or $path.EndsWith('/')) { throw "Unsafe legacy cleanup path: $path" }
        [void](Invoke-BigScreenAdb @("shell", "rm -f -- '$path'"))
        if (Test-BigScreenRemoteFile $path) {
            throw "Legacy cleanup could not remove the exact Big Screen payload $path."
        }
    }
}

function Remove-BigScreenLegacyRuntimePayload {
    # This literal directory contains only the embedded downloader runtime.
    # Videos, library.json, thumbnails, logs, and settings are sibling paths
    # and are deliberately outside this exact recursive cleanup target.
    $runtimeRoot = "$($script:BigScreenModData)/BigScreen/Runtime"
    $expected = "/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Runtime"
    if ($runtimeRoot -ne $expected) {
        throw "Refusing unexpected legacy runtime cleanup target: $runtimeRoot"
    }
    [void](Invoke-BigScreenAdb @("shell", "rm -rf -- '$runtimeRoot'"))
    if (Test-BigScreenRemoteDirectory $runtimeRoot) {
        throw "Legacy cleanup could not remove Big Screen's private Runtime directory."
    }
}
