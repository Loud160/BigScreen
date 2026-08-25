# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# Direct ADB deployment bypasses the QMOD installer's dependency resolver. This
# preflight therefore reads the same generated mod.json requirements that MBF
# consumes, checks the selected Quest's package registrations, verifies the
# registered payload files, and refuses to copy Big Screen when its shared ABI
# dependencies cannot be proven compatible.

function Get-BigScreenDependencyProperty($Object, [string]$Name, $Default = $null) {
    if (-not $Object) { return $Default }
    $property = $Object.PSObject.Properties[$Name]
    if (-not $property -or $null -eq $property.Value) { return $Default }
    return $property.Value
}

function ConvertTo-BigScreenSemanticVersion([string]$Text) {
    $value = $Text.Trim()
    if ($value.StartsWith("v", [StringComparison]::OrdinalIgnoreCase)) {
        $value = $value.Substring(1)
    }
    if ($value -notmatch '^(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)(?:-(?<prerelease>[0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$') {
        throw "Unsupported semantic version '$Text'."
    }
    return [pscustomobject]@{
        Major = [UInt64]$Matches.major
        Minor = [UInt64]$Matches.minor
        Patch = [UInt64]$Matches.patch
        Prerelease = [string]$Matches.prerelease
        Original = $Text
    }
}

function Compare-BigScreenSemanticVersion($Left, $Right) {
    foreach ($name in @("Major", "Minor", "Patch")) {
        if ($Left.$name -lt $Right.$name) { return -1 }
        if ($Left.$name -gt $Right.$name) { return 1 }
    }
    $leftPrerelease = [string]$Left.Prerelease
    $rightPrerelease = [string]$Right.Prerelease
    if (-not $leftPrerelease -and -not $rightPrerelease) { return 0 }
    if (-not $leftPrerelease) { return 1 }
    if (-not $rightPrerelease) { return -1 }
    return [StringComparer]::OrdinalIgnoreCase.Compare($leftPrerelease, $rightPrerelease)
}

function Test-BigScreenSemanticVersionRange([string]$Version, [string]$Range) {
    $candidate = ConvertTo-BigScreenSemanticVersion $Version
    $requirement = $Range.Trim()
    if ($requirement.StartsWith("^")) {
        $lower = ConvertTo-BigScreenSemanticVersion $requirement.Substring(1)
        if ($lower.Major -gt 0) {
            $upper = ConvertTo-BigScreenSemanticVersion "$($lower.Major + 1).0.0"
        }
        elseif ($lower.Minor -gt 0) {
            $upper = ConvertTo-BigScreenSemanticVersion "0.$($lower.Minor + 1).0"
        }
        else {
            $upper = ConvertTo-BigScreenSemanticVersion "0.0.$($lower.Patch + 1)"
        }
        return (Compare-BigScreenSemanticVersion $candidate $lower) -ge 0 -and
            (Compare-BigScreenSemanticVersion $candidate $upper) -lt 0
    }
    if ($requirement.StartsWith("=")) {
        $requirement = $requirement.Substring(1)
    }
    $exact = ConvertTo-BigScreenSemanticVersion $requirement
    return (Compare-BigScreenSemanticVersion $candidate $exact) -eq 0
}

function Get-BigScreenDependencyRequirements([string]$ManifestPath) {
    if (-not (Test-Path -LiteralPath $ManifestPath)) {
        throw "Big Screen dependency manifest was not found: $ManifestPath"
    }
    try {
        $manifest = Get-Content -LiteralPath $ManifestPath -Raw |
            ConvertFrom-Json -ErrorAction Stop
    }
    catch {
        throw "Big Screen dependency manifest is invalid: $($_.Exception.Message)"
    }
    $requirements = @()
    foreach ($dependency in @(Get-BigScreenDependencyProperty $manifest "dependencies" @())) {
        $id = [string](Get-BigScreenDependencyProperty $dependency "id" "")
        $range = [string](Get-BigScreenDependencyProperty $dependency "version" "")
        if (-not $id -or -not $range) {
            throw "Big Screen's generated dependency manifest contains an incomplete dependency entry."
        }
        # Fail here, before touching the Quest, if a future QPM range is added
        # without teaching this source-deployment guard how to interpret it.
        [void](Test-BigScreenSemanticVersionRange $range.TrimStart('^', '=') $range)
        $requirements += [pscustomobject]@{ Id = $id; VersionRange = $range }
    }
    return @($requirements)
}

function Invoke-BigScreenDependencyAdb {
    param(
        [Parameter(Mandatory=$true)][string[]]$Arguments,
        [switch]$AllowFailure
    )
    if ([string]::IsNullOrWhiteSpace($env:ANDROID_SERIAL)) {
        throw "No Quest serial was selected before the dependency check."
    }
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = & adb -s $env:ANDROID_SERIAL @Arguments 2>&1
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
    $text = ($output | ForEach-Object { $_.ToString() }) -join "`n"
    if (-not $AllowFailure -and $code -ne 0) {
        throw "ADB failed while checking Quest dependencies.`n$text"
    }
    return [pscustomobject]@{ ExitCode = $code; Text = $text }
}

function Assert-BigScreenDependencyRemotePath([string]$Path) {
    $root = "/sdcard/ModData/com.beatgames.beatsaber/"
    if ([string]::IsNullOrWhiteSpace($Path) -or
        -not $Path.StartsWith($root, [StringComparison]::Ordinal) -or
        $Path.Contains("'") -or $Path.Contains('"') -or
        $Path.Contains("`r") -or $Path.Contains("`n") -or
        @($Path.Split('/') | Where-Object { $_ -eq ".." }).Count -gt 0) {
        throw "Unsafe Quest dependency path: $Path"
    }
}

function Test-BigScreenDependencyRemoteFile([string]$Path) {
    Assert-BigScreenDependencyRemotePath $Path
    $result = Invoke-BigScreenDependencyAdb @("shell", "test -f '$Path'") -AllowFailure
    if ($result.ExitCode -ne 0 -and -not [string]::IsNullOrWhiteSpace($result.Text)) {
        throw "ADB could not inspect Quest dependency file $Path.`n$($result.Text)"
    }
    return $result.ExitCode -eq 0
}

function Get-BigScreenQuestDependencyPackages([string]$GameVersion) {
    if ($GameVersion -notmatch '^[0-9A-Za-z._-]+$') {
        throw "Unsafe Beat Saber package version: $GameVersion"
    }
    $modData = "/sdcard/ModData/com.beatgames.beatsaber"
    $packageRoot = "$modData/Packages/$GameVersion"
    $rootProbe = Invoke-BigScreenDependencyAdb @("shell", "test -d '$packageRoot'") -AllowFailure
    if ($rootProbe.ExitCode -ne 0) { return @() }
    $listing = Invoke-BigScreenDependencyAdb @(
        "shell", "find '$packageRoot' -type f -name mod.json -print 2>/dev/null")
    $packages = @()
    foreach ($manifestPath in ($listing.Text -split "`r?`n")) {
        $manifestPath = $manifestPath.Trim()
        if (-not $manifestPath) { continue }
        Assert-BigScreenDependencyRemotePath $manifestPath
        $raw = Invoke-BigScreenDependencyAdb @("exec-out", "cat", $manifestPath)
        try {
            $manifest = $raw.Text | ConvertFrom-Json -ErrorAction Stop
        }
        catch {
            Write-Warning "Skipping unreadable Quest package manifest $manifestPath"
            continue
        }
        $id = [string](Get-BigScreenDependencyProperty $manifest "id" "")
        $version = [string](Get-BigScreenDependencyProperty $manifest "version" "")
        if (-not $id -or -not $version) { continue }

        $requiredFiles = @()
        foreach ($name in @(Get-BigScreenDependencyProperty $manifest "modFiles" @())) {
            if ([IO.Path]::GetFileName([string]$name) -ne [string]$name) {
                throw "Unsafe modFiles entry in $manifestPath"
            }
            $requiredFiles += "$modData/Modloader/early_mods/$name"
        }
        foreach ($name in @(Get-BigScreenDependencyProperty $manifest "lateModFiles" @())) {
            if ([IO.Path]::GetFileName([string]$name) -ne [string]$name) {
                throw "Unsafe lateModFiles entry in $manifestPath"
            }
            $requiredFiles += "$modData/Modloader/mods/$name"
        }
        foreach ($name in @(Get-BigScreenDependencyProperty $manifest "libraryFiles" @())) {
            if ([IO.Path]::GetFileName([string]$name) -ne [string]$name) {
                throw "Unsafe libraryFiles entry in $manifestPath"
            }
            $requiredFiles += "$modData/Modloader/libs/$name"
        }
        foreach ($copy in @(Get-BigScreenDependencyProperty $manifest "fileCopies" @())) {
            $destination = [string](Get-BigScreenDependencyProperty $copy "destination" "")
            if ($destination) {
                Assert-BigScreenDependencyRemotePath $destination
                $requiredFiles += $destination
            }
        }
        $missingFiles = @($requiredFiles | Sort-Object -Unique | Where-Object {
            -not (Test-BigScreenDependencyRemoteFile $_)
        })
        $packages += [pscustomobject]@{
            Id = $id
            Version = $version
            ManifestPath = $manifestPath
            MissingFiles = $missingFiles
        }
    }
    return @($packages)
}

function Get-BigScreenDependencyStatuses($Requirements, $Packages) {
    $statuses = @()
    foreach ($requirement in @($Requirements)) {
        $matches = @($Packages | Where-Object { $_.Id -eq $requirement.Id })
        $compatible = @($matches | Where-Object {
            Test-BigScreenSemanticVersionRange ([string]$_.Version) ([string]$requirement.VersionRange)
        })
        $complete = @($compatible | Where-Object { @($_.MissingFiles).Count -eq 0 })
        if ($complete.Count -gt 0) {
            $selected = $complete[0]
            $statuses += [pscustomobject]@{
                Id = $requirement.Id
                VersionRange = $requirement.VersionRange
                Satisfied = $true
                InstalledVersion = [string]$selected.Version
                Message = ""
            }
            continue
        }
        if ($compatible.Count -gt 0) {
            $missing = @($compatible | ForEach-Object { @($_.MissingFiles) } |
                Sort-Object -Unique)
            $message = "compatible package metadata exists, but its payload is incomplete"
            if ($missing.Count -gt 0) {
                $message += ": " + (($missing | ForEach-Object { [IO.Path]::GetFileName($_) }) -join ", ")
            }
        }
        elseif ($matches.Count -gt 0) {
            $versions = ($matches | ForEach-Object { [string]$_.Version } |
                Sort-Object -Unique) -join ", "
            $message = "installed version $versions does not satisfy $($requirement.VersionRange)"
        }
        else {
            $message = "not installed or not registered for this Beat Saber version"
        }
        $statuses += [pscustomobject]@{
            Id = $requirement.Id
            VersionRange = $requirement.VersionRange
            Satisfied = $false
            InstalledVersion = ""
            Message = $message
        }
    }
    return @($statuses)
}

function Assert-BigScreenQuestDependencies(
    [string]$ManifestPath,
    [string]$GameVersion) {
    Write-Output ""
    Write-Output "Checking required Quest mod dependencies before building or deploying Big Screen..."
    $requirements = @(Get-BigScreenDependencyRequirements $ManifestPath)
    $packages = @(Get-BigScreenQuestDependencyPackages $GameVersion)
    $statuses = @(Get-BigScreenDependencyStatuses $requirements $packages)
    foreach ($status in $statuses | Where-Object Satisfied) {
        Write-Host ("  OK  {0} {1} (requires {2})" -f
            $status.Id, $status.InstalledVersion, $status.VersionRange) -ForegroundColor Green
    }
    $failures = @($statuses | Where-Object { -not $_.Satisfied })
    if ($failures.Count -gt 0) {
        Write-Output ""
        Write-Host "Required Quest dependencies are missing, outdated, or incomplete:" -ForegroundColor Red
        foreach ($failure in $failures) {
            Write-Host ("  - {0} {1}: {2}" -f
                $failure.Id, $failure.VersionRange, $failure.Message) -ForegroundColor Red
        }
        throw "Source deployment was stopped before any Big Screen files changed. Open ModsBeforeFriday, install or update the dependencies listed above for Beat Saber $GameVersion, then run Build-And-Deploy.bat again."
    }
    Write-Output "All required Quest mod dependencies are present and compatible."
}
