@echo off
@REM SPDX-License-Identifier: GPL-3.0-only
@REM SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
@REM
@REM Removes only receipt-owned source deployment files. It does not remove
@REM videos, library data, thumbnails, logs, maps, or movement files.
setlocal
set "BIGSCREEN_ADB_WAS_RUNNING=0"
set "BIGSCREEN_ADB_WAS_USED=0"
tasklist /NH /FI "IMAGENAME eq adb.exe" 2>nul | find /I "adb.exe" >nul
if not errorlevel 1 set "BIGSCREEN_ADB_WAS_RUNNING=1"
pushd "%~dp0"

echo ============================================================
echo Big Screen Source Installation Remover
echo ============================================================
echo.
echo Checking for ADB. A portable copy can be downloaded into this
echo repository if Android platform-tools are not already available.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\ensure-adb.ps1"
set "BIGSCREEN_RESULT=%ERRORLEVEL%"
if not "%BIGSCREEN_RESULT%"=="0" goto :finish
if exist "%~dp0BigScreen Tools\platform-tools\adb.exe" set "PATH=%~dp0BigScreen Tools\platform-tools;%PATH%"

set "BIGSCREEN_ADB_WAS_USED=1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\remove-bigscreen.ps1"
set "BIGSCREEN_RESULT=%ERRORLEVEL%"

:finish
echo.
if "%BIGSCREEN_RESULT%"=="0" (
    echo Removal workflow completed.
) else (
    echo Removal did not complete. Review the safety message above.
)
if "%BIGSCREEN_ADB_WAS_USED%"=="1" (
    echo.
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\complete-adb-session.ps1" -WasRunningAtStart %BIGSCREEN_ADB_WAS_RUNNING%
)
pause
popd
exit /b %BIGSCREEN_RESULT%
