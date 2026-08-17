@echo off
@REM SPDX-License-Identifier: GPL-3.0-only
@REM SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
@REM
@REM Part of Big Screen.
@REM Distributed under GPL-3.0-only with additional terms under GPLv3
@REM section 7(b)/(c) and an interoperability permission under section 7;
@REM see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
setlocal

title Big Screen Support Log Collector
cd /d "%~dp0"

set "BIGSCREEN_POWERSHELL="
where pwsh.exe >nul 2>nul && set "BIGSCREEN_POWERSHELL=pwsh.exe"
if not defined BIGSCREEN_POWERSHELL set "BIGSCREEN_POWERSHELL=powershell.exe"

echo Big Screen Support Log Collector
echo =================================
echo This collects recent Big Screen, Beat Saber, and Quest crash information.
echo It does not change anything on the headset.
echo.

"%BIGSCREEN_POWERSHELL%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\collect-crash-logs.ps1" %*
set "BIGSCREEN_RESULT=%ERRORLEVEL%"

echo.
if not "%BIGSCREEN_RESULT%"=="0" (
    echo Log collection did not complete. Read the message above for the cause.
) else (
    echo Log collection completed.
)
echo.
pause
exit /b %BIGSCREEN_RESULT%
