@echo off
title NetWatch Launcher
echo.
echo ============================================
echo   NetWatch — One-Click Launcher
echo ============================================
echo.

:: Step 1 — Kill any old NetWatch processes
echo [1/5] Killing old processes...
taskkill /F /IM collector.exe >nul 2>&1
taskkill /F /IM dashboard.exe >nul 2>&1
taskkill /F /IM simulator.exe >nul 2>&1
taskkill /F /IM agent.exe >nul 2>&1
timeout /t 2 /nobreak >nul

:: Step 2 — Build if binaries missing
echo [2/5] Checking binaries...
if not exist bin\collector.exe (
    echo        Building... please wait
    mingw32-make all
    if errorlevel 1 (
        echo [ERROR] Build failed. Make sure MinGW g++ is installed.
        pause
        exit /b 1
    )
) else (
    echo        All binaries found.
)

:: Step 3 — Open firewall (silently, may fail without admin — that's OK for localhost)
echo [3/5] Configuring firewall...
netsh advfirewall firewall add rule name="NetWatch UDP" protocol=UDP dir=in localport=9000 action=allow >nul 2>&1
netsh advfirewall firewall add rule name="NetWatch TCP" protocol=TCP dir=in localport=8080 action=allow >nul 2>&1
echo        Done (may need admin for remote access).

:: Step 4 — Clear old data files
echo [4/5] Clearing old data...
echo [] > devices.json
type nul > alerts.json

:: Step 5 — Launch everything
echo [5/5] Starting services...
echo.

start "NetWatch Collector" /min cmd /c "cd /d %~dp0 && bin\collector.exe"
timeout /t 2 /nobreak >nul

start "NetWatch Dashboard" /min cmd /c "cd /d %~dp0 && bin\dashboard.exe"
timeout /t 1 /nobreak >nul

start "NetWatch Simulator" /min cmd /c "cd /d %~dp0 && bin\simulator.exe 6 127.0.0.1 attack"
timeout /t 2 /nobreak >nul

:: Step 6 — Open browser
echo.
echo ============================================
echo   Everything is running!
echo.
echo   Dashboard:  http://localhost:8080
echo   Collector:  UDP port 9000
echo   Simulator:  6 virtual nodes (attack mode)
echo.
echo   To stop everything, close this window
echo   or run: taskkill /F /IM collector.exe
echo ============================================
echo.
start http://localhost:8080

:: Keep window open so user can see status
echo Press any key to STOP all NetWatch services...
pause >nul

:: Cleanup on exit
taskkill /F /IM collector.exe >nul 2>&1
taskkill /F /IM dashboard.exe >nul 2>&1
taskkill /F /IM simulator.exe >nul 2>&1
echo.
echo NetWatch stopped. Goodbye!
timeout /t 2 /nobreak >nul
