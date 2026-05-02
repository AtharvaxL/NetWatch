# NetWatch — Live Network Intelligence Platform

> A real-time network monitoring system that discovers devices, maps their connections visually,
> and detects anomalous traffic using **NWP** — a custom binary protocol designed from scratch.

---

## What It Does

NetWatch gives you a live browser map of every device on your network:
- Every device appears as a node; every active connection is an animated edge
- Devices self-report via **NWP (NetWatch Protocol)** — a purpose-built binary UDP protocol, not nmap or SNMP
- A rule-based anomaly engine detects packet floods, bandwidth spikes, and connection storms in real time
- Clicking any node shows the raw NWP packet byte-layout in the built-in inspector panel
- No external C++ dependencies — only the standard library and Winsock2 on Windows

---

## Quick Start

### Windows (MinGW)

```
# 1. Build all binaries (requires MinGW g++ with C++17 support)
mingw32-make all

# 2. Run unit tests (24 tests — all must pass before deploying)
mingw32-make test

# 3. Launch the full stack (collector + dashboard)
start.bat
```

`start.bat` opens two windows (collector and dashboard server) and then opens the dashboard in your browser at **http://localhost:8080**.

### Linux / macOS

```bash
make all
make test
make run
```

### Manual Start (any platform)

Open three separate terminals:

```
# Terminal 1 — Collector (receives NWP packets on UDP port 9000)
bin/collector.exe

# Terminal 2 — Dashboard (HTTP server on port 8080)
bin/dashboard.exe

# Terminal 3 — Connect a device agent
bin/agent.exe <collector_ip>
# e.g.  bin/agent.exe 192.168.1.100

# Or run the simulator for testing
bin/simulator.exe 4 127.0.0.1

# Open http://localhost:8080 in your browser
```

---

## NWP Protocol Specification

NWP (NetWatch Protocol) is a custom lightweight binary protocol carried over UDP.
Each device runs an NWP agent that actively pushes structured status packets to the
central collector every 5 seconds. This replaces passive polling (nmap) with active reporting.

### Packet Header (32 bytes, fixed)

| Bytes | Field          | Type     | Description                            |
|-------|----------------|----------|----------------------------------------|
| 0–1   | Magic          | uint16   | `0x4E57` ("NW") — protocol identifier |
| 2     | Version        | uint8    | `0x01` — NWP version 1                 |
| 3     | Opcode         | uint8    | Message type (see opcodes below)       |
| 4–5   | Payload Length | uint16   | Length of variable payload in bytes    |
| 6–9   | Device ID      | uint32   | Unique ID assigned at first contact    |
| 10–15 | MAC Address    | 6 bytes  | Hardware address of reporting node     |
| 16–19 | IP Address     | uint32   | IPv4 address (network byte order)      |
| 20–21 | Connections    | uint16   | Current open socket count              |
| 22–25 | Bytes Sent     | uint32   | Bytes sent since last packet           |
| 26–29 | Bytes Received | uint32   | Bytes received since last packet       |
| 30    | Alert Flag     | uint8    | `0x01` = anomaly detected on this node |
| 31    | Checksum       | uint8    | XOR of all bytes 0–30                  |
| 32+   | Payload        | variable | Opcode-specific data                   |

All multi-byte integers are **big-endian (network byte order)**.

### Opcodes

| Opcode | Name    | Payload                                | Description                           |
|--------|---------|----------------------------------------|---------------------------------------|
| 0x01   | HELLO   | hostname string (UTF-8)                | Agent first contact — register device |
| 0x02   | STATUS  | (none)                                 | Periodic heartbeat with traffic stats |
| 0x03   | ALERT   | `[severity byte]` + reason string      | Node-detected anomaly                 |
| 0x04   | GOODBYE | (none)                                 | Graceful disconnect                   |
| 0x05   | ACK     | (none)                                 | Collector acknowledgement             |

### Example STATUS Packet (hex dump)

```
4E 57  — Magic "NW"
01     — Version 1
02     — Opcode: STATUS
00 00  — Payload length: 0
00 00 03 E9  — Device ID: 1001
02 00 03 E9 1C 71  — MAC address
C0 A8 01 01  — IP: 192.168.1.1
00 0F  — Active connections: 15
00 01 E2 40  — Bytes sent: 123456
00 09 FB F1  — Bytes received: 654321
00     — Alert flag: normal
A5     — XOR checksum
```

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Each Device                          │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  NWP Agent (agent.exe)                              │   │
│  │  • Sends HELLO on startup                          │   │
│  │  • Sends STATUS every 5 seconds                    │   │
│  │  • Sends ALERT if anomaly detected locally         │   │
│  │  • Sends GOODBYE on shutdown                       │   │
│  └──────────────────┬──────────────────────────────────┘   │
│                     │ UDP port 9000 (NWP binary packets)    │
└─────────────────────┼───────────────────────────────────────┘
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                  Collector (collector.exe)                   │
│  • Parses NWP binary headers and payloads                   │
│  • Maintains live DeviceRegistry (thread-safe)              │
│  • Runs AnomalyEngine per device (z-score + thresholds)     │
│  • Writes devices.json every 2 seconds                      │
│  • Appends to alerts.json on each alert event               │
└──────────────────────┬──────────────────────────────────────┘
                       │ File I/O (JSON)
┌──────────────────────▼──────────────────────────────────────┐
│              Dashboard Server (dashboard.exe)               │
│  • HTTP server on port 8080                                 │
│  • GET /api/devices  → devices.json                         │
│  • GET /api/alerts   → alerts.json (last 50)                │
│  • GET /            → browser dashboard HTML                │
└──────────────────────┬──────────────────────────────────────┘
                       │ HTTP polling every 2 seconds
┌──────────────────────▼──────────────────────────────────────┐
│                  Browser Dashboard                          │
│  • D3.js force-directed topology graph                      │
│  • Live device list with online/offline status badges       │
│  • Alert feed with HIGH / MEDIUM / LOW severity levels      │
│  • NWP Packet Inspector (byte-level view of last packet)    │
└─────────────────────────────────────────────────────────────┘
```

### File Structure

```
netwatch/
├── nwp.h               — NWP protocol: packet struct, codec, all opcodes
├── anomaly.h           — Anomaly detection engine (z-score + thresholds)
├── registry.h          — Thread-safe device registry + JSON serialiser
├── collector.cpp       — UDP listener, parses NWP, runs anomaly engine
├── agent.cpp           — NWP agent: HELLO + STATUS + GOODBYE lifecycle
├── dashboard_server.cpp— HTTP server for browser dashboard
├── simulator.cpp       — Multi-threaded virtual device fleet (for testing)
├── index.html          — Browser UI: D3 topology graph + alerts + inspector
├── test_netwatch.cpp   — 24 unit tests (NWP protocol + anomaly + registry)
├── Makefile            — Build system (Windows MinGW + Linux)
├── start.bat           — Windows one-click launcher
├── REFERENCES.md       — External sources and attribution
└── README.md           — This file
```

---

## Anomaly Detection Rules

The engine maintains a 20-sample sliding window per device and fires alerts when:

| Rule            | Trigger                                            | Severity |
|-----------------|----------------------------------------------------|----------|
| `bw_spike`      | Bandwidth z-score > 3.5 (statistical outlier)     | Medium   |
| `bw_absolute`   | Total bytes > 50 MB in one interval                | High     |
| `conn_storm`    | Connection count z-score > 3.0                    | Medium   |
| `conn_absolute` | Active connections > 500                           | High     |
| `agent_alert`   | Agent self-reported alert flag set                 | Medium   |

Cooldown of 10 seconds per rule per device prevents alert flooding.

---

## Building from Source

### Requirements

- **Windows**: MinGW-w64 with g++ supporting C++17 (`mingw32-make --version`)
- **Linux/macOS**: g++ 11+ with pthreads
- No external libraries required

### Build Targets

| Command                 | Description                         |
|-------------------------|-------------------------------------|
| `mingw32-make all`      | Build all five binaries             |
| `mingw32-make test`     | Run the 24-test unit suite          |
| `mingw32-make clean`    | Remove `bin/` and generated JSON    |
| `mingw32-make help`     | Show all targets                    |

---

## Troubleshooting

| Problem | Fix |
|---|---|
| Dashboard shows "Waiting for devices..." | Make sure `collector.exe` is running first, then `agent.exe` or `simulator.exe` |
| `agent.exe` shows "send failed" | Collector is not running or firewall is blocking UDP port 9000 |
| No graph appears in browser | Hard-reload the page (Ctrl+Shift+R). Check browser console for errors |
| Port 8080 already in use | Another process is using 8080 — close it or edit `dashboard_server.cpp` to change the port |
| Port 9000 already in use | Edit `collector.cpp` line with `COLLECTOR_PORT` and rebuild |
| Devices go offline after 30s | Normal — agent must be running continuously. Restart `agent.exe` |

---

## Contributing

See `COMMIT_PLAN.md` for the branching strategy and commit message conventions used in this project.
