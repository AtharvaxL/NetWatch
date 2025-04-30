/*
 * NetWatch Unit Tests
 * Tests NWP protocol encode/decode, all opcodes, anomaly engine rules,
 * and device registry operations.
 *
 * Build (Linux/CI):
 *   g++ -std=c++17 -I../include test_netwatch.cpp -o test_netwatch -lpthread
 *   ./test_netwatch
 *
 * Build (Windows, MinGW):
 *   g++ -std=c++17 -I../include test_netwatch.cpp -o test_netwatch.exe -lws2_32
 */

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <arpa/inet.h>
#endif

#include "nwp.h"
#include "anomaly.h"
#include "registry.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <functional>

// ---------------------------------------------------------
// Minimal test framework
// ---------------------------------------------------------
static int g_pass = 0, g_fail = 0;

static void run_test(const std::string& name, std::function<void()> fn) {
    try {
        fn();
        std::cout << "  [PASS] " << name << "\n";
        g_pass++;
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] " << name << " -- " << e.what() << "\n";
        g_fail++;
    } catch (...) {
        std::cout << "  [FAIL] " << name << " -- unknown exception\n";
        g_fail++;
    }
}

#define ASSERT(cond) \
    if (!(cond)) throw std::runtime_error("Assert failed: " #cond " at line " + std::to_string(__LINE__))

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) throw std::runtime_error( \
        std::string("Assert failed: ") + #a + " == " + #b + \
        " (" + std::to_string(a) + " != " + std::to_string(b) + ")" + \
        " at line " + std::to_string(__LINE__))

// ---------------------------------------------------------
// Helper
// ---------------------------------------------------------
static uint8_t test_mac[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };

// ---------------------------------------------------------
// NWP Protocol Tests
// ---------------------------------------------------------
static void test_header_size() {
    ASSERT_EQ(sizeof(NWPHeader), (size_t)NWP_HEADER_SIZE);
}

static void test_hello_roundtrip() {
    auto pkt = NWPCodec::make_hello(42, test_mac, 0xC0A80101, "testhost");
    auto buf = NWPCodec::encode(pkt);

    NWPPacket out;
    ASSERT(NWPCodec::decode(buf.data(), buf.size(), out));
    ASSERT_EQ(out.header.magic,     (uint16_t)NWP_MAGIC);
    ASSERT_EQ(out.header.version,   (uint8_t)NWP_VERSION);
    ASSERT_EQ(out.header.opcode,    (uint8_t)NWPOpcode::HELLO);
    ASSERT_EQ(out.header.device_id, (uint32_t)42);
    ASSERT_EQ(out.header.ip_addr,   (uint32_t)0xC0A80101);
    ASSERT_EQ(out.header.mac[0],    (uint8_t)0xAA);
    ASSERT_EQ(out.header.mac[5],    (uint8_t)0xFF);

    std::string hostname(out.payload.begin(), out.payload.end());
    ASSERT(hostname == "testhost");
}

static void test_status_roundtrip() {
    auto pkt = NWPCodec::make_status(99, test_mac, 0x0A000001, 15, 123456, 654321, false);
    auto buf = NWPCodec::encode(pkt);

    NWPPacket out;
    ASSERT(NWPCodec::decode(buf.data(), buf.size(), out));
    ASSERT_EQ(out.header.opcode,       (uint8_t)NWPOpcode::STATUS);
    ASSERT_EQ(out.header.device_id,    (uint32_t)99);
    ASSERT_EQ(out.header.active_conns, (uint16_t)15);
    ASSERT_EQ(out.header.bytes_sent,   (uint32_t)123456);
    ASSERT_EQ(out.header.bytes_recv,   (uint32_t)654321);
    ASSERT_EQ(out.header.alert_flag,   (uint8_t)0x00);
}

static void test_alert_roundtrip() {
    auto pkt = NWPCodec::make_alert(7, test_mac, 0xC0A80102,
                                     AlertSeverity::HIGH, "flood");
    auto buf = NWPCodec::encode(pkt);

    NWPPacket out;
    ASSERT(NWPCodec::decode(buf.data(), buf.size(), out));
    ASSERT_EQ(out.header.opcode,     (uint8_t)NWPOpcode::ALERT);
    ASSERT_EQ(out.header.alert_flag, (uint8_t)0x01);
    ASSERT(!out.payload.empty());
    ASSERT_EQ(out.payload[0], (uint8_t)AlertSeverity::HIGH);
    std::string reason(out.payload.begin() + 1, out.payload.end());
    ASSERT(reason == "flood");
}

static void test_goodbye_roundtrip() {
    auto pkt = NWPCodec::make_goodbye(55, test_mac, 0xC0A80103);
    auto buf = NWPCodec::encode(pkt);

    NWPPacket out;
    ASSERT(NWPCodec::decode(buf.data(), buf.size(), out));
    ASSERT_EQ(out.header.opcode, (uint8_t)NWPOpcode::GOODBYE);
    ASSERT_EQ(out.header.device_id, (uint32_t)55);
}

static void test_ack_roundtrip() {
    auto pkt = NWPCodec::make_ack(100);
    auto buf = NWPCodec::encode(pkt);

    NWPPacket out;
    ASSERT(NWPCodec::decode(buf.data(), buf.size(), out));
    ASSERT_EQ(out.header.opcode, (uint8_t)NWPOpcode::ACK);
}

static void test_checksum_corruption() {
    auto pkt = NWPCodec::make_status(1, test_mac, 0xC0A80101, 5, 1000, 2000, false);
    auto buf = NWPCodec::encode(pkt);

    // Corrupt a data byte
    buf[10] ^= 0xFF;

    NWPPacket out;
    ASSERT(!NWPCodec::decode(buf.data(), buf.size(), out));
}

static void test_truncated_packet() {
    auto pkt = NWPCodec::make_hello(1, test_mac, 0xC0A80101, "host");
    auto buf = NWPCodec::encode(pkt);
    buf.resize(10); // truncate

    NWPPacket out;
    ASSERT(!NWPCodec::decode(buf.data(), buf.size(), out));
}

static void test_bad_magic() {
    auto pkt = NWPCodec::make_hello(1, test_mac, 0xC0A80101, "host");
    auto buf = NWPCodec::encode(pkt);
    buf[0] = 0xFF; buf[1] = 0xFF; // bad magic
    // Fix checksum so it passes checksum check but fails magic
    buf[31] = NWPCodec::compute_checksum(buf.data(), 31);

    NWPPacket out;
    ASSERT(!NWPCodec::decode(buf.data(), buf.size(), out));
}

static void test_ip_string() {
    auto pkt = NWPCodec::make_hello(1, test_mac, 0xC0A80101, "h");
    ASSERT(pkt.ip_string() == "192.168.1.1");
}

static void test_mac_string() {
    auto pkt = NWPCodec::make_hello(1, test_mac, 0, "h");
    ASSERT(pkt.mac_string() == "aa:bb:cc:dd:ee:ff");
}

static void test_opcode_name() {
    auto pkt = NWPCodec::make_hello(1, test_mac, 0, "h");
    ASSERT(pkt.opcode_name() == "HELLO");
    pkt.header.opcode = (uint8_t)NWPOpcode::STATUS;
    ASSERT(pkt.opcode_name() == "STATUS");
}

static void test_alert_flag_set() {
    auto pkt = NWPCodec::make_status(1, test_mac, 0xC0A80101, 5, 100, 200, true);
    auto buf = NWPCodec::encode(pkt);
    NWPPacket out;
    ASSERT(NWPCodec::decode(buf.data(), buf.size(), out));
    ASSERT_EQ(out.header.alert_flag, (uint8_t)0x01);
}

static void test_large_payload() {
    std::string big(500, 'X');
    auto pkt = NWPCodec::make_hello(1, test_mac, 0xC0A80101, big);
    auto buf = NWPCodec::encode(pkt);
    NWPPacket out;
    ASSERT(NWPCodec::decode(buf.data(), buf.size(), out));
    std::string s(out.payload.begin(), out.payload.end());
    ASSERT(s == big);
}

// ---------------------------------------------------------
// Anomaly Engine Tests
// ---------------------------------------------------------
static void test_no_alert_normal_traffic() {
    int alert_count = 0;
    AnomalyEngine engine([&](const AlertEvent&) { alert_count++; });
    engine.register_device(1);

    // Feed normal traffic — should not trigger
    for (int i = 0; i < 25; i++)
        engine.process(1, "192.168.1.1", 50000, 50000, 10, false);

    ASSERT_EQ(alert_count, 0);
}

static void test_bw_spike_triggers_alert() {
    int alert_count = 0;
    AnomalyEngine engine([&](const AlertEvent& ev) {
        if (ev.rule == "bw_spike") alert_count++;
    });
    engine.register_device(2);

    // Warmup with varied traffic so stddev > 0
    for (int i = 0; i < 15; i++)
        engine.process(2, "192.168.1.2",
                       static_cast<uint32_t>(8000 + i * 1000),
                       static_cast<uint32_t>(7000 + i * 500), 5, false);

    // Spike — 500x the baseline to guarantee z-score >> 3.5
    engine.process(2, "192.168.1.2", 500'000'000, 500'000'000, 5, false);

    ASSERT(alert_count > 0);
}

static void test_absolute_bw_threshold() {
    int alert_count = 0;
    AnomalyEngine engine([&](const AlertEvent& ev) {
        if (ev.rule == "bw_absolute") alert_count++;
    });
    engine.register_device(3);

    for (int i = 0; i < 12; i++)
        engine.process(3, "192.168.1.3", 100, 100, 1, false);

    // Absolute threshold: total > 50MB
    engine.process(3, "192.168.1.3", 30'000'000, 30'000'000, 1, false);
    ASSERT(alert_count > 0);
}

static void test_conn_storm_triggers_alert() {
    int alert_count = 0;
    AnomalyEngine engine([&](const AlertEvent& ev) {
        if (ev.rule == "conn_storm") alert_count++;
    });
    engine.register_device(4);

    // Varied warmup so stddev > 0
    for (int i = 0; i < 15; i++)
        engine.process(4, "192.168.1.4", 10000, 10000,
                       static_cast<uint16_t>(3 + (i % 5)), false);

    // Spike connections massively — guarantees z >> 3.0
    engine.process(4, "192.168.1.4", 10000, 10000, 9999, false);
    ASSERT(alert_count > 0);
}

static void test_agent_alert_forwarded() {
    int alert_count = 0;
    AnomalyEngine engine([&](const AlertEvent& ev) {
        if (ev.rule == "agent_alert") alert_count++;
    });
    engine.register_device(5);

    for (int i = 0; i < 5; i++)
        engine.process(5, "192.168.1.5", 100, 100, 2, false);

    engine.process(5, "192.168.1.5", 100, 100, 2, true); // agent says alert
    ASSERT(alert_count > 0);
}

static void test_cooldown_deduplication() {
    int alert_count = 0;
    AnomalyEngine engine([&](const AlertEvent&) { alert_count++; });
    engine.register_device(6);

    for (int i = 0; i < 12; i++)
        engine.process(6, "192.168.1.6", 100, 100, 1, false);

    // Two spikes in rapid succession — cooldown should prevent second fire
    engine.process(6, "192.168.1.6", 100'000'000, 100'000'000, 1, false);
    int after_first = alert_count;
    engine.process(6, "192.168.1.6", 100'000'000, 100'000'000, 1, false);
    // Count should not double for the same rule due to cooldown
    ASSERT(alert_count <= after_first + 1);
}

// ---------------------------------------------------------
// Device Registry Tests
// ---------------------------------------------------------
static void test_registry_insert_and_get() {
    DeviceRegistry reg;
    auto pkt = NWPCodec::make_hello(10, test_mac, 0xC0A80101, "myhost");
    reg.update(pkt, "myhost");

    DeviceRecord rec;
    ASSERT(reg.get(10, rec));
    ASSERT(rec.ip == "192.168.1.1");
    ASSERT(rec.hostname == "myhost");
    ASSERT(rec.status == DeviceStatus::ONLINE);
}

static void test_registry_goodbye_marks_offline() {
    DeviceRegistry reg;
    auto hello = NWPCodec::make_hello(11, test_mac, 0xC0A80102, "node");
    reg.update(hello);

    auto bye = NWPCodec::make_goodbye(11, test_mac, 0xC0A80102);
    reg.update(bye);

    DeviceRecord rec;
    ASSERT(reg.get(11, rec));
    ASSERT(rec.status == DeviceStatus::OFFLINE);
}

static void test_registry_online_count() {
    DeviceRegistry reg;
    uint8_t m[6] = {};
    for (int i = 0; i < 5; i++) {
        auto p = NWPCodec::make_hello(100 + i, m, 0xC0A80100 + i, "n");
        reg.update(p);
    }
    ASSERT_EQ(reg.online_count(), 5);
}

static void test_registry_snapshot_json_valid() {
    DeviceRegistry reg;
    auto pkt = NWPCodec::make_hello(20, test_mac, 0xC0A80101, "snap");
    reg.update(pkt, "snap");

    std::string json = reg.snapshot_json();
    ASSERT(json.front() == '[');
    ASSERT(json.back()  == ']');
    ASSERT(json.find("192.168.1.1") != std::string::npos);
}

// ---------------------------------------------------------
// Main
// ---------------------------------------------------------
int main() {
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    std::cout << "\n=== NetWatch C++ Test Suite ===\n\n";

    std::cout << "-- NWP Protocol --\n";
    run_test("header size is 32 bytes",         test_header_size);
    run_test("HELLO encode/decode roundtrip",   test_hello_roundtrip);
    run_test("STATUS encode/decode roundtrip",  test_status_roundtrip);
    run_test("ALERT encode/decode roundtrip",   test_alert_roundtrip);
    run_test("GOODBYE encode/decode roundtrip", test_goodbye_roundtrip);
    run_test("ACK encode/decode roundtrip",     test_ack_roundtrip);
    run_test("checksum corruption detected",    test_checksum_corruption);
    run_test("truncated packet rejected",       test_truncated_packet);
    run_test("bad magic rejected",              test_bad_magic);
    run_test("ip_string formatting",            test_ip_string);
    run_test("mac_string formatting",           test_mac_string);
    run_test("opcode_name() returns correct",   test_opcode_name);
    run_test("alert_flag set in STATUS",        test_alert_flag_set);
    run_test("large payload roundtrip",         test_large_payload);

    std::cout << "\n-- Anomaly Engine --\n";
    run_test("no alert on normal traffic",       test_no_alert_normal_traffic);
    run_test("bandwidth spike triggers alert",   test_bw_spike_triggers_alert);
    run_test("absolute BW threshold fires",      test_absolute_bw_threshold);
    run_test("connection storm triggers alert",  test_conn_storm_triggers_alert);
    run_test("agent self-alert forwarded",       test_agent_alert_forwarded);
    run_test("cooldown deduplicates alerts",     test_cooldown_deduplication);

    std::cout << "\n-- Device Registry --\n";
    run_test("insert device and get by ID",     test_registry_insert_and_get);
    run_test("GOODBYE marks device offline",    test_registry_goodbye_marks_offline);
    run_test("online_count returns correct",    test_registry_online_count);
    run_test("snapshot JSON is valid array",    test_registry_snapshot_json_valid);

    std::cout << "\n==============================\n";
    std::cout << "  PASSED: " << g_pass << "\n";
    std::cout << "  FAILED: " << g_fail << "\n";
    std::cout << "==============================\n\n";

#ifdef _WIN32
    WSACleanup();
#endif
    return g_fail > 0 ? 1 : 0;
}
