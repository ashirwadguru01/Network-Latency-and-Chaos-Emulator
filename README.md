# Network Latency & Chaos Emulator

A lightweight C++ tool that emulates network chaos (latency and packet loss) on Linux and WSL. It measures baseline network metrics, injects controlled chaos using `tc netem`, compares performance, and restores normal network operation automatically.

---

## Features

- **Auto-Detection** — Automatically detects your active network interface, local IP, gateway, and host Wi-Fi SSID (including inside WSL).
- **Network Metrics** — Measures Round-Trip Time (RTT latency), packet loss percentage, and jitter using ICMP pings.
- **Chaos Injection** — Injects 100 ms latency and 10% packet loss via Linux `tc netem`.
- **Before / After Comparison** — Displays a side-by-side comparison table showing impact deltas.
- **Automatic Restoration** — Automatically restores network settings to normal after each test.
- **Experiment History** — Logs all results to `experiment_history.txt`.

---

## Prerequisites

On Ubuntu, Debian, or WSL (Windows Subsystem for Linux), install the required tools:

```bash
sudo apt update
sudo apt install -y g++ make iproute2 iputils-ping
```

---

## Build & Run

### 1. Build

```bash
make
```

### 2. Run

The program requires root privileges because `tc` modifies kernel network settings:

```bash
sudo ./network_detector
```

To clean up build artifacts:

```bash
make clean
```

---

## Menu Options

When launched, the program presents an interactive menu:

```text
========================================
       NETWORK CHAOS EMULATOR
========================================

1. Run Full Chaos Experiment
2. Check Current Network
3. Restore Network
4. View Experiment History
5. Exit

Enter choice:
```

| Option | Action | Description |
|:---|:---|:---|
| **1** | **Run Full Chaos Experiment** | Measures baseline, applies chaos (100 ms delay, 10% loss), re-measures, shows comparison, restores network, and saves results. |
| **2** | **Check Current Network** | Displays current Wi-Fi SSID, interface, IP, gateway, and live ping metrics without applying chaos. |
| **3** | **Restore Network** | Manually removes any active `tc netem` rules to ensure the network is clean. |
| **4** | **View Experiment History** | Prints all previous experiment logs from `experiment_history.txt`. |
| **5** | **Exit** | Exits the program. |

---

## How It Works

1. **Detect Network** — Identifies the default gateway and active interface. Inside WSL, it queries host Windows networking to detect your real Wi-Fi SSID and router gateway instead of the static WSL virtual adapter.
2. **Measure Baseline** — Sends 10 pings to the gateway (or fallback `8.8.8.8`) to measure normal latency, packet loss, and jitter.
3. **Apply Chaos** — Runs:
   ```bash
   sudo tc qdisc replace dev <interface> root netem delay 100ms loss 10%
   ```
4. **Measure Under Chaos** — Sends 10 pings again to quantify the degradation.
5. **Compare & Display** — Prints a clean delta comparison table.
6. **Restore Network** — Removes the chaos queue discipline:
   ```bash
   sudo tc qdisc del dev <interface> root
   ```
7. **Save History** — Appends the results to `experiment_history.txt`.

---

## Sample Output

```text
========================================
 CURRENT NETWORK
========================================
WiFi SSID : MyHomeWiFi
Interface : Wi-Fi
Local IP  : 192.168.1.100
Gateway   : 192.168.1.1
Status    : Connected
========================================

========================================
 EXPERIMENT RESULT
========================================

                BEFORE        AFTER         
----------------------------------------
Latency         2.14 ms       102.31 ms     
Packet Loss     0%            10%           
Jitter          0.38 ms       0.84 ms       
----------------------------------------

Latency Change  : +100.17 ms
Loss Change     : +10%
Jitter Change   : +0.46 ms
========================================

Restoring network...
Network restored successfully.

Results saved to experiment_history.txt
```

---

## Common Issues & Notes

- **Permission Denied**: You must run the program with `sudo` because `tc` needs administrator privileges.
- **Gateway inside WSL**: If you switch Wi-Fi networks, the emulator automatically detects the updated host SSID and gateway by querying the host network.
- **Manual Cleanup**: If an experiment is interrupted with `Ctrl+C`, run option `3` or execute:
  ```bash
  sudo tc qdisc del dev eth0 root
  ```

---

## License

Open source under the [MIT License](LICENSE).
