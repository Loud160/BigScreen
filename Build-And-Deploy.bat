@echo off
@REM SPDX-License-Identifier: GPL-3.0-only
@REM SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
@REM
@REM Part of Big Screen. Distributed under GPL-3.0-only with additional terms
@REM under GPLv3 section 7(b)/(c) and an interoperability permission under
@REM section 7; see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
setlocal EnableExtensions EnableDelayedExpansion

set "BIGSCREEN_ADB_WAS_RUNNING=0"
set "BIGSCREEN_ADB_WAS_USED=0"
set "BIGSCREEN_RESULT=0"
set "BIGSCREEN_DEPLOY_TO_QUEST=0"
set "BIGSCREEN_PREFLIGHT_DEPLOY_ARGUMENT="
set "BIGSCREEN_SELECTED_ACTION=build and package the QMOD"
set "BIGSCREEN_QMOD_ONLY=0"
if /I "%~1"=="--qmod-only" (
    set "BIGSCREEN_QMOD_ONLY=1"
) else if not "%~1"=="" (
    echo Unknown option: %~1
    echo Usage: Build-And-Deploy.bat [--qmod-only]
    exit /b 2
)
tasklist /NH /FI "IMAGENAME eq adb.exe" 2>nul | find /I "adb.exe" >nul
if not errorlevel 1 set "BIGSCREEN_ADB_WAS_RUNNING=1"

rem This launcher is deliberately kept at the repository root so a developer
rem can build and install Big Screen by double-clicking one file. The complete
rem native build runs through the supported Linux/WSL path, so Visual Studio,
rem Windows QPM, PowerShell 7, Git, and a Windows NDK are not prerequisites.
rem Windows PowerShell 5.1 is used only by this thin Windows audit/deploy shell;
rem compilation, tests, validation, and packaging all use the Linux scripts.
pushd "%~dp0"

echo ============================================================
echo Big Screen source build and deployment launcher
echo ============================================================
echo.

if "%BIGSCREEN_QMOD_ONLY%"=="1" (
    echo Build-QMOD.bat selected the QMOD-only workflow.
    echo ADB and the Quest will not be accessed.
) else (
    echo Choose the output for this build:
    echo   [Q] Build and package Big Screen.qmod only
    echo   [D] Build the QMOD and deploy it directly to a connected Quest
    echo   [C] Cancel
    echo.
    choice /C QDC /N /M "Select QMOD only, direct deployment, or cancel [Q/D/C]: "
    if errorlevel 3 (
        echo.
        echo Cancelled. Nothing was checked, installed, downloaded, or built.
        popd
        exit /b 0
    )
    if errorlevel 2 (
        set "BIGSCREEN_DEPLOY_TO_QUEST=1"
        set "BIGSCREEN_PREFLIGHT_DEPLOY_ARGUMENT=-IncludeDeployment"
        set "BIGSCREEN_SELECTED_ACTION=build the QMOD and deploy it to the Quest"
    )
)

echo Checking every build prerequisite and local dependency cache...
echo Nothing will be installed or downloaded during this audit.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\windows-wsl-build-preflight.ps1" -Mode Audit %BIGSCREEN_PREFLIGHT_DEPLOY_ARGUMENT%
set "BIGSCREEN_RESULT=%ERRORLEVEL%"
if not "%BIGSCREEN_RESULT%"=="0" goto :failure

echo.
echo Review the READY and MISSING entries above. Only items marked
echo MISSING will be installed or downloaded. If Windows elevation is
echo required, the exact program requiring UAC is named above.
echo.
rem Build-QMOD.bat may approve this prompt for automated reproducibility tests.
rem Both guards are required: the internal flag can never approve deployment.
if "%BIGSCREEN_QMOD_ONLY%"=="1" if "%BIGSCREEN_QMOD_ASSUME_YES%"=="1" (
    echo --yes approved the disclosed QMOD-only build prerequisites.
) else (
    choice /C YN /N /M "Install/download the missing items, then %BIGSCREEN_SELECTED_ACTION%? [Y/N] "
    if errorlevel 2 (
        echo.
        echo Cancelled. The audit made no changes and nothing was downloaded.
        popd
        exit /b 0
    )
)

echo.
echo Installing only the missing Windows/WSL host prerequisites...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\windows-wsl-build-preflight.ps1" -Mode Install %BIGSCREEN_PREFLIGHT_DEPLOY_ARGUMENT%
set "BIGSCREEN_RESULT=%ERRORLEVEL%"
if not "%BIGSCREEN_RESULT%"=="0" goto :failure

if not "%BIGSCREEN_DEPLOY_TO_QUEST%"=="1" goto :skip_adb
echo.
echo Preparing ADB for the later Quest deployment...
echo The audit already disclosed whether a portable download is needed.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\ensure-adb.ps1" -MissingAdbAction Install
set "BIGSCREEN_RESULT=%ERRORLEVEL%"
if not "%BIGSCREEN_RESULT%"=="0" goto :failure
if exist "%~dp0BigScreen Tools\platform-tools\adb.exe" set "PATH=%~dp0BigScreen Tools\platform-tools;%PATH%"
set "BIGSCREEN_ADB_WAS_USED=1"
echo.
echo Checking for an authorized Quest before starting the build...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\check-quest-connection.ps1"
set "BIGSCREEN_RESULT=%ERRORLEVEL%"
if "%BIGSCREEN_RESULT%"=="2" goto :cancelled
if not "%BIGSCREEN_RESULT%"=="0" goto :failure
:skip_adb

echo.
echo Resolving the supported WSL builder selected by the audit...
set "BIGSCREEN_WSL_DISTRO=Ubuntu-24.04"
wsl.exe -d Ubuntu-24.04 -e true >nul 2>nul
if not "!ERRORLEVEL!"=="0" set "BIGSCREEN_WSL_DISTRO=Ubuntu"
echo Selected WSL builder candidate: !BIGSCREEN_WSL_DISTRO!
wsl.exe -d !BIGSCREEN_WSL_DISTRO! -e true >nul 2>nul
echo WSL builder probe exit code: !ERRORLEVEL!
if not "!ERRORLEVEL!"=="0" (
    set "BIGSCREEN_RESULT=2"
    echo ERROR: The verified WSL builder distribution could not be resolved.
    goto :failure
)

echo.
echo Building and testing Big Screen inside !BIGSCREEN_WSL_DISTRO!...
echo Linux build entrypoint: scripts/build-linux.sh
echo Only project inputs marked MISSING by the audit will be downloaded.
echo Longer compilation phases announce themselves and keep progress visible.
wsl.exe -d !BIGSCREEN_WSL_DISTRO! --cd "%~dp0" -e bash scripts/build-linux.sh
set "BIGSCREEN_RESULT=%ERRORLEVEL%"
if not "%BIGSCREEN_RESULT%"=="0" goto :failure

if not "%BIGSCREEN_DEPLOY_TO_QUEST%"=="1" goto :package_success
echo.
echo Deploying the verified Linux build to the connected Quest...

set "BIGSCREEN_ADB_WAS_USED=1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\copy.ps1" -UseExistingVerifiedBuild
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

:package_success
echo.
echo ============================================================
echo SUCCESS - Big Screen was built and packaged as:
echo   %~dp0Big Screen.qmod
echo No Quest was accessed and nothing was deployed.
echo ============================================================
goto :finish

:cancelled
echo.
echo Cancelled. No build or deployment was started.
set "BIGSCREEN_RESULT=0"
goto :finish

:failure
    echo ============================================================
    echo BUILD OR DEPLOY FAILED - error code %BIGSCREEN_RESULT%
    echo Review the error output above. Nothing after the failed step
    echo was reported as successfully installed.
    echo ============================================================

:finish
echo.
if "%BIGSCREEN_ADB_WAS_USED%"=="1" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\complete-adb-session.ps1" -WasRunningAtStart %BIGSCREEN_ADB_WAS_RUNNING%
    echo.
)
pause
popd
exit /b %BIGSCREEN_RESULT%
