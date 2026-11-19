@echo off
setlocal
cd /d "%~dp0"
echo ==========================================
echo BLE Scanner - automatic setup, build, run
echo ==========================================
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" (
  echo Setup failed with exit code %RC%.
) else (
  echo Setup finished.
)
pause
exit /b %RC%
