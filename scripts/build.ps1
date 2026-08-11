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

& $cmakeExe -G "Ninja" -DCMAKE_BUILD_TYPE="RelWithDebInfo" -B build
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $cmakeExe --build build
exit $LASTEXITCODE
