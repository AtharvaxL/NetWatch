/*
 * NetWatch Agent
 * Runs on a monitored host. Reads real network I/O statistics from the OS
 * and reports them to the collector via NWP over UDP.
 *
 * Metrics reported (real, not simulated):
 *   - bytes_sent / bytes_recv : delta since last interval (all non-loopback ifaces)
 *   - active_conns            : number of ESTABLISHED TCP connections
 *
 * Build (Windows, MinGW):
 *   g++ -std=c++17 -I. agent.cpp -o agent.exe -lws2_32 -liphlpapi
 *
 * Build (Linux):
 *   g++ -std=c++17 -I. agent.cpp -o agent -lpthread
 *
 * Usage:
 *   agent.exe [collector_ip] [collector_port] [device_id]
 *   e.g.  agent.exe 192.168.1.100 9000 1001
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
#include <fstream>
#endif
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

// ---------------------------------------------------------
// Configuration
// ---------------------------------------------------------
constexpr int STATUS_INTERVAL  = 5;  // seconds between STATUS packets
constexpr int MAX_SEND_RETRIES = 3;  // UDP send retries on transient failure

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

// MAC derived from device_id (locally-administered, deterministic)
static void make_mac(uint32_t device_id, uint8_t mac[6]) {
    mac[0] = 0x02;
    mac[1] = 0x00;
    mac[2] = (device_id >> 24) & 0xFF;
    mac[3] = (device_id >> 16) & 0xFF;
    mac[4] = (device_id >>  8) & 0xFF;
    mac[5] =  device_id        & 0xFF;
}

// ---------------------------------------------------------
// Real network statistics — reads actual OS counters
// ---------------------------------------------------------
struct NetStats {
    uint64_t bytes_sent = 0;
    uint64_t bytes_recv = 0;
    uint32_t tcp_conns  = 0;
};

static NetStats read_net_stats() {
    NetStats s{};
#ifdef _WIN32
    // Byte totals across all non-loopback interfaces
    DWORD needed = 0;
    GetIfTable(nullptr, &needed, FALSE);
    std::vector<BYTE> ifBuf(needed);
    auto* pIf = reinterpret_cast<MIB_IFTABLE*>(ifBuf.data());
    if (GetIfTable(pIf, &needed, FALSE) == 0) {
        for (DWORD i = 0; i < pIf->dwNumEntries; ++i) {
            if (pIf->table[i].dwType == MIB_IF_TYPE_LOOPBACK) continue;
            s.bytes_sent += pIf->table[i].dwOutOctets;
            s.bytes_recv += pIf->table[i].dwInOctets;
        }
    }
    // Count only ESTABLISHED TCP connections
    needed = 0;
    GetExtendedTcpTable(nullptr, &needed, FALSE, AF_INET,
                        TCP_TABLE_OWNER_PID_ALL, 0);
    std::vector<BYTE> tcpBuf(needed);
    auto* pTcp = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(tcpBuf.data());
    if (GetExtendedTcpTable(pTcp, &needed, FALSE, AF_INET,
                            TCP_TABLE_OWNER_PID_ALL, 0) == 0) {
        for (DWORD i = 0; i < pTcp->dwNumEntries; ++i)
            if (pTcp->table[i].dwState == MIB_TCP_STATE_ESTAB) ++s.tcp_conns;
    }
#else
    // Linux: /proc/net/dev for byte counts
    {
        std::ifstream f("/proc/net/dev");
        std::string line;
        std::getline(f, line); std::getline(f, line); // skip 2 header lines
        while (std::getline(f, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string iface = line.substr(0, colon);
            if (iface.find("lo") != std::string::npos) continue;
            std::istringstream iss(line.substr(colon + 1));
            uint64_t rb, rp, re, rd, rf, rc, rff, rm, sb;
            iss >> rb >> rp >> re >> rd >> rf >> rc >> rff >> rm >> sb;
            s.bytes_recv += rb;
            s.bytes_sent += sb;
        }
    }
    // Linux: /proc/net/tcp — count state 01 (ESTABLISHED)
    {
        std::ifstream f("/proc/net/tcp");
        std::string line;
        std::getline(f, line); // skip header
        while (std::getline(f, line)) {
            std::istringstream iss(line);
            std::string sl, local, remote, state;
            iss >> sl >> local >> remote >> state;
            if (state == "01") ++s.tcp_conns;
        }
    }
#endif
    return s;
}

static void handle_signal(int) { g_running = false; }

// ---------------------------------------------------------
// Send a single NWP packet over UDP
// ---------------------------------------------------------
static bool send_packet(SOCKET sock, sockaddr_in& dest, NWPPacket& pkt) {
    auto buf = NWPCodec::encode(pkt);
    for (int attempt = 0; attempt < MAX_SEND_RETRIES; ++attempt) {
        int n = sendto(sock, reinterpret_cast<const char*>(buf.data()),
                       static_cast<int>(buf.size()), 0,
                       reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        if (n != SOCKET_ERROR) return true;
#ifdef _WIN32
        Sleep(100);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
    }
    std::cerr << "[Agent] WARNING: send failed after " << MAX_SEND_RETRIES << " attempts\n";
    return false;
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

    // Baseline reading — we report deltas, not cumulative totals
    NetStats prev = read_net_stats();

    while (g_running) {
        #ifdef _WIN32
        Sleep(STATUS_INTERVAL * 1000);
        #else
        std::this_thread::sleep_for(std::chrono::seconds(STATUS_INTERVAL));
        #endif
        if (!g_running) break;

        NetStats cur = read_net_stats();

        // Delta bytes over this interval (handles counter wrap gracefully)
        uint32_t sent_delta = static_cast<uint32_t>(cur.bytes_sent - prev.bytes_sent);
        uint32_t recv_delta = static_cast<uint32_t>(cur.bytes_recv - prev.bytes_recv);
        uint16_t conns      = static_cast<uint16_t>(
                                  std::min(cur.tcp_conns, (uint32_t)65535));
        prev = cur;

        auto status = NWPCodec::make_status(
            device_id, mac, ip, conns, sent_delta, recv_delta, false);

        if (send_packet(sock, dest, status)) {
            std::cout << "[Agent] STATUS sent="
                      << sent_delta << "B recv=" << recv_delta
                      << "B conns=" << conns << "\n";
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
