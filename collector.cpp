/*
 * NetWatch Collector Server
 * Listens on UDP port 9000 for NWP packets from agents.
 * Parses each packet, updates the device registry,
 * runs anomaly detection, and logs events.
 *
 * Build (Windows, MinGW):
 *   g++ -std=c++17 -I../include collector.cpp -o collector.exe -lws2_32
 *
 * Build (Linux/CI):
 *   g++ -std=c++17 -I../include collector.cpp -o collector -lpthread
 */

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socklen_t = int;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #define SOCKET int
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR   (-1)
  #define closesocket    close
#endif

#include "nwp.h"
#include "registry.h"
#include "anomaly.h"

#include <cstdio>
#include <cstring>
#include <csignal>
#include <atomic>
#ifndef _WIN32
#include <thread>
#endif
#include <chrono>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------
// Configuration
// ---------------------------------------------------------
constexpr uint16_t COLLECTOR_PORT   = 9000;
constexpr int      RECV_TIMEOUT_MS  = 1000;
constexpr int      EXPIRE_INTERVAL  = 5;    // seconds between stale-check
constexpr int      STALE_TIMEOUT    = 30;   // seconds before marking offline

// ---------------------------------------------------------
// Globals (shared between threads)
// ---------------------------------------------------------
static std::atomic<bool> g_running{true};
static DeviceRegistry    g_registry;
static std::ofstream     g_log_file;

// ---------------------------------------------------------
// Logging
// ---------------------------------------------------------
static void log(const std::string& level, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char ts[32];
#ifdef _WIN32
    struct tm* tm_info = localtime(&t);
    if (tm_info) strftime(ts, sizeof(ts), "%H:%M:%S", tm_info);
    else ts[0] = '\0';
#else
    struct tm* tm_info = localtime(&t);
    strftime(ts, sizeof(ts), "%H:%M:%S", tm_info);
#endif
    std::string line = std::string("[") + ts + "] [" + level + "] " + msg;
    std::cout << line << "\n";
    if (g_log_file.is_open()) g_log_file << line << "\n";
}

// ---------------------------------------------------------
// Alert callback
// ---------------------------------------------------------
static void on_alert(const AlertEvent& ev) {
    std::string sev = ev.severity == 3 ? "HIGH" :
                      ev.severity == 2 ? "MEDIUM" : "LOW";
    log("ALERT", "[" + sev + "] device=" + std::to_string(ev.device_id) +
        " ip=" + ev.ip + " rule=" + ev.rule + " | " + ev.detail);

    // Append to alerts.json for dashboard to read
    std::ofstream af("alerts.json", std::ios::app);
    if (af.is_open()) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      ev.timestamp.time_since_epoch()).count();
        af << "{\"device_id\":" << ev.device_id
           << ",\"ip\":\"" << ev.ip << "\""
           << ",\"rule\":\"" << ev.rule << "\""
           << ",\"detail\":\"" << ev.detail << "\""
           << ",\"severity\":" << (int)ev.severity
           << ",\"ts\":" << ms << "}\n";
    }
}

// ---------------------------------------------------------
// Snapshot thread — writes devices.json every 2 seconds
// ---------------------------------------------------------
static void snapshot_thread_fn() {
    while (g_running) {
        #ifdef _WIN32
        Sleep(2000);
        #else
        std::this_thread::sleep_for(std::chrono::seconds(2));
        #endif
        g_registry.expire_stale(STALE_TIMEOUT);
        std::string snap = g_registry.snapshot_json();
        std::ofstream f("devices.json");
        if (f.is_open()) f << snap;
        log("INFO", "Snapshot written (" +
            std::to_string(g_registry.online_count()) + " online, " +
            std::to_string(g_registry.total_count()) + " total)");
    }
}

#ifdef _WIN32
static DWORD WINAPI snap_thread_starter(LPVOID) {
    snapshot_thread_fn();
    return 0;
}
#endif

// ---------------------------------------------------------
// Signal handler
// ---------------------------------------------------------
static void handle_signal(int) { g_running = false; }

// ---------------------------------------------------------
// Main
// ---------------------------------------------------------
int main(int argc, char* argv[]) {
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    // Optional log file argument
    if (argc > 1) {
        g_log_file.open(argv[1], std::ios::app);
    }

    // Clear stale output files
    { std::ofstream f("devices.json"); f << "[]"; }
    { std::ofstream f("alerts.json");  }

    log("INFO", "==============================================");
    log("INFO", "  NetWatch Collector v1.0");
    log("INFO", "  UDP port : " + std::to_string(COLLECTOR_PORT));
    log("INFO", "  Snapshot : devices.json (every 2s)");
    log("INFO", "  Alerts   : alerts.json (on event)");
    log("INFO", "==============================================");
    log("INFO", "Waiting for NWP agents...");

#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        log("ERROR", "WSAStartup failed");
        return 1;
    }
#endif

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        log("ERROR", "socket() failed");
        return 1;
    }

    // Set receive timeout so the main loop can check g_running
#ifdef _WIN32
    DWORD tv = RECV_TIMEOUT_MS;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv{ 0, RECV_TIMEOUT_MS * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(COLLECTOR_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        log("ERROR", "bind() failed on port " + std::to_string(COLLECTOR_PORT));
        closesocket(sock);
        return 1;
    }

    log("INFO", "Listening for NWP packets...");

    // Anomaly engine
    AnomalyEngine engine(on_alert);

    #ifdef _WIN32
    HANDLE snap_thread = CreateThread(NULL, 0, snap_thread_starter, NULL, 0, NULL);
    #else
    std::thread snap_thread(snapshot_thread_fn);
    #endif

    // Receive buffer
    constexpr size_t BUF_SIZE = 4096;
    uint8_t buf[BUF_SIZE];
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);

    uint64_t total_packets   = 0;
    uint64_t invalid_packets = 0;

    while (g_running) {
        int n = recvfrom(sock, reinterpret_cast<char*>(buf), BUF_SIZE, 0,
                         reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n <= 0) continue;  // timeout or error — loop

        NWPPacket pkt;
        if (!NWPCodec::decode(buf, static_cast<size_t>(n), pkt)) {
            invalid_packets++;
            log("WARN", "Invalid NWP packet (" + std::to_string(n) + " bytes) — bad magic/checksum");
            continue;
        }

        total_packets++;

        // Extract hostname from HELLO payload
        std::string hostname;
        if (static_cast<NWPOpcode>(pkt.header.opcode) == NWPOpcode::HELLO && !pkt.payload.empty())
            hostname.assign(pkt.payload.begin(), pkt.payload.end());

        g_registry.update(pkt, hostname);

        // Run anomaly detection for STATUS and ALERT packets
        auto op = static_cast<NWPOpcode>(pkt.header.opcode);
        if (op == NWPOpcode::STATUS || op == NWPOpcode::ALERT) {
            engine.process(pkt.header.device_id, pkt.ip_string(),
                           pkt.header.bytes_sent, pkt.header.bytes_recv,
                           pkt.header.active_conns,
                           pkt.header.alert_flag != 0);
        }
        if (op == NWPOpcode::HELLO)
            engine.register_device(pkt.header.device_id);

        log("INFO", "[" + pkt.opcode_name() + "] id=" +
            std::to_string(pkt.header.device_id) +
            " ip=" + pkt.ip_string() +
            " mac=" + pkt.mac_string() +
            " conns=" + std::to_string(pkt.header.active_conns) +
            " bw=" + std::to_string(pkt.header.bytes_sent + pkt.header.bytes_recv) +
            (pkt.header.alert_flag ? " [ALERT]" : ""));
    }

    log("INFO", "Shutting down. Total packets: " + std::to_string(total_packets) +
        " invalid: " + std::to_string(invalid_packets));

    #ifdef _WIN32
    if (snap_thread) {
        WaitForSingleObject(snap_thread, INFINITE);
        CloseHandle(snap_thread);
    }
    #else
    snap_thread.join();
    #endif
    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
