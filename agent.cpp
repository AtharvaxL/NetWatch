/*
 * NetWatch Agent
 * Runs on each monitored node. Sends NWP packets to the collector.
 *
 * Behaviour:
 *   1. On startup: sends HELLO with hostname
 *   2. Every STATUS_INTERVAL seconds: sends STATUS with traffic stats
 *   3. On Ctrl+C: sends GOODBYE and exits cleanly
 *
 * Build (Windows, MinGW):
 *   g++ -std=c++17 -I../include agent.cpp -o agent.exe -lws2_32 -liphlpapi
 *
 * Build (Linux/CI):
 *   g++ -std=c++17 -I../include agent.cpp -o agent -lpthread
 *
 * Usage:
 *   agent.exe <collector_ip> [collector_port] [device_id]
 *   e.g.  agent.exe 192.168.1.1 9000 1001
 */

#ifdef _WIN32
  #define _WIN32_WINNT 0x0600
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <iphlpapi.h>
  #pragma comment(lib, "ws2_32.lib")
  #pragma comment(lib, "iphlpapi.lib")
  using socklen_t = int;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <ifaddrs.h>
  #define SOCKET int
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR   (-1)
  #define closesocket    close
#endif

#include "nwp.h"
#include <cstdio>
#include <cstring>
#include <csignal>
#include <atomic>
#ifndef _WIN32
#include <thread>
#endif
#include <chrono>
#include <iostream>
#include <string>
#include <random>
#include <sstream>

// ---------------------------------------------------------
// Configuration
// ---------------------------------------------------------
constexpr int STATUS_INTERVAL = 5;    // seconds between STATUS packets

static std::atomic<bool> g_running{true};

// ---------------------------------------------------------
// Utilities
// ---------------------------------------------------------
static std::string get_hostname() {
    char buf[256] = "unknown-host";
    gethostname(buf, sizeof(buf));
    return buf;
}

// Get first non-loopback IPv4 address
static uint32_t get_local_ip() {
#ifdef _WIN32
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(hostname, nullptr, &hints, &res) == 0 && res) {
        auto* sa = reinterpret_cast<sockaddr_in*>(res->ai_addr);
        uint32_t ip = ntohl(sa->sin_addr.s_addr);
        freeaddrinfo(res);
        return ip;
    }
    return 0xC0A80001; // fallback 192.168.0.1
#else
    struct ifaddrs* ifa = nullptr;
    getifaddrs(&ifa);
    uint32_t result = 0x7F000001;
    for (auto* i = ifa; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        auto* sa = reinterpret_cast<sockaddr_in*>(i->ifa_addr);
        uint32_t ip = ntohl(sa->sin_addr.s_addr);
        if ((ip >> 24) != 127) { result = ip; break; }
    }
    if (ifa) freeifaddrs(ifa);
    return result;
#endif
}

// Generate a pseudo-MAC from device_id (deterministic, safe for demo)
static void make_mac(uint32_t device_id, uint8_t mac[6]) {
    mac[0] = 0x02; // locally administered
    mac[1] = 0x00;
    mac[2] = (device_id >> 24) & 0xFF;
    mac[3] = (device_id >> 16) & 0xFF;
    mac[4] = (device_id >>  8) & 0xFF;
    mac[5] =  device_id        & 0xFF;
}

// Simulate traffic counters (increases each interval)
struct TrafficSim {
    std::mt19937 rng;
    uint32_t total_sent = 0;
    uint32_t total_recv = 0;
    uint16_t conns      = 3;
    bool     attack_mode = false;

    TrafficSim() : rng(std::random_device{}()) {}

    void tick() {
        if (attack_mode) {
            // Flood mode: very high bandwidth
            std::uniform_int_distribution<uint32_t> d(5'000'000, 20'000'000);
            total_sent += d(rng);
            total_recv += d(rng) / 2;
            std::uniform_int_distribution<uint16_t> cd(300, 800);
            conns = cd(rng);
        } else {
            // Normal mode
            std::uniform_int_distribution<uint32_t> d(1'000, 100'000);
            total_sent += d(rng);
            total_recv += d(rng);
            std::uniform_int_distribution<uint16_t> cd(1, 20);
            conns = cd(rng);
        }
    }
};

static void handle_signal(int) { g_running = false; }

// ---------------------------------------------------------
// Send a single NWP packet over UDP
// ---------------------------------------------------------
static bool send_packet(SOCKET sock, sockaddr_in& dest, NWPPacket& pkt) {
    auto buf = NWPCodec::encode(pkt);
    int n = sendto(sock, reinterpret_cast<const char*>(buf.data()),
                   static_cast<int>(buf.size()), 0,
                   reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    return n != SOCKET_ERROR;
}

// ---------------------------------------------------------
// Main
// ---------------------------------------------------------
int main(int argc, char* argv[]) {
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::string collector_ip   = argc > 1 ? argv[1] : "127.0.0.1";
    uint16_t    collector_port = argc > 2 ? static_cast<uint16_t>(std::stoi(argv[2])) : 9000;
    uint32_t    device_id      = argc > 3 ? static_cast<uint32_t>(std::stoi(argv[3])) : 1001;

#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "socket() failed\n";
        return 1;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(collector_port);
    dest.sin_addr.s_addr = inet_addr(collector_ip.c_str());

    uint8_t mac[6];
    make_mac(device_id, mac);
    uint32_t ip = get_local_ip();
    std::string hostname = get_hostname();

    std::cout << "[Agent] device_id=" << device_id
              << " ip=" << (ip>>24&0xFF) << "." << (ip>>16&0xFF) << "."
              << (ip>>8&0xFF) << "." << (ip&0xFF)
              << " -> collector=" << collector_ip << ":" << collector_port << "\n";

    // Send HELLO
    auto hello = NWPCodec::make_hello(device_id, mac, ip, hostname);
    if (send_packet(sock, dest, hello))
        std::cout << "[Agent] Sent HELLO\n";

    TrafficSim sim;
    int tick = 0;

    while (g_running) {
        #ifdef _WIN32
        Sleep(STATUS_INTERVAL * 1000);
        #else
        std::this_thread::sleep_for(std::chrono::seconds(STATUS_INTERVAL));
        #endif
        if (!g_running) break;

        sim.tick();
        tick++;

        // Simulate attack burst every 30 ticks for demo purposes
        if (tick % 30 == 0) {
            sim.attack_mode = true;
            std::cout << "[Agent] >>> ATTACK MODE ON <<<\n";
        } else if (tick % 30 == 5) {
            sim.attack_mode = false;
            std::cout << "[Agent] Attack mode off\n";
        }

        bool alert = sim.attack_mode;
        auto status = NWPCodec::make_status(
            device_id, mac, ip,
            sim.conns, sim.total_sent, sim.total_recv, alert);

        if (send_packet(sock, dest, status)) {
            std::cout << "[Agent] Sent STATUS "
                      << "conns=" << sim.conns
                      << " bw=" << (sim.total_sent + sim.total_recv)
                      << (alert ? " [ALERT]" : "") << "\n";
        }

        // Optionally send ALERT packet separately in attack mode
        if (alert) {
            auto apkt = NWPCodec::make_alert(device_id, mac, ip,
                        AlertSeverity::HIGH, "Packet flood detected");
            send_packet(sock, dest, apkt);
            std::cout << "[Agent] Sent ALERT packet\n";
        }
    }

    // Send GOODBYE
    auto bye = NWPCodec::make_goodbye(device_id, mac, ip);
    send_packet(sock, dest, bye);
    std::cout << "[Agent] Sent GOODBYE, exiting.\n";

    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
