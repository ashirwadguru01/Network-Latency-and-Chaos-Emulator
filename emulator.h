#ifndef EMULATOR_H
#define EMULATOR_H

#include <string>

// ============================================================
//  Data structures
// ============================================================

// Holds a single network measurement snapshot
struct NetworkMetrics {
    double latency;      // Average round-trip time in ms  (-1 = N/A)
    double packetLoss;   // Packet loss percentage 0-100   (-1 = N/A)
    double jitter;       // Jitter in ms (mdev)            (-1 = N/A)
    int    packetsSent;  // Number of ping packets sent
};

// Information about the detected network connection
struct NetworkInfo {
    std::string interface;  // e.g. "Wi-Fi" or "eth0"
    std::string gateway;    // e.g. "192.168.1.1"
    std::string localIP;    // e.g. "192.168.1.100"
    std::string ssid;       // WiFi SSID e.g. "MyHomeWiFi"
    bool        detected;   // true if detection succeeded
};

// Full experiment result for comparison & saving
struct ExperimentResult {
    std::string timestamp;       // date/time string

    NetworkInfo  network;        // detected interface + gateway + IP + SSID
    std::string  target;         // actual ping target used

    NetworkMetrics baseline;     // before chaos
    NetworkMetrics afterChaos;   // after chaos

    double chaosDelay;           // delay injected (ms)
    double chaosLoss;            // loss injected (%)

    bool chaosApplied;           // true if chaos was applied
    bool networkRestored;        // true if chaos was removed
};

// ============================================================
//  Predefined constants (NOT user-entered)
// ============================================================
constexpr double PREDEFINED_DELAY_MS  = 100.0;
constexpr double PREDEFINED_LOSS_PCT  = 10.0;
constexpr int    PING_COUNT           = 10;
const std::string FALLBACK_TARGET     = "8.8.8.8";

// ============================================================
//  Core functions
// ============================================================

// Detect the active network interface, gateway, local IP, and WiFi SSID.
// In WSL, calls Windows commands to get the REAL WiFi info.
NetworkInfo detectNetwork();

// Choose a ping target: prefer gateway, fallback to 8.8.8.8.
std::string choosePingTarget(const NetworkInfo& net);

// Measure current network metrics by pinging the target.
NetworkMetrics measureNetwork(const std::string& target);

// Apply predefined tc netem chaos on the given interface.
bool applyChaos(const std::string& iface);

// Remove any tc netem qdisc from the given interface.
bool removeChaos(const std::string& iface);

// --- Menu actions ---
void runFullExperiment();
void checkCurrentNetwork();
void restoreNetwork();
void viewExperimentHistory();

// Save an ExperimentResult to experiment_history.txt.
void saveExperimentResult(const ExperimentResult& result);

// Display the main menu and return the user's choice (1-5).
int showMenu();

#endif // EMULATOR_H
