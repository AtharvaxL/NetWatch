/*
 * NetWatch Dashboard HTTP Server
 * Serves the dashboard HTML on port 8080.
 * Provides REST endpoints:
 *   GET /            -> dashboard HTML
 *   GET /api/devices -> devices.json (live device list)
 *   GET /api/alerts  -> alerts.json  (recent alerts)
 *   GET /api/stats   -> summary stats JSON
 *
 * Build (Windows):
 *   g++ -std=c++17 -I../include dashboard_server.cpp -o dashboard.exe -lws2_32
 *
 * Build (Linux):
 *   g++ -std=c++17 -I../include dashboard_server.cpp -o dashboard -lpthread
 *
 * Run AFTER collector is running.
 * Open http://localhost:8080 in your browser.
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

#include <cstdio>
#include <cstring>
#include <csignal>
#include <atomic>
#ifndef _WIN32
#include <thread>
#endif
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>

constexpr uint16_t HTTP_PORT = 8080;
static std::atomic<bool> g_running{true};
static void handle_signal(int) { g_running = false; }

// ---------------------------------------------------------
// File utilities
// ---------------------------------------------------------
static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Read last N lines from a file (for alerts)
static std::string read_last_lines(const std::string& path, int n = 50) {
    std::ifstream f(path);
    if (!f.is_open()) return "[]";
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line))
        if (!line.empty()) lines.push_back(line);
    // Return last n as JSON array
    std::ostringstream ss;
    ss << "[";
    int start = std::max(0, (int)lines.size() - n);
    for (int i = start; i < (int)lines.size(); i++) {
        if (i > start) ss << ",";
        ss << lines[i];
    }
    ss << "]";
    return ss.str();
}

// ---------------------------------------------------------
// HTTP response builder
// ---------------------------------------------------------
static std::string http_response(int code, const std::string& ctype,
                                  const std::string& body) {
    std::string status = (code == 200) ? "200 OK" :
                         (code == 404) ? "404 Not Found" : "500 Internal Server Error";
    std::ostringstream r;
    r << "HTTP/1.1 " << status << "\r\n"
      << "Content-Type: " << ctype << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Cache-Control: no-cache, no-store\r\n"
      << "X-Content-Type-Options: nosniff\r\n"
      << "Connection: close\r\n"
      << "\r\n"
      << body;
    return r.str();
}

// ---------------------------------------------------------
// Client handler thread
// ---------------------------------------------------------
static void handle_client(SOCKET client) {
    char buf[4096] = {};
    recv(client, buf, sizeof(buf) - 1, 0);

    std::string req(buf);
    std::string path = "/";

    // Parse path from "GET /path HTTP/..."
    auto get_pos = req.find("GET ");
    if (get_pos != std::string::npos) {
        size_t start = get_pos + 4;
        size_t end   = req.find(' ', start);
        if (end != std::string::npos)
            path = req.substr(start, end - start);
    }

    std::string response;

    if (path == "/" || path == "/index.html") {
        std::string html = read_file("index.html");
        if (html.empty()) html = "<h1>NetWatch</h1><p>index.html not found. Run from project root.</p>";
        response = http_response(200, "text/html; charset=utf-8", html);

    } else if (path == "/api/devices") {
        std::string data = read_file("devices.json");
        if (data.empty()) data = "[]";
        response = http_response(200, "application/json", data);

    } else if (path == "/api/alerts") {
        std::string data = read_last_lines("alerts.json", 50);
        response = http_response(200, "application/json", data);

    } else if (path == "/api/stats") {
        // Simple stats JSON
        std::string devs = read_file("devices.json");
        int online = 0;
        for (size_t p = 0; (p = devs.find("\"online\"", p)) != std::string::npos; p++) online++;
        std::ostringstream stats;
        stats << "{\"online_devices\":" << online
              << ",\"server_time\":" << std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch()).count()
              << "}";
        response = http_response(200, "application/json", stats.str());

    } else {
        response = http_response(404, "text/plain", "Not found");
    }

    send(client, response.c_str(), static_cast<int>(response.size()), 0);
    closesocket(client);
}

#ifdef _WIN32
static DWORD WINAPI handle_client_win(LPVOID lpParam) {
    SOCKET client = static_cast<SOCKET>(reinterpret_cast<uintptr_t>(lpParam));
    handle_client(client);
    return 0;
}
#endif

// ---------------------------------------------------------
// Main
// ---------------------------------------------------------
int main() {
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::cerr << "socket() failed\n";
        return 1;
    }

    // Allow port reuse
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed on port " << HTTP_PORT << "\n";
        return 1;
    }

    listen(server, 10);
    std::cout << "[Dashboard] Serving at http://localhost:" << HTTP_PORT << "\n";
    std::cout << "[Dashboard] Endpoints: /api/devices  /api/alerts  /api/stats\n";

    while (g_running) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);
        SOCKET client = accept(server,
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &client_len);
        if (client == INVALID_SOCKET) continue;

        // Handle each client in a detached thread
#ifdef _WIN32
        HANDLE h = CreateThread(NULL, 0, handle_client_win, reinterpret_cast<LPVOID>(static_cast<uintptr_t>(client)), 0, NULL);
        if (h) CloseHandle(h); // detach
#else
        std::thread(handle_client, client).detach();
#endif
    }

    closesocket(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
