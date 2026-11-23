#include "ui_scan.h"
#include "theme.h"
#include "touch.h"
#include "ble_manager.h"

/*
 * Scan screen
 * ───────────
 * Layout (480×320)
 *   [28px]  Top bar: "Bluetooth Scan"
 *   [244px] Device list (up to 5 visible rows, 48px each)
 *   [48px]  Bottom bar: [SCAN] button centred
 *
 * States:
 *   IDLE       – shows "No devices" placeholder + SCAN button
 *   SCANNING   – shows spinner + SCANNING label
 *   LIST       – shows found devices; tap a row to connect
 *   CONNECTING – shows "Connecting to …"
 */

// ── Layout consts ─────────────────────────────────────────────
static constexpr int LIST_Y     = TOPBAR_H + 4;
static constexpr int ROW_H      = 48;
static constexpr int MAX_ROWS   = 5;
static constexpr int SCAN_BTN_W = 140;
static constexpr int SCAN_BTN_H = 36;
static constexpr int SCAN_BTN_X = (SCREEN_W - SCAN_BTN_W) / 2;
static constexpr int SCAN_BTN_Y = BOTBAR_Y + (BOTBAR_H - SCAN_BTN_H) / 2;

// ── State ─────────────────────────────────────────────────────
static bool g_scanRequested  = false;
static bool g_scanStarted    = false;
static uint32_t g_scanStart  = 0;
static constexpr uint32_t SCAN_DURATION_MS = 5500;

static int  g_selectedRow = -1;
static int  g_prevDevCount = -1;

// ── Draw helpers ──────────────────────────────────────────────
static void drawTopBar(TFT_eSPI& tft) {
  tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, CLR_SURFACE);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(CLR_TEXT_PRI, CLR_SURFACE);
  tft.drawString("Bluetooth Scan", 12, TOPBAR_H / 2, FONT_SMALL);
}

static void drawScanButton(TFT_eSPI& tft, bool scanning) {
  tft.fillRect(0, BOTBAR_Y, SCREEN_W, BOTBAR_H, CLR_SURFACE);
  tft.drawFastHLine(0, BOTBAR_Y, SCREEN_W, CLR_BORDER);

  uint16_t btnColor  = scanning ? CLR_ACCENT_DIM : CLR_ACCENT;
  uint16_t textColor = CLR_BG;

  tft.fillRoundRect(SCAN_BTN_X, SCAN_BTN_Y, SCAN_BTN_W, SCAN_BTN_H, 6, btnColor);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(textColor, btnColor);
  const char* label = scanning ? "Scanning..." : "SCAN";
  tft.drawString(label, SCAN_BTN_X + SCAN_BTN_W/2, SCAN_BTN_Y + SCAN_BTN_H/2, FONT_SMALL);
}

static void drawRow(TFT_eSPI& tft, int row, const BleDevice& dev, bool selected) {
  int y = LIST_Y + row * ROW_H;
  uint16_t bg = selected ? CLR_ACCENT_DIM : CLR_BG;

  tft.fillRect(0, y, SCREEN_W, ROW_H - 1, bg);
  tft.drawFastHLine(0, y + ROW_H - 1, SCREEN_W, CLR_BORDER);

  // BLE dot
  tft.fillCircle(20, y + ROW_H/2, 5, CLR_ACCENT);

  // Name
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(CLR_TEXT_PRI, bg);
  tft.drawString(dev.name.substring(0, 22).c_str(), 36, y + 14, FONT_SMALL);

  // Address + RSSI
  tft.setTextColor(CLR_TEXT_DIM, bg);
  String sub = dev.address + "  " + String(dev.rssi) + " dBm";
  tft.drawString(sub.c_str(), 36, y + 32, 1);
}

static void drawDeviceList(TFT_eSPI& tft) {
  const auto& devs = ble_get_devices();
  int count = (int)devs.size();
  int show  = min(count, MAX_ROWS);

  // Clear list area
  tft.fillRect(0, LIST_Y, SCREEN_W, MAX_ROWS * ROW_H, CLR_BG);

  if (count == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
    int cy = LIST_Y + (MAX_ROWS * ROW_H) / 2;
    BleState st = ble_get_state();
    const char* msg = (st == BleState::SCANNING) ? "Scanning for devices..." : "No devices found.\nPress SCAN to start.";
    tft.drawString(msg, SCREEN_W/2, cy, FONT_SMALL);
  } else {
    for (int i = 0; i < show; i++)
      drawRow(tft, i, devs[i], i == g_selectedRow);
  }
  g_prevDevCount = count;
}

static void drawConnecting(TFT_eSPI& tft, const String& name) {
  tft.fillRect(0, LIST_Y, SCREEN_W, MAX_ROWS * ROW_H, CLR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(CLR_TEXT_PRI, CLR_BG);
  tft.drawString("Connecting...", SCREEN_W/2, LIST_Y + 60, FONT_MED);
  tft.setTextColor(CLR_TEXT_SEC, CLR_BG);
  tft.drawString(name.c_str(), SCREEN_W/2, LIST_Y + 90, FONT_SMALL);
}

// ── Spinner (dots cycling) ────────────────────────────────────
static uint32_t g_spinLast = 0;
static uint8_t  g_spinIdx  = 0;
static void tickSpinner(TFT_eSPI& tft) {
  uint32_t now = millis();
  if (now - g_spinLast < 400) return;
  g_spinLast = now;

  int cx = SCREEN_W - 24, cy = TOPBAR_H / 2;
  tft.fillRect(cx - 10, cy - 5, 22, 10, CLR_SURFACE);  // clear
  static const char* dots[] = {"   ", ".  ", ".. ", "..."};
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(CLR_ACCENT, CLR_SURFACE);
  tft.drawString(dots[g_spinIdx % 4], cx + 10, cy, FONT_SMALL);
  g_spinIdx++;
}

// ── Public ───────────────────────────────────────────────────
void ui_scan_enter(TFT_eSPI& tft) {
  g_scanRequested = false;
  g_scanStarted   = false;
  g_selectedRow   = -1;
  g_prevDevCount  = -1;

  tft.fillScreen(CLR_BG);
  drawTopBar(tft);
  drawScanButton(tft, false);
  drawDeviceList(tft);
}

AppState ui_scan_update(TFT_eSPI& tft) {
  BleState bst = ble_get_state();

  // ── Handle connecting state ──────────────────────────────
  if (bst == BleState::CONNECTING) {
    // spinner already drawn; wait
    return AppState::SCAN;
  }
  if (bst == BleState::CONNECTED) {
    return AppState::PLAYER;
  }

  // ── Scan timeout ─────────────────────────────────────────
  if (g_scanStarted && millis() - g_scanStart >= SCAN_DURATION_MS) {
    ble_scan_stop();
    g_scanStarted = false;
    drawScanButton(tft, false);
    drawDeviceList(tft);
  }

  // ── Spinner while scanning ───────────────────────────────
  if (bst == BleState::SCANNING) {
    tickSpinner(tft);
    // Refresh list if new devices arrived
    if ((int)ble_get_devices().size() != g_prevDevCount)
      drawDeviceList(tft);
  }

  // ── Touch ─────────────────────────────────────────────────
  int16_t tx, ty;
  if (!touch_get(tft, tx, ty)) return AppState::SCAN;

  // SCAN button
  if (tx >= SCAN_BTN_X && tx <= SCAN_BTN_X + SCAN_BTN_W &&
      ty >= SCAN_BTN_Y && ty <= SCAN_BTN_Y + SCAN_BTN_H) {
    if (!g_scanStarted) {
      ble_scan_start();
      g_scanStarted = true;
      g_scanStart   = millis();
      drawScanButton(tft, true);
      drawDeviceList(tft);
    }
    return AppState::SCAN;
  }

  // Device list rows
  const auto& devs = ble_get_devices();
  int show = min((int)devs.size(), MAX_ROWS);
  for (int i = 0; i < show; i++) {
    int rowY = LIST_Y + i * ROW_H;
    if (ty >= rowY && ty < rowY + ROW_H) {
      g_selectedRow = i;
      drawConnecting(tft, devs[i].name);
      ble_connect(devs[i].address);
      return AppState::SCAN;  // next tick will detect CONNECTED
    }
  }

  return AppState::SCAN;
}
