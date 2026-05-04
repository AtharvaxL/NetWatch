#!/bin/bash
echo ""
echo "============================================"
echo "  NetWatch — One-Click Launcher (Linux)"
echo "============================================"
echo ""

cd "$(dirname "$0")"

# Step 1 — Kill old processes (Linux + Windows if running in WSL)
echo "[1/5] Killing old processes..."
pkill -x collector 2>/dev/null
pkill -x dashboard 2>/dev/null
pkill -x simulator 2>/dev/null
pkill -x agent 2>/dev/null

# If running inside WSL, also kill Windows-side NetWatch processes
if grep -qi microsoft /proc/version 2>/dev/null; then
    echo "       WSL detected — killing Windows processes too..."
    taskkill.exe /F /IM collector.exe 2>/dev/null
    taskkill.exe /F /IM dashboard.exe 2>/dev/null
    taskkill.exe /F /IM simulator.exe 2>/dev/null
fi

# Force-kill anything on our ports
fuser -k 9000/udp 2>/dev/null
fuser -k 8080/tcp 2>/dev/null
sleep 2

# Double-check ports are free
if ss -tlnp 2>/dev/null | grep -q ':8080'; then
    echo "[WARN] Port 8080 still in use — trying harder..."
    fuser -k -9 8080/tcp 2>/dev/null
    sleep 1
fi
if ss -ulnp 2>/dev/null | grep -q ':9000'; then
    echo "[WARN] Port 9000 still in use — trying harder..."
    fuser -k -9 9000/udp 2>/dev/null
    sleep 1
fi

# Step 2 — Build if needed
echo "[2/5] Checking binaries..."
if [ ! -f bin/collector ] || [ ! -f bin/dashboard ] || [ ! -f bin/simulator ]; then
    echo "       Building... please wait"
    make all
    if [ $? -ne 0 ]; then
        echo "[ERROR] Build failed. Make sure g++ is installed."
        exit 1
    fi
else
    echo "       All binaries found."
fi

# Step 3 — Open firewall (silently, may need sudo)
echo "[3/5] Configuring firewall..."
sudo ufw allow 9000/udp 2>/dev/null || true
sudo ufw allow 8080/tcp 2>/dev/null || true
echo "       Done."

# Step 4 — Clear old data
echo "[4/5] Clearing old data..."
echo "[]" > devices.json
> alerts.json

# Step 5 — Launch everything
echo "[5/5] Starting services..."
echo ""

./bin/collector &
COLLECTOR_PID=$!
sleep 1

./bin/dashboard &
DASHBOARD_PID=$!
sleep 1

./bin/simulator 6 127.0.0.1 attack &
SIMULATOR_PID=$!
sleep 2

echo ""
echo "============================================"
echo "  Everything is running!"
echo ""
echo "  Dashboard:  http://localhost:8080"
echo "  Collector:  UDP port 9000 (PID $COLLECTOR_PID)"
echo "  Dashboard:  TCP port 8080 (PID $DASHBOARD_PID)"
echo "  Simulator:  6 virtual nodes  (PID $SIMULATOR_PID)"
echo ""
echo "  Press Ctrl+C to stop everything"
echo "============================================"
echo ""

# Open browser
xdg-open http://localhost:8080 2>/dev/null || open http://localhost:8080 2>/dev/null || true

# Wait for Ctrl+C, then clean up
cleanup() {
    echo ""
    echo "Stopping NetWatch..."
    kill $COLLECTOR_PID $DASHBOARD_PID $SIMULATOR_PID 2>/dev/null
    wait 2>/dev/null
    echo "NetWatch stopped. Goodbye!"
    exit 0
}
trap cleanup SIGINT SIGTERM

wait
