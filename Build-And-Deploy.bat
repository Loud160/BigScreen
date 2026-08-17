@echo off
@REM SPDX-License-Identifier: GPL-3.0-only
@REM SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
@REM
@REM Part of Big Screen. Distributed under GPL-3.0-only with additional terms
@REM under GPLv3 section 7(b)/(c) and an interoperability permission under
@REM section 7; see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
setlocal

rem This launcher is deliberately kept at the repository root so a developer
rem can build and install Big Screen by double-clicking one file. copy.ps1 is
rem still the single authoritative deployment workflow: it locates ADB, checks
rem for an authorized Quest, builds every required runtime, validates the ELF
rem dependencies, copies the complete mod payload, and restarts Beat Saber.
pushd "%~dp0"

echo ============================================================
echo Building and deploying Big Screen to the connected Quest...
echo ============================================================
echo.

echo FIRST-RUN NETWORK DOWNLOADS
echo ------------------------------------------------------------
echo This build may download the following pinned dependencies when
echo they are not already present in a local cache:
echo.
echo   * Quest mod headers and libraries through QPM
echo   * Android NDK r27d for Windows through QPM
echo   * Android NDK r27d for Linux/WSL from dl.google.com
echo   * FFmpeg 4.4.8 and 9.0.1 source from ffmpeg.org
echo   * CPython 3.14.7 Android runtime from python.org
echo   * QuickJS-NG 0.16.1 from its official GitHub release
echo   * yt-dlp 2026.07.04 with yt-dlp-ejs 0.8.0 from GitHub
echo   * certifi 2026.7.22 from Python Package Index
echo.
echo Existing verified files are reused. Direct archives are checked
echo against pinned hashes before they are used. The first build can
echo download several gigabytes and take a while while FFmpeg compiles.
echo ------------------------------------------------------------
echo.
choice /C YN /N /M "Continue with dependency restore, build, and deployment? [Y/N] "
if errorlevel 2 (
    echo.
    echo Cancelled. No build or dependency download was started.
    popd
    exit /b 0
)

echo.
echo Preparing the pinned build toolchain and dependencies...
echo This phase may take several minutes on a first run. Each long
echo download, restore, or compilation phase will announce itself.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\bootstrap-build.ps1"
set "BIGSCREEN_RESULT=%ERRORLEVEL%"
if not "%BIGSCREEN_RESULT%"=="0" goto :failure

echo.
echo Building Big Screen and deploying it to the Quest...
echo Native compilation, final link optimization, QMOD preparation,
echo and USB deployment will print wait notices before longer phases.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\copy.ps1"
set "BIGSCREEN_RESULT=%ERRORLEVEL%"

echo.
if not "%BIGSCREEN_RESULT%"=="0" (
    goto :failure
) else (
    echo ============================================================
    echo SUCCESS - Big Screen was built, installed, and Beat Saber was
    echo asked to restart on the connected Quest.
    echo ============================================================
)
goto :finish

:failure
    echo ============================================================
    echo BUILD OR DEPLOY FAILED - error code %BIGSCREEN_RESULT%
    echo Review the error output above. Nothing after the failed step
    echo was reported as successfully installed.
    echo ============================================================

:finish
echo.
pause
popd
exit /b %BIGSCREEN_RESULT%
