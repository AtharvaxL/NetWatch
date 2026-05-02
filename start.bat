@echo off
:: NetWatch Launcher — Windows
:: Run this after: mingw32-make all
:: Usage: start.bat [collector_ip]
::   collector_ip defaults to 127.0.0.1 (localhost)

setlocal

set "ROOT=%~dp0"
set "BIN=%ROOT%bin"
set "COLLECTOR_IP=%~1"
if "%COLLECTOR_IP%"=="" set "COLLECTOR_IP=127.0.0.1"

:: Verify binaries exist
if not exist "%BIN%\collector.exe" (
    echo [ERROR] bin\collector.exe not found. Run: mingw32-make all
    pause & exit /b 1
)
if not exist "%BIN%\dashboard.exe" (
    echo [ERROR] bin\dashboard.exe not found. Run: mingw32-make all
    pause & exit /b 1
)

:: Clear stale data files
if exist "%ROOT%devices.json" del "%ROOT%devices.json"
if exist "%ROOT%alerts.json"  del "%ROOT%alerts.json"

echo ============================================================
echo   NetWatch — Live Network Intelligence Platform
echo ============================================================
echo.
echo  [1] Starting collector  (UDP port 9000)...
START "NetWatch Collector" /D "%ROOT%" "%BIN%\collector.exe"

echo  [2] Waiting 1 second...
timeout /t 1 /nobreak >nul

echo  [3] Starting dashboard  (http://localhost:8080)...
START "NetWatch Dashboard" /D "%ROOT%" "%BIN%\dashboard.exe"

timeout /t 1 /nobreak >nul

echo.
echo ============================================================
echo   Dashboard ready at: http://localhost:8080
echo ============================================================
echo.
echo  To connect a device from this machine:
echo    bin\agent.exe 127.0.0.1
echo.
echo  To connect a device from another machine:
echo    bin\agent.exe %COLLECTOR_IP%
echo.
echo  To run the test simulator (optional):
echo    bin\simulator.exe 4 127.0.0.1
echo.
echo  Press any key to open the dashboard in your browser...
pause >nul

start "" http://localhost:8080
