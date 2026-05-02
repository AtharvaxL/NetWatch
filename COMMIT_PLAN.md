# NetWatch — Git Contribution Guide

## Branching Strategy

```
main          — stable release branch
dev           — integration branch (PRs merge here first)
feature/nwp-protocol      — NWP protocol + collector
feature/anomaly-engine    — anomaly detection engine
feature/agent-simulator   — agent + simulator
feature/dashboard         — HTTP server + browser UI
```

## Commit Message Convention

| Prefix    | When to use                                      |
|-----------|--------------------------------------------------|
| `feat:`   | New feature or behaviour added                   |
| `fix:`    | Bug fix                                          |
| `test:`   | Adding or updating unit tests                    |
| `docs:`   | Documentation only (README, comments)            |
| `build:`  | Makefile, compiler flags, CI                     |
| `refactor:`| Code restructure without behaviour change       |

Examples:
```
feat: implement NWPCodec encode/decode with XOR checksum
fix: mark devices offline after 30s inactivity
test: 14 unit tests for NWP encode/decode roundtrip
docs: add NWP packet spec to README
```

---

## Initial Setup (run once)

```bash
git init
git remote add origin <your-github-repo-url>

# Create branches
git checkout -b dev
git checkout -b feature/nwp-protocol
git checkout -b feature/anomaly-engine
git checkout -b feature/agent-simulator
git checkout -b feature/dashboard

# Push all branches
git push -u origin dev
git push origin feature/nwp-protocol
git push origin feature/anomaly-engine
git push origin feature/agent-simulator
git push origin feature/dashboard
```

---

## Module Ownership

| Branch                    | Files                                               | Owner        |
|---------------------------|-----------------------------------------------------|--------------|
| `feature/nwp-protocol`    | `nwp.h`, `registry.h`, `collector.cpp`, `Makefile` | Teammate A   |
| `feature/anomaly-engine`  | `anomaly.h`, integration into `collector.cpp`       | Teammate B   |
| `feature/agent-simulator` | `agent.cpp`, `simulator.cpp`                        | Teammate C   |
| `feature/dashboard`       | `dashboard_server.cpp`, `index.html`                | Teammate D   |

---

## Teammate A — NWP Protocol + Collector

```bash
git checkout feature/nwp-protocol

# A1 — Project scaffold
echo "# NetWatch" > README.md
git add README.md
git commit -m "docs: initial README and project scaffold"

# A2 — NWP packet struct and constants
git add nwp.h
git commit -m "feat: define NWP packet header struct and opcodes (32-byte fixed layout)"

# A3 — NWP encode/decode codec
git add nwp.h
git commit -m "feat: implement NWPCodec encode/decode with XOR checksum"

# A4 — NWP packet builders (make_hello, make_status, etc.)
git add nwp.h
git commit -m "feat: add NWPCodec helper builders for all 5 opcodes"

# A5 — Collector: UDP listen + parse
git add collector.cpp
git commit -m "feat: collector UDP server receives and parses NWP packets"

# A6 — Collector: device registry + snapshot thread
git add registry.h collector.cpp
git commit -m "feat: add DeviceRegistry and snapshot thread writing devices.json every 2s"

# A7 — Collector: liveness timeout and GOODBYE handling
git add collector.cpp registry.h
git commit -m "feat: mark devices offline after 30s inactivity and on GOODBYE opcode"

# A8 — Makefile
git add Makefile
git commit -m "build: Makefile with Windows MinGW and Linux targets"

git push origin feature/nwp-protocol
# Open PR: feature/nwp-protocol -> dev
```

---

## Teammate B — Anomaly Engine

```bash
git checkout feature/anomaly-engine
git merge origin/feature/nwp-protocol   # get nwp.h first

# B1 — DeviceStats sliding window
git add anomaly.h
git commit -m "feat: DeviceStats sliding window with z-score helpers (window size 20)"

# B2 — AnomalyEngine class with callback
git add anomaly.h
git commit -m "feat: AnomalyEngine class with AlertCallback and register_device"

# B3 — Rule: bandwidth spike
git add anomaly.h
git commit -m "feat: anomaly rule bw_spike fires when bandwidth z-score > 3.5"

# B4 — Rules: connection storm + absolute thresholds
git add anomaly.h
git commit -m "feat: anomaly rules conn_storm, bw_absolute, conn_absolute"

# B5 — Cooldown deduplication
git add anomaly.h
git commit -m "feat: 10s cooldown per rule per device prevents alert flooding"

# B6 — Wire anomaly engine into collector
git add collector.cpp
git commit -m "feat: integrate AnomalyEngine into collector, write alerts.json on fire"

# B7 — Edge case: warm-up guard for new devices
git add anomaly.h
git commit -m "fix: skip anomaly check until window has 10+ samples (warm-up guard)"

# B8 — Alert severity levels
git add anomaly.h
git commit -m "feat: alert severity HIGH/MEDIUM/LOW based on z-score magnitude"

git push origin feature/anomaly-engine
# Open PR: feature/anomaly-engine -> dev
```

---

## Teammate C — Agent + Simulator

```bash
git checkout feature/agent-simulator
git merge origin/feature/nwp-protocol   # get nwp.h first

# C1 — Agent skeleton: socket setup, HELLO
git add agent.cpp
git commit -m "feat: NWP agent sends HELLO packet on startup with hostname"

# C2 — Agent: STATUS heartbeat loop
git add agent.cpp
git commit -m "feat: agent sends STATUS packets every 5s with live traffic counters"

# C3 — Agent: GOODBYE on clean shutdown
git add agent.cpp
git commit -m "feat: agent sends GOODBYE on Ctrl+C for clean device handoff"

# C4 — Agent: traffic simulation (normal and burst)
git add agent.cpp
git commit -m "feat: agent simulates variable bandwidth and connection counts per interval"

# C5 — Simulator: multi-thread virtual nodes
git add simulator.cpp
git commit -m "feat: simulator spawns N virtual NWP nodes as threads"

# C6 — Simulator: attack traffic scenario
git add simulator.cpp
git commit -m "feat: simulator attack scenario floods bandwidth on a subset of nodes"

# C7 — Simulator: churn scenario
git add simulator.cpp
git commit -m "feat: simulator churn scenario has devices go offline and rejoin"

# C8 — Staggered simulator startup
git add simulator.cpp
git commit -m "fix: stagger node startup by 200ms to prevent simultaneous HELLO burst"

git push origin feature/agent-simulator
# Open PR: feature/agent-simulator -> dev
```

---

## Teammate D — Dashboard

```bash
git checkout feature/dashboard

# D1 — HTTP server skeleton
git add dashboard_server.cpp
git commit -m "feat: HTTP server accepts TCP connections on port 8080"

# D2 — REST endpoints
git add dashboard_server.cpp
git commit -m "feat: add /api/devices and /api/alerts endpoints serving JSON files"

# D3 — Dashboard HTML layout and tabs
git add index.html
git commit -m "feat: dashboard layout with graph canvas, device list, alerts sidebar tabs"

# D4 — D3 force-directed graph
git add index.html
git commit -m "feat: D3 force simulation renders live topology with drag support"

# D5 — Alert feed with severity colours
git add index.html
git commit -m "feat: alert sidebar shows live alerts with HIGH/MEDIUM/LOW severity colours"

# D6 — NWP packet inspector
git add index.html
git commit -m "feat: click any node to inspect its NWP packet byte layout in inspector tab"

# D7 — Header stats bar
git add index.html
git commit -m "feat: header stats bar shows online count, total bandwidth, alert count"

# D8 — Alert pulse animation
git add index.html
git commit -m "feat: red pulse ring animation on alerting nodes in topology graph"

git push origin feature/dashboard
# Open PR: feature/dashboard -> dev
```

---

## Integration — Merging into dev

```bash
# Run on dev branch after all PRs are reviewed
git checkout dev
git merge feature/nwp-protocol    --no-ff -m "merge: nwp protocol and collector"
git merge feature/anomaly-engine  --no-ff -m "merge: anomaly detection engine"
git merge feature/agent-simulator --no-ff -m "merge: agent and simulator"
git merge feature/dashboard        --no-ff -m "merge: dashboard UI and HTTP server"
git push origin dev
```

---

## Unit Tests

```bash
git add test_netwatch.cpp
git commit -m "test: 14 unit tests for NWP encode/decode, opcodes and checksum validation"

git add test_netwatch.cpp
git commit -m "test: 10 unit tests for anomaly engine rules and device registry"
```

---

## Final Release

```bash
# Merge dev into main
git checkout main
git merge dev --no-ff -m "release: v1.0 — NetWatch complete, 24/24 tests passing"
git tag v1.0 -m "NetWatch v1.0"
git push origin main --tags
```

---

## Before Pushing — Final Checklist

```
mingw32-make test   → all 24 tests pass
start.bat           → dashboard opens at http://localhost:8080
git log --oneline   → commits spread across all 4 branches
```
