#pragma once
#include <Arduino.h>
#include <vector>

// ── Device entry found during scan ───────────────────────────
struct BleDevice {
  String name;
  String address;
  int8_t rssi;
};

// ── Now-playing info pushed via BLE Notify (AB03) ────────────
struct TrackInfo {
  String title   = "---";
  String artist  = "---";
  String album   = "---";
  bool   playing = false;
};

// ── EQ band ──────────────────────────────────────────────────
struct EqBand {
  const char* label;
  int8_t      gain;   // -12 … +12 dB
};

// ── Connection state ─────────────────────────────────────────
enum class BleState : uint8_t {
  IDLE,
  SCANNING,
  CONNECTING,
  CONNECTED,
  DISCONNECTED,
};

// ── API ──────────────────────────────────────────────────────
void ble_manager_init();

void ble_scan_start();
void ble_scan_stop();

// Returns current list (populated after scan)
const std::vector<BleDevice>& ble_get_devices();

void ble_connect(const String& address);
void ble_disconnect();

BleState ble_get_state();

// Transport commands → write to AB02
void ble_cmd_play();
void ble_cmd_pause();
void ble_cmd_next();
void ble_cmd_prev();

// EQ: set one band gain (-12..+12)
void ble_cmd_eq_set(uint8_t band, int8_t gain);

// Call from loop to process BLE events
void ble_manager_update();

// Latest notify data (AB03)
const TrackInfo& ble_get_track();

// Mutable EQ state (5-band example)
EqBand* ble_get_eq();      // returns pointer to EqBand[5]
int     ble_eq_band_count();
