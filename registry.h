#pragma once
/*
 * Device Registry
 * Maintains the live state of every device that has sent
 * an NWP packet. Used by the collector and dashboard server.
 */

#include "nwp.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
struct sys_mutex {
    CRITICAL_SECTION cs;
    sys_mutex() { InitializeCriticalSection(&cs); }
    ~sys_mutex() { DeleteCriticalSection(&cs); }
    void lock() { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }
};
#else
#include <mutex>
using sys_mutex = std::mutex;
#endif

template<typename M>
struct sys_lock_guard {
    M& m;
    sys_lock_guard(M& mutex) : m(mutex) { m.lock(); }
    ~sys_lock_guard() { m.unlock(); }
};


using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

enum class DeviceStatus { ONLINE, OFFLINE };

struct DeviceRecord {
    uint32_t    device_id;
    std::string ip;
    std::string mac;
    std::string hostname;
    uint16_t    active_conns  = 0;
    uint32_t    bytes_sent    = 0;
    uint32_t    bytes_recv    = 0;
    bool        alert_flag    = false;
    DeviceStatus status       = DeviceStatus::ONLINE;
    TimePoint   last_seen;
    TimePoint   first_seen;
    int         packet_count  = 0;
    uint64_t    total_bytes   = 0;

    // Serialise to JSON (no external library needed)
    std::string to_json() const {
        auto ms_since_epoch = [](TimePoint tp) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                tp.time_since_epoch()).count();
        };
        std::ostringstream j;
        j << "{"
          << "\"device_id\":" << device_id << ","
          << "\"ip\":\"" << ip << "\","
          << "\"mac\":\"" << mac << "\","
          << "\"hostname\":\"" << hostname << "\","
          << "\"active_conns\":" << active_conns << ","
          << "\"bytes_sent\":" << bytes_sent << ","
          << "\"bytes_recv\":" << bytes_recv << ","
          << "\"alert_flag\":" << (alert_flag ? "true" : "false") << ","
          << "\"status\":\"" << (status == DeviceStatus::ONLINE ? "online" : "offline") << "\","
          << "\"last_seen\":" << ms_since_epoch(last_seen) << ","
          << "\"packet_count\":" << packet_count << ","
          << "\"total_bytes\":" << total_bytes
          << "}";
        return j.str();
    }
};

class DeviceRegistry {
public:
    // Update or insert a device from a parsed NWP packet
    void update(const NWPPacket& pkt, const std::string& hostname = "") {
        sys_lock_guard<sys_mutex> lock(mutex_);
        uint32_t id = pkt.header.device_id;
        bool is_new = (records_.find(id) == records_.end());

        auto& rec = records_[id];
        if (is_new) {
            rec.device_id  = id;
            rec.first_seen = Clock::now();
        }

        rec.ip           = pkt.ip_string();
        rec.mac          = pkt.mac_string();
        rec.active_conns = pkt.header.active_conns;
        rec.bytes_sent   = pkt.header.bytes_sent;
        rec.bytes_recv   = pkt.header.bytes_recv;
        rec.alert_flag   = pkt.header.alert_flag != 0;
        rec.status       = DeviceStatus::ONLINE;
        rec.last_seen    = Clock::now();
        rec.packet_count++;
        rec.total_bytes += pkt.header.bytes_sent + pkt.header.bytes_recv;

        if (!hostname.empty()) rec.hostname = hostname;

        auto opcode = static_cast<NWPOpcode>(pkt.header.opcode);
        if (opcode == NWPOpcode::GOODBYE)
            rec.status = DeviceStatus::OFFLINE;
    }

    // Mark devices offline if not seen for timeout_secs
    void expire_stale(int timeout_secs = 30) {
        sys_lock_guard<sys_mutex> lock(mutex_);
        auto now = Clock::now();
        for (auto& kv : records_) {
            auto& rec = kv.second;
            if (rec.status == DeviceStatus::ONLINE) {
                auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    now - rec.last_seen).count();
                if (secs > timeout_secs)
                    rec.status = DeviceStatus::OFFLINE;
            }
        }
    }

    // Get snapshot of all devices as JSON array
    std::string snapshot_json() const {
        sys_lock_guard<sys_mutex> lock(mutex_);
        std::ostringstream j;
        j << "[";
        bool first = true;
        for (auto& kv : records_) {
            auto& rec = kv.second;
            if (!first) j << ",";
            j << rec.to_json();
            first = false;
        }
        j << "]";
        return j.str();
    }

    // Count online devices
    int online_count() const {
        sys_lock_guard<sys_mutex> lock(mutex_);
        int n = 0;
        for (auto& kv : records_) {
            auto& rec = kv.second;
            if (rec.status == DeviceStatus::ONLINE) n++;
        }
        return n;
    }

    size_t total_count() const {
        sys_lock_guard<sys_mutex> lock(mutex_);
        return records_.size();
    }

    // Get copy of a record by ID
    bool get(uint32_t id, DeviceRecord& out) const {
        sys_lock_guard<sys_mutex> lock(mutex_);
        auto it = records_.find(id);
        if (it == records_.end()) return false;
        out = it->second;
        return true;
    }

private:
    mutable sys_mutex mutex_;
    std::unordered_map<uint32_t, DeviceRecord> records_;
};
