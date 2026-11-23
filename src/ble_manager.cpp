#include "ble_manager.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>

// ── KBox service UUIDs (AB00 / AB01 / AB02 / AB03) ───────────
static const BLEUUID SVC_UUID  ("0000AB00-0000-1000-8000-00805F9B34FB");
static const BLEUUID CHR_READ  ("0000AB01-0000-1000-8000-00805F9B34FB");
static const BLEUUID CHR_WRITE ("0000AB02-0000-1000-8000-00805F9B34FB");
static const BLEUUID CHR_NOTIFY("0000AB03-0000-1000-8000-00805F9B34FB");

// ── State ────────────────────────────────────────────────────
static BleState               g_state = BleState::IDLE;
static std::vector<BleDevice> g_devices;
static TrackInfo              g_track;
static BLEClient*             g_client   = nullptr;
static BLERemoteCharacteristic* g_chrWrite  = nullptr;
static BLERemoteCharacteristic* g_chrNotify = nullptr;

// 5-band EQ: 60Hz, 250Hz, 1kHz, 4kHz, 16kHz
static EqBand g_eq[5] = {
  {"60",    0},
  {"250",   0},
  {"1k",    0},
  {"4k",    0},
  {"16k",   0},
};

// ── BLE scan callbacks ────────────────────────────────────────
class ScanCB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    BleDevice d;
    d.name    = dev.haveName()  ? dev.getName().c_str() : "(no name)";
    d.address = dev.getAddress().toString().c_str();
    d.rssi    = dev.getRSSI();
    // Avoid duplicates
    for (auto& e : g_devices)
      if (e.address == d.address) return;
    g_devices.push_back(d);
  }
};
static ScanCB g_scanCB;

// ── Notify callback ───────────────────────────────────────────
static void notifyCallback(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  // Minimal parser: expect null-delimited  "title\0artist\0album\0playState"
  // Adapt to your KBox protocol as needed.
  String raw;
  for (size_t i = 0; i < len; i++) raw += (char)data[i];

  int p0 = raw.indexOf('\0');
  int p1 = raw.indexOf('\0', p0 + 1);
  int p2 = raw.indexOf('\0', p1 + 1);

  if (p0 > 0) g_track.title  = raw.substring(0, p0);
  if (p1 > p0) g_track.artist = raw.substring(p0+1, p1);
  if (p2 > p1) g_track.album  = raw.substring(p1+1, p2);
  if (p2 + 1 < (int)len) g_track.playing = (data[p2+1] == 1);
}

// ── Client callbacks ──────────────────────────────────────────
class ClientCB : public BLEClientCallbacks {
  void onConnect(BLEClient*)    override { g_state = BleState::CONNECTED; }
  void onDisconnect(BLEClient*) override {
    g_state   = BleState::DISCONNECTED;
    g_chrWrite  = nullptr;
    g_chrNotify = nullptr;
  }
};
static ClientCB g_clientCB;

// ── API impl ──────────────────────────────────────────────────
void ble_manager_init() {
  BLEDevice::init("MusicHub");
}

void ble_scan_start() {
  g_devices.clear();
  g_state = BleState::SCANNING;
  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&g_scanCB, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->start(5, false);  // 5-second scan, non-blocking callback style
}

void ble_scan_stop() {
  BLEDevice::getScan()->stop();
  if (g_state == BleState::SCANNING) g_state = BleState::IDLE;
}

const std::vector<BleDevice>& ble_get_devices() { return g_devices; }

void ble_connect(const String& address) {
  g_state  = BleState::CONNECTING;
  g_client = BLEDevice::createClient();
  g_client->setClientCallbacks(&g_clientCB);

  BLEAddress bleAddr(address.c_str());
  if (!g_client->connect(bleAddr)) { g_state = BleState::DISCONNECTED; return; }

  BLERemoteService* svc = g_client->getService(SVC_UUID);
  if (!svc) { g_client->disconnect(); return; }

  g_chrWrite  = svc->getCharacteristic(CHR_WRITE);
  g_chrNotify = svc->getCharacteristic(CHR_NOTIFY);
  if (g_chrNotify && g_chrNotify->canNotify())
    g_chrNotify->registerForNotify(notifyCallback);
  // state → CONNECTED is set via ClientCB::onConnect
}

void ble_disconnect() {
  if (g_client) g_client->disconnect();
}

BleState ble_get_state() { return g_state; }

// ── Write helper ──────────────────────────────────────────────
static void send(const uint8_t* buf, size_t len) {
  if (g_chrWrite && g_state == BleState::CONNECTED)
    g_chrWrite->writeValue(const_cast<uint8_t*>(buf), len, false);
}

// KBox command bytes – replace with actual protocol bytes from docs
void ble_cmd_play()  { uint8_t b[] = {0x05,0x01}; send(b,2); }
void ble_cmd_pause() { uint8_t b[] = {0x05,0x02}; send(b,2); }
void ble_cmd_next()  { uint8_t b[] = {0x05,0x03}; send(b,2); }
void ble_cmd_prev()  { uint8_t b[] = {0x05,0x04}; send(b,2); }

void ble_cmd_eq_set(uint8_t band, int8_t gain) {
  uint8_t b[] = {0x06, band, (uint8_t)gain};
  send(b, 3);
  if (band < 5) g_eq[band].gain = gain;
}

void ble_manager_update() {
  // BLE events are handled via callbacks; nothing polled here yet.
  // Add state-machine logic (e.g. reconnect) as needed.
}

const TrackInfo& ble_get_track() { return g_track; }
EqBand*          ble_get_eq()    { return g_eq; }
int              ble_eq_band_count() { return 5; }
