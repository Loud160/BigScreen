@echo off
@REM SPDX-License-Identifier: GPL-3.0-only
@REM SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
@REM
@REM Part of Big Screen. Distributed under GPL-3.0-only with additional terms
@REM under GPLv3 section 7(b)/(c) and an interoperability permission under
@REM section 7; see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
@REM
@REM Windows build-only counterpart to Build-QMOD-Linux.sh. The implementation
@REM remains centralized in Build-And-Deploy.bat and the canonical WSL/Linux
@REM pipeline; --qmod-only suppresses deployment selection and never uses ADB.
@REM Optional --yes is restricted to this QMOD-only wrapper so automated
@REM Windows/Linux reproducibility checks can approve the disclosed audit
@REM without creating a noninteractive direct-deployment path.
setlocal EnableExtensions
if /I "%~1"=="--yes" (
    set "BIGSCREEN_QMOD_ASSUME_YES=1"
) else if not "%~1"=="" (
    echo Unknown option: %~1
    echo Usage: Build-QMOD.bat [--yes]
    endlocal
    exit /b 2
)
call "%~dp0Build-And-Deploy.bat" --qmod-only
set "BIGSCREEN_RESULT=%ERRORLEVEL%"
endlocal & exit /b %BIGSCREEN_RESULT%
