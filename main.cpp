#include "emulator.h"
#include <iostream>

// ============================================================
//  Network Latency & Packet Loss Chaos Emulator
//
//  Automatically detects the active WiFi network (even in WSL),
//  shows WiFi SSID, local IP, and gateway.
//  Measures baseline performance, injects tc netem chaos,
//  re-measures, compares, saves results, and restores
//  the network — all without manual user input.
//
//  Usage:  sudo ./network_detector
// ============================================================

int main() {
  std::cout << "\n";
  std::cout << "======================================================\n";
  std::cout << "  Network Latency & Packet Loss Chaos Emulator\n";
  std::cout << "======================================================\n";

  bool running = true;

  while (running) {
    int choice = showMenu();

    switch (choice) {
    case 1:
      runFullExperiment();
      break;

    case 2:
      checkCurrentNetwork();
      break;

    case 3:
      restoreNetwork();
      break;

    case 4:
      viewExperimentHistory();
      break;

    case 5:
      std::cout << "\nExiting. Goodbye!\n";
      running = false;
      break;

    default:
      std::cout << "\n[!] Invalid choice. Please enter 1-5.\n";
      break;
    }
  }

  return 0;
}
