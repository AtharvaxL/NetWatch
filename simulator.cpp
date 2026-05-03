/*
 * NetWatch Simulator
 * Spawns N virtual devices that send NWP packets to the collector.
 * Used for demo and testing without real network devices.
 *
 * Build (Windows):
 *   g++ -std=c++17 -I../include simulator.cpp -o simulator.exe -lws2_32
 *
 * Build (Linux):
 *   g++ -std=c++17 -I../include simulator.cpp -o simulator -lpthread
 *
 * Usage:
 *   simulator.exe [num_nodes] [collector_ip] [scenario]
 *   scenario: normal | attack | churn
 *   e.g.  simulator.exe 6 127.0.0.1 attack
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
#include <atomic>
#ifndef _WIN32
#include <thread>
#endif
#include <vector>
#include <iostream>
#include <string>
#include <random>
#include <chrono>
#include <csignal>
#include <functional>

static std::atomic<bool> g_running{true};
static void handle_signal(int) { g_running = false; }

// Thread args for Windows
struct DeviceArgs {
    uint32_t device_id;
    std::string collector_ip;
    uint16_t port;
    std::string scenario;
};

static void device_thread(uint32_t device_id, const std::string& collector_ip,
                           uint16_t port, const std::string& scenario);

#ifdef _WIN32
static DWORD WINAPI device_thread_win(LPVOID lpParam) {
    auto args = reinterpret_cast<DeviceArgs*>(lpParam);
    device_thread(args->device_id, args->collector_ip, args->port, args->scenario);
    delete args;
    return 0;
}
#endif
static void device_thread(uint32_t device_id, const std::string& collector_ip,
                           uint16_t port, const std::string& scenario) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return;

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(port);
    dest.sin_addr.s_addr = inet_addr(collector_ip.c_str());

    // Build fake MAC and IP
    uint8_t mac[6] = { 0x02, 0x00,
        (uint8_t)(device_id >> 8), (uint8_t)device_id,
        (uint8_t)(device_id * 7), (uint8_t)(device_id * 13) };

    uint32_t ip = (192u << 24) | (168u << 16) | (1u << 8) | (device_id & 0xFF);

    std::string hostname = "node-" + std::to_string(device_id);

    std::mt19937 rng(device_id);
    std::uniform_int_distribution<uint32_t> bw_normal(5000, 200000);
    std::uniform_int_distribution<uint32_t> bw_attack(5000000, 20000000);
    std::uniform_int_distribution<uint16_t> conn_normal(1, 25);
    std::uniform_int_distribution<uint16_t> conn_attack(300, 900);
    std::uniform_int_distribution<int>      attack_start(10, 20);

    auto send_pkt = [&](NWPPacket& p) {
        auto buf = NWPCodec::encode(p);
        sendto(sock, reinterpret_cast<const char*>(buf.data()),
               static_cast<int>(buf.size()), 0,
               reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    };

    // HELLO
    auto hello = NWPCodec::make_hello(device_id, mac, ip, hostname);
    send_pkt(hello);
    std::cout << "[SIM] Node " << device_id << " (" << hostname << ") online\n";

    uint32_t sent = 0, recv = 0;
    int ticks = 0;
    bool attacking = false;
    int attack_tick = attack_start(rng);

    while (g_running) {
        #ifdef _WIN32
        Sleep(5000);
        #else
        std::this_thread::sleep_for(std::chrono::seconds(5));
        #endif
        if (!g_running) break;

        ticks++;

        // Determine if this node should attack this tick
        if (scenario == "attack" && device_id % 3 == 1) {
            // One third of nodes launch attacks periodically
            attacking = (ticks >= attack_tick && ticks < attack_tick + 4);
        } else if (scenario == "churn") {
            // Churn: device goes offline and comes back
            if (ticks % 12 == 0) {
                auto bye = NWPCodec::make_goodbye(device_id, mac, ip);
                send_pkt(bye);
                std::cout << "[SIM] Node " << device_id << " going offline (churn)\n";
                #ifdef _WIN32
                Sleep(8000);
                #else
                std::this_thread::sleep_for(std::chrono::seconds(8));
                #endif
                auto re_hello = NWPCodec::make_hello(device_id, mac, ip, hostname);
                send_pkt(re_hello);
                std::cout << "[SIM] Node " << device_id << " back online (churn)\n";
                continue;
            }
        }

        if (attacking) {
            sent += bw_attack(rng);
            recv += bw_attack(rng) / 2;
        } else {
            sent += bw_normal(rng);
            recv += bw_normal(rng);
        }

        uint16_t conns = attacking ? conn_attack(rng) : conn_normal(rng);
        auto status = NWPCodec::make_status(device_id, mac, ip,
                                             conns, sent, recv, attacking);
        send_pkt(status);

        if (attacking) {
            auto apkt = NWPCodec::make_alert(device_id, mac, ip,
                        AlertSeverity::HIGH, "Simulated flood");
            send_pkt(apkt);
            std::cout << "[SIM] Node " << device_id << " >>> ATTACK <<< bw="
                      << (sent + recv) << "\n";
        }
    }

    auto bye = NWPCodec::make_goodbye(device_id, mac, ip);
    send_pkt(bye);
    std::cout << "[SIM] Node " << device_id << " sent GOODBYE\n";
    closesocket(sock);
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "Usage: simulator.exe [num_nodes] [collector_ip] [scenario]\n"
                  << "  num_nodes    : number of virtual devices (default: 6)\n"
                  << "  collector_ip : IP of the collector (default: 127.0.0.1)\n"
                  << "  scenario     : normal | attack | churn (default: attack)\n"
                  << "Example: simulator.exe 4 192.168.1.10 normal\n";
        return 0;
    }

    int         num_nodes     = argc > 1 ? std::stoi(argv[1]) : 6;
    std::string collector_ip  = argc > 2 ? argv[2] : "127.0.0.1";
    std::string scenario      = argc > 3 ? argv[3] : "attack";
    uint16_t    port          = 9000;

    std::cout << "[SIM] Starting " << num_nodes << " virtual nodes"
              << " -> " << collector_ip << ":" << port
              << " scenario=" << scenario << "\n";

#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

#ifdef _WIN32
    std::vector<HANDLE> threads;
#else
    std::vector<std::thread> threads;
#endif
    threads.reserve(num_nodes);

    // Stagger start times so HELLO packets don't all arrive at once
    for (int i = 0; i < num_nodes; i++) {
#ifdef _WIN32
        auto args = new DeviceArgs{static_cast<uint32_t>(1000 + i), collector_ip, port, scenario};
        HANDLE h = CreateThread(NULL, 0, device_thread_win, args, 0, NULL);
        if (h) threads.push_back(h);
        Sleep(200);
#else
        threads.emplace_back(device_thread,
                             static_cast<uint32_t>(1000 + i),
                             collector_ip, port, scenario);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif
    }

    std::cout << "[SIM] All nodes running. Ctrl+C to stop.\n";

#ifdef _WIN32
    if (!threads.empty()) {
        WaitForMultipleObjects(static_cast<DWORD>(threads.size()), threads.data(), TRUE, INFINITE);
        for (auto h : threads) CloseHandle(h);
    }
#else
    for (auto& t : threads) t.join();
#endif

#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "[SIM] Done.\n";
    return 0;
}
