#!/bin/bash
echo ""
echo "============================================"
echo "  NetWatch — One-Click Launcher (Linux)"
echo "============================================"
echo ""

cd "$(dirname "$0")"

# Step 1 — Kill old processes
echo "[1/5] Killing old processes..."
pkill -x collector 2>/dev/null
pkill -x dashboard 2>/dev/null
pkill -x simulator 2>/dev/null
pkill -x agent 2>/dev/null
sleep 1

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

# Step 3 — Open firewall (may need sudo)
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

# Wait for Ctrl+C
cleanup() {
    echo ""
    echo "Stopping NetWatch..."
    kill $COLLECTOR_PID $DASHBOARD_PID $SIMULATOR_PID 2>/dev/null
    wait 2>/dev/null
    echo "NetWatch stopped. Goodbye!"
    exit 0
}
trap cleanup SIGINT SIGTERM

# Keep running
wait
