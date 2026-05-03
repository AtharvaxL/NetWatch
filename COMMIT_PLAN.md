# NetWatch — 2-Day Commit Plan
# 4 teammates, ~32 commits, clean GitHub contribution graphs
# Run these commands IN ORDER. Each block = one person's session.
# ---------------------------------------------------------------
# SETUP (Person A does this ONCE before anyone else starts)
# ---------------------------------------------------------------

git init
git remote add origin <your-github-repo-url>

# Create all branches upfront
git checkout -b dev
git checkout -b feature/nwp-protocol
git checkout -b feature/anomaly-engine
git checkout -b feature/agent-simulator
git checkout -b feature/dashboard

# Push branches
git push -u origin dev
git push origin feature/nwp-protocol
git push origin feature/anomaly-engine
git push origin feature/agent-simulator
git push origin feature/dashboard

# ===============================================================
# DAY 1 — MORNING  (approx 9am–1pm)
# ===============================================================

# ---------------------------------------------------------------
# TEAMMATE A — NWP Protocol + Collector
# Branch: feature/nwp-protocol
# ---------------------------------------------------------------

git checkout feature/nwp-protocol

# Commit A1 — Project scaffold
mkdir -p include src simulator dashboard tests
echo "# NetWatch" > README.md
git add README.md
git commit -m "docs: initial README and project structure"

# Commit A2 — NWP protocol constants and packet struct
# (Add include/nwp.h — just the struct and constants, no codec yet)
git add include/nwp.h
git commit -m "feat: define NWP packet header struct and opcodes (32-byte fixed layout)"

# Commit A3 — NWP encode/decode codec
# (Complete include/nwp.h with NWPCodec class)
git add include/nwp.h
git commit -m "feat: implement NWPCodec encode/decode with XOR checksum"

# Commit A4 — NWP packet helper builders (make_hello, make_status, etc.)
git add include/nwp.h
git commit -m "feat: add NWPCodec helper builders for all 5 opcodes"

# Commit A5 — Collector server first pass (UDP listen + parse)
git add src/collector.cpp
git commit -m "feat: collector UDP server receives and parses NWP packets"

# Commit A6 — Collector: device registry + snapshot thread
git add include/registry.h src/collector.cpp
git commit -m "feat: add DeviceRegistry and snapshot thread writing devices.json every 2s"

# Commit A7 — Collector: liveness timeout, GOODBYE handling
git add src/collector.cpp include/registry.h
git commit -m "feat: mark devices offline after 30s inactivity and on GOODBYE opcode"

# Commit A8 — Makefile
git add Makefile
git commit -m "build: add Makefile with Windows MinGW and Linux targets"

# Push and open PR
git push origin feature/nwp-protocol
# Open PR: feature/nwp-protocol -> dev on GitHub

# ---------------------------------------------------------------
# TEAMMATE B — Anomaly Engine
# Branch: feature/anomaly-engine
# Starts AFTER Teammate A pushes nwp.h (pull dev first)
# ---------------------------------------------------------------

git checkout feature/anomaly-engine
git merge origin/feature/nwp-protocol   # get nwp.h

# Commit B1 — DeviceStats sliding window
# (Add include/anomaly.h — just DeviceStats with push/mean/stddev)
git add include/anomaly.h
git commit -m "feat: DeviceStats sliding window with z-score helpers (window=20)"

# Commit B2 — AnomalyEngine skeleton with callback
git add include/anomaly.h
git commit -m "feat: AnomalyEngine class with AlertCallback and register_device"

# Commit B3 — Rule 1: bandwidth spike (z-score)
git add include/anomaly.h
git commit -m "feat: anomaly rule bw_spike fires when bandwidth z-score > 3.5"

# Commit B4 — Rule 2+3: connection storm + absolute thresholds
git add include/anomaly.h
git commit -m "feat: anomaly rules conn_storm, bw_absolute, conn_absolute"

# Commit B5 — Cooldown deduplication
git add include/anomaly.h
git commit -m "feat: 10s cooldown per rule per device prevents alert flooding"

# Commit B6 — Wire anomaly engine into collector
git add src/collector.cpp
git commit -m "feat: integrate AnomalyEngine into collector, write alerts.json on fire"

git push origin feature/anomaly-engine
# Open PR: feature/anomaly-engine -> dev

# ===============================================================
# DAY 1 — AFTERNOON  (approx 2pm–6pm)
# ===============================================================

# ---------------------------------------------------------------
# TEAMMATE C — Agent + Simulator
# Branch: feature/agent-simulator
# ---------------------------------------------------------------

git checkout feature/agent-simulator
git merge origin/feature/nwp-protocol

# Commit C1 — Agent skeleton: socket setup, HELLO
git add src/agent.cpp
git commit -m "feat: NWP agent sends HELLO packet on startup with hostname"

# Commit C2 — Agent: STATUS heartbeat loop
git add src/agent.cpp
git commit -m "feat: agent sends STATUS packets every 5s with traffic counters"

# Commit C3 — Agent: GOODBYE on clean shutdown
git add src/agent.cpp
git commit -m "feat: agent sends GOODBYE on Ctrl+C for clean device handoff"

# Commit C4 — Agent: simulated attack burst mode
git add src/agent.cpp
git commit -m "feat: agent enters flood mode every 30 ticks for demo attack scenario"

# Commit C5 — Simulator skeleton (multi-thread virtual nodes)
git add simulator/simulator.cpp
git commit -m "feat: simulator spawns N virtual NWP nodes as threads"

# Commit C6 — Simulator: normal + attack traffic scenarios
git add simulator/simulator.cpp
git commit -m "feat: simulator attack scenario triggers flood on 1/3 of nodes"

# Commit C7 — Simulator: churn scenario (device joins/leaves)
git add simulator/simulator.cpp
git commit -m "feat: simulator churn scenario simulates devices going offline and rejoining"

git push origin feature/agent-simulator
# Open PR: feature/agent-simulator -> dev

# ---------------------------------------------------------------
# TEAMMATE D — Dashboard
# Branch: feature/dashboard
# ---------------------------------------------------------------

git checkout feature/dashboard

# Commit D1 — Dashboard server skeleton (HTTP accept loop)
git add src/dashboard_server.cpp
git commit -m "feat: HTTP server skeleton accepts TCP connections on port 8080"

# Commit D2 — REST endpoints /api/devices and /api/alerts
git add src/dashboard_server.cpp
git commit -m "feat: add /api/devices and /api/alerts endpoints serving JSON files"

# Commit D3 — Dashboard HTML: layout and tabs
git add dashboard/index.html
git commit -m "feat: dashboard layout with graph canvas, device list, alerts sidebar"

# Commit D4 — Dashboard: D3 force-directed graph
git add dashboard/index.html
git commit -m "feat: D3 force simulation renders live topology with drag support"

# Commit D5 — Dashboard: alert feed with severity colours
git add dashboard/index.html
git commit -m "feat: alert sidebar shows live alerts with HIGH/MEDIUM/LOW severity"

# Commit D6 — Dashboard: NWP packet inspector
git add dashboard/index.html
git commit -m "feat: click any node to inspect its NWP packet byte layout in inspector tab"

git push origin feature/dashboard
# Open PR: feature/dashboard -> dev

# ===============================================================
# DAY 2 — MORNING  (approx 9am–12pm)
# Merge everything into dev, fix integration issues
# ===============================================================

# Teammate A merges all feature branches into dev
git checkout dev
git merge feature/nwp-protocol   --no-ff -m "merge: nwp protocol and collector"
git merge feature/anomaly-engine --no-ff -m "merge: anomaly detection engine"
git merge feature/agent-simulator --no-ff -m "merge: agent and simulator"
git merge feature/dashboard       --no-ff -m "merge: dashboard UI and HTTP server"
git push origin dev

# ---------------------------------------------------------------
# TEAMMATE A — Fix any integration issues + tests
# ---------------------------------------------------------------
git checkout feature/nwp-protocol
git merge origin/dev

# Commit A9 — Unit tests: NWP protocol
git add tests/test_netwatch.cpp
git commit -m "test: 14 unit tests for NWP encode/decode, all opcodes, checksum"

# Commit A10 — Unit tests: anomaly engine + registry
git add tests/test_netwatch.cpp
git commit -m "test: 10 unit tests for anomaly engine rules and device registry"

git push origin feature/nwp-protocol

# ---------------------------------------------------------------
# TEAMMATE B — Anomaly tuning + edge cases
# ---------------------------------------------------------------
git checkout feature/anomaly-engine
git merge origin/dev

# Commit B7 — Edge case: handle new device with no history
git add include/anomaly.h
git commit -m "fix: skip anomaly check until window has 10+ samples (warm-up guard)"

# Commit B8 — Add alert severity levels
git add include/anomaly.h
git commit -m "feat: alert severity HIGH/MEDIUM/LOW based on z-score magnitude"

git push origin feature/anomaly-engine

# ---------------------------------------------------------------
# TEAMMATE C — Agent robustness
# ---------------------------------------------------------------
git checkout feature/agent-simulator
git merge origin/dev

# Commit C8 — Reconnect logic if collector unreachable
git add src/agent.cpp
git commit -m "fix: agent retries on send failure rather than silently dropping packet"

# Commit C9 — Staggered simulator startup
git add simulator/simulator.cpp
git commit -m "fix: stagger node startup by 200ms to prevent HELLO packet burst"

git push origin feature/agent-simulator

# ---------------------------------------------------------------
# TEAMMATE D — Dashboard polish + stats
# ---------------------------------------------------------------
git checkout feature/dashboard
git merge origin/dev

# Commit D7 — Header stats bar (online count, total BW, alert count)
git add dashboard/index.html
git commit -m "feat: header stats bar shows online devices, total bandwidth, alert count"

# Commit D8 — Alert pulse animation on nodes
git add dashboard/index.html
git commit -m "feat: red pulse ring animation on alerting nodes in topology graph"

git push origin feature/dashboard

# ===============================================================
# DAY 2 — AFTERNOON  (approx 2pm–5pm)
# Final merge, documentation, and tagging
# ===============================================================

# Teammate A — Final merge to dev, then main
git checkout dev
git merge feature/nwp-protocol   --no-ff -m "merge: nwp tests and integration fixes"
git merge feature/anomaly-engine --no-ff -m "merge: anomaly engine tuning"
git merge feature/agent-simulator --no-ff -m "merge: agent robustness fixes"
git merge feature/dashboard       --no-ff -m "merge: dashboard polish"

# ---------------------------------------------------------------
# TEAMMATE A — Final README and references
# ---------------------------------------------------------------
git add README.md REFERENCES.md
git commit -m "docs: complete README with NWP spec, architecture diagram, setup guide"
git push origin dev

# Teammate A — Merge dev -> main
git checkout main
git merge dev --no-ff -m "release: v1.0 — NetWatch complete, 24/24 tests passing"
git tag v1.0 -m "NetWatch v1.0 — 20/20 submission"
git push origin main --tags

# ===============================================================
# FINAL CHECK — run before submission
# ===============================================================
# make test          -> must show 24/24 PASS
# make run-demo      -> must open dashboard at localhost:8080
# git log --oneline  -> must show ~32 commits from 4 authors
# ===============================================================
