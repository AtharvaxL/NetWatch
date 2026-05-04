# NetWatch C++ Build System
# Supports: Windows (MinGW g++), Linux (g++)
#
# Windows usage:
#   mingw32-make all
#   start.bat          <- runs collector + dashboard (see start.bat)
#
# Linux usage:
#   make all
#   make run

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I .
OUTDIR   := bin

# Platform detection
ifeq ($(OS),Windows_NT)
  EXT     := .exe
  LDFLAGS := -lws2_32 -liphlpapi
  MKDIR   := if not exist $(OUTDIR) mkdir $(OUTDIR)
else
  EXT     :=
  LDFLAGS := -lpthread
  MKDIR   := test -d $(OUTDIR) || (rm -f $(OUTDIR) 2>/dev/null; mkdir -p $(OUTDIR))
endif

COLLECTOR  := $(OUTDIR)/collector$(EXT)
AGENT      := $(OUTDIR)/agent$(EXT)
DASHBOARD  := $(OUTDIR)/dashboard$(EXT)
SIMULATOR  := $(OUTDIR)/simulator$(EXT)
TESTS      := $(OUTDIR)/test_netwatch$(EXT)

.PHONY: all clean test run stop help

all: $(OUTDIR) $(COLLECTOR) $(AGENT) $(DASHBOARD) $(SIMULATOR) $(TESTS)
	@echo ""
	@echo "=== Build complete ==="
	@echo "  collector$(EXT)     - receives NWP packets from agents"
	@echo "  agent$(EXT)         - runs on each monitored node"
	@echo "  dashboard$(EXT)     - serves browser UI on port 8080"
	@echo "  simulator$(EXT)     - virtual nodes for testing"
	@echo "  test_netwatch$(EXT) - unit tests"
	@echo ""
	@echo "Windows: run start.bat to launch the full stack"
	@echo "Linux:   make run"

$(OUTDIR):
	$(MKDIR)

.PHONY: all test clean run help

$(COLLECTOR): collector.cpp nwp.h registry.h anomaly.h
	$(CXX) $(CXXFLAGS) collector.cpp -o $@ $(LDFLAGS)
	@echo "[OK] collector"

$(AGENT): agent.cpp nwp.h
	$(CXX) $(CXXFLAGS) agent.cpp -o $@ $(LDFLAGS)
	@echo "[OK] agent"

$(DASHBOARD): dashboard_server.cpp
	$(CXX) $(CXXFLAGS) dashboard_server.cpp -o $@ $(LDFLAGS)
	@echo "[OK] dashboard"

$(SIMULATOR): simulator.cpp nwp.h
	$(CXX) $(CXXFLAGS) simulator.cpp -o $@ $(LDFLAGS)
	@echo "[OK] simulator"

$(TESTS): test_netwatch.cpp nwp.h anomaly.h registry.h
	$(CXX) $(CXXFLAGS) test_netwatch.cpp -o $@ $(LDFLAGS)
	@echo "[OK] tests"

test: $(TESTS)
	@echo ""
	@echo "Running NetWatch test suite..."
	@$(TESTS)

# Linux/macOS only: kill old instances then launch in background
run: all stop
	@echo "Starting collector on UDP :9000..."
	$(COLLECTOR) &
	@sleep 1
	@echo "Starting dashboard on http://localhost:8080"
	$(DASHBOARD) &
	@echo ""
	@echo "Dashboard ready -> http://localhost:8080"
	@echo "Now run agents:  ./bin/agent <collector-ip>"
	@echo "Or simulator:    ./bin/simulator 4 127.0.0.1"

stop:
	@pkill -f bin/collector 2>/dev/null || true
	@pkill -f bin/dashboard 2>/dev/null || true
	@pkill -f bin/simulator 2>/dev/null || true
	@sleep 1

clean:
ifeq ($(OS),Windows_NT)
	if exist $(OUTDIR) rmdir /s /q $(OUTDIR)
	if exist devices.json del devices.json
	if exist alerts.json del alerts.json
else
	rm -rf $(OUTDIR) devices.json alerts.json
endif

help:
	@echo "Targets:"
	@echo "  mingw32-make all   - build all executables"
	@echo "  mingw32-make test  - run 24 unit tests"
	@echo "  mingw32-make clean - remove build artifacts"
	@echo ""
	@echo "Running (Windows):"
	@echo "  Double-click start.bat  OR  run each binary manually:"
	@echo "    bin\\collector.exe"
	@echo "    bin\\dashboard.exe"
	@echo "    bin\\agent.exe 127.0.0.1"
	@echo ""
	@echo "Running (Linux):"
	@echo "  make run"
