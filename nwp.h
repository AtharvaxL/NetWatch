#pragma once
/*
 * NWP — NetWatch Protocol
 * Custom binary protocol designed for NetWatch.
 * Each monitored node runs an NWP agent that actively sends
 * structured status packets to the central collector.
 *
 * Packet Layout (32 bytes fixed header):
 *  0-1   Magic        0x4E57 ("NW")
 *  2     Version      0x01
 *  3     Opcode       0x01-0x05
 *  4-5   Payload Len  uint16_t
 *  6-9   Device ID    uint32_t
 * 10-15  MAC Address  6 bytes
 * 16-19  IP Address   uint32_t
 * 20-21  Connections  uint16_t
 * 22-25  Bytes Sent   uint32_t
 * 26-29  Bytes Recv   uint32_t
 * 30     Alert Flag   0x00/0x01
 * 31     Checksum     XOR of bytes 0-30
 * 32+    Payload      variable
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <arpa/inet.h>

// ---------------------------------------------------------
// Constants
// ---------------------------------------------------------
constexpr uint16_t NWP_MAGIC   = 0x4E57;  // "NW"
constexpr uint8_t  NWP_VERSION = 0x01;
constexpr size_t   NWP_HEADER_SIZE = 32;

// Checksum algorithm: XOR of all bytes 0..30 stored in byte 31.
// Detects single-bit corruption; simple and fast for embedded nodes.

// Opcodes
enum class NWPOpcode : uint8_t {
    HELLO     = 0x01,  // Agent first contact
    STATUS    = 0x02,  // Periodic heartbeat
    ALERT     = 0x03,  // Anomaly detected
    GOODBYE   = 0x04,  // Graceful disconnect
    ACK       = 0x05   // Collector acknowledgement
};

// Alert severity levels
enum class AlertSeverity : uint8_t {
    LOW    = 0x01,
    MEDIUM = 0x02,
    HIGH   = 0x03
};

// ---------------------------------------------------------
// NWP Packet structure
// ---------------------------------------------------------
#pragma pack(push, 1)
struct NWPHeader {
    uint16_t magic;           // 0-1
    uint8_t  version;         // 2
    uint8_t  opcode;          // 3
    uint16_t payload_len;     // 4-5
    uint32_t device_id;       // 6-9
    uint8_t  mac[6];          // 10-15
    uint32_t ip_addr;         // 16-19
    uint16_t active_conns;    // 20-21
    uint32_t bytes_sent;      // 22-25
    uint32_t bytes_recv;      // 26-29
    uint8_t  alert_flag;      // 30
    uint8_t  checksum;        // 31
};
#pragma pack(pop)

static_assert(sizeof(NWPHeader) == NWP_HEADER_SIZE, "NWPHeader must be 32 bytes");

// Full packet with payload
struct NWPPacket {
    NWPHeader header;
    std::vector<uint8_t> payload;

    std::string opcode_name() const {
        switch (static_cast<NWPOpcode>(header.opcode)) {
            case NWPOpcode::HELLO:   return "HELLO";
            case NWPOpcode::STATUS:  return "STATUS";
            case NWPOpcode::ALERT:   return "ALERT";
            case NWPOpcode::GOODBYE: return "GOODBYE";
            case NWPOpcode::ACK:     return "ACK";
            default:                 return "UNKNOWN";
        }
    }

    std::string ip_string() const {
        uint32_t ip = header.ip_addr;
        return std::to_string((ip >> 24) & 0xFF) + "." +
               std::to_string((ip >> 16) & 0xFF) + "." +
               std::to_string((ip >> 8)  & 0xFF) + "." +
               std::to_string( ip        & 0xFF);
    }

    std::string mac_string() const {
        std::ostringstream oss;
        for (int i = 0; i < 6; i++) {
            if (i) oss << ":";
            oss << std::hex << std::setw(2) << std::setfill('0')
                << (int)header.mac[i];
        }
        return oss.str();
    }
};

// ---------------------------------------------------------
// NWP Codec — build and parse packets
// ---------------------------------------------------------
class NWPCodec {
public:
    // Compute XOR checksum of bytes 0..30
    static uint8_t compute_checksum(const uint8_t* data, size_t len) {
        uint8_t cs = 0;
        for (size_t i = 0; i < len; i++) cs ^= data[i];
        return cs;
    }

    // Serialize a packet to a raw byte buffer
    static std::vector<uint8_t> encode(NWPPacket& pkt) {
        // Convert header fields to network byte order (big-endian)
        NWPHeader h = pkt.header;
        h.magic        = htons(h.magic);
        h.payload_len  = htons(static_cast<uint16_t>(pkt.payload.size()));
        h.device_id    = htonl(h.device_id);
        h.ip_addr      = htonl(h.ip_addr);
        h.active_conns = htons(h.active_conns);
        h.bytes_sent   = htonl(h.bytes_sent);
        h.bytes_recv   = htonl(h.bytes_recv);

        std::vector<uint8_t> buf(NWP_HEADER_SIZE + pkt.payload.size());
        std::memcpy(buf.data(), &h, NWP_HEADER_SIZE);
        if (!pkt.payload.empty())
            std::memcpy(buf.data() + NWP_HEADER_SIZE, pkt.payload.data(), pkt.payload.size());

        // Checksum over first 31 bytes (excluding checksum byte itself)
        buf[31] = compute_checksum(buf.data(), 31);
        return buf;
    }

    // Parse raw bytes into NWPPacket. Returns false if invalid.
    static bool decode(const uint8_t* data, size_t len, NWPPacket& out) {
        if (len < NWP_HEADER_SIZE) return false;

        // Verify checksum
        uint8_t expected_cs = compute_checksum(data, 31);
        if (data[31] != expected_cs) return false;

        std::memcpy(&out.header, data, NWP_HEADER_SIZE);

        // Convert from network byte order
        out.header.magic        = ntohs(out.header.magic);
        out.header.payload_len  = ntohs(out.header.payload_len);
        out.header.device_id    = ntohl(out.header.device_id);
        out.header.ip_addr      = ntohl(out.header.ip_addr);
        out.header.active_conns = ntohs(out.header.active_conns);
        out.header.bytes_sent   = ntohl(out.header.bytes_sent);
        out.header.bytes_recv   = ntohl(out.header.bytes_recv);

        if (out.header.magic != NWP_MAGIC) return false;
        if (out.header.version != NWP_VERSION) return false;

        size_t payload_len = out.header.payload_len;
        if (len < NWP_HEADER_SIZE + payload_len) return false;

        out.payload.assign(data + NWP_HEADER_SIZE,
                           data + NWP_HEADER_SIZE + payload_len);
        return true;
    }

    // Helper: build a HELLO packet
    static NWPPacket make_hello(uint32_t device_id, const uint8_t mac[6],
                                 uint32_t ip, const std::string& hostname) {
        NWPPacket pkt{};
        pkt.header.magic      = NWP_MAGIC;
        pkt.header.version    = NWP_VERSION;
        pkt.header.opcode     = static_cast<uint8_t>(NWPOpcode::HELLO);
        pkt.header.device_id  = device_id;
        pkt.header.ip_addr    = ip;
        std::memcpy(pkt.header.mac, mac, 6);
        pkt.payload.assign(hostname.begin(), hostname.end());
        return pkt;
    }

    // Helper: build a STATUS packet
    static NWPPacket make_status(uint32_t device_id, const uint8_t mac[6],
                                  uint32_t ip, uint16_t conns,
                                  uint32_t sent, uint32_t recv,
                                  bool alert = false) {
        NWPPacket pkt{};
        pkt.header.magic        = NWP_MAGIC;
        pkt.header.version      = NWP_VERSION;
        pkt.header.opcode       = static_cast<uint8_t>(NWPOpcode::STATUS);
        pkt.header.device_id    = device_id;
        pkt.header.ip_addr      = ip;
        pkt.header.active_conns = conns;
        pkt.header.bytes_sent   = sent;
        pkt.header.bytes_recv   = recv;
        pkt.header.alert_flag   = alert ? 0x01 : 0x00;
        std::memcpy(pkt.header.mac, mac, 6);
        return pkt;
    }

    // Helper: build an ALERT packet
    static NWPPacket make_alert(uint32_t device_id, const uint8_t mac[6],
                                 uint32_t ip, AlertSeverity sev,
                                 const std::string& reason) {
        NWPPacket pkt{};
        pkt.header.magic      = NWP_MAGIC;
        pkt.header.version    = NWP_VERSION;
        pkt.header.opcode     = static_cast<uint8_t>(NWPOpcode::ALERT);
        pkt.header.device_id  = device_id;
        pkt.header.ip_addr    = ip;
        pkt.header.alert_flag = 0x01;
        std::memcpy(pkt.header.mac, mac, 6);
        // Payload: [severity byte] + reason string
        pkt.payload.push_back(static_cast<uint8_t>(sev));
        pkt.payload.insert(pkt.payload.end(), reason.begin(), reason.end());
        return pkt;
    }

    // Helper: build a GOODBYE packet
    static NWPPacket make_goodbye(uint32_t device_id, const uint8_t mac[6],
                                   uint32_t ip) {
        NWPPacket pkt{};
        pkt.header.magic     = NWP_MAGIC;
        pkt.header.version   = NWP_VERSION;
        pkt.header.opcode    = static_cast<uint8_t>(NWPOpcode::GOODBYE);
        pkt.header.device_id = device_id;
        pkt.header.ip_addr   = ip;
        std::memcpy(pkt.header.mac, mac, 6);
        return pkt;
    }

    // Helper: build an ACK packet
    static NWPPacket make_ack(uint32_t device_id) {
        NWPPacket pkt{};
        pkt.header.magic     = NWP_MAGIC;
        pkt.header.version   = NWP_VERSION;
        pkt.header.opcode    = static_cast<uint8_t>(NWPOpcode::ACK);
        pkt.header.device_id = device_id;
        return pkt;
    }
};

// Windows-compatible htons/htonl stubs for cross-compilation testing
// On real Windows these come from <winsock2.h>
#ifndef _WIN32
#include <arpa/inet.h>
#endif
