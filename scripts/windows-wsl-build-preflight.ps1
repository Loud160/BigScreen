# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

<#
.SYNOPSIS
Audits and, after explicit approval by the root BAT, installs the Windows/WSL
host prerequisites for Big Screen's Visual-Studio-free build path.

.DESCRIPTION
Audit mode is strictly read-only. It reports each prerequisite and cached
project input as READY or MISSING before the launcher asks for permission to
change anything. Missing Windows features are kept separate because enabling
WSL or installing Ubuntu opens a Windows UAC prompt and may require a reboot.

Install mode changes only missing operating-system prerequisites. It installs
Ubuntu through WSL when necessary and installs the disclosed apt packages.
The complete build runs with Bash and Python already supplied by Ubuntu;
PowerShell 7, Visual Studio, Git, and a separate archive program are not build
prerequisites. Windows PowerShell 5.1 runs only this thin audit/deployment
wrapper. Project-specific inputs remain owned by the hash-verifying build scripts
and are downloaded only when their individual audit entry says MISSING.
#>

[CmdletBinding()]
param(
    [ValidateSet("Audit", "Install")]
    [string] $Mode = "Audit",

    [switch] $IncludeDeployment
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$preferredDistro = "Ubuntu-24.04"
$supportedUbuntuVersions = @("22.04", "24.04")
$linuxPackages = @(
    "build-essential",
    "ca-certificates",
    "cmake",
    "curl",
    "ffmpeg",
    "libavcodec-dev",
    "libavformat-dev",
    "libavutil-dev",
    "libswscale-dev",
    "ninja-build",
    "pkg-config",
    "python3",
    "unzip",
    "xz-utils"
)

function ConvertTo-CleanLines {
    param([object[]] $InputLines)

    return @($InputLines | ForEach-Object {
        ([string] $_).Replace(([string] [char]0), "").Trim()
    } | Where-Object { $_ })
}

function Get-WslExecutable {
    $command = Get-Command wsl.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $systemCandidate = Join-Path $env:SystemRoot "System32\wsl.exe"
    if (Test-Path -LiteralPath $systemCandidate -PathType Leaf) {
        return $systemCandidate
    }
    return $null
}

function Get-WslDistributions {
    param([string] $WslExecutable)

    if (-not $WslExecutable) { return @() }
    $raw = @(& $WslExecutable --list --quiet 2>$null)
    if ($LASTEXITCODE -ne 0) { return @() }
    return @(ConvertTo-CleanLines $raw)
}

function Get-DistroFacts {
    param(
        [string] $WslExecutable,
        [string] $DistroName
    )

    if (-not $WslExecutable -or -not $DistroName) { return $null }
    $osRelease = @(ConvertTo-CleanLines @(
        & $WslExecutable -d $DistroName -e cat /etc/os-release 2>$null))
    if ($LASTEXITCODE -ne 0 -or $osRelease.Count -eq 0) { return $null }
    $architecture = @(ConvertTo-CleanLines @(
        & $WslExecutable -d $DistroName -e uname -m 2>$null) |
        Select-Object -First 1)
    if ($LASTEXITCODE -ne 0 -or $architecture.Count -eq 0) { return $null }

    $values = @{}
    foreach ($line in $osRelease) {
        if ($line -match '^(?<key>[A-Z_]+)=(?<value>.*)$') {
            $values[$Matches.key] = $Matches.value.Trim('"')
        }
    }
    if (-not $values.ContainsKey("ID") -or
        -not $values.ContainsKey("VERSION_ID")) { return $null }
    $description = if ($values.ContainsKey("PRETTY_NAME")) {
        $values["PRETTY_NAME"]
    } else {
        "$($values['ID']) $($values['VERSION_ID'])"
    }
    return [pscustomobject]@{
        Name = $DistroName
        Id = $values["ID"]
        Version = $values["VERSION_ID"]
        Architecture = $architecture[0]
        Description = $description
        Supported = ($values["ID"] -eq "ubuntu" -and
            $supportedUbuntuVersions -contains $values["VERSION_ID"] -and
            $architecture[0] -eq "x86_64")
    }
}

function Select-BuildDistro {
    param(
        [string] $WslExecutable,
        [string[]] $DistributionNames
    )

    # Keep the Windows wrapper deterministic: these are the two standard names
    # it can invoke without passing PowerShell-captured Unicode back through
    # cmd.exe. Custom-renamed distributions are intentionally not selected.
    $orderedNames = @($preferredDistro, "Ubuntu") | Where-Object {
        $DistributionNames -contains $_
    }
    foreach ($name in $orderedNames) {
        $facts = Get-DistroFacts -WslExecutable $WslExecutable -DistroName $name
        if ($facts -and $facts.Supported) { return $facts }
    }
    return $null
}

function Test-LinuxPackage {
    param(
        [string] $WslExecutable,
        [string] $DistroName,
        [string] $PackageName
    )

    & $WslExecutable -d $DistroName -e bash -lc `
        "dpkg-query -W -f='`${Status}' -- '$PackageName' 2>/dev/null | grep -qx 'install ok installed'" 2>$null
    return $LASTEXITCODE -eq 0
}

function Test-WslCommand {
    param(
        [string] $WslExecutable,
        [string] $DistroName,
        [string] $CommandName
    )

    & $WslExecutable -d $DistroName -e bash -lc `
        "command -v '$CommandName' >/dev/null 2>&1" 2>$null
    return $LASTEXITCODE -eq 0
}

function Test-WslPath {
    param(
        [string] $WslExecutable,
        [string] $DistroName,
        [string] $ShellTest
    )

    & $WslExecutable -d $DistroName -e bash -lc $ShellTest 2>$null
    return $LASTEXITCODE -eq 0
}

function Test-WslQpmInputs {
    param(
        [string] $WslExecutable,
        [string] $DistroName,
        [string] $WindowsRepositoryRoot,
        [string] $ExpectedLockHash
    )

    # QPM's Linux restore creates Linux symlinks under extern/. Windows
    # Test-Path cannot follow those targets and would falsely report the cache
    # as missing. Probe from the selected build environment, passing paths as
    # positional arguments so spaces and parentheses in source ZIP folders are
    # not interpreted as shell syntax.
    $linuxRootLines = @(ConvertTo-CleanLines @(
        & $WslExecutable -d $DistroName -e wslpath -a $WindowsRepositoryRoot 2>$null))
    if ($LASTEXITCODE -ne 0 -or $linuxRootLines.Count -eq 0) { return $false }
    $probe = @'
root="$1"
expected="$2"
[[ -f "$root/.cache/qpm-restore.sha256" ]] &&
grep -qx "$expected" "$root/.cache/qpm-restore.sha256" &&
[[ -f "$root/.cache/qpm-restore-host.txt" ]] &&
grep -qx "linux" "$root/.cache/qpm-restore-host.txt" &&
[[ -f "$root/extern.cmake" ]] &&
[[ -f "$root/qpm_defines.cmake" ]] &&
[[ -f "$root/extern/libs/libbeatsaber-hook.so" ]] &&
[[ -f "$root/extern/libs/libbsml.so" ]] &&
[[ -f "$root/extern/libs/libpaper2_scotland2.so" ]] &&
[[ -f "$root/extern/libs/libsl2.so" ]] &&
[[ -f "$root/extern/libs/libsongcore.so" ]] &&
[[ -f "$root/extern/includes/rapidjson/rapidjson/include/rapidjson/document.h" ]]
'@
    & $WslExecutable -d $DistroName -e bash -lc $probe bigscreen-audit `
        $linuxRootLines[0] $ExpectedLockHash 2>$null
    return $LASTEXITCODE -eq 0
}

function Find-Adb {
    $command = Get-Command adb.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $candidates = New-Object System.Collections.Generic.List[string]
    $candidates.Add((Join-Path $repositoryRoot "BigScreen Tools\platform-tools\adb.exe"))
    foreach ($sdkRoot in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)) {
        if ($sdkRoot) { $candidates.Add((Join-Path $sdkRoot "platform-tools\adb.exe")) }
    }
    if ($env:LOCALAPPDATA) {
        $candidates.Add((Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"))
        $candidates.Add((Join-Path $env:LOCALAPPDATA "Programs\SideQuest\resources\app.asar.unpacked\build\platform-tools\adb.exe"))
        $candidates.Add((Join-Path $env:LOCALAPPDATA "SideQuest\resources\app.asar.unpacked\build\platform-tools\adb.exe"))
    }
    return $candidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
}

function Test-RepositoryPath([string] $RelativePath) {
    return Test-Path -LiteralPath (Join-Path $repositoryRoot $RelativePath)
}

function Get-FileSha256 {
    param([string] $Path)

    # Use the .NET API directly rather than relying on Get-FileHash module
    # autoloading. This keeps the public BAT reliable even when a parent process
    # supplied a nonstandard PSModulePath to Windows PowerShell 5.1.
    $stream = [IO.File]::OpenRead($Path)
    try {
        $sha256 = [Security.Cryptography.SHA256]::Create()
        try {
            return ([BitConverter]::ToString(
                $sha256.ComputeHash($stream))).Replace("-", "").ToLowerInvariant()
        } finally {
            $sha256.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

function Get-PrerequisiteState {
    $wslExecutable = Get-WslExecutable
    $distributions = @(Get-WslDistributions $wslExecutable)
    $distro = Select-BuildDistro -WslExecutable $wslExecutable `
        -DistributionNames $distributions

    $missingPackages = New-Object System.Collections.Generic.List[string]
    $installedPackages = New-Object System.Collections.Generic.List[string]
    if ($distro) {
        foreach ($package in $linuxPackages) {
            if (Test-LinuxPackage -WslExecutable $wslExecutable `
                -DistroName $distro.Name -PackageName $package) {
                $installedPackages.Add($package)
            } else {
                $missingPackages.Add($package)
            }
        }
    }

    $qpmReady = $false
    $ndkReady = $false
    if ($distro) {
        $qpmReady = Test-WslPath -WslExecutable $wslExecutable `
            -DistroName $distro.Name `
            -ShellTest 'test -x "$HOME/.cache/bigscreen-toolchains/qpm-1.5.11/qpm"'
        $wslHome = @(ConvertTo-CleanLines @(
            & $wslExecutable -d $distro.Name -e printenv HOME 2>$null) |
            Select-Object -First 1)
        if ($LASTEXITCODE -eq 0 -and $wslHome.Count -gt 0) {
            $ndkPropertiesPath = "$($wslHome[0])/.cache/bigscreen-toolchains/android-ndk-r27d/source.properties"
            $ndkProperties = @(ConvertTo-CleanLines @(
                & $wslExecutable -d $distro.Name -e cat $ndkPropertiesPath 2>$null))
            $ndkReady = $LASTEXITCODE -eq 0 -and
                ($ndkProperties -match '^Pkg\.Revision\s*=\s*27\.3\.13750724\s*$').Count -gt 0
        }
    }

    $qpmLockHash = Get-FileSha256 `
        -Path (Join-Path $repositoryRoot "qpm.shared.json")
    $qpmInputsReady = $false
    if ($distro) {
        $qpmInputsReady = Test-WslQpmInputs -WslExecutable $wslExecutable `
            -DistroName $distro.Name -WindowsRepositoryRoot $repositoryRoot `
            -ExpectedLockHash $qpmLockHash
    }

    return [pscustomobject]@{
        WslExecutable = $wslExecutable
        Distro = $distro
        DistributionNames = $distributions
        InstalledPackages = @($installedPackages)
        MissingPackages = @($missingPackages)
        QpmReady = $qpmReady
        NdkReady = $ndkReady
        QpmInputsReady = $qpmInputsReady
        Ffmpeg44Ready = (Test-RepositoryPath ".cache\dependencies\ffmpeg-lgpl\bigscreen-ffmpeg-4.4.8.ready")
        Ffmpeg9Ready = (Test-RepositoryPath ".cache\dependencies\ffmpeg-lgpl-9.0.1\bigscreen-ffmpeg-9.0.1.ready")
        PythonReady = (Test-RepositoryPath ".cache\dependencies\downloader\python-3.14.7-aarch64-linux-android.tar.gz")
        YtDlpReady = (Test-RepositoryPath ".cache\dependencies\downloader\yt-dlp-2026.08.19")
        CertifiReady = (Test-RepositoryPath ".cache\dependencies\downloader\certifi-2026.7.22-py3-none-any.whl")
        QuickJsReady = (Test-RepositoryPath ".cache\dependencies\quickjs-ng\quickjs-0.16.1.ready")
        AdbPath = if ($IncludeDeployment) { Find-Adb } else { $null }
        DeploymentRequested = [bool] $IncludeDeployment
        ShaderReady = (Test-RepositoryPath "assets\bigscreen_video_shader")
    }
}

function Write-StateLine {
    param(
        [bool] $Ready,
        [string] $Description,
        [string] $MissingAction,
        [switch] $RequiresUac
    )

    if ($Ready) {
        Write-Host ("  [READY]   " + $Description) -ForegroundColor Green
    } else {
        $suffix = if ($RequiresUac) { " [WINDOWS UAC REQUIRED]" } else { "" }
        Write-Host ("  [MISSING] " + $Description + " -> " + $MissingAction + $suffix) `
            -ForegroundColor Yellow
    }
}

function Show-PrerequisiteState {
    param([pscustomobject] $State)

    Write-Host ""
    Write-Host "BIG SCREEN BUILD PREREQUISITE AUDIT" -ForegroundColor Cyan
    Write-Host "============================================================"
    Write-Host "This audit is read-only. Nothing has been installed or downloaded."
    Write-Host ""
    Write-Host "Windows and Linux build environment"
    Write-StateLine ([bool]$State.WslExecutable) "Windows Subsystem for Linux" `
        "enable WSL and install its kernel" -RequiresUac
    Write-StateLine ([bool]$State.Distro) `
        $(if ($State.Distro) { "Supported Linux builder: $($State.Distro.Description) [$($State.Distro.Name)]" } else { "Supported x86-64 Ubuntu 22.04/24.04 builder" }) `
        "install Ubuntu 24.04 for WSL" -RequiresUac

    if ($State.Distro) {
        foreach ($package in $linuxPackages) {
            Write-StateLine ($State.InstalledPackages -contains $package) `
                "Linux package: $package" "install it inside $($State.Distro.Name) (no Windows UAC)"
        }
    } else {
        Write-Host "  [PENDING] Linux packages will be checked after Ubuntu is available." -ForegroundColor DarkYellow
    }

    Write-Host ""
    Write-Host "Pinned project inputs and deployment tools"
    Write-StateLine $State.QpmReady "QPM CLI 1.5.11 (~/.cache/bigscreen-toolchains/qpm-1.5.11)" `
        "download the pinned official archive into the WSL user cache"
    Write-StateLine $State.NdkReady "Android NDK r27d (~/.cache/bigscreen-toolchains/android-ndk-r27d)" `
        "download the pinned Google toolchain into the WSL user cache"
    Write-StateLine $State.QpmInputsReady "Quest headers/libraries for the current qpm.shared.json (extern/)" `
        "restore only the locked QPM packages into this repository"
    Write-StateLine $State.Ffmpeg44Ready "FFmpeg 4.4.8 Android runtime (.cache/dependencies/ffmpeg-lgpl)" `
        "download verified source and compile the missing runtime"
    Write-StateLine $State.Ffmpeg9Ready "FFmpeg 9.0.1 Android runtime (.cache/dependencies/ffmpeg-lgpl-9.0.1)" `
        "download verified source and compile the missing runtime"
    Write-StateLine $State.PythonReady "CPython 3.14.7 Android ARM64 archive (.cache/dependencies/downloader)" `
        "download the pinned python.org archive into .cache"
    Write-StateLine $State.YtDlpReady "stable yt-dlp 2026.08.19 package (.cache/dependencies/downloader)" `
        "download the pinned GitHub release into .cache"
    Write-StateLine $State.CertifiReady "certifi 2026.7.22 wheel (.cache/dependencies/downloader)" `
        "download the pinned Python Package Index wheel into .cache"
    Write-StateLine $State.QuickJsReady "QuickJS-NG 0.16.1 source (.cache/dependencies/quickjs-ng)" `
        "download the pinned GitHub archive into .cache"
    if ($State.DeploymentRequested) {
        Write-StateLine ([bool]$State.AdbPath) `
            $(if ($State.AdbPath) { "ADB: $($State.AdbPath)" } else { "Google Android Platform Tools 37.0.0" }) `
            "download a verified portable copy into BigScreen Tools (no Windows UAC)"
    } else {
        Write-Host "  [NOT REQUIRED] ADB and Quest access (QMOD-only build selected)"
    }
    Write-StateLine $State.ShaderReady "Committed Big Screen video shader bundle" `
        "restore the tracked asset from the source checkout"

    Write-Host ""
    Write-Host "Not required by this build path"
    Write-Host "  [NOT REQUIRED] Visual Studio"
    Write-Host "  [NOT REQUIRED] PowerShell 7 or Git for a downloaded source archive"
    Write-Host "  [NOT REQUIRED] Windows QPM or a Windows Android NDK"
    Write-Host "  [NOT REQUIRED] Docker or 7-Zip"

    $uacNeeded = (-not $State.WslExecutable) -or (-not $State.Distro)
    $systemInstallNeeded = $uacNeeded -or $State.MissingPackages.Count -gt 0
    $downloadNeeded = -not ($State.QpmReady -and $State.NdkReady -and
        $State.QpmInputsReady -and $State.Ffmpeg44Ready -and
        $State.Ffmpeg9Ready -and $State.PythonReady -and
        $State.YtDlpReady -and $State.CertifiReady -and
        $State.QuickJsReady -and
        (-not $State.DeploymentRequested -or $State.AdbPath))

    Write-Host ""
    if ($uacNeeded) {
        $uacComponents = New-Object System.Collections.Generic.List[string]
        if (-not $State.WslExecutable) {
            $uacComponents.Add("Windows Subsystem for Linux")
        }
        if (-not $State.Distro) {
            $uacComponents.Add("Ubuntu 24.04 for WSL")
        }
        Write-Host ("WINDOWS UAC REQUIRED FOR: " + ($uacComponents -join ", ")) -ForegroundColor Yellow
        Write-Host "Approving installation will open a Windows User Account Control prompt." -ForegroundColor Yellow
        Write-Host "A Windows restart may be required before the build can continue." -ForegroundColor Yellow
    } else {
        Write-Host "WINDOWS UAC NOTICE: No missing prerequisite in this audit requires Windows elevation." -ForegroundColor Green
    }
    if (-not $systemInstallNeeded) {
        Write-Host "All operating-system build prerequisites are already installed." -ForegroundColor Green
    }
    if (-not $downloadNeeded) {
        Write-Host "All pinned project downloads required by the normal build are already cached." -ForegroundColor Green
    }
    Write-Host "============================================================"
}

function Install-UbuntuWithWsl {
    param([string] $WslExecutable)

    Write-Host ""
    Write-Host "Windows will now request administrator approval to enable/install WSL and Ubuntu 24.04." -ForegroundColor Yellow
    $executable = if ($WslExecutable) { $WslExecutable } else { "wsl.exe" }
    try {
        $process = Start-Process -FilePath $executable -Verb RunAs -Wait -PassThru `
            -ArgumentList @("--install", "-d", $preferredDistro, "--no-launch")
    } catch {
        throw "WSL installation was not approved or could not start: $($_.Exception.Message)"
    }
    if ($process.ExitCode -ne 0) {
        throw "The elevated WSL installer returned exit code $($process.ExitCode). Restart Windows if requested, then rerun Build-And-Deploy.bat."
    }
}

function Install-LinuxPrerequisites {
    param(
        [string] $WslExecutable,
        [pscustomobject] $Distro,
        [string[]] $MissingPackages
    )

    if ($MissingPackages.Count -gt 0) {
        Write-Host ""
        Write-Host "Installing only the missing Linux packages inside $($Distro.Name):" -ForegroundColor Cyan
        Write-Host ("  " + ($MissingPackages -join ", "))
        # Big Screen builds only on the verified x86-64 host architecture.
        # Restrict this operation to amd64 so unrelated foreign architectures
        # configured for another project cannot make their package mirrors
        # fail this build or be modified/removed as a side effect.
        $aptHostOption = "-o APT::Architectures=amd64"
        $packageCommand = "export DEBIAN_FRONTEND=noninteractive; " +
            "apt-get $aptHostOption update && " +
            "apt-get $aptHostOption install -y " + ($MissingPackages -join " ")
        & $WslExecutable -d $Distro.Name -u root -e bash -lc $packageCommand
        if ($LASTEXITCODE -ne 0) {
            throw "Ubuntu package installation failed with exit code $LASTEXITCODE."
        }
    }

}

$state = Get-PrerequisiteState

if ($Mode -eq "Audit") {
    Show-PrerequisiteState $state
    exit 0
}

if (-not $state.WslExecutable -or -not $state.Distro) {
    Install-UbuntuWithWsl -WslExecutable $state.WslExecutable
    $state = Get-PrerequisiteState
    if (-not $state.Distro) {
        throw "Ubuntu is not ready yet. Restart Windows if requested, open Ubuntu once if Windows asks, and then rerun Build-And-Deploy.bat."
    }
}

Install-LinuxPrerequisites -WslExecutable $state.WslExecutable `
    -Distro $state.Distro -MissingPackages $state.MissingPackages

$verifiedState = Get-PrerequisiteState
if (-not $verifiedState.Distro -or
    $verifiedState.MissingPackages.Count -gt 0) {
    throw "The Windows/WSL prerequisite installation completed but verification still reports missing items."
}

Write-Host ""
Write-Host "Operating-system prerequisites are installed and verified." -ForegroundColor Green
Write-Host "The build may now retrieve only the project inputs marked MISSING above."
