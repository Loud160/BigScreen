# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
#
# Part of Big Screen.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$build = Join-Path $root "build-host-tests"
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakeExe = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmakeExe) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vsInstall = & $vswhere -latest -products * -property installationPath
        $candidate = Join-Path $vsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path -LiteralPath $candidate) { $cmakeExe = $candidate }
    }
}
if (-not $cmakeExe) { throw "CMake was not found." }
& (Join-Path $PSScriptRoot "fetch-quickjs-ng.ps1")
if ($LASTEXITCODE -ne 0) { throw "Could not prepare QuickJS-NG for host tests." }
& $cmakeExe -S (Join-Path $root "tests") -B $build
if ($LASTEXITCODE -ne 0) { throw "Could not configure Big Screen core tests." }
& $cmakeExe --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "Could not build Big Screen core tests." }
$ctestExe = Join-Path (Split-Path -Parent $cmakeExe) "ctest.exe"
& $ctestExe --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Big Screen core tests failed." }
