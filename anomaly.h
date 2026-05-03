#pragma once
/*
 * Anomaly Detection Engine
 * Rule-based detection using sliding window statistics.
 * Detects: packet floods, bandwidth spikes, connection storms,
 *          rapid device churn, and high alert frequency.
 */

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <functional>

using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

// ---------------------------------------------------------
// Detection thresholds (tune these per environment)
// ---------------------------------------------------------
constexpr double BW_SPIKE_ZSCORE_THRESHOLD   = 3.5;  // z-score for bandwidth spike
constexpr double CONN_STORM_ZSCORE_THRESHOLD = 3.0;  // z-score for connection storm
constexpr double BW_ABSOLUTE_THRESHOLD_MB    = 10.0; // MB per interval — absolute cap
constexpr int    CONN_ABSOLUTE_THRESHOLD     = 200;  // max simultaneous connections
constexpr int    ANOMALY_COOLDOWN_SECS       = 10;   // min seconds between same-rule alerts

// ---------------------------------------------------------
// Alert event
// ---------------------------------------------------------
struct AlertEvent {
    uint32_t    device_id;
    std::string ip;
    std::string rule;        // Which rule triggered
    std::string detail;      // Human-readable explanation
    double      score;       // Anomaly score (z-score or rate)
    TimePoint   timestamp;
    uint8_t     severity;    // 1=LOW 2=MEDIUM 3=HIGH
};

// ---------------------------------------------------------
// Per-device sliding window stats
// ---------------------------------------------------------
struct DeviceStats {
    std::deque<double> bw_window;      // bytes per interval
    std::deque<double> conn_window;    // connection count per interval
    std::deque<double> pkt_window;     // packet rate per interval

    double bw_sum   = 0.0;
    double conn_sum = 0.0;
    double pkt_sum  = 0.0;

    static constexpr size_t WINDOW_SIZE = 20;

    void push_bw(double val) {
        bw_window.push_back(val);
        bw_sum += val;
        if (bw_window.size() > WINDOW_SIZE) {
            bw_sum -= bw_window.front();
            bw_window.pop_front();
        }
    }

    void push_conn(double val) {
        conn_window.push_back(val);
        conn_sum += val;
        if (conn_window.size() > WINDOW_SIZE) {
            conn_sum -= conn_window.front();
            conn_window.pop_front();
        }
    }

    void push_pkt(double val) {
        pkt_window.push_back(val);
        pkt_sum += val;
        if (pkt_window.size() > WINDOW_SIZE) {
            pkt_sum -= pkt_window.front();
            pkt_window.pop_front();
        }
    }

    // Mean and stddev helpers
    double mean(const std::deque<double>& w, double sum) const {
        if (w.empty()) return 0.0;
        return sum / w.size();
    }

    double stddev(const std::deque<double>& w, double sum) const {
        if (w.size() < 2) return 0.0;
        double m = sum / w.size();
        double var = 0.0;
        for (double v : w) var += (v - m) * (v - m);
        return std::sqrt(var / w.size());
    }

    double zscore(double val, const std::deque<double>& w, double sum) const {
        double sd = stddev(w, sum);
        if (sd < 1e-9) return 0.0;
        return std::abs(val - mean(w, sum)) / sd;
    }
};

// ---------------------------------------------------------
// Anomaly Engine
// ---------------------------------------------------------
class AnomalyEngine {
public:
    using AlertCallback = std::function<void(const AlertEvent&)>;

    explicit AnomalyEngine(AlertCallback cb) : on_alert_(std::move(cb)) {}

    // Call this for every STATUS/ALERT packet received
    void process(uint32_t device_id, const std::string& ip,
                 uint32_t bytes_sent, uint32_t bytes_recv,
                 uint16_t active_conns, bool agent_alert) {

        auto& stats = device_stats_[device_id];
        double total_bw = static_cast<double>(bytes_sent + bytes_recv);

        // Warmup: need enough samples to establish baseline before firing alerts
        bool warm = stats.bw_window.size() >= DeviceStats::WINDOW_SIZE / 2;

        if (warm) {
            // Rule 1 — Bandwidth spike (z-score > 3.5)
            // Z-score: (value - mean) / stddev
            // A z-score > 3 means the value is 3 standard deviations above average —
            // statistically, this occurs by chance only ~0.13% of the time.
            double bw_z = stats.zscore(total_bw, stats.bw_window, stats.bw_sum);
            if (bw_z > 3.5 && !in_cooldown(device_id, "bw_spike")) {
                fire(device_id, ip, "bw_spike",
                     "Bandwidth spike: z-score=" + fmt(bw_z),
                     bw_z, severity_from_z(bw_z));
                set_cooldown(device_id, "bw_spike");
            }

            // Rule 2 — Connection storm (z-score > 3.0)
            double conn_z = stats.zscore(active_conns, stats.conn_window, stats.conn_sum);
            if (conn_z > 3.0 && !in_cooldown(device_id, "conn_storm")) {
                fire(device_id, ip, "conn_storm",
                     "Connection storm: " + std::to_string(active_conns) + " conns, z=" + fmt(conn_z),
                     conn_z, severity_from_z(conn_z));
                set_cooldown(device_id, "conn_storm");
            }

            // Rule 3 — Absolute bandwidth threshold (> 10MB per 5-second interval)
            if (total_bw > 10'000'000 && !in_cooldown(device_id, "bw_absolute")) {
                fire(device_id, ip, "bw_absolute",
                     "Extreme bandwidth: " + fmt(total_bw / 1e6) + " MB/interval",
                     total_bw / 1e6, 3);
                set_cooldown(device_id, "bw_absolute");
            }

            // Rule 4 — Connection count absolute threshold (> 200)
            if (active_conns > 200 && !in_cooldown(device_id, "conn_absolute")) {
                fire(device_id, ip, "conn_absolute",
                     "Extreme connection count: " + std::to_string(active_conns),
                     active_conns, 3);
                set_cooldown(device_id, "conn_absolute");
            }
        }

        // Rule 5 — Agent self-reported alert (always fire, no cooldown needed)
        if (agent_alert && !in_cooldown(device_id, "agent_alert")) {
            fire(device_id, ip, "agent_alert",
                 "Agent self-reported anomaly", 0.0, 2);
            set_cooldown(device_id, "agent_alert");
        }

        // Push new sample after checking (so it doesn't inflate its own z-score)
        stats.push_bw(total_bw);
        stats.push_conn(static_cast<double>(active_conns));
    }

    // Called when a new device joins — register it
    void register_device(uint32_t device_id) {
        device_stats_.emplace(device_id, DeviceStats{});
    }

    // Get all fired alerts
    const std::vector<AlertEvent>& alerts() const { return alerts_; }

    void clear_alerts() { alerts_.clear(); }

private:
    AlertCallback                          on_alert_;
    std::unordered_map<uint32_t, DeviceStats> device_stats_;
    std::vector<AlertEvent>                alerts_;

    // Cooldown map: device_id -> rule -> expiry time
    std::unordered_map<uint32_t,
        std::unordered_map<std::string, TimePoint>> cooldowns_;

    static constexpr int COOLDOWN_SECONDS = 10;

    bool in_cooldown(uint32_t id, const std::string& rule) {
        auto dit = cooldowns_.find(id);
        if (dit == cooldowns_.end()) return false;
        auto rit = dit->second.find(rule);
        if (rit == dit->second.end()) return false;
        return Clock::now() < rit->second;
    }

    void set_cooldown(uint32_t id, const std::string& rule) {
        cooldowns_[id][rule] = Clock::now() +
                               std::chrono::seconds(COOLDOWN_SECONDS);
    }

    void fire(uint32_t id, const std::string& ip,
              const std::string& rule, const std::string& detail,
              double score, uint8_t severity) {
        AlertEvent ev;
        ev.device_id = id;
        ev.ip        = ip;
        ev.rule      = rule;
        ev.detail    = detail;
        ev.score     = score;
        ev.severity  = severity;
        ev.timestamp = Clock::now();
        alerts_.push_back(ev);
        on_alert_(ev);
    }

    uint8_t severity_from_z(double z) const {
        if (z > 5.0) return 3;
        if (z > 3.5) return 2;
        return 1;
    }

    static std::string fmt(double v) {
        std::ostringstream oss;
        oss << std::fixed;
        oss.precision(2);
        oss << v;
        return oss.str();
    }
};
