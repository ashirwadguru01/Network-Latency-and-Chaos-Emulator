#include "emulator.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <array>
#include <vector>
#include <cmath>
#include <regex>
#include <thread>
#include <chrono>
#include <algorithm>
#include <iomanip>

// ============================================================
//  Helper: run a shell command and capture its stdout
// ============================================================
static std::string execCommand(const std::string& cmd) {
    std::string output;
    std::array<char, 256> buf;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        output += buf.data();
    }
    pclose(pipe);
    return output;
}

// ============================================================
//  Helper: run a shell command and return its exit code
// ============================================================
static int execCommandStatus(const std::string& cmd) {
    int ret = system(cmd.c_str());
    return WEXITSTATUS(ret);
}

// ============================================================
//  Helper: get current date/time as a readable string
// ============================================================
static std::string currentTimestamp() {
    std::time_t now = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S",
                  std::localtime(&now));
    return std::string(buf);
}

// ============================================================
//  Helper: trim whitespace from a string
// ============================================================
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

// ============================================================
//  Helper: detect if running inside WSL
// ============================================================
static bool isWSL() {
    std::ifstream f("/proc/version");
    if (!f.is_open()) return false;
    std::string line;
    std::getline(f, line);
    f.close();
    // WSL kernels contain "microsoft" or "Microsoft" in /proc/version
    return (line.find("microsoft") != std::string::npos ||
            line.find("Microsoft") != std::string::npos);
}

// ============================================================
//  Detect the REAL Windows WiFi network (from WSL or native)
//
//  Key fix: WSL has its own virtual adapter with a fixed
//  gateway (172.x.x.x). To see the actual WiFi, we call
//  cmd.exe /c ipconfig  and  cmd.exe /c netsh  from WSL.
// ============================================================
static NetworkInfo detectWindowsNetwork() {
    NetworkInfo info;
    info.detected = false;

    // --- Detect WiFi SSID via netsh ---
    std::string wlanCmd = "/mnt/c/Windows/System32/cmd.exe /c \"netsh wlan show interfaces\" 2>/dev/null";
    std::string wlanOutput = execCommand(wlanCmd);

    if (!wlanOutput.empty()) {
        std::istringstream wstream(wlanOutput);
        std::string line;
        while (std::getline(wstream, line)) {
            // "    SSID                   : MyHomeWiFi"
            if (line.find("SSID") != std::string::npos &&
                line.find("BSSID") == std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string val = trim(line.substr(pos + 1));
                    if (!val.empty()) {
                        info.ssid = val;
                    }
                }
            }
        }
    }

    // --- Detect Interface, Gateway, and Local IP via ipconfig ---
    std::string ipconfigCmd = "/mnt/c/Windows/System32/cmd.exe /c ipconfig 2>/dev/null";
    std::string ipconfigOutput = execCommand(ipconfigCmd);

    if (ipconfigOutput.empty()) {
        std::cerr << "[ERROR] Could not run 'ipconfig'.\n";
        return info;
    }

    std::istringstream istream(ipconfigOutput);
    std::string line;
    std::string currentAdapter;
    std::string currentIP;
    std::string currentGateway;
    bool foundActive = false;

    while (std::getline(istream, line)) {
        // Adapter header: "Wireless LAN adapter Wi-Fi:" or "Ethernet adapter Ethernet:"
        if (line.find("adapter") != std::string::npos && line.find(':') != std::string::npos) {
            // Save previous adapter if it had a gateway
            if (!currentGateway.empty() && !foundActive) {
                info.interface = currentAdapter;
                info.localIP   = currentIP;
                info.gateway   = currentGateway;
                foundActive    = true;
            }
            // Extract adapter name
            size_t adStart = line.find("adapter ");
            size_t adEnd   = line.rfind(':');
            if (adStart != std::string::npos && adEnd != std::string::npos) {
                currentAdapter = trim(line.substr(adStart + 8, adEnd - adStart - 8));
            }
            currentIP.clear();
            currentGateway.clear();
        }

        // "   IPv4 Address. . . . . . . . . . . : 192.168.1.100"
        if (line.find("IPv4") != std::string::npos && line.find(':') != std::string::npos) {
            size_t pos = line.rfind(':');
            if (pos != std::string::npos) {
                currentIP = trim(line.substr(pos + 1));
            }
        }

        // "   Default Gateway . . . . . . . . . : 192.168.1.1"
        if (line.find("Default Gateway") != std::string::npos && line.find(':') != std::string::npos) {
            size_t pos = line.rfind(':');
            if (pos != std::string::npos) {
                std::string gw = trim(line.substr(pos + 1));
                if (!gw.empty()) {
                    currentGateway = gw;
                }
            }
        }
    }

    // Check the last adapter parsed
    if (!currentGateway.empty() && !foundActive) {
        info.interface = currentAdapter;
        info.localIP   = currentIP;
        info.gateway   = currentGateway;
        foundActive    = true;
    }

    if (foundActive) {
        info.detected = true;
    } else {
        std::cerr << "[ERROR] No active Windows network connection found.\n";
    }

    return info;
}

// ============================================================
//  Detect native Linux network (non-WSL)
// ============================================================
static NetworkInfo detectLinuxNetwork() {
    NetworkInfo info;
    info.detected = false;

    std::string output = execCommand("ip route 2>/dev/null");

    if (output.empty()) {
        std::cerr << "[ERROR] Could not run 'ip route'.\n";
        return info;
    }

    // Parse the default route line
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("default") == std::string::npos) continue;

        std::istringstream ls(line);
        std::string token;
        while (ls >> token) {
            if (token == "via") {
                ls >> info.gateway;
            } else if (token == "dev") {
                ls >> info.interface;
            }
        }
        break;
    }

    if (info.interface.empty()) {
        std::cerr << "[ERROR] No default route found.\n";
        return info;
    }

    info.detected = true;

    // Get local IP
    std::string ipCmd = "ip -4 addr show " + info.interface +
                        " 2>/dev/null | grep -oP 'inet \\K[^/]+'";
    std::string ipOut = trim(execCommand(ipCmd));
    if (!ipOut.empty()) {
        size_t nl = ipOut.find('\n');
        info.localIP = (nl != std::string::npos) ? ipOut.substr(0, nl) : ipOut;
    }

    // Get WiFi SSID (if wireless)
    std::string ssidOut = trim(execCommand("iwgetid -r 2>/dev/null"));
    if (!ssidOut.empty()) {
        info.ssid = ssidOut;
    }

    return info;
}

// ============================================================
//  Public: detectNetwork — picks the right detection method
// ============================================================
NetworkInfo detectNetwork() {
    if (isWSL()) {
        // Running in WSL: use Windows commands to get REAL WiFi info
        std::cout << "[*] WSL detected — reading Windows network info...\n";
        return detectWindowsNetwork();
    } else {
        // Native Linux
        return detectLinuxNetwork();
    }
}

// ============================================================
//  Choose a ping target — prefer gateway, fallback to 8.8.8.8
// ============================================================
std::string choosePingTarget(const NetworkInfo& net) {
    if (!net.gateway.empty()) {
        return net.gateway;
    }
    return FALLBACK_TARGET;
}

// ============================================================
//  Measure network metrics via ping
// ============================================================
NetworkMetrics measureNetwork(const std::string& target) {
    NetworkMetrics m{};
    m.latency    = -1;
    m.packetLoss = -1;
    m.jitter     = -1;
    m.packetsSent = PING_COUNT;

    std::string cmd = "ping -c " + std::to_string(PING_COUNT) +
                      " -W 2 " + target + " 2>&1";
    std::string output = execCommand(cmd);

    if (output.empty()) {
        std::cerr << "[ERROR] Ping command returned no output.\n";
        return m;
    }

    // --- Parse packet loss ---
    {
        std::regex lossRe(R"((\d+)%\s+packet\s+loss)");
        std::smatch match;
        if (std::regex_search(output, match, lossRe)) {
            m.packetLoss = std::stod(match[1].str());
        }
    }

    // --- Parse RTT stats ---
    {
        std::regex rttRe(
            R"(rtt\s+min/avg/max/mdev\s*=\s*)"
            R"(([0-9.]+)/([0-9.]+)/([0-9.]+)/([0-9.]+)\s*ms)");
        std::smatch match;
        if (std::regex_search(output, match, rttRe)) {
            m.latency = std::stod(match[2].str());  // avg
            m.jitter  = std::stod(match[4].str());  // mdev ≈ jitter
        }
    }

    return m;
}

// ============================================================
//  Apply predefined chaos (tc netem)
// ============================================================
bool applyChaos(const std::string& iface) {
    std::cout << "\n========================================\n";
    std::cout << " APPLYING NETWORK CHAOS\n";
    std::cout << "========================================\n";
    std::cout << "Interface    : " << iface << "\n";
    std::cout << "Added Delay  : "
              << static_cast<int>(PREDEFINED_DELAY_MS) << " ms\n";
    std::cout << "Packet Loss  : "
              << static_cast<int>(PREDEFINED_LOSS_PCT) << "%\n";
    std::cout << "========================================\n\n";

    // In WSL, we apply chaos on the WSL eth0 interface (not the Windows adapter name)
    std::string chaosIface = iface;
    if (isWSL()) {
        // Get the WSL network interface for tc netem
        std::string wslIface = trim(execCommand(
            "ip route | grep default | awk '{print $5}' 2>/dev/null"));
        if (!wslIface.empty()) {
            chaosIface = wslIface;
            std::cout << "[*] WSL: applying chaos on WSL interface '"
                      << chaosIface << "'\n";
        }
    }

    std::ostringstream cmd;
    cmd << "sudo tc qdisc replace dev " << chaosIface
        << " root netem delay "
        << static_cast<int>(PREDEFINED_DELAY_MS) << "ms"
        << " loss " << static_cast<int>(PREDEFINED_LOSS_PCT) << "%"
        << " 2>&1";

    std::string output = execCommand(cmd.str());
    int status = execCommandStatus(cmd.str());

    if (status != 0) {
        std::cerr << "[ERROR] Failed to apply tc netem.\n";
        if (output.find("Operation not permitted") != std::string::npos ||
            output.find("permission") != std::string::npos ||
            output.find("not allowed") != std::string::npos) {
            std::cerr << "This operation requires administrator privileges.\n"
                      << "Run the program with: sudo ./network_detector\n";
        } else {
            std::cerr << "tc output: " << output << "\n";
        }
        return false;
    }

    std::cout << "Chaos applied successfully.\n";
    return true;
}

// ============================================================
//  Remove chaos (tc netem)
// ============================================================
bool removeChaos(const std::string& iface) {
    std::string chaosIface = iface;
    if (isWSL()) {
        std::string wslIface = trim(execCommand(
            "ip route | grep default | awk '{print $5}' 2>/dev/null"));
        if (!wslIface.empty()) {
            chaosIface = wslIface;
        }
    }

    std::string cmdStr = "sudo tc qdisc del dev " + chaosIface + " root 2>&1";

    std::string output;
    std::array<char, 256> buf;
    FILE* pipe = popen(cmdStr.c_str(), "r");
    if (!pipe) {
        std::cerr << "[ERROR] Could not run tc command.\n";
        return false;
    }
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        output += buf.data();
    }
    int status = WEXITSTATUS(pclose(pipe));

    // These errors are harmless — they mean no custom qdisc exists
    if (status != 0) {
        if (output.find("No such file") != std::string::npos ||
            output.find("Cannot delete") != std::string::npos ||
            output.find("handle of zero") != std::string::npos) {
            return true;
        }
        std::cerr << "[WARNING] tc qdisc del returned an error.\n";
        std::cerr << "tc output: " << output << "\n";
        return false;
    }
    return true;
}

// ============================================================
//  Helper: format a metric value for the comparison table
// ============================================================
static std::string fmtLatency(double v) {
    if (v < 0) return "N/A";
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << v << " ms";
    return os.str();
}

static std::string fmtLoss(double v) {
    if (v < 0) return "N/A";
    std::ostringstream os;
    os << std::fixed << std::setprecision(0) << v << "%";
    return os.str();
}

static std::string fmtJitter(double v) {
    if (v < 0) return "N/A";
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << v << " ms";
    return os.str();
}

// ============================================================
//  Print network info
// ============================================================
static void printNetworkInfo(const NetworkInfo& net) {
    std::cout << "\n========================================\n";
    std::cout << " CURRENT NETWORK\n";
    std::cout << "========================================\n";
    if (!net.ssid.empty()) {
        std::cout << "WiFi SSID : " << net.ssid << "\n";
    }
    std::cout << "Interface : " << net.interface << "\n";
    std::cout << "Local IP  : "
              << (net.localIP.empty() ? "N/A" : net.localIP) << "\n";
    std::cout << "Gateway   : "
              << (net.gateway.empty() ? "N/A" : net.gateway) << "\n";
    std::cout << "Status    : Connected\n";
    std::cout << "========================================\n";
}

// ============================================================
//  Print comparison table
// ============================================================
static void printComparison(const NetworkMetrics& before,
                            const NetworkMetrics& after) {
    std::cout << "\n========================================\n";
    std::cout << " EXPERIMENT RESULT\n";
    std::cout << "========================================\n\n";

    // Table header
    std::cout << std::left
              << std::setw(16) << ""
              << std::setw(14) << "BEFORE"
              << std::setw(14) << "AFTER" << "\n";
    std::cout << "----------------------------------------\n";

    // Latency row
    std::cout << std::setw(16) << "Latency"
              << std::setw(14) << fmtLatency(before.latency)
              << std::setw(14) << fmtLatency(after.latency) << "\n";

    // Packet Loss row
    std::cout << std::setw(16) << "Packet Loss"
              << std::setw(14) << fmtLoss(before.packetLoss)
              << std::setw(14) << fmtLoss(after.packetLoss) << "\n";

    // Jitter row
    std::cout << std::setw(16) << "Jitter"
              << std::setw(14) << fmtJitter(before.jitter)
              << std::setw(14) << fmtJitter(after.jitter) << "\n";

    std::cout << "----------------------------------------\n\n";

    // Change summary
    if (before.latency >= 0 && after.latency >= 0) {
        double d = after.latency - before.latency;
        std::cout << "Latency Change  : " << (d >= 0 ? "+" : "")
                  << std::fixed << std::setprecision(2) << d << " ms\n";
    } else {
        std::cout << "Latency Change  : N/A\n";
    }

    if (before.packetLoss >= 0 && after.packetLoss >= 0) {
        double d = after.packetLoss - before.packetLoss;
        std::cout << "Loss Change     : " << (d >= 0 ? "+" : "")
                  << std::fixed << std::setprecision(0) << d << "%\n";
    } else {
        std::cout << "Loss Change     : N/A\n";
    }

    if (before.jitter >= 0 && after.jitter >= 0) {
        double d = after.jitter - before.jitter;
        std::cout << "Jitter Change   : " << (d >= 0 ? "+" : "")
                  << std::fixed << std::setprecision(2) << d << " ms\n";
    } else {
        std::cout << "Jitter Change   : N/A\n";
    }

    std::cout << "========================================\n";
}

// ============================================================
//  Save experiment result to file
// ============================================================
void saveExperimentResult(const ExperimentResult& r) {
    std::ofstream ofs("experiment_history.txt", std::ios::app);
    if (!ofs.is_open()) {
        std::cerr << "[ERROR] Could not open experiment_history.txt "
                  << "for writing.\n";
        return;
    }

    ofs << "========================================\n";
    ofs << "NETWORK CHAOS EXPERIMENT\n";
    ofs << "Date: " << r.timestamp << "\n";
    ofs << "========================================\n\n";

    if (!r.network.ssid.empty()) {
        ofs << "WiFi SSID : " << r.network.ssid << "\n";
    }
    ofs << "Interface : " << r.network.interface << "\n";
    ofs << "Local IP  : "
        << (r.network.localIP.empty() ? "N/A" : r.network.localIP) << "\n";
    ofs << "Gateway   : " << (r.network.gateway.empty()
                               ? "N/A" : r.network.gateway) << "\n";
    ofs << "Target    : " << r.target << "\n\n";

    ofs << "BASELINE\n";
    ofs << "  Latency     : " << fmtLatency(r.baseline.latency) << "\n";
    ofs << "  Packet Loss : " << fmtLoss(r.baseline.packetLoss) << "\n";
    ofs << "  Jitter      : " << fmtJitter(r.baseline.jitter) << "\n\n";

    ofs << "CHAOS APPLIED\n";
    ofs << "  Delay       : "
        << static_cast<int>(r.chaosDelay) << " ms\n";
    ofs << "  Packet Loss : "
        << static_cast<int>(r.chaosLoss) << "%\n\n";

    ofs << "AFTER CHAOS\n";
    ofs << "  Latency     : " << fmtLatency(r.afterChaos.latency) << "\n";
    ofs << "  Packet Loss : " << fmtLoss(r.afterChaos.packetLoss) << "\n";
    ofs << "  Jitter      : " << fmtJitter(r.afterChaos.jitter) << "\n\n";

    ofs << "IMPACT\n";
    if (r.baseline.latency >= 0 && r.afterChaos.latency >= 0) {
        double d = r.afterChaos.latency - r.baseline.latency;
        ofs << "  Latency Change  : " << (d >= 0 ? "+" : "")
            << std::fixed << std::setprecision(2) << d << " ms\n";
    } else {
        ofs << "  Latency Change  : N/A\n";
    }
    if (r.baseline.packetLoss >= 0 && r.afterChaos.packetLoss >= 0) {
        double d = r.afterChaos.packetLoss - r.baseline.packetLoss;
        ofs << "  Loss Change     : " << (d >= 0 ? "+" : "")
            << std::fixed << std::setprecision(0) << d << "%\n";
    } else {
        ofs << "  Loss Change     : N/A\n";
    }
    if (r.baseline.jitter >= 0 && r.afterChaos.jitter >= 0) {
        double d = r.afterChaos.jitter - r.baseline.jitter;
        ofs << "  Jitter Change   : " << (d >= 0 ? "+" : "")
            << std::fixed << std::setprecision(2) << d << " ms\n";
    } else {
        ofs << "  Jitter Change   : N/A\n";
    }

    ofs << "\nStatus: " << (r.networkRestored ? "Network Restored"
                                              : "WARNING: Network NOT Restored")
        << "\n";
    ofs << "========================================\n\n";

    ofs.close();
    std::cout << "\nResults saved to experiment_history.txt\n";
}

// ============================================================
//  Run Full Chaos Experiment  (Option 1)
// ============================================================
void runFullExperiment() {
    ExperimentResult result{};
    result.timestamp      = currentTimestamp();
    result.chaosDelay     = PREDEFINED_DELAY_MS;
    result.chaosLoss      = PREDEFINED_LOSS_PCT;
    result.chaosApplied   = false;
    result.networkRestored = false;

    // --- Step 1: Detect network ---
    std::cout << "\n[*] Detecting active network interface...\n";
    result.network = detectNetwork();

    if (!result.network.detected) {
        std::cout << "\nNo active network connection detected.\n";
        return;
    }

    printNetworkInfo(result.network);

    // --- Step 2: Choose target ---
    result.target = choosePingTarget(result.network);
    std::cout << "\n[*] Ping target: " << result.target << "\n";

    // --- Step 3: Baseline measurement ---
    std::cout << "\n========================================\n";
    std::cout << " NORMAL NETWORK MEASUREMENT\n";
    std::cout << "========================================\n";
    std::cout << "Interface    : " << result.network.interface << "\n";
    std::cout << "Target       : " << result.target << "\n\n";

    std::cout << "[*] Measuring baseline (pinging x"
              << PING_COUNT << ")...\n\n";
    result.baseline = measureNetwork(result.target);

    std::cout << "Packets Sent : " << result.baseline.packetsSent << "\n";
    std::cout << "Packet Loss  : " << fmtLoss(result.baseline.packetLoss) << "\n";
    std::cout << "Avg Latency  : " << fmtLatency(result.baseline.latency) << "\n";
    std::cout << "Jitter       : " << fmtJitter(result.baseline.jitter) << "\n";
    std::cout << "========================================\n";

    // --- Step 4: Apply chaos ---
    result.chaosApplied = applyChaos(result.network.interface);
    if (!result.chaosApplied) {
        std::cerr << "\n[ERROR] Chaos was NOT applied. Aborting experiment.\n";
        return;
    }

    // Brief pause for netem to settle
    std::cout << "\n[*] Waiting 2 seconds for chaos to take effect...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // --- Step 5: Measure after chaos ---
    std::cout << "\n========================================\n";
    std::cout << " AFTER CHAOS MEASUREMENT\n";
    std::cout << "========================================\n";

    std::cout << "[*] Measuring network under chaos (pinging x"
              << PING_COUNT << ")...\n\n";
    result.afterChaos = measureNetwork(result.target);

    std::cout << "Packets Sent : " << result.afterChaos.packetsSent << "\n";
    std::cout << "Packet Loss  : " << fmtLoss(result.afterChaos.packetLoss) << "\n";
    std::cout << "Avg Latency  : " << fmtLatency(result.afterChaos.latency) << "\n";
    std::cout << "Jitter       : " << fmtJitter(result.afterChaos.jitter) << "\n";
    std::cout << "========================================\n";

    // --- Step 6: Compare ---
    printComparison(result.baseline, result.afterChaos);

    // --- Step 7: Restore network (ALWAYS attempt) ---
    std::cout << "\nRestoring network...\n";
    if (removeChaos(result.network.interface)) {
        result.networkRestored = true;
        std::cout << "Network restored successfully.\n";
    } else {
        result.networkRestored = false;
        std::cerr << "[WARNING] Could not confirm chaos removal.\n"
                  << "Please run: sudo tc qdisc del dev "
                  << result.network.interface << " root\n";
    }

    // --- Step 8: Save history ---
    saveExperimentResult(result);
}

// ============================================================
//  Check Current Network  (Option 2)
// ============================================================
void checkCurrentNetwork() {
    NetworkInfo net = detectNetwork();
    if (!net.detected) {
        std::cout << "\nNo active network connection detected.\n";
        return;
    }

    printNetworkInfo(net);

    std::string target = choosePingTarget(net);
    std::cout << "\n[*] Ping target: " << target << "\n";

    std::cout << "\n========================================\n";
    std::cout << " NORMAL NETWORK MEASUREMENT\n";
    std::cout << "========================================\n";
    std::cout << "Interface    : " << net.interface << "\n";
    std::cout << "Target       : " << target << "\n\n";

    std::cout << "[*] Measuring network (pinging x"
              << PING_COUNT << ")...\n\n";

    NetworkMetrics m = measureNetwork(target);

    std::cout << "Packets Sent : " << m.packetsSent << "\n";
    std::cout << "Packet Loss  : " << fmtLoss(m.packetLoss) << "\n";
    std::cout << "Avg Latency  : " << fmtLatency(m.latency) << "\n";
    std::cout << "Jitter       : " << fmtJitter(m.jitter) << "\n";
    std::cout << "========================================\n";
}

// ============================================================
//  Restore Network  (Option 3)
// ============================================================
void restoreNetwork() {
    NetworkInfo net = detectNetwork();
    if (!net.detected) {
        std::cout << "\nNo active network connection detected.\n";
        return;
    }

    std::cout << "\nInterface : " << net.interface << "\n";
    std::cout << "Restoring network...\n";

    if (removeChaos(net.interface)) {
        std::cout << "Network restored successfully.\n";
    } else {
        std::cerr << "[WARNING] Could not confirm chaos removal.\n"
                  << "You may need to run manually:\n"
                  << "  sudo tc qdisc del dev "
                  << net.interface << " root\n";
    }
}

// ============================================================
//  View Experiment History  (Option 4)
// ============================================================
void viewExperimentHistory() {
    std::ifstream ifs("experiment_history.txt");
    if (!ifs.is_open()) {
        std::cout << "\nNo experiment history found.\n"
                  << "(experiment_history.txt does not exist yet)\n";
        return;
    }

    std::cout << "\n";
    std::string line;
    while (std::getline(ifs, line)) {
        std::cout << line << "\n";
    }
    ifs.close();
}

// ============================================================
//  Show menu and get user choice
// ============================================================
int showMenu() {
    std::cout << "\n========================================\n";
    std::cout << "       NETWORK CHAOS EMULATOR\n";
    std::cout << "========================================\n\n";
    std::cout << "1. Run Full Chaos Experiment\n";
    std::cout << "2. Check Current Network\n";
    std::cout << "3. Restore Network\n";
    std::cout << "4. View Experiment History\n";
    std::cout << "5. Exit\n\n";
    std::cout << "Enter choice: ";

    int choice = 0;
    if (!(std::cin >> choice)) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        return -1;
    }
    return choice;
}
