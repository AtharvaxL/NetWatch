# NetWatch C++ Build System
# Supports: Windows (MinGW g++), Linux (g++)
#
# Windows usage:
#   mingw32-make all
#
# Linux usage:
#   make all

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
  MKDIR   := mkdir -p $(OUTDIR)
endif

COLLECTOR  := $(OUTDIR)/collector$(EXT)
AGENT      := $(OUTDIR)/agent$(EXT)
DASHBOARD  := $(OUTDIR)/dashboard$(EXT)
SIMULATOR  := $(OUTDIR)/simulator$(EXT)
TESTS      := $(OUTDIR)/test_netwatch$(EXT)

.PHONY: all clean test run-demo help

all: $(OUTDIR) $(COLLECTOR) $(AGENT) $(DASHBOARD) $(SIMULATOR) $(TESTS)
	@echo ""
	@echo "=== Build complete ==="
	@echo "  collector$(EXT)   - receives NWP packets from agents"
	@echo "  agent$(EXT)       - runs on each monitored node"
	@echo "  dashboard$(EXT)   - serves browser UI on port 8080"
	@echo "  simulator$(EXT)   - virtual nodes for demo/testing"
	@echo "  test_netwatch$(EXT) - run unit tests"
	@echo ""
	@echo "Quick start: make run-demo"

$(OUTDIR):
	$(MKDIR)

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

# Demo: starts collector + simulator + dashboard in background
# Open http://localhost:8080 after running this
run-demo: all
	@echo "Starting NetWatch demo..."
	@echo "Open http://localhost:8080 in your browser"
ifeq ($(OS),Windows_NT)
	start /B $(COLLECTOR)
	timeout /t 1 > nul
	start /B $(DASHBOARD)
	timeout /t 1 > nul
	$(SIMULATOR) 6 127.0.0.1 attack
else
	$(COLLECTOR) &
	sleep 1
	$(DASHBOARD) &
	sleep 1
	$(SIMULATOR) 6 127.0.0.1 attack
endif

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
	@echo "  make all       - build everything"
	@echo "  make test      - run 24 unit tests"
	@echo "  make run-demo  - start full demo (collector + sim + dashboard)"
	@echo "  make clean     - remove build artifacts"
