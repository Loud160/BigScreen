@echo off
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

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\copy.ps1"
set "BIGSCREEN_RESULT=%ERRORLEVEL%"

echo.
if not "%BIGSCREEN_RESULT%"=="0" (
    echo ============================================================
    echo BUILD OR DEPLOY FAILED - error code %BIGSCREEN_RESULT%
    echo Review the error output above. Nothing after the failed step
    echo was reported as successfully installed.
    echo ============================================================
) else (
    echo ============================================================
    echo SUCCESS - Big Screen was built, installed, and Beat Saber was
    echo asked to restart on the connected Quest.
    echo ============================================================
)
echo.
pause
popd
exit /b %BIGSCREEN_RESULT%
